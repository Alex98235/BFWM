#include "events.h"
#include "../../core/bfwm_context.h"
#include "../../core/sync.h"
#include "../../dpi/dpi.h"
#include "../../logging/logger.h"
#include "../../monitor/monitor.h"
#include "../../notification/snackbar.h"
#include "../../win/win_error.h"
#include "../../win/win_utils.h"
#include "../../workspace/placement.h"
#include "../../workspace/workspace.h"
#include "../border/border_manager.h"
#include "../border/overlay.h"
#include "../filter.h"
#include "../fullscreen/detect.h"
#include "../rules/rules.h"
#include "../window.h"
#include <cstdlib>
#include <cwchar>
#include <dwmapi.h>
#include <memory>
#include <minwindef.h>
#include <stringapiset.h>
#include <synchapi.h>
#include <utility>
#include <vector>
#include <windef.h>
#include <windows.h>
#include <winnls.h>
#include <winnt.h>

enum {
   WS_NAME_BUF_SIZE = 32,
   WINDOW_STR_BUF_SIZE = 256,
   MIN_WINDOW_WIDTH = 100,
   MIN_WINDOW_HEIGHT = 150,
   MOD_KEY_DOWN_MASK = 0x8000,
};

namespace {

// ── UTF-8 helper ──
// Convert a wide string to its UTF-8 narrow representation (two-pass
// WideCharToMultiByte with an exact-size output buffer).
inline auto WideToNarrow(std::wstring const &wide) -> std::string {
   if (wide.empty())
      return {};
   int const len = WideCharToMultiByte(
       CP_UTF8, 0, wide.data(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
   if (len <= 0)
      return {};
   std::string narrow;
   narrow.resize((size_t)len);
   WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), narrow.data(),
                       len, nullptr, nullptr);
   return narrow;
}

// ── WinEvent hook handles ──
BFWMContext *ctx = nullptr;
HWINEVENTHOOK g_obj_hook = nullptr; // EVENT_OBJECT_DESTROY..LOCATIONCHANGE
HWINEVENTHOOK g_foreground_hook = nullptr; // EVENT_SYSTEM_FOREGROUND
HWINEVENTHOOK g_minimize_hook =
    nullptr;                         // EVENT_SYSTEM_MINIMIZESTART..MINIMIZEEND
HWINEVENTHOOK g_move_hook = nullptr; // EVENT_SYSTEM_MOVESIZESTART..MOVESIZEEND

/* ── Border overlay sync ──
 * The per-window ring follows its target through the event pipeline. The
 * "auto" variant derives the fullscreen suppress from the window's own flags;
 * the explicit variant forces a state (minimize hides, restore shows). Both
 * no-op when the window has no overlay ("border dies, WM survives"). */

inline void SyncWindowOverlay(BFWMContext *ctx, HWND hwnd, BOOL suppress) {
   Window *win = ctx->windows->FindByHwnd(hwnd);
   if (win != nullptr)
      win->SyncOverlay(suppress);
}

inline void SyncWindowOverlayAuto(BFWMContext *ctx, HWND hwnd) {
   Window *win = ctx->windows->FindByHwnd(hwnd);
   if (win == nullptr)
      return;
   win->SyncOverlay(static_cast<BOOL>((win->IsFullscreen() == TRUE) ||
                                      (win->IsUnmanagedFullscreen() == TRUE)));
}

inline auto NewWindowTrigger(HWND hwnd, BFWMContext *ctx, BOOL startup)
    -> enum WindowTriggerReturnType {
   if (ctx == nullptr)
      return NO_CONTEXT;

   if (ctx->windows->FindByHwnd(hwnd) != nullptr) {
      return WINDOW_ALREADY_REGISTERED;
   }

   if (IsManageableWindow(ctx, hwnd, startup)) {
      // Reject owned windows — tooltips, dropdowns, context menus
      HWND owner = GetWindow(hwnd, GW_OWNER);
      if ((owner != nullptr) && (ctx->windows->FindByHwnd(owner) != nullptr))
         return REGISTER_SKIPPED;

      // Reject very small windows (dialogs, Steam update popups, etc.)
      {
         RECT rect;
         if ((GetWindowRect(hwnd, &rect) == TRUE) &&
             (rect.right - rect.left < MIN_WINDOW_WIDTH ||
              rect.bottom - rect.top < MIN_WINDOW_HEIGHT))
            return REGISTER_SKIPPED;
      }

      std::wstring window_title;
      std::wstring window_class;

      window_title.resize(WINDOW_STR_BUF_SIZE);
      int const title_len =
          GetWindowTextW(hwnd, window_title.data(), WINDOW_STR_BUF_SIZE);
      window_title.resize(title_len > 0 ? (size_t)title_len : 0);

      window_class.resize(WINDOW_STR_BUF_SIZE);
      int const class_len =
          GetClassNameW(hwnd, window_class.data(), WINDOW_STR_BUF_SIZE);
      if (class_len == 0) {
         BFWMLogLastError("Could not get window class");
         return NO_CLASS;
      }
      window_class.resize((size_t)class_len);

      // Convert class name and title to narrow strings for rule matching.
      std::string const class_name_a = WideToNarrow(window_class);
      std::string const title_a = WideToNarrow(window_title);

      // Fetch process name once for all rule checks.
      std::string process_buf(MAX_PATH, '\0');
      const char *process =
          GetWindowProcessName(hwnd, process_buf.data(), process_buf.size());

      // Check hardcoded blacklist and user ignore rules before creating
      // the Window.
      if (IsExcludedByRules(ctx, class_name_a.data(), title_a.data(),
                            process)) {
         Debug("Window excluded by rules: hwnd=%p class=%s title=%s "
               "process=%s",
               (void *)hwnd, class_name_a.data(), title_a.data(),
               (process != nullptr) ? process : "(null)");
         return REGISTER_SKIPPED;
      }

      auto window_owner = std::make_unique<Window>(hwnd, window_title);
      Window *window = window_owner.get();

      // Detect fullscreen windows at registration time so they never enter
      // the layout tree.
      FullscreenType const fullscreen_type = DetectFullscreenWindow(hwnd);
      if ((fullscreen_type &
           (FS_EXCLUSIVE_FULLSCREEN | FS_BORDERLESS_WINDOW)) != 0)
         window->SetUnmanagedFullscreen(TRUE);

      // Restore maximized windows so the layout can position them — the
      // OS-level WS_MAXIMIZE state overrides SetWindowPos.
      if (IsZoomed(hwnd) == TRUE)
         ShowWindow(hwnd, SW_RESTORE);

      // Apply non-ignore rules (floating, workspace).
      WindowRulesApply(ctx, window, class_name_a.data(), title_a.data(),
                       process);

      ctx->windows->Register(std::move(window_owner));

      // Create the border overlay ring. OverlayCreate establishes
      // initial geometry/visibility; a fullscreen window's ring stays
      // suppressed until it exits fullscreen. Construction never fails — on
      // failure the overlay is inert and every method no-ops.
      window->SetOverlay(std::make_unique<Overlay>(
          ctx, window, ctx->config.border_color, ctx->config.border_width,
          ctx->config.border_radius));
      if (window->IsUnmanagedFullscreen() == TRUE)
         window->SyncOverlay(TRUE);

      DebugW(L"Registered window: hwnd=%p title=\"%ls\" class=\"%ls\" "
             L"startup=%d",
             (void *)hwnd, window_title.c_str(), window_class.c_str(), startup);

      return REGISTER_SUCCESSFUL;
   }

   return REGISTER_SKIPPED;
}

/**
 * @brief Find the best window to focus after a window is removed/minimized.
 *
 * Priority: same workspace -> all other workspaces on all monitors.
 * Returns nullptr if no window (other than @c exclude_hwnd) exists anywhere.
 */
inline auto IsUnmanagedFullscreen(BFWMContext *ctx, HWND hwnd) -> BOOL {
   Window *w = ctx->windows->FindByHwnd(hwnd);
   return static_cast<BOOL>((w != nullptr) &&
                            (w->IsUnmanagedFullscreen() == TRUE));
}

inline auto FindNextFocusTarget(BFWMContext *ctx, Workspace *current_ws,
                                HWND exclude_hwnd) -> HWND {
   if (current_ws != nullptr) {
      // Prefer fullscreen windows — focusing a non-fullscreen window
      // triggers UpdateFocusTracking's z-order fix that lowers fullscreen
      // windows and raises all tiled windows.
      for (const auto &window : current_ws->Windows()) {
         HWND candidate = window->GetHwnd();
         if (candidate != exclude_hwnd &&
             (IsUnmanagedFullscreen(ctx, candidate) == FALSE)) {
            Window *w = ctx->windows->FindByHwnd(candidate);
            if ((w != nullptr) && (w->IsFullscreen() == TRUE))
               return candidate;
         }
      }

      if (current_ws->GetEngine() != nullptr) {
         HWND first = current_ws->GetEngine()->get_closest_window(exclude_hwnd);
         if ((first != nullptr) && first != exclude_hwnd &&
             (IsUnmanagedFullscreen(ctx, first) == 0))
            return first;
      }
      for (const auto &window : current_ws->Windows()) {
         HWND candidate = window->GetHwnd();
         if (candidate != exclude_hwnd &&
             (IsUnmanagedFullscreen(ctx, candidate) == 0))
            return candidate;
      }

      // current_ws is empty — keep the empty workspace alive and
      // leave focused_hwnd nullptr.  The user can navigate away with a
      // keybind or click.
   }

   return nullptr;
}

// ------------------------------------------------------------------
// Window transition detection  (formerly in HandleObjectLocationChange)
// ------------------------------------------------------------------

/**
 * @brief Check for fullscreen/maximize/restore transitions and update state.
 *
 * Called from ProcessMoveSizeEnd (user drag) and ProcessCreateOrForeground
 * (focus change).  Runs inside a transaction so it may queue ops.
 *
 * This replaces the per-pixel LOCATIONCHANGE handler.  Transitions are
 * now detected only at meaningful checkpoints (end of drag, focus change)
 * instead of on every pixel change.
 */
inline void CheckWindowTransitions(BFWMContext *ctx, HWND hwnd) {
   if (ctx->toggling_fullscreen_hwnd == hwnd)
      return;

   Window *win = ctx->windows->FindByHwnd(hwnd);
   if (win == nullptr)
      return;

   Workspace *workspace = FindWorkspaceByHwnd(ctx, hwnd);
   if ((workspace == nullptr) || (workspace->GetEngine() == nullptr))
      return;

   {
      Monitor *mon = FindMonitorByWorkspace(ctx, workspace);
      if ((mon != nullptr) && workspace != mon->GetActiveWorkspace())
         return;
   }

   FullscreenType const fullscreen_type = DetectFullscreenWindow(hwnd);

   // ── Unmanaged fullscreen → not fullscreen (game went windowed) ──
   if ((win->IsUnmanagedFullscreen() == TRUE) &&
       fullscreen_type == FS_NOT_FULLSCREEN) {
      win->SetUnmanagedFullscreen(FALSE);
      SyncWindowOverlayAuto(ctx, hwnd);
      if (IsWindowManagedByLayout(win) == TRUE)
         workspace->GetEngine()->insert(hwnd, ctx->focused_hwnd);
      ctx->transaction.QueueRelayout(workspace);
      return;
   }

   // ── Managed fullscreen → not fullscreen (externally toggled) ──
   if ((win->IsFullscreen() == TRUE) && fullscreen_type == FS_NOT_FULLSCREEN) {
      win->SetFullscreen(FALSE);
      SyncWindowOverlayAuto(ctx, hwnd);
      ctx->transaction.QueueSetWindowStyle(hwnd, win->SavedStyle(),
                                           win->SavedExstyle());
      ctx->transaction.QueueZOrder(hwnd, HWND_NOTOPMOST);

      if (IsWindowManagedByLayout(win) == TRUE)
         workspace->GetEngine()->insert(hwnd, ctx->focused_hwnd);
      ctx->transaction.QueueRelayout(workspace);

      Monitor *mon = FindMonitorByWorkspace(ctx, workspace);
      if (mon != nullptr) {
         if (mon->GetFullscreenCount() > 0)
            mon->DecrementFullscreenCount();
         if (mon->GetFullscreenCount() == 0)
            ctx->transaction.QueueBarVisibility(hwnd, TRUE);
      }
      return;
   }

   // ── Maximized → not maximized (user restored from taskbar) ──
   if ((win->IsMaximized() == TRUE) && (IsZoomed(hwnd) == FALSE)) {
      win->SetMaximized(FALSE);
      win->SetFloating(FALSE);
      if (IsWindowManagedByLayout(win) == TRUE)
         workspace->GetEngine()->insert(hwnd, ctx->focused_hwnd);
      ctx->transaction.QueueRelayout(workspace);
      return;
   }

   // ── Managed → becoming maximized ──
   BOOL const window_is_managed = IsWindowManagedByLayout(win);
   if ((window_is_managed == TRUE) && (win->ForceTiled() == FALSE) &&
       (win->ForceFloating() == FALSE) &&
       ((fullscreen_type & FS_MAXIMIZED_WINDOW) != 0)) {
      win->SetMaximized(TRUE);
      win->SetFloating(TRUE);
      workspace->GetEngine()->remove(hwnd);
      ctx->transaction.QueueRelayout(workspace);
      return;
   }

   // ── Managed → becoming fullscreen ──
   if ((window_is_managed == TRUE) && (win->ForceTiled() == FALSE) &&
       (win->ForceFloating() == FALSE) &&
       ((fullscreen_type & (FS_EXCLUSIVE_FULLSCREEN | FS_BORDERLESS_WINDOW)) !=
        0)) {
      workspace->GetEngine()->remove(hwnd);
      win->SetUnmanagedFullscreen(TRUE);
      SyncWindowOverlayAuto(ctx, hwnd);
      ctx->transaction.QueueBarVisibility(hwnd, FALSE);
      ctx->transaction.QueueRelayout(workspace);
      return;
   }
}

/**
 * @brief After a resize drag, revert the window position if the unlock
 *        modifier was not held during the drag.
 *
 * Called from ProcessMoveSizeEnd.
 */
inline void CheckResizeRevert(BFWMContext *ctx, HWND hwnd,
                              Workspace *workspace) {
   if (ctx->config.unlock_modifier == 0)
      return;
   auto mod_state = (SHORT)GetAsyncKeyState((int)ctx->config.unlock_modifier);
   if ((mod_state & MOD_KEY_DOWN_MASK) != 0)
      return; // modifier is held — accept the new position

   Window *win = FindWindowIfManaged(ctx, hwnd);
   if (win == nullptr)
      return;

   Debug("resize reverted on %p", (void *)hwnd);
   RECT tiled;
   if ((workspace->GetEngine() != nullptr) &&
       workspace->GetEngine()->get_rect(hwnd, &tiled)) {
      ctx->transaction.QueueMoveWindow(hwnd, tiled.left, tiled.top,
                                       tiled.right - tiled.left,
                                       tiled.bottom - tiled.top);
   }
}

/**
 * @brief After a resize drag, update the layout tree to reflect the new
 *        window size (in-drag resizing settled at end position).
 */
inline void CheckResizeSettle(BFWMContext *ctx, HWND hwnd,
                              Workspace *workspace) {
   if (workspace->GetEngine() == nullptr)
      return;
   Window *win = FindWindowIfManaged(ctx, hwnd);
   if (win == nullptr)
      return;
   RECT cur;
   RECT tiled;
   if ((GetWindowRect(hwnd, &cur) == TRUE) &&
       workspace->GetEngine()->get_rect(hwnd, &tiled)) {
      if (cur.right - cur.left != tiled.right - tiled.left ||
          cur.bottom - cur.top != tiled.bottom - tiled.top) {
         workspace->GetEngine()->resize_window_to_rect(hwnd, cur);
      }
   }
}

/**
 * @brief Resolve a window's rule-target workspace, creating it on the
 *        assigned/focused monitor when it does not exist yet.
 *
 * Falls back: existing workspace by id → create on the monitor pinned by the
 * workspace config → create on the focused workspace's monitor.
 *
 * @param ctx The BFWM context
 * @param win The managed window (rule_target_workspace must be > 0)
 * @return The rule-resolved workspace, or nullptr
 */
inline auto ResolveRuleWorkspace(BFWMContext *ctx, const Window *win)
    -> Workspace * {
   Workspace *target_ws = nullptr;
   if ((win != nullptr) && win->RuleTargetWorkspace() > 0) {
      target_ws = FindWorkspaceById(ctx, (size_t)win->RuleTargetWorkspace());
      if (target_ws == nullptr) {
         Monitor *monitor = nullptr;
         const struct WorkspaceConfig *ws_config =
             FindWorkspaceConfig(ctx, (size_t)win->RuleTargetWorkspace());
         if ((ws_config != nullptr) && ws_config->assigned_monitor > 0) {
            monitor = FindMonitorByDisplayNumber(
                ctx, (UINT)ws_config->assigned_monitor);
         }
         if (monitor == nullptr)
            monitor = FindMonitorByWorkspace(ctx, ctx->focused_workspace);
         if (monitor != nullptr) {
            std::wstring const ws_name =
                L"ws_" + std::to_wstring(win->RuleTargetWorkspace());
            target_ws = CreateWorkspaceForMonitor(
                ctx, (size_t)win->RuleTargetWorkspace(), &ws_name, monitor);
            if (target_ws != nullptr) {
               target_ws->ApplyConfig(ctx, (size_t)win->RuleTargetWorkspace());
               monitor->TrackWorkspace(target_ws);
            }
         }
      }
   }
   return target_ws;
}

/**
 * @brief Resolve the workspace under a window's centre point.
 *
 * Falls back to the focused workspace when the centre is outside every
 * monitor's work area or no active workspace covers it.
 *
 * @param ctx  The BFWM context
 * @param hwnd The window to hit-test
 * @return The hit-test workspace, or nullptr
 */
inline auto ResolveHitTestWorkspace(BFWMContext *ctx, HWND hwnd)
    -> Workspace * {
   Workspace *target_ws = ctx->focused_workspace;
   RECT win_rect;
   if (GetWindowRect(hwnd, &win_rect) == TRUE) {
      POINT const centre = {.x = (win_rect.left + win_rect.right) / 2,
                            .y = (win_rect.top + win_rect.bottom) / 2};
      for (Monitor *mon : ctx->monitors->Monitors()) {
         RECT const mon_work_area = mon->GetWorkArea();
         if ((PtInRect(&mon_work_area, centre) == TRUE) &&
             (mon->GetActiveWorkspace() != nullptr)) {
            target_ws = mon->GetActiveWorkspace();
            break;
         }
      }
   }
   return target_ws;
}

/**
 * @brief Insert a window into a workspace's layout tree and window list.
 *
 * Order matters: the engine tree is separate from workspace->Windows(), so the
 * engine insert runs first, then the grow-array + append, and floating-window
 * clamping runs last (it may queue a move op for the clamped rect).
 *
 * @param ctx       The BFWM context
 * @param workspace The target workspace
 * @param hwnd      The window handle
 * @param win       The managed window
 */
inline void InsertWindowIntoWorkspace(BFWMContext *ctx, Workspace *workspace,
                                      HWND hwnd, Window *win) {
   if ((workspace->GetEngine() != nullptr) && (win != nullptr) &&
       (IsWindowManagedByLayout(win) == TRUE)) {
      workspace->GetEngine()->insert(hwnd, ctx->focused_hwnd);
   }
   workspace->TrackWindow(win);

   if ((win != nullptr) && (win->IsFloating() != 0)) {
      Monitor *mon = FindMonitorByWorkspace(ctx, workspace);
      if (mon != nullptr) {
         UINT const dpi = (ctx->dpi != nullptr)
                              ? ctx->dpi->GetDpi(mon->GetHandle())
                              : DpiSystem::BaseDpi();
         RECT const clamped = win->ClampTo(mon->GetWorkArea(), dpi);
         if (EqualRect(&clamped, win->SavedRectPtr()) == FALSE) {
            win->SetSavedRect(clamped);
            /* Synchronous placement: a chosen placement is handed to the
             * window once — no async move in flight, so the ring converges
             * same-commit. */
            IssueRectWithDpiConversion(ctx, win->GetHwnd(), mon->GetHandle(),
                                       clamped, BFWMSetWindowPos);
            win->MarkOverlayDirty();
         }
      }
   }
}

// ── Window-creation dispatch ──
// Lives here (not in the keybind handlers) because ProcessCreateOrForeground
// is the only caller; keeping it in events.c lets events.c drop its handlers.h
// include and removes the handlers.h <-> events.h include cycle.

inline auto ResolveRuleTargetWorkspace(Window *win, struct BFWMContext *ctx)
    -> Workspace * {
   Workspace *target_ws =
       FindWorkspaceById(ctx, (size_t)win->RuleTargetWorkspace());
   if (target_ws != nullptr)
      return target_ws;

   Monitor *monitor = nullptr;
   const struct WorkspaceConfig *ws_config =
       FindWorkspaceConfig(ctx, (size_t)win->RuleTargetWorkspace());
   if ((ws_config != nullptr) && ws_config->assigned_monitor > 0) {
      monitor =
          FindMonitorByDisplayNumber(ctx, (UINT)ws_config->assigned_monitor);
   }
   if (monitor == nullptr)
      monitor = FindMonitorByWorkspace(ctx, ctx->focused_workspace);
   if (monitor == nullptr)
      return nullptr;

   std::wstring const ws_name =
       L"ws_" + std::to_wstring(win->RuleTargetWorkspace());
   target_ws = CreateWorkspaceForMonitor(
       ctx, (size_t)win->RuleTargetWorkspace(), &ws_name, monitor);
   if (target_ws == nullptr)
      return nullptr;

   target_ws->ApplyConfig(ctx, (size_t)win->RuleTargetWorkspace());
   monitor->TrackWorkspace(target_ws);
   return target_ws;
}

inline auto ResolveDefaultTargetWorkspace(HWND hwnd, struct BFWMContext *ctx)
    -> Workspace * {
   Workspace *target_ws = ctx->focused_workspace;
   RECT win_rect;
   if (GetWindowRect(hwnd, &win_rect) == FALSE)
      return target_ws;

   POINT const centre = {.x = (win_rect.left + win_rect.right) / 2,
                         .y = (win_rect.top + win_rect.bottom) / 2};
   for (Monitor *mon : ctx->monitors->Monitors()) {
      RECT const mon_work_area = mon->GetWorkArea();
      if ((PtInRect(&mon_work_area, centre) == TRUE) &&
          (mon->GetActiveWorkspace() != nullptr)) {
         target_ws = mon->GetActiveWorkspace();
         break;
      }
   }
   return target_ws;
}

inline void HandleWindowReRegistered(HWND hwnd, struct BFWMContext *ctx) {
   Workspace *target_ws = nullptr;
   {
      ScopedLock const lock(ctx->lock);
      if (ctx->focused_hwnd == hwnd)
         return;

      // Focus must always shift to the workspace owning this window: cloak the
      // previously active workspace, uncloak this one, apply layout. This must
      // run before UpdateFocusTracking below (which applies layout), and it
      // must run even inside the transaction — the guard further down only
      // protects the orphan re-add path. Previously the shift sat after that
      // guard and was unreachable, because DrainEventQueue always processes
      // events inside a transaction.
      WorkspaceActivateForWindow(hwnd, ctx);

      if ((ctx->focused_hwnd != nullptr) &&
          (IsWindowVisible(ctx->focused_hwnd) == TRUE)) {
         BorderManagerSetInactive(ctx, ctx->focused_hwnd);
         BorderManagerRedraw(ctx, ctx->focused_hwnd);
      }
      UpdateFocusTracking(hwnd, ctx);
      if (IsWindowVisible(hwnd) == TRUE) {
         BorderManagerSetActive(ctx, hwnd);
         BorderManagerRedraw(ctx, hwnd);
      }

      if (ctx->transaction.IsInTransaction() == TRUE)
         return;

      Workspace *hwnd_ws = FindWorkspaceByHwnd(ctx, hwnd);
      if (hwnd_ws == nullptr) {
         /* Window is registered but orphaned.  If the window is still
            minimized or hidden (e.g. HandleMinimizeStart removed it and
            a spurious foreground event arrived before the actual restore),
            don't re-add it yet — wait for HandleMinimizeEnd / the real
            restore. */
         Window *win = ctx->windows->FindByHwnd(hwnd);
         if (win == nullptr)
            return;

         // Don't re-add a window that's still minimized or hidden —
         // it was likely removed by HandleMinimizeStart / HandleObjectHide
         // and a spurious foreground event arrived before restore.
         if ((IsIconic(hwnd) == TRUE) || (IsWindowVisible(hwnd) == FALSE))
            return;

         target_ws = ctx->focused_workspace;
         RECT win_rect;
         if (GetWindowRect(hwnd, &win_rect) == TRUE) {
            POINT const centre = {.x = (win_rect.left + win_rect.right) / 2,
                                  .y = (win_rect.top + win_rect.bottom) / 2};
            for (Monitor *mon : ctx->monitors->Monitors()) {
               RECT const mon_work_area = mon->GetWorkArea();
               if ((PtInRect(&mon_work_area, centre) == TRUE) &&
                   (mon->GetActiveWorkspace() != nullptr)) {
                  target_ws = mon->GetActiveWorkspace();
                  break;
               }
            }
         }

         if (target_ws != nullptr) {
            if ((target_ws->GetEngine() != nullptr) &&
                (IsWindowManagedByLayout(win) == TRUE)) {
               target_ws->GetEngine()->insert(hwnd, ctx->focused_hwnd);
            }
            target_ws->TrackWindow(win);
         }
      }
   }

   // ApplyLayout must run outside the lock (the original
   // release/re-acquire dance is preserved via scopes).
   if (target_ws != nullptr)
      target_ws->ApplyLayout(ctx);

   {
      ScopedLock const lock(ctx->lock);
      return;
   }
}

inline void HandleWindowCreate(HWND hwnd, BFWMContext *ctx) {
   if ((ctx == nullptr) || (IsWindow(hwnd) == FALSE))
      return;

   enum WindowTriggerReturnType const ret = NewWindowTrigger(hwnd, ctx, FALSE);

   {
      ScopedLock const lock(ctx->lock);

      if (ret == REGISTER_SUCCESSFUL) {
         Window *win = ctx->windows->Windows().back().get();

         Workspace *target_ws = nullptr;
         if (win->RuleTargetWorkspace() > 0)
            target_ws = ResolveRuleTargetWorkspace(win, ctx);
         if (target_ws == nullptr)
            target_ws = ResolveDefaultTargetWorkspace(hwnd, ctx);

         InfoW(L"New window registered and focused: %p (%ls)", hwnd,
               win->GetTitle().c_str());
         target_ws->AddWindow(ctx, win);
      } else if (ret == WINDOW_ALREADY_REGISTERED) {
         HandleWindowReRegistered(hwnd, ctx);
      }
   }
}

void HandleFloatingMove(BFWMContext *&ctx, HWND &hwnd, Workspace *&rws,
                        Window *&float_win) {
   if ((float_win != nullptr) && (float_win->IsFloating() == TRUE)) {
      GetWindowRect(hwnd, float_win->SavedRectPtr());

      POINT const center = {
          .x = (float_win->SavedRectPtr()->left +
                float_win->SavedRectPtr()->right) /
               2,
          .y = (float_win->SavedRectPtr()->top +
                float_win->SavedRectPtr()->bottom) /
               2,
      };

      Monitor *current_mon = FindMonitorByWorkspace(ctx, rws);
      Monitor *mon = FindMonitorByPoint(ctx, center);

      // Stuck-signature fallback: Windows pins an unaware window's center to a
      // mixed-DPI monitor boundary mid-drag, so the rect cannot express the
      // drag direction. When the center is within a few px of at least two
      // monitor rects with different DPI, break the tie by geometry: the
      // destination is the boundary monitor that is not the source (a pinned
      // window can only have crossed from its own workspace's monitor).
      if ((current_mon != nullptr) && (mon == current_mon)) {
         auto const &mons = ctx->monitors->Monitors();
         std::vector<FloatDragMonitor> cand;
         cand.reserve(mons.size());
         size_t src_idx = 0;
         for (size_t i = 0; i < mons.size(); i++) {
            cand.push_back({.rect = mons[i]->GetRect(),
                            .dpi = (ctx->dpi != nullptr)
                                       ? ctx->dpi->GetDpi(mons[i]->GetHandle())
                                       : DpiSystem::BaseDpi()});
            if (mons[i] == current_mon) {
               src_idx = i;
            }
         }
         int const tolerance = 3; // pin is exact at the boundary; 3px covers
                                  // rounding at common scales
         size_t const dst_idx = ClassifyPinnedFloatDrag(
             center, tolerance, cand.data(), cand.size(), src_idx);
         if (dst_idx != src_idx) {
            mon = mons[dst_idx];
         }
      }

      if ((mon != nullptr) && (mon != current_mon) &&
          mon->GetActiveWorkspace() != rws) {
         rws->RemoveWindow(float_win);
         mon->GetActiveWorkspace()->AddWindow(ctx, float_win);
         ctx->focused_workspace = mon->GetActiveWorkspace();
      }

      rws = FindWorkspaceByHwnd(ctx, hwnd);
      Monitor *clamp_mon = FindMonitorByWorkspace(ctx, rws);
      if (clamp_mon != nullptr) {
         UINT const dpi = (ctx != nullptr && ctx->dpi != nullptr)
                              ? ctx->dpi->GetDpi(clamp_mon->GetHandle())
                              : DpiSystem::BaseDpi();
         RECT const clamped = float_win->ClampTo(clamp_mon->GetWorkArea(), dpi);
         if (EqualRect(&clamped, float_win->SavedRectPtr()) == FALSE) {
            float_win->SetSavedRect(clamped);
            /* Synchronous placement: the drag is settled, so issue the rect
             * directly — no async move in flight, the ring converges
             * same-commit. */
            IssueRectWithDpiConversion(ctx, float_win->GetHwnd(),
                                       clamp_mon->GetHandle(), clamped,
                                       BFWMSetWindowPos);
            float_win->MarkOverlayDirty();
         }
      }
   }
}

} // namespace

void RefreshFullscreenStates(BFWMContext *ctx) {
   for (const auto &window : ctx->windows->Windows()) {
      CheckWindowTransitions(ctx, window->GetHwnd());
   }
}

auto SetupWindowEventHook(BFWMContext *context) -> BOOL {
   ctx = context;
   if (ctx == nullptr)
      return FALSE;

   g_obj_hook = SetWinEventHook(
       EVENT_OBJECT_DESTROY, EVENT_OBJECT_LOCATIONCHANGE, nullptr, WinEventProc,
       0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

   if (g_obj_hook == nullptr) {
      Snackbar::LogLastError(ctx, "SetWinEventHook (object events) failed");
      ctx = nullptr;
      return FALSE;
   }

   g_foreground_hook = SetWinEventHook(
       EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, WinEventProc,
       0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

   if (g_foreground_hook == nullptr) {
      Snackbar::LogLastError(ctx, "SetWinEventHook (foreground) failed");
      UnhookWinEvent(g_obj_hook);
      g_obj_hook = nullptr;
      ctx = nullptr;
      return FALSE;
   }

   g_minimize_hook = SetWinEventHook(
       EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr,
       WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

   if (g_minimize_hook == nullptr) {
      Snackbar::LogLastError(ctx, "SetWinEventHook (minimize) failed");
      UnhookWinEvent(g_obj_hook);
      UnhookWinEvent(g_foreground_hook);
      g_obj_hook = nullptr;
      g_foreground_hook = nullptr;
      ctx = nullptr;
      return FALSE;
   }

   g_move_hook = SetWinEventHook(
       EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND, nullptr,
       WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

   if (g_move_hook == nullptr) {
      TeardownWindowEventHook();
      Snackbar::LogLastError(ctx, "SetWinEventHook (move/size) failed");
      ctx = nullptr;
      return FALSE;
   }

   return TRUE;
}

void TeardownWindowEventHook() {
   if (g_obj_hook != nullptr) {
      UnhookWinEvent(g_obj_hook);
      g_obj_hook = nullptr;
   }
   if (g_foreground_hook != nullptr) {
      UnhookWinEvent(g_foreground_hook);
      g_foreground_hook = nullptr;
   }
   if (g_minimize_hook != nullptr) {
      UnhookWinEvent(g_minimize_hook);
      g_minimize_hook = nullptr;
   }
   if (g_move_hook != nullptr) {
      UnhookWinEvent(g_move_hook);
      g_move_hook = nullptr;

      ctx = nullptr;
   }
}

// NOLINTNEXTLINE
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
   auto *context = (BFWMContext *)lParam;
   return static_cast<BOOL>(NewWindowTrigger(hwnd, context, TRUE) !=
                            NO_CONTEXT);
}

// ProcessObjectDestroy — called from DrainEventQueue (main thread, in
// transaction)
void ProcessObjectDestroy(BFWMContext *ctx, HWND hwnd) {
   Window *win = ctx->windows->FindByHwnd(hwnd);
   if (win == nullptr)
      return;

   InfoW(L"Managed window destroyed: %p", hwnd);

   Workspace *target_ws = FindWorkspaceByHwnd(ctx, hwnd);

   // Compute focus fallback BEFORE removal so get_closest_window can
   // still find the reference window in the layout tree.
   BOOL const was_focused = static_cast<BOOL>(ctx->focused_hwnd == hwnd);
   HWND next_focus = nullptr;
   if ((was_focused == TRUE) && (target_ws != nullptr) &&
       target_ws->Windows().size() > 1 && (target_ws->GetEngine() != nullptr)) {
      next_focus = target_ws->GetEngine()->get_closest_window(hwnd);
   }

   if (target_ws != nullptr)
      target_ws->RemoveWindow(win);

   // Decrement fullscreen_count if the destroyed window was fullscreen
   if ((win->IsFullscreen() == TRUE) && (target_ws != nullptr)) {
      Monitor *mon = FindMonitorByWorkspace(ctx, target_ws);
      if (mon != nullptr) {
         if (mon->GetFullscreenCount() > 0)
            mon->DecrementFullscreenCount();
         if (mon->GetFullscreenCount() == 0)
            ctx->transaction.QueueBarVisibility(win->GetHwnd(), TRUE);
      }
   }

   ctx->windows->Remove(win);

   if (was_focused == TRUE)
      ctx->focused_hwnd = nullptr;

   if (target_ws != nullptr)
      ctx->transaction.QueueRelayout(target_ws);

   if (was_focused == TRUE) {
      Workspace *next_ws =
          (target_ws != nullptr) ? target_ws : ctx->focused_workspace;
      if (next_focus == nullptr)
         next_focus = FindNextFocusTarget(ctx, next_ws, hwnd);
      if (next_focus != nullptr) {
         ctx->focus_intent.ArmKnown(next_focus);
      }
   }
}

// ProcessObjectHide — called from DrainEventQueue (main thread, in transaction)
void ProcessObjectHide(BFWMContext *ctx, HWND hwnd) {
   Window *win = ctx->windows->FindByHwnd(hwnd);
   if (win == nullptr)
      return;

   // Any hide hides the ring with it (minimize, programmatic SW_HIDE, etc.).
   SyncWindowOverlay(ctx, hwnd, TRUE);

   InfoW(L"Managed window hidden: %p", hwnd);

   Workspace *target_ws = FindWorkspaceByHwnd(ctx, hwnd);

   // Compute focus fallback BEFORE removal — EVENT_OBJECT_HIDE precedes
   // EVENT_OBJECT_DESTROY, so a subsequent HandleObjectDestroy would
   // find the window already removed from the tree.
   BOOL const was_focused = static_cast<BOOL>(ctx->focused_hwnd == hwnd);
   HWND next_focus = nullptr;
   if ((was_focused != 0) && (target_ws != nullptr) &&
       target_ws->Windows().size() > 1 && (target_ws->GetEngine() != nullptr)) {
      next_focus = target_ws->GetEngine()->get_closest_window(hwnd);
   }

   if (target_ws != nullptr)
      target_ws->RemoveWindow(win);

   if (was_focused == TRUE)
      ctx->focused_hwnd = nullptr;

   if (target_ws != nullptr)
      ctx->transaction.QueueRelayout(target_ws);

   if (was_focused == TRUE) {
      Workspace *next_ws =
          (target_ws != nullptr) ? target_ws : ctx->focused_workspace;
      if (next_focus == nullptr)
         next_focus = FindNextFocusTarget(ctx, next_ws, hwnd);
      if (next_focus != nullptr) {
         ctx->focus_intent.ArmKnown(next_focus);
      }
   }
}

// ProcessMinimizeStart — called from DrainEventQueue (main thread, in
// transaction)
void ProcessMinimizeStart(BFWMContext *ctx, HWND hwnd) {
   Window *win = ctx->windows->FindByHwnd(hwnd);
   if (win == nullptr)
      return;

   // Hide-not-destroy: the ring is suppressed, the overlay survives and
   // re-appears on restore (ProcessMinimizeEnd).
   SyncWindowOverlay(ctx, hwnd, TRUE);

   Workspace *target_ws = FindWorkspaceByHwnd(ctx, hwnd);

   // Compute focus fallback BEFORE removal so get_closest_window can
   // still find the reference window in the layout tree.
   BOOL const was_focused = static_cast<BOOL>(ctx->focused_hwnd == hwnd);
   HWND next_focus = nullptr;
   if ((was_focused == TRUE) && (target_ws != nullptr) &&
       target_ws->Windows().size() > 1 && (target_ws->GetEngine() != nullptr)) {
      next_focus = target_ws->GetEngine()->get_closest_window(hwnd);
   }

   if (target_ws != nullptr)
      target_ws->RemoveWindow(win);

   if (was_focused == TRUE)
      ctx->focused_hwnd = nullptr;

   if (target_ws != nullptr) {
      ctx->transaction.QueueRelayout(target_ws);
      for (const auto &window : target_ws->Windows()) {
         ctx->transaction.QueueRedrawContent(window->GetHwnd());
      }
   }

   if (was_focused == TRUE) {
      Workspace *next_ws =
          (target_ws != nullptr) ? target_ws : ctx->focused_workspace;
      if (next_focus == nullptr)
         next_focus = FindNextFocusTarget(ctx, next_ws, hwnd);
      if (next_focus != nullptr) {
         ctx->focus_intent.ArmKnown(next_focus);
      }
   }
}

// ProcessMinimizeEnd — called from DrainEventQueue (main thread, in
// transaction)
void ProcessMinimizeEnd(BFWMContext *ctx, HWND hwnd) {
   Window *win = ctx->windows->FindByHwnd(hwnd);
   BOOL new_registration = FALSE;
   if (win == nullptr) {
      // Window was not registered — the FOREGROUND event may have fired
      // while it was still DWM-cloaked (minimized), causing
      // IsManageableWindow's cloak check to reject it.  The window is
      // now fully restored — register it now so it can be added.
      if (IsWindowVisible(hwnd) == FALSE)
         return;
      enum WindowTriggerReturnType const ret =
          NewWindowTrigger(hwnd, ctx, FALSE);
      if (ret != REGISTER_SUCCESSFUL)
         return;
      win = ctx->windows->Windows().back().get();
      new_registration = TRUE;
   }

   // Window restored — re-show the ring.
   SyncWindowOverlay(ctx, hwnd, FALSE);

   Workspace *target_ws = nullptr;
   Workspace *ws_with_window = FindWorkspaceByHwnd(ctx, hwnd);
   if (ws_with_window == nullptr) {
      // Only skip re-add if the window is actually hidden.
      if (IsWindowVisible(hwnd) == FALSE)
         return;
      target_ws = ctx->focused_workspace;
      RECT win_rect;
      if (GetWindowRect(hwnd, &win_rect) == TRUE) {
         POINT const centre = {.x = (win_rect.left + win_rect.right) / 2,
                               .y = (win_rect.top + win_rect.bottom) / 2};
         for (Monitor *mon : ctx->monitors->Monitors()) {
            RECT const mon_work_area = mon->GetWorkArea();
            if (PtInRect(&mon_work_area, centre) == TRUE) {
               target_ws = mon->GetActiveWorkspace();
               break;
            }
         }
      }

      if (target_ws != nullptr) {
         if ((target_ws->GetEngine() != nullptr) &&
             (IsWindowManagedByLayout(win) == TRUE)) {
            target_ws->GetEngine()->insert(hwnd, ctx->focused_hwnd);
         }
         target_ws->TrackWindow(win);
      }
   }

   if (target_ws != nullptr)
      ctx->transaction.QueueRelayout(target_ws);

   // Newly registered on restore: focus it, matching the show-path behavior
   // for brand-new windows.
   if ((new_registration == TRUE) && (target_ws != nullptr)) {
      FocusWindow(hwnd, ctx);
   }
}

// ProcessMoveSizeStart — called from DrainEventQueue (main thread, in
// transaction)
void ProcessMoveSizeStart(BFWMContext *ctx, HWND hwnd) {
   Workspace *mws = FindWorkspaceByHwnd(ctx, hwnd);
   if (mws != nullptr)
      ctx->resize_hwnd = hwnd;
}

// ProcessMoveSizeEnd — called from DrainEventQueue (main thread, in
// transaction)
void ProcessMoveSizeEnd(BFWMContext *ctx, HWND hwnd) {
   if (ctx->resize_hwnd != hwnd)
      return;

   ctx->resize_hwnd = nullptr;

   // ── Settled: clear any in-flight layout move ──
   // The window's move/size operation ended, so it is at its final rect —
   // whether it landed at the issued rect (layout move) or somewhere else
   // (user drag, app-driven move). Clearing the flag lets the commit-end
   // flush sync the ring to the settled rect immediately.
   Window *settled_win = ctx->windows->FindByHwnd(hwnd);
   if (settled_win != nullptr) {
      settled_win->SetMoveInFlight(FALSE);
   }

   // ── Transition detection (formerly in LOCATIONCHANGE) ──
   CheckWindowTransitions(ctx, hwnd);

   // ── Main relayout ──
   Workspace *rws = FindWorkspaceByHwnd(ctx, hwnd);
   if (rws == nullptr) {
      Warn("Window without an assigned workspace reached ProcessMoveSizeEnd, "
           "this should be near impossible. Processing aborted.");
      return;
   }
   if (rws != nullptr)
      ctx->transaction.QueueRelayout(rws);

   // ── Revert if unlock modifier not held ──
   if (rws != nullptr)
      CheckResizeRevert(ctx, hwnd, rws);

   // ── Update layout tree to settled size ──
   if (rws != nullptr)
      CheckResizeSettle(ctx, hwnd, rws);

   // ── Floating-window handling ──
   Window *float_win = ctx->windows->FindByHwnd(hwnd);
   HandleFloatingMove(ctx, hwnd, rws, float_win);

   // ── Border overlay: converge the settled geometry at commit-end flush ──
   // Tiled windows settle without emitting OP_MOVE_WINDOW (layout skips
   // same-rect windows); floating/fullscreen windows never relayout.
   if (float_win != nullptr)
      float_win->MarkOverlayDirty();
}

void ProcessObjectShow(BFWMContext *ctx, HWND hwnd) {
   Window *win = ctx->windows->FindByHwnd(hwnd);
   BOOL new_registration = FALSE;
   if (win == nullptr) {
      enum WindowTriggerReturnType const ret =
          NewWindowTrigger(hwnd, ctx, FALSE);
      if (ret != REGISTER_SUCCESSFUL)
         return;
      win = ctx->windows->Windows().back().get();
      new_registration = TRUE;
   }

   // Window visible again — sync the ring (suppressed while fullscreen).
   SyncWindowOverlayAuto(ctx, hwnd);

   Workspace *ws_with_window = FindWorkspaceByHwnd(ctx, hwnd);

   // Minimized windows must never enter the workspace layout.
   if (IsIconic(hwnd) == TRUE)
      return;

   if (ws_with_window == nullptr) {
      Workspace *target_ws = ResolveRuleWorkspace(ctx, win);
      if (target_ws == nullptr)
         target_ws = ResolveHitTestWorkspace(ctx, hwnd);
      if (target_ws != nullptr) {
         InsertWindowIntoWorkspace(ctx, target_ws, hwnd, win);
         ctx->transaction.QueueRelayout(target_ws);
         // Windows registered via EVENT_OBJECT_SHOW never pass through
         // HandleWindowCreate's focus path, so focus the new window here:
         // FocusWindow queues OP_FOCUS_WINDOW + border colors + relayout and
         // is safe to call inside the current transaction.
         if (new_registration == TRUE) {
            FocusWindow(hwnd, ctx);
         }
      }
   } else {
      ctx->transaction.QueueRelayout(ws_with_window);
   }
}

// ProcessObjectReorder — called from DrainEventQueue (main thread, in
// transaction)
void ProcessObjectReorder(BFWMContext *ctx, HWND hwnd) {
   Window *win = ctx->windows->FindByHwnd(hwnd);
   if (win == nullptr)
      return;
   // Re-assert the ring's relative z-slot after something was raised above
   // the target window (e.g. a click-raise, dialog, or window manager).
   win->AssertOverlayZOrder();

   /* Repair every ring, not just the reordered window's: a raise of one
    * window buries others' rings too, and this is the event-speed repair. */
   for (const auto &window : ctx->windows->Windows()) {
      Window *w = window.get();
      if (w != nullptr)
         w->AssertOverlayZOrder();
   }
}

// ProcessCreateOrForeground — called from DrainEventQueue (main thread, in
// transaction)
void ProcessCreateOrForeground(BFWMContext *ctx, DWORD event, HWND hwnd) {
   if (ctx->setup_in_progress == TRUE)
      return;

   // ── Focus intent ──
   // A Windows-supplied foreground must not overrule the user's most recent
   // explicit focus action. While the intent is armed, drop a registered
   // non-target foreground and re-assert our planned target; STAY armed so a
   // later stray in a spawn storm cannot slip through.
   if (event == EVENT_SYSTEM_FOREGROUND &&
       ctx->focus_intent.ShouldSuppress(
           hwnd, static_cast<BOOL>(ctx->windows->FindByHwnd(hwnd) !=
                                   nullptr)) == TRUE) {
      if (ctx->focus_intent.Kind() == FocusIntentKind::Known) {
         PostThreadMessage(ctx->main_thread_id, WM_FORCE_FOCUS,
                           (WPARAM)ctx->focus_intent.Target(), 0);
      }
      return; // drop the noise, STAY armed
   }

   HandleWindowCreate(hwnd, ctx);

   // Check for window state transitions (fullscreen, maximize, restore)
   CheckWindowTransitions(ctx, hwnd);

   if (event == EVENT_SYSTEM_FOREGROUND)
      ctx->focus_intent.NoteLanded(hwnd);
}

void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
                           LONG idObject, LONG idChild, DWORD dwEventThread,
                           DWORD dwmsEventTime) {
   (void)hWinEventHook;
   (void)dwEventThread;
   (void)dwmsEventTime;
   (void)idChild;

   if (ctx == nullptr)
      return;

   if (idObject != OBJID_WINDOW)
      return;

   switch (event) {
   default:
      break;
   case EVENT_OBJECT_DESTROY:
   case EVENT_OBJECT_HIDE:
   case EVENT_OBJECT_SHOW:
   case EVENT_OBJECT_REORDER:
   case EVENT_SYSTEM_MINIMIZESTART:
   case EVENT_SYSTEM_MINIMIZEEND:
   case EVENT_SYSTEM_MOVESIZESTART:
   case EVENT_SYSTEM_MOVESIZEEND: {
      EventRecord const record = {.event = event,
                                  .hwnd = hwnd,
                                  .idObject = idObject,
                                  .idChild = idChild,
                                  .dwEventThread = dwEventThread,
                                  .dwmsEventTime = dwmsEventTime,
                                  .dpi = 0};
      ctx->event_queue.Push(record);
      break;
   }
   case EVENT_OBJECT_CREATE:
   case EVENT_SYSTEM_FOREGROUND:
      if (ctx->setup_in_progress == TRUE)
         break;
      {
         EventRecord const record = {.event = event,
                                     .hwnd = hwnd,
                                     .idObject = idObject,
                                     .idChild = idChild,
                                     .dwEventThread = dwEventThread,
                                     .dwmsEventTime = dwmsEventTime,
                                     .dpi = 0};
         ctx->event_queue.Push(record);
      }
      break;
   }
}
