#include "events.h"
#include "../../core/main_context.h"
#include "../../core/shared_state.h"
#include "../../core/sync.h"
#include "../../core/worker_context.h"
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
   int const len = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                       static_cast<int>(wide.size()), nullptr,
                                       0, nullptr, nullptr);
   if (len <= 0)
      return {};
   std::string narrow;
   narrow.resize(static_cast<size_t>(len));
   WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                       narrow.data(), len, nullptr, nullptr);
   return narrow;
}

// ── WinEvent hook handles ──
WorkerContext *g_worker = nullptr;
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

inline void SyncWindowOverlay(MainContext *main, HWND hwnd, BOOL suppress) {
   Window *win = main->windows->FindByHwnd(hwnd);
   if (win != nullptr)
      win->SyncOverlay(suppress);
}

inline void SyncWindowOverlayAuto(MainContext *main, HWND hwnd) {
   Window *win = main->windows->FindByHwnd(hwnd);
   if (win == nullptr)
      return;
   win->SyncOverlay(static_cast<BOOL>((win->IsFullscreen() == TRUE) ||
                                      (win->IsUnmanagedFullscreen() == TRUE)));
}

inline auto NewWindowTrigger(HWND hwnd, SharedState *shared, MainContext *main,
                             BOOL startup) -> enum WindowTriggerReturnType {
   if (shared == nullptr) return NO_CONTEXT;

   if (main->windows->FindByHwnd(hwnd) !=
       nullptr){return WINDOW_ALREADY_REGISTERED;}

if (IsManageableWindow(shared, main, hwnd, startup)) {
   // Reject owned windows — tooltips, dropdowns, context menus
   HWND owner = GetWindow(hwnd, GW_OWNER);
   if ((owner != nullptr) && (main->windows->FindByHwnd(owner) != nullptr))
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
   window_title.resize(title_len > 0 ? static_cast<size_t>(title_len) : 0);

   window_class.resize(WINDOW_STR_BUF_SIZE);
   int const class_len =
       GetClassNameW(hwnd, window_class.data(), WINDOW_STR_BUF_SIZE);
   if (class_len == 0) {
      BFWMLogLastError("Could not get window class");
      return NO_CLASS;
   }
   window_class.resize(static_cast<size_t>(class_len));

   // Convert class name and title to narrow strings for rule matching.
   std::string const class_name_a = WideToNarrow(window_class);
   std::string const title_a = WideToNarrow(window_title);

   // Fetch process name once for all rule checks.
   std::string process_buf(MAX_PATH, '\0');
   const char *process =
       GetWindowProcessName(hwnd, process_buf.data(), process_buf.size());

   // Check hardcoded blacklist and user ignore rules before creating
   // the Window.
   if (IsExcludedByRules(shared, main, class_name_a.data(), title_a.data(),
                         process)) {
      return REGISTER_SKIPPED;
   }

   auto window_owner = std::make_unique<Window>(hwnd, window_title);
   Window *window = window_owner.get();

   // Detect fullscreen windows at registration time so they never enter
   // the layout tree.
   FullscreenType const fullscreen_type = DetectFullscreenWindow(hwnd);
   if ((fullscreen_type & (FS_EXCLUSIVE_FULLSCREEN | FS_BORDERLESS_WINDOW)) !=
       0)
      window->SetUnmanagedFullscreen(TRUE);

   // Restore maximized windows so the layout can position them — the
   // OS-level WS_MAXIMIZE state overrides SetWindowPos.
   if (IsZoomed(hwnd) == TRUE)
      ShowWindow(hwnd, SW_RESTORE);

   // Apply non-ignore rules (floating, workspace).
   WindowRulesApply(shared, main, window, class_name_a.data(), title_a.data(),
                    process);

   main->windows->Register(std::move(window_owner));

   // Create the border overlay ring. OverlayCreate establishes
   // initial geometry/visibility; a fullscreen window's ring stays
   // suppressed until it exits fullscreen. Construction never fails — on
   // failure the overlay is inert and every method no-ops.
   window->SetOverlay(std::make_unique<Overlay>(
       shared, main, hwnd, shared->config.border_color,
       shared->config.border_width, shared->config.border_radius));
   if (window->IsUnmanagedFullscreen() == TRUE)
      window->SyncOverlay(TRUE);

   DebugW(L"Registered window: hwnd=%p title=\"%ls\" class=\"%ls\" "
          L"startup=%d",
          reinterpret_cast<void *>(hwnd), window_title.c_str(),
          window_class.c_str(), startup);

   return REGISTER_SUCCESSFUL;
}

return REGISTER_SKIPPED;
} // namespace

/**
 * @brief Find the best window to focus after a window is removed/minimized.
 *
 * Priority: same workspace -> all other workspaces on all monitors.
 * Returns nullptr if no window (other than @c exclude_hwnd) exists anywhere.
 */
inline auto IsUnmanagedFullscreen(MainContext *main, HWND hwnd) -> BOOL {
   Window const *w = main->windows->FindByHwnd(hwnd);
   return static_cast<BOOL>((w != nullptr) &&
                            (w->IsUnmanagedFullscreen() == TRUE));
}

inline auto FindNextFocusTarget(MainContext *main, Workspace const *current_ws,
                                HWND exclude_hwnd) -> HWND {
   if (current_ws != nullptr) {
      // Prefer fullscreen windows — focusing a non-fullscreen window
      // triggers UpdateFocusTracking's z-order fix that lowers fullscreen
      // windows and raises all tiled windows.
      for (const auto &window : current_ws->Windows()) {
         HWND candidate = window->GetHwnd();
         if (candidate != exclude_hwnd &&
             (IsUnmanagedFullscreen(main, candidate) == FALSE)) {
            Window const *w = main->windows->FindByHwnd(candidate);
            if ((w != nullptr) && (w->IsFullscreen() == TRUE))
               return candidate;
         }
      }

      if (current_ws->GetEngine() != nullptr) {
         HWND first = current_ws->GetEngine()->get_closest_window(exclude_hwnd);
         if ((first != nullptr) && first != exclude_hwnd &&
             (IsUnmanagedFullscreen(main, first) == 0))
            return first;
      }
      for (const auto &window : current_ws->Windows()) {
         HWND candidate = window->GetHwnd();
         if (candidate != exclude_hwnd &&
             (IsUnmanagedFullscreen(main, candidate) == 0))
            return candidate;
      }

      // current_ws is empty — keep the empty workspace alive and
      // leave focused_hwnd nullptr.  The user can navigate away with a
      // keybind or click.
   }

   return nullptr;
}

// ------------------------------------------------------------------
// Window transition detection
// ------------------------------------------------------------------

/**
 * @brief Check for fullscreen/maximize/restore transitions and update state.
 *
 * Called from ProcessMoveSizeEnd (user drag) and ProcessCreateOrForeground
 * (focus change).  Runs inside a transaction so it may queue ops.
 *
 * Transitions are detected only at meaningful checkpoints (end of drag,
 * focus change), not on every pixel change.
 */
inline void CheckWindowTransitions(SharedState *shared, MainContext *main,
                                   HWND hwnd) {
   if (main->toggling_fullscreen_hwnd == hwnd)
      return;

   Window *win = main->windows->FindByHwnd(hwnd);
   if (win == nullptr)
      return;

   Workspace *workspace = FindWorkspaceByHwnd(shared, main, hwnd);
   if ((workspace == nullptr) || (workspace->GetEngine() == nullptr))
      return;

   {
      Monitor const *mon = FindMonitorByWorkspace(shared, main, workspace);
      if ((mon != nullptr) && workspace != mon->GetActiveWorkspace())
         return;
   }

   FullscreenType const fullscreen_type = DetectFullscreenWindow(hwnd);

   // ── Unmanaged fullscreen → not fullscreen (game went windowed) ──
   if ((win->IsUnmanagedFullscreen() == TRUE) &&
       fullscreen_type == FS_NOT_FULLSCREEN) {
      win->SetUnmanagedFullscreen(FALSE);
      SyncWindowOverlayAuto(main, hwnd);
      if (IsWindowManagedByLayout(win) == TRUE)
         workspace->GetEngine()->insert(hwnd, main->focused_hwnd);
      main->transaction.QueueRelayout(workspace);
      return;
   }

   // ── Managed fullscreen → not fullscreen (externally toggled) ──
   if ((win->IsFullscreen() == TRUE) && fullscreen_type == FS_NOT_FULLSCREEN) {
      win->SetFullscreen(FALSE);
      SyncWindowOverlayAuto(main, hwnd);
      main->transaction.QueueSetWindowStyle(hwnd, win->SavedStyle(),
                                            win->SavedExstyle());
      main->transaction.QueueZOrder(hwnd, HWND_NOTOPMOST);

      if (IsWindowManagedByLayout(win) == TRUE)
         workspace->GetEngine()->insert(hwnd, main->focused_hwnd);
      main->transaction.QueueRelayout(workspace);

      Monitor *mon = FindMonitorByWorkspace(shared, main, workspace);
      if (mon != nullptr) {
         if (mon->GetFullscreenCount() > 0)
            mon->DecrementFullscreenCount();
         if (mon->GetFullscreenCount() == 0)
            main->transaction.QueueBarVisibility(hwnd, TRUE);
      }
      return;
   }

   // ── Maximized → not maximized (user restored from taskbar) ──
   if ((win->IsMaximized() == TRUE) && (IsZoomed(hwnd) == FALSE)) {
      win->SetMaximized(FALSE);
      win->SetFloating(FALSE);
      if (IsWindowManagedByLayout(win) == TRUE)
         workspace->GetEngine()->insert(hwnd, main->focused_hwnd);
      main->transaction.QueueRelayout(workspace);
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
      main->transaction.QueueRelayout(workspace);
      return;
   }

   // ── Managed → becoming fullscreen ──
   if ((window_is_managed == TRUE) && (win->ForceTiled() == FALSE) &&
       (win->ForceFloating() == FALSE) &&
       ((fullscreen_type & (FS_EXCLUSIVE_FULLSCREEN | FS_BORDERLESS_WINDOW)) !=
        0)) {
      workspace->GetEngine()->remove(hwnd);
      win->SetUnmanagedFullscreen(TRUE);
      SyncWindowOverlayAuto(main, hwnd);
      main->transaction.QueueBarVisibility(hwnd, FALSE);
      main->transaction.QueueRelayout(workspace);
      return;
   }
}

/**
 * @brief After a resize drag, revert the window position if the unlock
 *        modifier was not held during the drag.
 *
 * Called from ProcessMoveSizeEnd.
 */
inline void CheckResizeRevert(SharedState const *shared, MainContext *main,
                              HWND hwnd, Workspace const *workspace) {
   if (shared->config.unlock_modifier == 0)
      return;
   auto const mod_state =
       GetAsyncKeyState(static_cast<int>(shared->config.unlock_modifier));
   if ((mod_state & MOD_KEY_DOWN_MASK) != 0)
      return; // modifier is held — accept the new position

   Window const *win = FindWindowIfManaged(main, hwnd);
   if (win == nullptr)
      return;

   Debug("resize reverted on %p", reinterpret_cast<void *>(hwnd));
   RECT tiled;
   if ((workspace->GetEngine() != nullptr) &&
       workspace->GetEngine()->get_rect(hwnd, &tiled)) {
      main->transaction.QueueMoveWindow(hwnd, tiled.left, tiled.top,
                                        tiled.right - tiled.left,
                                        tiled.bottom - tiled.top);
   }
}

/**
 * @brief After a resize drag, update the layout tree to reflect the new
 *        window size (in-drag resizing settled at end position).
 */
inline void CheckResizeSettle(MainContext *main, HWND hwnd,
                              Workspace const *workspace) {
   if (workspace->GetEngine() == nullptr)
      return;
   Window const *win = FindWindowIfManaged(main, hwnd);
   if (win == nullptr)
      return;
   RECT cur;
   RECT tiled;
   if (((GetWindowRect(hwnd, &cur) == TRUE) &&
        workspace->GetEngine()->get_rect(hwnd, &tiled)) &&
       (cur.right - cur.left != tiled.right - tiled.left ||
        cur.bottom - cur.top != tiled.bottom - tiled.top)) {
      workspace->GetEngine()->resize_window_to_rect(hwnd, cur);
   }
}

/**
 * @brief Resolve a window's rule-target workspace, creating it on the
 *        assigned/focused monitor when it does not exist yet.
 *
 * Falls back: existing workspace by id → create on the monitor pinned by the
 * workspace config → create on the focused workspace's monitor.
 *
 * @param shared The BFWM shared state
 * @param main   The main-thread context
 * @param win The managed window (rule_target_workspace must be > 0)
 * @return The rule-resolved workspace, or nullptr
 */
inline auto ResolveRuleWorkspace(SharedState *shared, MainContext *main,
                                 const Window *win) -> Workspace * {
   Workspace *target_ws = nullptr;
   if ((win != nullptr) && win->RuleTargetWorkspace() > 0) {
      target_ws = FindWorkspaceById(
          shared, main, static_cast<size_t>(win->RuleTargetWorkspace()));
      if (target_ws == nullptr) {
         Monitor *monitor = nullptr;
         const struct WorkspaceConfig *ws_config = FindWorkspaceConfig(
             shared, main, static_cast<size_t>(win->RuleTargetWorkspace()));
         if ((ws_config != nullptr) && ws_config->assigned_monitor > 0) {
            monitor = FindMonitorByDisplayNumber(
                shared, main, static_cast<UINT>(ws_config->assigned_monitor));
         }
         if (monitor == nullptr) {
            monitor =
                FindMonitorByWorkspace(shared, main, main->focused_workspace);
         }
         if (monitor != nullptr) {
            std::wstring const ws_name =
                L"ws_" + std::to_wstring(win->RuleTargetWorkspace());
            target_ws = CreateWorkspaceForMonitor(
                shared, main, static_cast<size_t>(win->RuleTargetWorkspace()),
                &ws_name, monitor);
            if (target_ws != nullptr) {
               target_ws->ApplyConfig(
                   shared, main,
                   static_cast<size_t>(win->RuleTargetWorkspace()));
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
 * @param main  The main-thread context
 * @param hwnd The window to hit-test
 * @return The hit-test workspace, or nullptr
 */
inline auto ResolveWorkAreaWorkspaceAtCentre(HWND hwnd, MainContext *main)
    -> Workspace * {
   Workspace *target_ws = main->focused_workspace;
   RECT win_rect;
   if (GetWindowRect(hwnd, &win_rect) == FALSE)
      return target_ws;

   POINT const centre = {
       .x = (win_rect.left + win_rect.right) / 2,
       .y = (win_rect.top + win_rect.bottom) / 2,
   };
   Monitor const *const mon = FindMonitorByWorkAreaPoint(nullptr, main, centre);
   if ((mon != nullptr) && (mon->GetActiveWorkspace() != nullptr))
      target_ws = mon->GetActiveWorkspace();
   return target_ws;
}

inline auto ResolveHitTestWorkspace(MainContext *main, HWND hwnd)
    -> Workspace * {
   return ResolveWorkAreaWorkspaceAtCentre(hwnd, main);
}

/**
 * @brief Insert a window into a workspace's layout tree and window list.
 *
 * Order matters: the engine tree is separate from workspace->Windows(), so the
 * engine insert runs first, then the grow-array + append, and floating-window
 * clamping runs last (it may queue a move op for the clamped rect).
 *
 * @param shared The BFWM shared state
 * @param main   The main-thread context
 * @param workspace The target workspace
 * @param hwnd      The window handle
 * @param win       The managed window
 */
inline void InsertWindowIntoWorkspace(SharedState *shared, MainContext *main,
                                      Workspace *workspace, HWND hwnd,
                                      Window *win) {
   if ((workspace->GetEngine() != nullptr) && (win != nullptr) &&
       (IsWindowManagedByLayout(win) == TRUE)) {
      workspace->GetEngine()->insert(hwnd, main->focused_hwnd);
   }
   workspace->TrackWindow(win);

   if ((win != nullptr) && (win->IsFloating() != 0)) {
      Monitor const *mon = FindMonitorByWorkspace(shared, main, workspace);
      if (mon != nullptr) {
         UINT const dpi = (main->dpi != nullptr)
                              ? main->dpi->GetDpiOrDefault(mon->GetHandle())
                              : DpiSystem::BaseDpi();
         RECT const clamped = win->ClampTo(mon->GetWorkArea(), dpi);
         if (EqualRect(&clamped, win->SavedRectPtr()) == FALSE) {
            win->SetSavedRect(clamped);
            /* Synchronous placement: a chosen placement is handed to the
             * window once — no async move in flight, so the ring converges
             * same-commit. */
            IssueRectWithDpiConversion(shared, main, win->GetHwnd(),
                                       mon->GetHandle(), clamped,
                                       BFWMSetWindowPos);
            win->MarkOverlayDirty();
         }
      }
   }
}

// ── Window-creation dispatch ──
// Lives here (not in the keybind handlers) because ProcessCreateOrForeground
// is the only caller; keeping it in events.c lets events.c drop its handlers.h
// include and removes the handlers.h <-> events.h include cycle.

inline auto ResolveRuleTargetWorkspace(Window const *win,
                                       struct SharedState *shared,
                                       struct MainContext *main)
    -> Workspace * {
   Workspace *target_ws = FindWorkspaceById(
       shared, main, static_cast<size_t>(win->RuleTargetWorkspace()));
   if (target_ws != nullptr)
      return target_ws;

   Monitor *monitor = nullptr;
   const struct WorkspaceConfig *ws_config = FindWorkspaceConfig(
       shared, main, static_cast<size_t>(win->RuleTargetWorkspace()));
   if ((ws_config != nullptr) && ws_config->assigned_monitor > 0) {
      monitor = FindMonitorByDisplayNumber(
          shared, main, static_cast<UINT>(ws_config->assigned_monitor));
   }
   if (monitor == nullptr)
      monitor = FindMonitorByWorkspace(shared, main, main->focused_workspace);
   if (monitor == nullptr)
      return nullptr;

   std::wstring const ws_name =
       L"ws_" + std::to_wstring(win->RuleTargetWorkspace());
   target_ws = CreateWorkspaceForMonitor(
       shared, main, static_cast<size_t>(win->RuleTargetWorkspace()), &ws_name,
       monitor);
   if (target_ws == nullptr)
      return nullptr;

   target_ws->ApplyConfig(shared, main,
                          static_cast<size_t>(win->RuleTargetWorkspace()));
   monitor->TrackWorkspace(target_ws);
   return target_ws;
}

inline auto ResolveDefaultTargetWorkspace(HWND hwnd, struct MainContext *main)
    -> Workspace * {
   return ResolveWorkAreaWorkspaceAtCentre(hwnd, main);
}

inline void HandleWindowReRegistered(HWND hwnd, struct SharedState *shared,
                                     struct MainContext *main) {
   Workspace *target_ws = nullptr;
   {
      ScopedLock const lock(main->lock);
      if (main->focused_hwnd == hwnd)
         return;

      // Focus must always shift to the workspace owning this window: cloak the
      // previously active workspace, uncloak this one, apply layout. This must
      // run before UpdateFocusTracking below (which applies layout), and it
      // must run before the transaction check below — that check only protects
      // the orphan re-add path, and DrainEventQueue always processes events
      // inside a transaction, so a shift placed after it would never run.
      WorkspaceActivateForWindow(hwnd, shared, main);

      if ((main->focused_hwnd != nullptr) &&
          (IsWindowVisible(main->focused_hwnd) == TRUE)) {
         BorderManagerSetInactive(shared, main, main->focused_hwnd);
         BorderManagerRedraw(shared, main, main->focused_hwnd);
      }
      UpdateFocusTracking(hwnd, shared, main);
      if (IsWindowVisible(hwnd) == TRUE) {
         BorderManagerSetActive(shared, main, hwnd);
         BorderManagerRedraw(shared, main, hwnd);
      }

      if (main->transaction.IsInTransaction() == TRUE)
         return;

      Workspace const *hwnd_ws = FindWorkspaceByHwnd(shared, main, hwnd);
      if (hwnd_ws == nullptr) {
         /* Window is registered but orphaned.  If the window is still
            minimized or hidden (e.g. HandleMinimizeStart removed it and
            a spurious foreground event arrived before the actual restore),
            don't re-add it yet — wait for HandleMinimizeEnd / the real
            restore. */
         Window *win = main->windows->FindByHwnd(hwnd);
         if (win == nullptr)
            return;

         // Don't re-add a window that's still minimized or hidden —
         // it was likely removed by HandleMinimizeStart / HandleObjectHide
         // and a spurious foreground event arrived before restore.
         if (BFWMIsWindowShown(hwnd) == FALSE)
            return;

         target_ws = main->focused_workspace;
         RECT win_rect;
         if (GetWindowRect(hwnd, &win_rect) == TRUE) {
            POINT const centre = {
                .x = (win_rect.left + win_rect.right) / 2,
                .y = (win_rect.top + win_rect.bottom) / 2,
            };
            Monitor const *const mon =
                FindMonitorByWorkAreaPoint(nullptr, main, centre);
            if ((mon != nullptr) && (mon->GetActiveWorkspace() != nullptr))
               target_ws = mon->GetActiveWorkspace();
         }

         if (target_ws != nullptr) {
            if ((target_ws->GetEngine() != nullptr) &&
                (IsWindowManagedByLayout(win) == TRUE)) {
               target_ws->GetEngine()->insert(hwnd, main->focused_hwnd);
            }
            target_ws->TrackWindow(win);
         }
      }
   }

   // ApplyLayout must run outside the lock (the original
   // release/re-acquire dance is preserved via scopes).
   if (target_ws != nullptr)
      target_ws->ApplyLayout(shared, main);

   {
      ScopedLock const lock(main->lock);
      return;
   }
}

inline void HandleWindowCreate(HWND hwnd, SharedState *shared,
                               MainContext *main) {
   if ((shared == nullptr) || (IsWindow(hwnd) == FALSE))
      return;

   enum WindowTriggerReturnType const ret =
       NewWindowTrigger(hwnd, shared, main, FALSE);

   {
      ScopedLock const lock(main->lock);

      if (ret == REGISTER_SUCCESSFUL) {
         Window *win = main->windows->Windows().back().get();

         Workspace *target_ws = nullptr;
         if (win->RuleTargetWorkspace() > 0)
            target_ws = ResolveRuleTargetWorkspace(win, shared, main);
         if (target_ws == nullptr)
            target_ws = ResolveDefaultTargetWorkspace(hwnd, main);

         InfoW(L"New window registered and focused: %p (%ls)", hwnd,
               win->GetTitle().c_str());
         target_ws->AddWindow(shared, main, win);
      } else if (ret == WINDOW_ALREADY_REGISTERED) {
         HandleWindowReRegistered(hwnd, shared, main);
      }
   }
}

void HandleFloatingMove(SharedState *&shared, MainContext *&main, HWND &hwnd,
                        Workspace *&rws, Window *&float_win) {
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

      Monitor const *current_mon = FindMonitorByWorkspace(shared, main, rws);
      Monitor const *mon = FindMonitorByPoint(shared, main, center);

      // Stuck-signature fallback: Windows pins an unaware window's center to a
      // mixed-DPI monitor boundary mid-drag, so the rect cannot express the
      // drag direction. When the center is within a few px of at least two
      // monitor rects with different DPI, break the tie by geometry: the
      // destination is the boundary monitor that is not the source (a pinned
      // window can only have crossed from its own workspace's monitor).
      if ((current_mon != nullptr) && (mon == current_mon)) {
         auto const &mons = main->monitors->Monitors();
         std::vector<FloatDragMonitor> cand;
         cand.reserve(mons.size());
         size_t src_idx = 0;
         for (size_t i = 0; i < mons.size(); i++) {
            cand.push_back({
                .rect = mons[i]->GetRect(),
                .dpi = (main->dpi != nullptr)
                           ? main->dpi->GetDpiOrDefault(mons[i]->GetHandle())
                           : DpiSystem::BaseDpi(),
            });
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
         mon->GetActiveWorkspace()->AddWindow(shared, main, float_win);
         main->focused_workspace = mon->GetActiveWorkspace();
      }

      rws = FindWorkspaceByHwnd(shared, main, hwnd);
      Monitor const *clamp_mon = FindMonitorByWorkspace(shared, main, rws);
      if (clamp_mon != nullptr) {
         UINT const dpi =
             (shared != nullptr && main->dpi != nullptr)
                 ? main->dpi->GetDpiOrDefault(clamp_mon->GetHandle())
                 : DpiSystem::BaseDpi();
         RECT const clamped = float_win->ClampTo(clamp_mon->GetWorkArea(), dpi);
         if (EqualRect(&clamped, float_win->SavedRectPtr()) == FALSE) {
            float_win->SetSavedRect(clamped);
            /* Synchronous placement: the drag is settled, so issue the rect
             * directly — no async move in flight, the ring converges
             * same-commit. */
            IssueRectWithDpiConversion(shared, main, float_win->GetHwnd(),
                                       clamp_mon->GetHandle(), clamped,
                                       BFWMSetWindowPos);
            float_win->MarkOverlayDirty();
         }
      }
   }
}

} // namespace

void RefreshFullscreenStates(SharedState *shared, MainContext *main) {
   for (const auto &window : main->windows->Windows()) {
      CheckWindowTransitions(shared, main, window->GetHwnd());
   }
}

// NOLINTNEXTLINE
static auto CALLBACK HarvestMissedWindowsProc(HWND hwnd, LPARAM lParam)
    -> BOOL {
   auto const *data = (WindowEnumData *)lParam;
   MainContext *main = data->main;
   SharedState *shared = data->shared;

   if (main->windows->FindByHwnd(hwnd) != nullptr)
      return TRUE; // already registered — skip

   if (IsWindowVisible(hwnd) == FALSE)
      return TRUE;

   if (GetParent(hwnd) != nullptr)
      return TRUE;

   // Cloak check still applies — a window still initialising is correctly
   // rejected.  Non-cloaked, visible windows that were missed at creation
   // (e.g. GPU-accelerated terminals like Rio) are now registered.
   enum WindowTriggerReturnType const ret =
       NewWindowTrigger(hwnd, shared, main, FALSE);
   if (ret == REGISTER_SUCCESSFUL) {
      Window *win = main->windows->Windows().back().get();
      Workspace *target_ws = nullptr;
      if (win->RuleTargetWorkspace() > 0)
         target_ws = ResolveRuleTargetWorkspace(win, shared, main);
      if (target_ws == nullptr)
         target_ws = ResolveDefaultTargetWorkspace(hwnd, main);
      if (target_ws != nullptr) {
         target_ws->AddWindow(shared, main, win, FALSE);
         /* AddWindow with focus=FALSE skips FocusWindow, which normally
          * queues the relayout.  Without an explicit QueueRelayout the
          * window is registered and inserted into the engine tree but
          * apply_layout/commit_layout never runs, so the window stays
          * at its untiled OS position until a subsequent event (e.g.
          * foreground) triggers layout. */
         main->transaction.QueueRelayout(target_ws);
      }
   }
   return TRUE;
}

void HarvestMissedWindows(struct SharedState *shared,
                          struct MainContext *main) {
   if ((shared == nullptr) || (main == nullptr))
      return;

   main->transaction.Begin();
   {
      ScopedLock const lock(main->lock);
      WindowEnumData const data = {.shared = shared, .main = main};
      EnumWindows(HarvestMissedWindowsProc, (LPARAM)&data);
   }
   BFWMTransactionCommit(shared, main);
}

auto SetupWindowEventHook(WorkerContext *worker) -> BOOL {
   g_worker = worker;
   if (g_worker == nullptr)
      return FALSE;

   g_obj_hook = SetWinEventHook(
       EVENT_OBJECT_DESTROY, EVENT_OBJECT_LOCATIONCHANGE, nullptr, WinEventProc,
       0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

   if (g_obj_hook == nullptr) {
      Snackbar::LogLastError(worker->shared, worker->main,
                             "SetWinEventHook (object events) failed");
      g_worker = nullptr;
      return FALSE;
   }

   g_foreground_hook = SetWinEventHook(
       EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, WinEventProc,
       0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

   if (g_foreground_hook == nullptr) {
      Snackbar::LogLastError(worker->shared, worker->main,
                             "SetWinEventHook (foreground) failed");
      UnhookWinEvent(g_obj_hook);
      g_obj_hook = nullptr;
      g_worker = nullptr;
      return FALSE;
   }

   g_minimize_hook = SetWinEventHook(
       EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr,
       WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

   if (g_minimize_hook == nullptr) {
      Snackbar::LogLastError(worker->shared, worker->main,
                             "SetWinEventHook (minimize) failed");
      UnhookWinEvent(g_obj_hook);
      UnhookWinEvent(g_foreground_hook);
      g_obj_hook = nullptr;
      g_foreground_hook = nullptr;
      g_worker = nullptr;
      return FALSE;
   }

   g_move_hook = SetWinEventHook(
       EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND, nullptr,
       WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

   if (g_move_hook == nullptr) {
      TeardownWindowEventHook();
      Snackbar::LogLastError(worker->shared, worker->main,
                             "SetWinEventHook (move/size) failed");
      g_worker = nullptr;
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

      g_worker = nullptr;
   }
}

// NOLINTNEXTLINE
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
   auto const *data = (WindowEnumData *)lParam;
   return static_cast<BOOL>(
       NewWindowTrigger(hwnd, data->shared, data->main, TRUE) != NO_CONTEXT);
}

// ProcessObjectDestroy — called from DrainEventQueue (main thread, in
// transaction)
void ProcessObjectDestroy(SharedState *shared, MainContext *main, HWND hwnd) {
   Window *win = main->windows->FindByHwnd(hwnd);
   if (win == nullptr)
      return;

   InfoW(L"Managed window destroyed: %p", hwnd);

   Workspace *target_ws = FindWorkspaceByHwnd(shared, main, hwnd);

   // Compute focus fallback BEFORE removal so get_closest_window can
   // still find the reference window in the layout tree.
   BOOL const was_focused = static_cast<BOOL>(main->focused_hwnd == hwnd);
   HWND next_focus = nullptr;
   if ((was_focused == TRUE) && (target_ws != nullptr) &&
       target_ws->Windows().size() > 1 && (target_ws->GetEngine() != nullptr)) {
      next_focus = target_ws->GetEngine()->get_closest_window(hwnd);
   }

   if (target_ws != nullptr)
      target_ws->RemoveWindow(win);

   // Decrement fullscreen_count if the destroyed window was fullscreen
   if ((win->IsFullscreen() == TRUE) && (target_ws != nullptr)) {
      Monitor *mon = FindMonitorByWorkspace(shared, main, target_ws);
      if (mon != nullptr) {
         if (mon->GetFullscreenCount() > 0)
            mon->DecrementFullscreenCount();
         if (mon->GetFullscreenCount() == 0)
            main->transaction.QueueBarVisibility(win->GetHwnd(), TRUE);
      }
   }

   main->windows->Remove(win);

   if (was_focused == TRUE)
      main->focused_hwnd = nullptr;

   if (target_ws != nullptr)
      main->transaction.QueueRelayout(target_ws);

   if (was_focused == TRUE) {
      Workspace const *next_ws =
          (target_ws != nullptr) ? target_ws : main->focused_workspace;
      if (next_focus == nullptr)
         next_focus = FindNextFocusTarget(main, next_ws, hwnd);
      if (next_focus != nullptr) {
         main->focus_intent.ArmKnown(next_focus);
      }
   }
}

// ProcessObjectHide — called from DrainEventQueue (main thread, in transaction)
void ProcessObjectHide(SharedState *shared, MainContext *main, HWND hwnd) {
   Window *win = main->windows->FindByHwnd(hwnd);
   if (win == nullptr)
      return;

   // Any hide hides the ring with it (minimize, programmatic SW_HIDE, etc.).
   SyncWindowOverlay(main, hwnd, TRUE);

   InfoW(L"Managed window hidden: %p", hwnd);

   Workspace *target_ws = FindWorkspaceByHwnd(shared, main, hwnd);

   // Compute focus fallback BEFORE removal — EVENT_OBJECT_HIDE precedes
   // EVENT_OBJECT_DESTROY, so a subsequent HandleObjectDestroy would
   // find the window already removed from the tree.
   BOOL const was_focused = static_cast<BOOL>(main->focused_hwnd == hwnd);
   HWND next_focus = nullptr;
   if ((was_focused != 0) && (target_ws != nullptr) &&
       target_ws->Windows().size() > 1 && (target_ws->GetEngine() != nullptr)) {
      next_focus = target_ws->GetEngine()->get_closest_window(hwnd);
   }

   if (target_ws != nullptr)
      target_ws->RemoveWindow(win);

   if (was_focused == TRUE)
      main->focused_hwnd = nullptr;

   if (target_ws != nullptr)
      main->transaction.QueueRelayout(target_ws);

   if (was_focused == TRUE) {
      Workspace const *next_ws =
          (target_ws != nullptr) ? target_ws : main->focused_workspace;
      if (next_focus == nullptr)
         next_focus = FindNextFocusTarget(main, next_ws, hwnd);
      if (next_focus != nullptr) {
         main->focus_intent.ArmKnown(next_focus);
      }
   }
}

// ProcessMinimizeStart — called from DrainEventQueue (main thread, in
// transaction)
void ProcessMinimizeStart(SharedState *shared, MainContext *main, HWND hwnd) {
   Window *win = main->windows->FindByHwnd(hwnd);
   if (win == nullptr)
      return;

   // Hide-not-destroy: the ring is suppressed, the overlay survives and
   // re-appears on restore (ProcessMinimizeEnd).
   SyncWindowOverlay(main, hwnd, TRUE);

   Workspace *target_ws = FindWorkspaceByHwnd(shared, main, hwnd);

   // Compute focus fallback BEFORE removal so get_closest_window can
   // still find the reference window in the layout tree.
   BOOL const was_focused = static_cast<BOOL>(main->focused_hwnd == hwnd);
   HWND next_focus = nullptr;
   if ((was_focused == TRUE) && (target_ws != nullptr) &&
       target_ws->Windows().size() > 1 && (target_ws->GetEngine() != nullptr)) {
      next_focus = target_ws->GetEngine()->get_closest_window(hwnd);
   }

   if (target_ws != nullptr)
      target_ws->RemoveWindow(win);

   if (was_focused == TRUE)
      main->focused_hwnd = nullptr;

   if (target_ws != nullptr) {
      main->transaction.QueueRelayout(target_ws);
      for (const auto &window : target_ws->Windows()) {
         main->transaction.QueueRedrawContent(window->GetHwnd());
      }
   }

   if (was_focused == TRUE) {
      Workspace const *next_ws =
          (target_ws != nullptr) ? target_ws : main->focused_workspace;
      if (next_focus == nullptr)
         next_focus = FindNextFocusTarget(main, next_ws, hwnd);
      if (next_focus != nullptr) {
         main->focus_intent.ArmKnown(next_focus);
      }
   }
}

// ProcessMinimizeEnd — called from DrainEventQueue (main thread, in
// transaction)
void ProcessMinimizeEnd(SharedState *shared, MainContext *main, HWND hwnd) {
   Window *win = main->windows->FindByHwnd(hwnd);
   BOOL new_registration = FALSE;
   if (win == nullptr) {
      // Window was not registered — the FOREGROUND event may have fired
      // while it was still DWM-cloaked (minimized), causing
      // IsManageableWindow's cloak check to reject it.  The window is
      // now fully restored — register it now so it can be added.
      if (IsWindowVisible(hwnd) == FALSE)
         return;
      enum WindowTriggerReturnType const ret =
          NewWindowTrigger(hwnd, shared, main, FALSE);
      if (ret != REGISTER_SUCCESSFUL)
         return;
      win = main->windows->Windows().back().get();
      new_registration = TRUE;
   }

   // Window restored — re-show the ring.
   SyncWindowOverlay(main, hwnd, FALSE);

   Workspace *target_ws = nullptr;
   Workspace const *ws_with_window = FindWorkspaceByHwnd(shared, main, hwnd);
   if (ws_with_window == nullptr) {
      // Only skip re-add if the window is actually hidden.
      if (IsWindowVisible(hwnd) == FALSE)
         return;
      target_ws = main->focused_workspace;
      RECT win_rect;
      if (GetWindowRect(hwnd, &win_rect) == TRUE) {
         POINT const centre = {
             .x = (win_rect.left + win_rect.right) / 2,
             .y = (win_rect.top + win_rect.bottom) / 2,
         };
         Monitor const *const mon =
             FindMonitorByWorkAreaPoint(nullptr, main, centre);
         if (mon != nullptr)
            target_ws = mon->GetActiveWorkspace();
      }

      if (target_ws != nullptr) {
         if ((target_ws->GetEngine() != nullptr) &&
             (IsWindowManagedByLayout(win) == TRUE)) {
            target_ws->GetEngine()->insert(hwnd, main->focused_hwnd);
         }
         target_ws->TrackWindow(win);
      }
   }

   if (target_ws != nullptr)
      main->transaction.QueueRelayout(target_ws);

   // Newly registered on restore: focus it, matching the show-path behavior
   // for brand-new windows.
   if ((new_registration == TRUE) && (target_ws != nullptr)) {
      FocusWindow(hwnd, shared, main);
   }
}

// ProcessMoveSizeStart — called from DrainEventQueue (main thread, in
// transaction)
void ProcessMoveSizeStart(SharedState *shared, MainContext *main, HWND hwnd) {
   Workspace const *mws = FindWorkspaceByHwnd(shared, main, hwnd);
   if (mws != nullptr)
      main->resize_hwnd = hwnd;
}

// ProcessMoveSizeEnd — called from DrainEventQueue (main thread, in
// transaction)
void ProcessMoveSizeEnd(SharedState *shared, MainContext *main, HWND hwnd) {
   if (main->resize_hwnd != hwnd)
      return;

   main->resize_hwnd = nullptr;

   // ── Settled: clear any in-flight layout move ──
   // The window's move/size operation ended, so it is at its final rect —
   // whether it landed at the issued rect (layout move) or somewhere else
   // (user drag, app-driven move). Clearing the flag lets the commit-end
   // flush sync the ring to the settled rect immediately.
   Window *settled_win = main->windows->FindByHwnd(hwnd);
   if (settled_win != nullptr) {
      settled_win->SetMoveInFlight(FALSE);
   }

   // ── Transition detection ──
   CheckWindowTransitions(shared, main, hwnd);

   // ── Main relayout ──
   Workspace *rws = FindWorkspaceByHwnd(shared, main, hwnd);
   if (rws == nullptr) {
      Warn("Window without an assigned workspace reached ProcessMoveSizeEnd, "
           "this should be near impossible. Processing aborted.");
      return;
   }
   if (rws != nullptr)
      main->transaction.QueueRelayout(rws);

   // ── Revert if unlock modifier not held ──
   if (rws != nullptr)
      CheckResizeRevert(shared, main, hwnd, rws);

   // ── Update layout tree to settled size ──
   if (rws != nullptr)
      CheckResizeSettle(main, hwnd, rws);

   // ── Floating-window handling ──
   Window *float_win = main->windows->FindByHwnd(hwnd);
   HandleFloatingMove(shared, main, hwnd, rws, float_win);

   // ── Border overlay: converge the settled geometry at commit-end flush ──
   // Tiled windows settle without emitting OP_MOVE_WINDOW (layout skips
   // same-rect windows); floating/fullscreen windows never relayout.
   if (float_win != nullptr)
      float_win->MarkOverlayDirty();
}

void ProcessObjectShow(SharedState *shared, MainContext *main, HWND hwnd) {
   Window *win = main->windows->FindByHwnd(hwnd);
   BOOL new_registration = FALSE;
   if (win == nullptr) {
      enum WindowTriggerReturnType const ret =
          NewWindowTrigger(hwnd, shared, main, FALSE);
      if (ret != REGISTER_SUCCESSFUL)
         return;
      win = main->windows->Windows().back().get();
      new_registration = TRUE;
   }

   // Window visible again — sync the ring (suppressed while fullscreen).
   SyncWindowOverlayAuto(main, hwnd);

   Workspace *ws_with_window = FindWorkspaceByHwnd(shared, main, hwnd);

   // Minimized windows must never enter the workspace layout.
   if (IsIconic(hwnd) == TRUE)
      return;

   if (ws_with_window == nullptr) {
      Workspace *target_ws = ResolveRuleWorkspace(shared, main, win);
      if (target_ws == nullptr)
         target_ws = ResolveHitTestWorkspace(main, hwnd);
      if (target_ws != nullptr) {
         InsertWindowIntoWorkspace(shared, main, target_ws, hwnd, win);
         main->transaction.QueueRelayout(target_ws);
         // Windows registered via EVENT_OBJECT_SHOW never pass through
         // HandleWindowCreate's focus path, so focus the new window here:
         // FocusWindow queues OP_FOCUS_WINDOW + border colors + relayout and
         // is safe to call inside the current transaction.
         if (new_registration == TRUE) {
            FocusWindow(hwnd, shared, main);
         }
      }
   } else {
      main->transaction.QueueRelayout(ws_with_window);
   }
}

// ProcessObjectReorder — called from DrainEventQueue (main thread, in
// transaction)
void ProcessObjectReorder(SharedState * /*shared*/, MainContext *main,
                          HWND hwnd) {
   Window *win = main->windows->FindByHwnd(hwnd);
   if (win == nullptr)
      return;
   // Re-assert the ring's relative z-slot after something was raised above
   // the target window (e.g. a click-raise, dialog, or window manager).
   win->AssertOverlayZOrder();

   /* Repair every ring, not just the reordered window's: a raise of one
    * window buries others' rings too, and this is the event-speed repair. */
   for (const auto &window : main->windows->Windows()) {
      Window *w = window.get();
      if (w != nullptr)
         w->AssertOverlayZOrder();
   }
}

// ProcessCreateOrForeground — called from DrainEventQueue (main thread, in
// transaction)
void ProcessCreateOrForeground(SharedState *shared, MainContext *main,
                               DWORD event, HWND hwnd) {
   if (g_worker->setup_in_progress == TRUE)
      return;

   // ── Focus intent ──
   // A Windows-supplied foreground must not overrule the user's most recent
   // explicit focus action. While the intent is armed, drop a registered
   // non-target foreground and re-assert our planned target; STAY armed so a
   // later stray in a spawn storm cannot slip through.
   if (event == EVENT_SYSTEM_FOREGROUND &&
       main->focus_intent.ShouldSuppress(
           hwnd, static_cast<BOOL>(main->windows->FindByHwnd(hwnd) !=
                                   nullptr)) == TRUE) {
      if (main->focus_intent.Kind() == FocusIntentKind::Known) {
         PostThreadMessage(shared->main_thread_id, WM_FORCE_FOCUS,
                           (WPARAM)main->focus_intent.Target(), 0);
      }
      return; // drop the noise, STAY armed
   }

   HandleWindowCreate(hwnd, shared, main);

   // Check for window state transitions (fullscreen, maximize, restore)
   CheckWindowTransitions(shared, main, hwnd);

   if (event == EVENT_SYSTEM_FOREGROUND)
      main->focus_intent.NoteLanded(hwnd);
}

void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
                           LONG idObject, LONG idChild, DWORD dwEventThread,
                           DWORD dwmsEventTime) {
   (void)hWinEventHook;
   (void)dwEventThread;
   (void)dwmsEventTime;
   (void)idChild;

   if (g_worker == nullptr)
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
      EventRecord const record = {
          .event = event,
          .hwnd = hwnd,
          .idObject = idObject,
          .idChild = idChild,
          .dwEventThread = dwEventThread,
          .dwmsEventTime = dwmsEventTime,
          .dpi = 0,
      };
      g_worker->event_queue.Push(record);
      break;
   }
   case EVENT_OBJECT_CREATE:
   case EVENT_SYSTEM_FOREGROUND:
      if (g_worker->setup_in_progress == TRUE)
         break;
      {
         EventRecord const record = {
             .event = event,
             .hwnd = hwnd,
             .idObject = idObject,
             .idChild = idChild,
             .dwEventThread = dwEventThread,
             .dwmsEventTime = dwmsEventTime,
             .dpi = 0,
         };
         g_worker->event_queue.Push(record);
      }
      break;
   }
}
