/**
 * @file handlers.c
 * @brief Handler implementations for every BFWMActionType.
 *
 * Each function is registered with the ActionDispatcher during startup
 * and called from ActionDispatcherDispatch at keystroke time.
 */

#include "handlers.h"

#include "../../bar/bar.h"
#include "../../config/action.h"
#include "../../config/lua/parser.h"
#include "../../config/config_paths.h"
#include "../../core/bfwm_context.h"
#include "../../core/portable.h"
#include "../../core/sync.h"
#include "../../dpi/dpi.h"
#include "../../logging/logger.h"
#include "../../math/rect.h"
#include "../../monitor/monitor.h"
#include "../../window/border/border_manager.h"
#include "../../window/border/overlay.h"
#include "../../window/cleanup.h"
#include "../../window/cloaking.h"
#include "../../window/window.h"
#include "../../workspace/workspace.h"
#include <array>
#include <cstddef>
#include <cwchar>
#include <dwmapi.h>
#include <handleapi.h>
#include <memory>
#include <minwindef.h>
#include <processthreadsapi.h>
#include <string>
#include <stringapiset.h>
#include <synchapi.h>
#include <sysinfoapi.h>
#include <windef.h>
#include <windows.h>
#include <winnls.h>
#include <winnt.h>

#include "../../core/bfwm_def.h"
#include "../../notification/snackbar.h"
#include "../../transaction/transaction.h"
#include "../../win/win_utils.h"
#include "../../workspace/layouts/layout_api.h"

enum {
   FLOAT_RESIZE_PX = 20,
   FLOAT_SIZE_PCT_90 = 90,
   FLOAT_SIZE_PCT_100 = 100,
   FLOAT_SIZE_PCT_60 = 60,
};

enum { MAX_PENDING_KILLS = 32, KILL_TIMEOUT_MS = 5000 };

namespace {

inline auto spawn_process(struct BFWMContext *ctx, const char *command,
                          DWORD flags) -> int {
   if (command[0] == '\0') {
      Snackbar::Warn(ctx, L"spawn_process: empty command, ignoring");
      return 0;
   }

   int const wide_len =
       MultiByteToWideChar(CP_UTF8, 0, command, -1, nullptr, 0);
   if (wide_len <= 0) {
      Snackbar::LogLastError(ctx, "spawn_process: MultiByteToWideChar failed");
      return -1;
   }

   std::wstring wide_cmd(static_cast<size_t>(wide_len), L'\0');
   MultiByteToWideChar(CP_UTF8, 0, command, -1, wide_cmd.data(), wide_len);

   DebugW(L"spawn_process: executing \"%ls\" (flags=0x%lx)", wide_cmd.c_str(),
          (unsigned long)flags);

   STARTUPINFOW startup_info = {};
   startup_info.cb = sizeof(startup_info);
   startup_info.dwFlags = STARTF_USESHOWWINDOW;
   startup_info.wShowWindow = SW_SHOWNORMAL;

   PROCESS_INFORMATION process_info = {};

   BOOL const success =
       CreateProcessW(nullptr, wide_cmd.data(), nullptr, nullptr, FALSE, flags,
                      nullptr, nullptr, &startup_info, &process_info);

   if (success == 0) {
      Snackbar::LogLastError(ctx, "spawn_process: CreateProcessW failed");
      return -1;
   }

   Info("Spawned process %lu: %s", process_info.dwProcessId, command);

   CloseHandle(process_info.hThread);
   CloseHandle(process_info.hProcess);
   return 0;
}

inline auto resize_in_direction(BFWMContext *ctx, BFWMDirection direction,
                                int pixels) -> int {

   // Keyboard-driven resize: stamp the time so MainLoop suppresses the
   // 500ms overlay-reconcile tick for a short window (avoids injecting an
   // extra commit mid-resize). resize_hwnd is only set for mouse drags.
   ctx->last_keyboard_resize = GetTickCount64();

   HWND focused = nullptr;
   Workspace *workspace = nullptr;
   {
      ScopedLock const lock(ctx->lock);
      focused = ctx->focused_hwnd;
      workspace = ctx->focused_workspace;
   }

   Window *win = ctx->windows->FindByHwnd(focused);
   if ((win != nullptr) && (win->IsFloating() != 0)) {
      POINT win_center = {};
      {
         ScopedLock const lock(ctx->lock);
         switch (direction) {
         case DirLeft:
            win->SavedRectPtr()->right -= FLOAT_RESIZE_PX;
            break;
         case DirRight:
            win->SavedRectPtr()->right += FLOAT_RESIZE_PX;
            break;
         case DirUp:
            win->SavedRectPtr()->bottom -= FLOAT_RESIZE_PX;
            break;
         case DirDown:
            win->SavedRectPtr()->bottom += FLOAT_RESIZE_PX;
            break;
         default:
            return 0;
         }

         // Clamp to monitor work area so the window stays fully visible
         win_center = {
             .x = (win->SavedRectPtr()->left + win->SavedRectPtr()->right) / 2,
             .y = (win->SavedRectPtr()->top + win->SavedRectPtr()->bottom) / 2};
      }

      Monitor *mon = FindMonitorByPoint(ctx, win_center);
      if (mon != nullptr) {
         {
            ScopedLock const lock(ctx->lock);
            UINT const dpi = (ctx->dpi != nullptr)
                                 ? ctx->dpi->GetDpi(mon->GetHandle())
                                 : DpiSystem::BaseDpi();
            RECT const clamped = win->ClampTo(mon->GetWorkArea(), dpi);
            win->SetSavedRect(clamped);
         }
      }

      BOOL const result =
          BFWMSetWindowPos(win->GetHwnd(), win->SavedRectPtr());
      win->MarkOverlayDirty();
      return static_cast<int>(result == 0);
   }

   if ((workspace == nullptr) || (workspace->GetEngine() == nullptr))
      return -1;

   bool resized = false;
   {
      ScopedLock const lock(ctx->lock);
      resized =
          workspace->GetEngine()->resize_window(focused, direction, pixels);
   }
   if (resized)
      workspace->ApplyLayout(ctx);
   return resized ? 0 : -1;
}

// ============================================================
// Fullscreen helpers
// ============================================================

inline void EnterFullscreenForHwnd(HWND hwnd, struct BFWMContext *ctx,
                                   Window *win, Workspace *workspace,
                                   Monitor *mon) {
   // Save window style and position so RestoreFullscreenForHwnd
   // can return the window to its pre-fullscreen state on shutdown.
   win->SetSavedStyle(GetWindowLong(hwnd, GWL_STYLE));
   win->SetSavedExstyle(GetWindowLong(hwnd, GWL_EXSTYLE));
   GetWindowRect(hwnd, win->SavedRectPtr());

   // Remove from layout
   workspace->GetEngine()->remove(hwnd);

   RECT const fs_rect = mon->GetRect();

   // New style: POPUP removes title bar, borders, and sizing grip
   LONG new_style = WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
   LONG const new_exstyle = WS_EX_TOPMOST;

   // If the window had a menu, preserve it
   if ((win->SavedStyle() & WS_SYSMENU) != 0) {
      new_style |= WS_SYSMENU;
   }

   SetWindowLong(hwnd, GWL_STYLE, new_style);
   SetWindowLong(hwnd, GWL_EXSTYLE, new_exstyle);

   /* The monitor rect is physical; a DPI-unaware target on a scaled monitor
    * must receive it in its own logical space (see IssueRectWithDpiConversion)
    * or the DWM double-scales it. Mirrors the IssueMove conversion. */
   IssueRectWithDpiConversion(ctx, hwnd, mon->GetHandle(), fs_rect,
                              BFWMSetFullscreenPosition);

   /* Fullscreen rects bypass the placement state machine (IssueMove), so arm
    * the landing-gate state here: on exit the gate must not mistake the
    * fullscreen rect for a stalled move. Drop any stale cross-monitor trust
    * from a previous move as well. */
   win->SetLastIssued(fs_rect);
   win->SetLastIssueTime(GetTickCount64());
   win->SetMoveInFlight(FALSE);
   win->SetFailedLandings(0);
   win->SetCrossMonitorTrusted(FALSE);

   win->SetFullscreen(TRUE);
   win->SetMaximized(FALSE);
   win->SetFloating(FALSE);
   win->SetUnmanagedFullscreen(FALSE);

   // Hide bar
   if (mon != nullptr) {
      mon->IncrementFullscreenCount();
      if (mon->GetFullscreenCount() == 1 && (mon->GetBar() != nullptr))
         mon->GetBar()->Hide();
   }
}

inline void ExitFullscreenForHwnd(HWND hwnd, BFWMContext *ctx, Window *win,
                                  Workspace *workspace, Monitor *mon) {
   ctx->toggling_fullscreen_hwnd = hwnd;
   win->SetFullscreen(FALSE);

   // Restore window style
   SetWindowLong(hwnd, GWL_STYLE, win->SavedStyle());
   SetWindowLong(hwnd, GWL_EXSTYLE, win->SavedExstyle());

   // Make sure it's not topmost
   BFWMSetWindowZ(hwnd, BFWM_Z_NOTOPMOST);

   // Restore from any OS-level maximized state so the layout can
   // position the window correctly.
   if (IsZoomed(hwnd) != 0)
      ShowWindow(hwnd, SW_RESTORE);

   // Reinsert into layout — only if the window is managed by layout. A
   // floating window is not a layout member; the float toggle / re-tile path
   // owns its re-insertion.
   if (IsWindowManagedByLayout(win) != 0) {
      workspace->GetEngine()->insert(hwnd, ctx->focused_hwnd);
   }

   workspace->ApplyLayout(ctx);

   ctx->toggling_fullscreen_hwnd = nullptr;

   // Update bar visibility
   if ((mon != nullptr) && mon->GetFullscreenCount() > 0) {
      mon->DecrementFullscreenCount();
      if (mon->GetFullscreenCount() == 0 && (mon->GetBar() != nullptr))
         mon->GetBar()->Show();
   }
}

inline void GetPreferredFloatRect(HWND hwnd, Workspace *workspace,
                                  struct BFWMContext *ctx, RECT *out) {
   Monitor *mon = FindMonitorByWorkspace(ctx, workspace);
   RECT area = {};
   if (mon != nullptr) {
      area = mon->GetWorkArea(); // physical pixels
   } else {
      GetWindowRect(hwnd, &area); // physical pixels
   }

   /* Coordinate spaces: GetWorkArea()/GetWindowRect() return PHYSICAL pixels.
    * GetWindowPlacement() reports rcNormalPosition in the window's own
    * coordinate space — logical (96-DPI) pixels for DPI-virtualized unaware
    * targets (the majority of managed windows), physical for PMv2-aware ones.
    * AWARE targets: the norm is already physical, so compare it against the
    * physical area and center the chosen physical size directly (no
    * unscale/ScaleRect round-trip). UNAWARE targets: compare in LOGICAL space
    * (the heuristic's intent for the common unaware case) — unscale the
    * physical area, compare against the (logical) norm, then scale the chosen
    * logical size back to physical for the move. */
   DPI_AWARENESS_CONTEXT target_ctx = GetWindowDpiAwarenessContext(hwnd);
   BOOL const unaware = static_cast<BOOL>(
       (AreDpiAwarenessContextsEqual(target_ctx,
                                     DPI_AWARENESS_CONTEXT_UNAWARE) == TRUE) ||
       (AreDpiAwarenessContextsEqual(
            target_ctx, DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED) == TRUE));

   UINT dpi = ((ctx->dpi != nullptr) && (mon != nullptr))
                  ? ctx->dpi->GetDpi(mon->GetHandle())
                  : DpiSystem::BaseDpi();
   if (dpi == 0)
      dpi = DpiSystem::BaseDpi();

   /* Comparison space: physical for aware targets, logical for unaware ones.
    * The 90%/60% thresholds are applied in the same space as the norm. */
   int const move_window = (unaware == TRUE)
                               ? DpiSystem::Unscale(area.right - area.left, dpi)
                               : (area.right - area.left);
   int const monitor_height =
       (unaware == TRUE) ? DpiSystem::Unscale(area.bottom - area.top, dpi)
                         : (area.bottom - area.top);

   RECT preferred_size = {};

   // Try the window's own "normal" restore size
   WINDOWPLACEMENT window_placement = {.length = sizeof(window_placement),
                                       .flags = {},
                                       .showCmd = {},
                                       .ptMinPosition = {},
                                       .ptMaxPosition = {},
                                       .rcNormalPosition = {}};
   if (GetWindowPlacement(hwnd, &window_placement) != 0) {
      RECT const norm = window_placement.rcNormalPosition;
      int const norm_w = norm.right - norm.left;
      int const norm_h = norm.bottom - norm.top;

      if (norm_w <= move_window * FLOAT_SIZE_PCT_90 / FLOAT_SIZE_PCT_100 &&
          norm_h <= monitor_height * FLOAT_SIZE_PCT_90 / FLOAT_SIZE_PCT_100) {
         preferred_size = norm;
      }
   }

   // If no valid preferred size, use fallback
   if (preferred_size.right == 0 && preferred_size.bottom == 0) {
      preferred_size.left = 0;
      preferred_size.top = 0;
      preferred_size.right =
          move_window * FLOAT_SIZE_PCT_60 / FLOAT_SIZE_PCT_100;
      preferred_size.bottom =
          monitor_height * FLOAT_SIZE_PCT_60 / FLOAT_SIZE_PCT_100;
   }

   if (unaware == TRUE) {
      /* Scale the logical preferred size back to PHYSICAL pixels — the
       * invariant is that *out is a window rect in physical pixels. At
       * dpi == 96 (or with no monitor) ScaleRect is the identity, so the
       * no-monitor fallback is unchanged. */
      RECT const physical = DpiSystem::ScaleRect(preferred_size, dpi);
      *out = CenterRectInRect(area, physical);
   } else {
      /* Aware targets: preferred_size is already physical — center it
       * directly. */
      *out = CenterRectInRect(area, preferred_size);
   }
}

inline auto
HandlePinnedWorkspace(struct BFWMContext *ctx, size_t target_id,
                      const struct WorkspaceConfig *workspace_config) -> int {
   Monitor *assigned_mon = FindMonitorByDisplayNumber(
       ctx, (UINT)workspace_config->assigned_monitor);
   if (assigned_mon == nullptr)
      return -1;

   Workspace *existing = FindWorkspaceOnMonitor(assigned_mon, target_id);
   if (existing == nullptr) {
      // Don't create a duplicate if it exists on another monitor — activate
      // it there instead.
      Workspace *other = FindWorkspaceById(ctx, target_id);
      if (other != nullptr) {
         Monitor *other_mon = FindMonitorByWorkspace(ctx, other);
         if (other_mon != nullptr) {
            WorkspaceActivate(ctx, other_mon, target_id);
            return 0;
         }
      }

      std::wstring const name = L"ws_" + std::to_wstring(target_id);
      existing = CreateWorkspaceForMonitor(ctx, target_id, &name, assigned_mon);
      if (existing != nullptr) {
         existing->ApplyConfig(ctx, target_id);
         assigned_mon->TrackWorkspace(existing);
      }
   }
   if (existing != nullptr)
      WorkspaceActivate(ctx, assigned_mon, target_id);
   return 0;
}

inline auto ActivateExistingWorkspace(struct BFWMContext *ctx,
                                      Workspace *existing, size_t target_id)
    -> int {
   Monitor *mon = FindMonitorByWorkspace(ctx, existing);
   if (mon == nullptr)
      return -1;

   WorkspaceActivate(ctx, mon, target_id);

   if (ctx->focused_workspace != existing) {
      ctx->focused_workspace = existing;
      HWND focused_window = ctx->focused_hwnd;
      if (!existing->ContainsWindow(focused_window) ||
          (IsWindowVisible(focused_window) == 0)) {
         if (existing->GetEngine() != nullptr) {
            focused_window = existing->GetEngine()->get_closest_window(nullptr);
         } else {
            focused_window = nullptr;
         }
      }
      if (focused_window != nullptr) {
         FocusWindow(focused_window, ctx);
      } else {
         if ((ctx->focused_hwnd != nullptr) &&
             (IsWindowVisible(ctx->focused_hwnd) != 0)) {
            BorderManagerSetInactive(ctx, ctx->focused_hwnd);
            BorderManagerRedraw(ctx, ctx->focused_hwnd);
         }
         ctx->focused_hwnd = nullptr;
      }
      BarUpdateRequest(ctx, BAR_UPDATE_DIRTY);
   }
   return 0;
}

// ============================================================
// Move-to-workspace helpers
// ============================================================

inline auto CenterFloatingWindowOnMonitor(Window *win, Monitor *dst_mon,
                                          struct BFWMContext *ctx) {
   RECT work_area = dst_mon->GetWorkArea();
   work_area.top += dst_mon->GetBarHeight();
   RECT window_rect;
   if (GetWindowRect(win->GetHwnd(), &window_rect) != 0) {
      RECT r = CenterRectInRect(work_area, window_rect);
      ClampToRect(work_area, &r);
      /* Synchronous placement: a chosen placement is handed to the window
       * once — no async move in flight, so the ring converges same-commit. */
      IssueRectWithDpiConversion(ctx, win->GetHwnd(), dst_mon->GetHandle(), r,
                                 BFWMSetWindowPos);
      win->MarkOverlayDirty();
      win->SetSavedRect(r);
   }
}

inline auto NameToLayoutType(const char *name) -> LayoutType {
   if (strcmpi_portable(name, "dwindle") == 0)
      return DWINDLE;
   if (strcmpi_portable(name, "monocle") == 0)
      return MONOCLE;
   if (strcmpi_portable(name, "master") == 0)
      return MASTER;
   return LAYOUT_NONE;
}

inline auto SwitchWorkspaceLayout(BFWMContext *ctx, Workspace *workspace,
                                  LayoutType new_type) -> int {
   if ((workspace == nullptr) || EngineType(workspace->GetEngine()) == new_type)
      return 0;

   {
      ScopedLock const lock(ctx->lock);

      /* The engine's gap/border values must be physical. Resolve the
       * workspace's current monitor and scale the logical config for it;
       * with no monitor, ScaleForMonitor falls back to 96 DPI (identity). */
      Monitor *mon = FindMonitorByWorkspace(ctx, workspace);
      HMONITOR hmon = (mon != nullptr) ? mon->GetHandle() : nullptr;
      ScaledLayoutConfig const scaled = ScaleLayoutConfigForMonitor(ctx, hmon);
      auto new_engine = Workspace::CreateLayoutEngine(
          new_type, workspace->GetWorkspaceRect(), scaled.gap_between,
          scaled.gap_edge, scaled.border_width);
      if (new_engine == nullptr) {
         return -1;
      }

      workspace->SetEngine(std::move(new_engine));

      for (size_t index = 0; index < workspace->Windows().size(); index++) {
         HWND hwnd = workspace->Windows()[index]->GetHwnd();
         if (IsWindowManagedByLayout(workspace->Windows()[index]) != 0) {
            workspace->GetEngine()->insert(hwnd, nullptr);
         }
      }
   }

   workspace->ApplyLayout(ctx);
   return 0;
}

inline void RestoreFullscreenForHwnd(Window *win) {
   if ((win == nullptr) || (IsWindow(win->GetHwnd()) == 0))
      return;
   win->SetFullscreen(FALSE);
   SetWindowLong(win->GetHwnd(), GWL_STYLE, win->SavedStyle());
   SetWindowLong(win->GetHwnd(), GWL_EXSTYLE, win->SavedExstyle());
   BFWMEnableNCRendering(win->GetHwnd());
   BFWMSetWindowPosEx(win->GetHwnd(), HWND_NOTOPMOST, win->SavedRectPtr(),
                        SWP_NOCOPYBITS | SWP_FRAMECHANGED);
}

std::array<HWND, MAX_PENDING_KILLS> s_pending_kills;
std::array<DWORD, MAX_PENDING_KILLS> s_pending_kill_times;
size_t s_pending_kill_count;

inline void AddPendingKill(HWND hwnd) {
   if (s_pending_kill_count >= MAX_PENDING_KILLS) {
      Warn("Pending kill list full, dropping HWND %p", hwnd);
      return;
   }
   s_pending_kills[s_pending_kill_count] = hwnd;
   s_pending_kill_times[s_pending_kill_count] = GetTickCount();
   s_pending_kill_count++;
}

} // namespace

auto HandleSpawn(BFWMContext *ctx, BFWMAction *action) -> int {
   auto &args = std::get<ActionArgsSpawn>(action->args);
   return spawn_process(ctx, args.command.c_str(),
                        DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP);
}

auto HandleExec(BFWMContext *ctx, BFWMAction *action) -> int {
   auto &args = std::get<ActionArgsExec>(action->args);
   return spawn_process(ctx, args.command.c_str(), CREATE_NO_WINDOW);
}

auto HandleKillActive(BFWMContext *ctx, BFWMAction *action) -> int {
   (void)action;

   HWND hWnd = nullptr;
   {
      ScopedLock const lock(ctx->lock);
      if (ctx->focused_hwnd == nullptr) {
         Debug("No focused window, ignoring kill command");
         return 0;
      }

      hWnd = ctx->focused_hwnd;
   }

   PostMessage(hWnd, WM_CLOSE, 0, 0);
   AddPendingKill(hWnd);
   return 0;
}

auto HandleMinimize(BFWMContext *ctx, BFWMAction *action) -> int {
   (void)action;
   HWND focused = nullptr;
   {
      ScopedLock const lock(ctx->lock);
      focused = ctx->focused_hwnd;
   }
   if ((focused != nullptr) && (IsWindow(focused) != 0) &&
       (IsIconic(focused) == 0))
      BFWMMinimizeWindow(focused);
   return 0;
}

auto HandleResizeWindow(BFWMContext *ctx, BFWMAction *action) -> int {
   auto &args = std::get<ActionArgsMoveWindow>(action->args);
   return resize_in_direction(ctx, args.direction, args.pixels);
}

void ToggleFullscreenForHwnd(HWND hwnd, BFWMContext *ctx) {
   if (IsWindow(hwnd) == 0)
      return;

   Window *win = ctx->windows->FindByHwnd(hwnd);
   if (win == nullptr)
      return;

   if ((win->ForceFloating() != 0) || (win->ForceTiled() != 0))
      return;

   Workspace *workspace = FindWorkspaceByHwnd(ctx, hwnd);
   if ((workspace == nullptr) || (workspace->GetEngine() == nullptr))
      return;

   Monitor *mon = FindMonitorByWorkspace(ctx, workspace);
   if (mon == nullptr)
      return;

   if (win->IsFullscreen() != 0) {
      ExitFullscreenForHwnd(hwnd, ctx, win, workspace, mon);
   } else {
      EnterFullscreenForHwnd(hwnd, ctx, win, workspace, mon);
   }

    // Ring follows the fullscreen transition: suppressed over the fullscreen
    // window, shown again on restore. Mark dirty (not an immediate sync): the
    // commit-end flush already recomputes suppress from the window's live
    // fullscreen state, so an immediate sync would only paint the ring at the
    // still-in-flight/stale rect and flash before the move lands.
    win->MarkOverlayDirty();
}

auto HandleFullscreen(BFWMContext *ctx, BFWMAction *action) -> int {
   (void)action;
   if ((ctx == nullptr) || (ctx->focused_hwnd == nullptr))
      return -1;
   ToggleFullscreenForHwnd(ctx->focused_hwnd, ctx);
   return 0;
}

auto HandleToggleFloat(BFWMContext *ctx, BFWMAction *action) -> int {
   (void)action;

   HWND focused = nullptr;
   Workspace *workspace = nullptr;
   Window *win = nullptr;
   BOOL do_unfloat = FALSE;
   RECT saved_rect = {};
   {
      ScopedLock const lock(ctx->lock);
      focused = ctx->focused_hwnd;
      workspace = ctx->focused_workspace;
      if ((focused == nullptr) || (workspace == nullptr) ||
          (workspace->GetEngine() == nullptr)) {
         return -1;
      }

      win = ctx->windows->FindByHwnd(focused);
      if (win == nullptr) {
         return -1;
      }

      // Force flags take priority — block the toggle
      if ((win->ForceFloating() != 0) || (win->ForceTiled() != 0)) {
         return 0;
      }

      // Fullscreen takes priority — ignore float toggle
      if ((win->IsFullscreen() != 0) || (win->IsUnmanagedFullscreen() != 0)) {
         return 0;
      }

      do_unfloat = win->IsFloating();
      saved_rect = win->SavedRect();

      if (do_unfloat != 0) {
         win->SetFloating(FALSE);
         workspace->GetEngine()->insert(win->GetHwnd(), focused);
      } else {
         GetPreferredFloatRect(win->GetHwnd(), workspace, ctx, &saved_rect);
         win->SetFloating(TRUE);
         win->SetSavedRect(saved_rect);
         workspace->GetEngine()->remove(win->GetHwnd());
      }
   }

   workspace->ApplyLayout(ctx);

   if (do_unfloat == 0) {
      /* saved_rect is physical (GetPreferredFloatRect output / clamped work
       * area); a DPI-unaware target on a scaled monitor must receive it in its
       * own logical space or the DWM double-scales it (see
       * IssueRectWithDpiConversion). */
      Monitor *mon = FindMonitorByWorkspace(ctx, workspace);
      if (mon != nullptr) {
         IssueRectWithDpiConversion(ctx, win->GetHwnd(), mon->GetHandle(),
                                    saved_rect, BFWMSetWindowPos);
      } else {
         BFWMSetWindowPos(win->GetHwnd(), &saved_rect);
      }
   }
   win->MarkOverlayDirty();

   return 0;
}

auto HandleMoveWindow(BFWMContext *ctx, BFWMAction *action) -> int {
   auto &args = std::get<ActionArgsMoveWindow>(action->args);

   HWND focused = nullptr;
   Workspace *workspace = nullptr;
   Window *win = nullptr;
   {
      ScopedLock const lock(ctx->lock);
      focused = ctx->focused_hwnd;
      workspace = ctx->focused_workspace;
      win = ctx->windows->FindByHwnd(focused);

      // For floating windows, offset the saved rect by a fixed increment
      if ((win != nullptr) && (win->IsFloating() != 0)) {
         static const int FLOAT_MOVE_PX = 20;
         int delta_x = 0;
         int delta_y = 0;
         switch (args.direction) {
         case DirLeft:
            delta_x = -FLOAT_MOVE_PX;
            break;
         case DirRight:
            delta_x = FLOAT_MOVE_PX;
            break;
         case DirUp:
            delta_y = -FLOAT_MOVE_PX;
            break;
         case DirDown:
            delta_y = FLOAT_MOVE_PX;
            break;
         default:
            return 0;
         }
         OffsetRect(win->SavedRectPtr(), delta_x, delta_y);

         // Clamp to monitor work area so the window stays fully visible
         Monitor *mon = FindMonitorByWorkspace(ctx, workspace);
         if (mon != nullptr) {
            UINT const dpi = (ctx->dpi != nullptr)
                                 ? ctx->dpi->GetDpi(mon->GetHandle())
                                 : DpiSystem::BaseDpi();
            RECT const clamped = win->ClampTo(mon->GetWorkArea(), dpi);
            win->SetSavedRect(clamped);
         }
      }
   }

   if ((win != nullptr) && (win->IsFloating() != 0)) {
      BFWMSetWindowPos(win->GetHwnd(), win->SavedRectPtr());
      win->MarkOverlayDirty();
      return 0;
   }

   {
      ScopedLock const lock(ctx->lock);
      bool moved = false;
      if ((workspace != nullptr) && (workspace->GetEngine() != nullptr)) {
         moved = workspace->GetEngine()->move_window(focused, args.direction);
      }

      if (moved) {
         ctx->transaction.QueueRelayout(workspace);
      } else if (workspace != nullptr) {
         // No move/promotion possible in this direction — move to neighboring
         // workspace
         Workspace *neighbouring_ws =
             FindNeighbouringWorkspace(ctx, args.direction);
         if ((neighbouring_ws != nullptr) && neighbouring_ws != workspace) {
            win = ctx->windows->FindByHwnd(focused);
            if (win != nullptr) {
               workspace->RemoveWindow(win);
               ctx->transaction.QueueRelayout(workspace);
               neighbouring_ws->AddWindow(ctx, win);
            }
         }
      }
   }

   return 0;
}

auto HandleFocus(BFWMContext *ctx, BFWMAction *action) -> int {
   auto &args = std::get<ActionArgsFocus>(action->args);

   Workspace *workspace = nullptr;
   Workspace *neighbouring_ws = nullptr;
   BOOL switch_to_ws = FALSE;
   {
      ScopedLock const lock(ctx->lock);

      workspace = ctx->focused_workspace;
      HWND focused = ctx->focused_hwnd;

      // Try current workspace first
      HWND neighbor = nullptr;
      if ((workspace != nullptr) && (workspace->GetEngine() != nullptr)) {
         neighbor =
             workspace->GetEngine()->get_neighbor(focused, args.direction);
      }

      if ((neighbor == nullptr) && (workspace != nullptr) &&
          (args.direction == DirNext || args.direction == DirPrev) &&
          (workspace->GetEngine() != nullptr)) {
         neighbor = workspace->GetEngine()->get_closest_window(focused);
      }

      neighbouring_ws = nullptr;

      if (neighbor == nullptr) {
         neighbouring_ws = FindNeighbouringWorkspace(ctx, args.direction);
         if ((neighbouring_ws != nullptr) &&
             (neighbouring_ws->GetEngine() != nullptr)) {
            neighbor =
                neighbouring_ws->GetEngine()->get_closest_window(focused);
         }
      }

      // Skip unmanaged fullscreen windows in keyboard focus navigation
      if (neighbor != nullptr) {
         Window *neighbor_win = ctx->windows->FindByHwnd(neighbor);
         if ((neighbor_win != nullptr) &&
             (neighbor_win->IsUnmanagedFullscreen() != 0))
            neighbor = nullptr;
      }

      HWND old_focused = ctx->focused_hwnd;
      switch_to_ws = static_cast<BOOL>((neighbor == nullptr) &&
                                       (neighbouring_ws != nullptr) &&
                                       neighbouring_ws != workspace);

      if (neighbor != nullptr) {
         ctx->transaction.QueueFocus(neighbor);
         if ((old_focused != nullptr) && old_focused != neighbor &&
             (IsWindow(old_focused) != 0) &&
             (IsWindowVisible(old_focused) != 0)) {
            ctx->transaction.QueueBorderColor(old_focused,
                                              ctx->config.inactive_border);
         }
         if ((IsWindow(neighbor) != 0) && (IsWindowVisible(neighbor) != 0)) {
            ctx->transaction.QueueBorderColor(neighbor,
                                              ctx->config.border_color);
         }
      } else if (switch_to_ws != 0) {
         if ((old_focused != nullptr) && (IsWindowVisible(old_focused) != 0)) {
            BorderManagerSetInactive(ctx, old_focused);
            BorderManagerRedraw(ctx, old_focused);
         }
         ctx->focused_hwnd = nullptr;
         ctx->focused_workspace = neighbouring_ws;
      }
   }

   // Blocking calls outside lock
   if ((neighbouring_ws != nullptr) && neighbouring_ws != workspace) {
      Monitor *mon = FindMonitorByWorkspace(ctx, neighbouring_ws);
      if ((mon != nullptr) && neighbouring_ws != mon->GetActiveWorkspace()) {
         WorkspaceActivateSimple(ctx, mon, mon->GetActiveWorkspace(),
                                 neighbouring_ws, true);
      }
   }

   if (switch_to_ws != 0)
      BarUpdateRequest(ctx, BAR_UPDATE_DIRTY);

   return 0;
}

auto HandleWorkspace(BFWMContext *ctx, BFWMAction *action) -> int {
   auto &args = std::get<ActionArgsWorkspace>(action->args);
   auto target_id = (size_t)args.index;

   const struct WorkspaceConfig *workspace_config =
       FindWorkspaceConfig(ctx, target_id);
   if ((workspace_config != nullptr) &&
       workspace_config->assigned_monitor > 0) {
      if (HandlePinnedWorkspace(ctx, target_id, workspace_config) == 0)
         return 0;
   }

   Workspace *existing = FindWorkspaceById(ctx, target_id);
   if (existing != nullptr) {
      if (ActivateExistingWorkspace(ctx, existing, target_id) == 0)
         return 0;
   }

   Workspace *workspace = ctx->focused_workspace;
   Monitor *mon = FindMonitorByWorkspace(ctx, workspace);
   if (mon == nullptr)
      return 0;
   WorkspaceActivate(ctx, mon, target_id);
   return 0;
}

auto HandleMoveToWorkspace(BFWMContext *ctx, BFWMAction *action) -> int {
   auto &args = std::get<ActionArgsMoveToWorkspace>(action->args);
   Workspace *src_ws = ctx->focused_workspace;
   if (src_ws == nullptr)
      return 0;
   auto target_id = (size_t)args.index;

   /* Check if workspace is pinned — redirect to assigned monitor */
   const struct WorkspaceConfig *workspace_config =
       FindWorkspaceConfig(ctx, target_id);
   Monitor *assigned_mon = nullptr;
   if ((workspace_config != nullptr) &&
       workspace_config->assigned_monitor > 0) {
      assigned_mon = FindMonitorByDisplayNumber(
          ctx, (UINT)workspace_config->assigned_monitor);
      if (assigned_mon == nullptr) {
         return 0;
      }
   }

   Window *win = ctx->windows->FindByHwnd(ctx->focused_hwnd);
   if (win == nullptr) {
      // No focused window — just switch to workspace
      return HandleWorkspace(ctx, action);
   }

   Monitor *src_mon = FindMonitorByWorkspace(ctx, src_ws);
   if (src_mon == nullptr)
      return 0;

   // Global lookup for destination workspace
   Workspace *dst_ws = FindWorkspaceById(ctx, target_id);
   Monitor *dst_mon = nullptr;

   if (dst_ws == nullptr) {
      // If workspace is pinned, create on the assigned monitor
      Monitor *create_on = (assigned_mon != nullptr) ? assigned_mon : src_mon;
      std::wstring const name = L"ws_" + std::to_wstring(target_id);
      dst_ws = CreateWorkspaceForMonitor(ctx, target_id, &name, create_on);
      if (dst_ws == nullptr)
         return 0;
      dst_ws->ApplyConfig(ctx, target_id);
      create_on->TrackWorkspace(dst_ws);
      dst_mon = create_on;
   } else {
      dst_mon = FindMonitorByWorkspace(ctx, dst_ws);
      if (dst_mon == nullptr)
         return 0;
   }

   if (dst_ws == src_ws)
      return 0;

   src_ws->RemoveWindow(win);
   dst_ws->AddWindow(ctx, win);
   src_ws->ApplyLayout(ctx);
   dst_ws->ApplyLayout(ctx);

   BarUpdateRequest(ctx, BAR_UPDATE_DIRTY);

   // Activate destination workspace (cross-monitor supported)
   WorkspaceActivate(ctx, dst_mon, target_id);
   BarUpdateRequest(ctx, BAR_UPDATE_DIRTY);

   // Same-monitor moves: AddWindow → FocusWindow → WorkspaceActivateForWindow
   // already activated dst_ws above (with destroy_empty_old_ws = FALSE), so
   // WorkspaceActivate short-circuits at its "already active" branch and
   // never destroys the source workspace. Destroy it here if it is empty.
   // Cross-monitor moves keep the empty source workspace active on its own
   // monitor.
   if (dst_mon == src_mon) {
      // Guard: if the window was not actually adopted (iconic window or a
      // failed layout insert), AddWindow never called FocusWindow, so
      // WorkspaceActivate took its normal path and already destroyed src_ws.
      // Only destroy when the source workspace is still tracked.
      bool src_tracked = false;
      for (auto *workspace : src_mon->Workspaces()) {
         if (workspace == src_ws) {
            src_tracked = true;
            break;
         }
      }
      if (src_tracked)
         WorkspaceDestroyIfEmpty(src_mon, src_ws);
   }

   // Ensure the moved window is not cloaked — ApplyLayout's
   // re-cloak logic may have cloaked it when the source became empty.
   BOOL is_cloaked = FALSE;
   DwmGetWindowAttribute(win->GetHwnd(), DWMWA_CLOAKED, &is_cloaked,
                         sizeof(is_cloaked));
   if (is_cloaked != 0) {
      SetWindowCloakState(win->GetHwnd(), FALSE);
      win->SetCloaked(FALSE);
   }

   // Center floating windows on the destination monitor
   if (win->IsFloating() != 0)
      CenterFloatingWindowOnMonitor(win, dst_mon, ctx);

   return 0;
}

auto HandleSplit(BFWMContext *ctx, BFWMAction *action) -> int {
   (void)action;

   Workspace *workspace = nullptr;
   bool toggled = false;
   {
      ScopedLock const lock(ctx->lock);
      workspace = ctx->focused_workspace;
      HWND focused = ctx->focused_hwnd;
      if ((workspace == nullptr) || (workspace->GetEngine() == nullptr)) {
         return -1;
      }
      toggled = workspace->GetEngine()->toggle_split(focused);
   }

   if (toggled)
      workspace->ApplyLayout(ctx);
   return toggled ? 0 : -1;
}

auto HandleSwapSplit(BFWMContext *ctx, BFWMAction *action) -> int {
   (void)action;

   Workspace *workspace = nullptr;
   bool swapped = false;
   {
      ScopedLock const lock(ctx->lock);
      workspace = ctx->focused_workspace;
      HWND focused = ctx->focused_hwnd;
      if ((workspace == nullptr) || (workspace->GetEngine() == nullptr)) {
         return -1;
      }
      swapped = workspace->GetEngine()->swap_split(focused);
   }

   if (swapped)
      workspace->ApplyLayout(ctx);
   return swapped ? 0 : -1;
}

auto HandleLayout(BFWMContext *ctx, BFWMAction *action) -> int {
   auto &args = std::get<ActionArgsLayout>(action->args);
   LayoutType const layout_type = NameToLayoutType(args.layout_name.c_str());
   if (layout_type == LAYOUT_NONE) {
      Warn("HandleLayout: unknown layout '%s'", args.layout_name.c_str());
      return -1;
   }
   Workspace *workspace = ctx->focused_workspace;
   if (workspace == nullptr)
      return -1;
   return SwitchWorkspaceLayout(ctx, workspace, layout_type);
}

auto HandleCycleLayout(BFWMContext *ctx, BFWMAction *action) -> int {
   auto &args = std::get<ActionArgsCycleLayout>(action->args);
   Workspace *workspace = ctx->focused_workspace;
   if (workspace == nullptr)
      return -1;

   LayoutType const cur = EngineType(workspace->GetEngine());
   LayoutType next;

   switch (args.direction) {
   case DirNext:
      next = static_cast<LayoutType>(static_cast<int>(cur) + 1);
      if (next >= LAYOUT_TYPE_COUNT)
         next = static_cast<LayoutType>(0);
      break;
   case DirPrev:
      next = static_cast<LayoutType>(static_cast<int>(cur) - 1);
      if (next < 0 || next >= LAYOUT_TYPE_COUNT)
         next = static_cast<LayoutType>(LAYOUT_TYPE_COUNT - 1);
      break;
   default:
      return -1;
   }

   return SwitchWorkspaceLayout(ctx, workspace, next);
}

auto HandleCustom(BFWMContext *ctx, BFWMAction *action) -> int {
   auto &args = std::get<ActionArgsCustom>(action->args);
   LuaConfigCall(&ctx->lua, ctx, args.lua_callback.c_str());
   return 0;
}

auto HandleReloadConfig(BFWMContext *ctx, BFWMAction *action) -> int {
   (void)action;

   /* Re-resolve the config path on every reload so a deleted primary config
      (e.g. %APPDATA%\BFWMwm\config.lua) falls back to the secondary
      location (%USERPROFILE%\.config\BFWMwm\config.lua). */
   std::array<char, 1024> resolved{};
   const char *reload_path = nullptr;
   if (ResolveConfigPath("config.lua", resolved.data(), resolved.size())) {
      reload_path = resolved.data();
   }
   int const result = LuaConfigLoad(&ctx->lua, ctx, reload_path) ? 0 : -1;

   Debug("ReloadConfig: updating bar_height and recalculating workspace rects");
   ctx->config.bar_height =
       ctx->config.bar_cfg.enabled ? ctx->config.bar_cfg.height : 0;
   for (size_t index = 0; index < ctx->monitors->Size(); index++) {
      Monitor *mon = ctx->monitors->At(index);
      /* bar_height is logical config; store it scaled to each monitor's
       * physical pixels (the value reserved from the work area). */
      mon->SetBarHeight(
          ctx->dpi->ScaleForMonitor(mon->GetHandle(), ctx->config.bar_height));
   }
   RecalculateAllWorkspaceRects(ctx);

   // Bars are destroyed + recreated by the presenter (main.c PresentBars)
   // at the end of the MainLoop iteration.
   BarUpdateRequest(ctx, BAR_UPDATE_RECREATE);

   Debug("ReloadConfig: pushing gaps");
   PushGapConfigToAllWorkspaces(ctx);

   // Re-apply border colors so a changed border_color / inactive_border
   // takes effect immediately.
   Debug("ReloadConfig: re-applying border colors");
   for (size_t index = 0; index < ctx->monitors->Size(); index++) {
      Monitor *mon = ctx->monitors->At(index);
      for (auto *workspace : mon->Workspaces()) {
         for (const auto &window : workspace->Windows()) {
            HWND hwnd = window->GetHwnd();
            if (IsWindowVisible(hwnd) != 0)
               BorderManagerSetInactive(ctx, hwnd);
            window->SetOverlayBorder(ctx->config.border_width,
                                     ctx->config.border_radius);
         }
      }
   }
   if ((ctx->focused_hwnd != nullptr) &&
       (IsWindowVisible(ctx->focused_hwnd) != 0))
      BorderManagerSetActive(ctx, ctx->focused_hwnd);

   Debug("ReloadConfig: done");
   return result;
}

auto HandleToggleGaps(BFWMContext *ctx, BFWMAction *action) -> int {
   (void)action;
   BOOL enabled = FALSE;
   {
      ScopedLock const lock(ctx->lock);
      ctx->config.gaps_enabled =
          static_cast<BOOL>(ctx->config.gaps_enabled == 0);
      enabled = ctx->config.gaps_enabled;
   }
   Info("Gaps %s", (enabled != 0) ? "enabled" : "disabled");
   PushGapConfigToAllWorkspaces(ctx);
   return 0;
}
auto HandleMoveWorkspaceToMonitor(BFWMContext *ctx, BFWMAction *action)
    -> int {
   if ((ctx == nullptr) || (action == nullptr))
      return -1;
   auto &args = std::get<ActionArgsMoveWorkspaceToMonitor>(action->args);
   Workspace *workspace = ctx->focused_workspace;
   if (workspace == nullptr)
      return 0;

   const struct WorkspaceConfig *workspace_config =
       FindWorkspaceConfig(ctx, workspace->GetIdentifier());
   if ((workspace_config != nullptr) &&
       workspace_config->assigned_monitor > 0) {
      return 0;
   }

   Monitor *src_mon = FindMonitorByWorkspace(ctx, workspace);
   if (src_mon == nullptr)
      return 0;

   Monitor *dst_mon = nullptr;
   if (args.index >= 0) {
      dst_mon = FindMonitorByDisplayNumber(ctx, (UINT)args.index);
   } else {
      dst_mon = FindNeighbouringMonitor(ctx, src_mon, args.direction);
   }
   if ((dst_mon == nullptr) || dst_mon == src_mon)
      return 0;

   InfoW(L"Moving workspace %zu from monitor %u to monitor %u",
         workspace->GetIdentifier(), src_mon->GetDisplayNumber(),
         dst_mon->GetDisplayNumber());

   ctx->transaction.Begin();

   BOOL const was_active =
       static_cast<BOOL>(src_mon->GetActiveWorkspace() == workspace);

   // --- Step 1: handle source monitor BEFORE moving workspace ---
   // Activate the next workspace on the source monitor first, so that
   // workspace's windows are cloaked while they're still on the source monitor.
   // This prevents the cloak from affecting the destination later.
   Workspace *src_other_ws = nullptr;
   if (was_active != 0) {
      for (const auto &j : src_mon->Workspaces()) {
         if (j != workspace) {
            src_other_ws = j;
            break;
         }
      }
      if (src_other_ws != nullptr) {
         WorkspaceActivateSimple(ctx, src_mon, workspace, src_other_ws, true);
      }
   }

   // --- Step 2: move workspace between monitors ---
   int ws_idx = -1;
   for (size_t j = 0; j < src_mon->Workspaces().size(); j++) {
      if (src_mon->Workspaces()[j] == workspace) {
         ws_idx = (int)j;
         break;
      }
   }
   if (ws_idx < 0) {
      BFWMTransactionCommit(ctx);
      return 0;
   }
   src_mon->RemoveWorkspaceAt((size_t)ws_idx);

   dst_mon->TrackWorkspace(workspace);

   // Recalculate workspace rect for new monitor geometry
   workspace->RecalculateRect(dst_mon, ctx);
   // User explicitly moved this workspace — clear origin so auto-restore
   // on monitor reconnect does not pull it back.
   workspace->ClearOriginMonitorUid();

   // --- Step 3: source monitor had no other workspace — create a fresh one ---
   if ((was_active != 0) && src_mon->Workspaces().empty()) {
      size_t const new_id = FindNextWorkspaceId(ctx);
      std::wstring const name = L"ws_" + std::to_wstring(new_id);
      auto *new_ws = CreateWorkspaceForMonitor(ctx, new_id, &name, src_mon);
      if (new_ws != nullptr) {
         new_ws->ApplyConfig(ctx, new_id);
         src_mon->TrackWorkspace(new_ws);
         src_mon->SetActiveWorkspace(new_ws);
      }
   }

   // --- Step 4: activate the moved workspace on destination ---
   ctx->focused_workspace = workspace;
   WorkspaceActivate(ctx, dst_mon, workspace->GetIdentifier());

   BFWMTransactionCommit(ctx);

   BarUpdateRequest(ctx, BAR_UPDATE_DIRTY);
   return 0;
}

void RestoreFullscreenWindows(BFWMContext *ctx) {
   for (const auto &window : ctx->windows->Windows()) {
      Window *win = window.get();
      if (win->IsFullscreen() != 0)
         RestoreFullscreenForHwnd(win);
   }
   for (auto *mon : ctx->monitors->Monitors()) {
      if (mon->GetBar() != nullptr)
         mon->GetBar()->Show();
   }
}

void CheckPendingKills(BFWMContext *ctx) {
   size_t index = 0;
   while (index < s_pending_kill_count) {
      HWND hwnd = s_pending_kills[index];

      if (IsWindow(hwnd) == 0) {
         s_pending_kills[index] = s_pending_kills[--s_pending_kill_count];
         continue;
      }

      if (GetTickCount() - s_pending_kill_times[index] >= KILL_TIMEOUT_MS) {
         // If the window handled WM_CLOSE by hiding itself (tray app),
         // don't force-kill — that would trigger a restart cycle.
         if ((IsWindowVisible(hwnd) == 0) || (IsIconic(hwnd) != 0)) {
            s_pending_kills[index] = s_pending_kills[--s_pending_kill_count];
            continue;
         }

         DWORD pid = 0;
         GetWindowThreadProcessId(hwnd, &pid);
         if (pid != 0) {
            HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (hProcess != nullptr) {
               if (TerminateProcess(hProcess, 1) != 0) {
                  CleanupTerminatedWindow(ctx, hwnd);
               }
               CloseHandle(hProcess);
            }
         }
         s_pending_kills[index] = s_pending_kills[--s_pending_kill_count];
         continue;
      }

      index++;
   }
}
