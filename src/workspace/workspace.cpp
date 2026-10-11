#include "workspace.h"
#include "../bar/bar.h"
#include "../config/lua/parser.h"
#include "../core/main_context.h"
#include "../core/shared_state.h"
#include "../core/sync.h"
#include "../dpi/dpi.h"
#include "../logging/logger.h"
#include "../math/rect.h"
#include "../monitor/monitor.h"
#include "../transaction/transaction.h"
#include "../win/win_utils.h"
#include "../window/border/border_manager.h"
#include "../window/cloaking.h"
#include "../window/filter.h"
#include "../window/focus.h"
#include "../window/fullscreen/detect.h"
#include "../window/window.h"
#include "layouts/dwindle/dwindle_vtable.h"
#include "layouts/layout_api.h"
#include "layouts/master/master_vtable.h"
#include "layouts/monocle/monocle_vtable.h"
#include "placement.h"
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <dwmapi.h>
#include <minwindef.h>
#include <utility>
#include <windef.h>
#include <windows.h>
#include <winerror.h>
#include <winnt.h>

/**
 * @file workspace.cpp
 * @brief Workspace management.
 */

// ---------------------------------------------------------------
// Workspace config lookup
// ---------------------------------------------------------------

auto FindWorkspaceConfig(struct SharedState *shared,
                         struct MainContext * /*main*/, size_t workspace_id)
    -> const struct WorkspaceConfig * {
   if (shared == nullptr)
      return nullptr;
   for (int i = 0; i < shared->config.workspace_config_count; i++) {
      if (shared->config.workspace_configs[i].id == workspace_id)
         return &shared->config.workspace_configs[i];
   }
   return nullptr;
}

// ---------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------

namespace {

inline void LowerFullscreenWindows(HWND hwnd, Workspace *hwnd_workspace,
                                   struct SharedState *shared,
                                   struct MainContext *main) {
   Window *target = main->windows->FindByHwnd(hwnd);
   if ((target == nullptr) || (target->IsFullscreen() == TRUE) ||
       (target->IsUnmanagedFullscreen() == TRUE))
      return;

   for (auto *w : hwnd_workspace->Windows()) {
      if ((w->IsFullscreen() == TRUE) || (w->IsUnmanagedFullscreen() == TRUE)) {
         BFWMSetWindowZ(w->GetHwnd(), BFWM_Z_NOTOPMOST);
         BFWMSetWindowZ(w->GetHwnd(), BFWM_Z_BOTTOM);
      }
   }
   for (auto *w : hwnd_workspace->Windows()) {
      if ((w->IsFullscreen() == FALSE) &&
          (w->IsUnmanagedFullscreen() == FALSE) &&
          (IsWindowVisible(w->GetHwnd()) == TRUE) &&
          (IsIconic(w->GetHwnd()) == FALSE)) {
         BFWMSetWindowZ(w->GetHwnd(), BFWM_Z_TOP);
      }
   }

   // Ensure bar stays on top of raised tiled windows
   Monitor *bar_mon = FindMonitorByWorkspace(shared, main, hwnd_workspace);
   if ((bar_mon != nullptr) && (bar_mon->GetBar() != nullptr))
      bar_mon->GetBar()->EnsureTopZ();
}

// ── Fullscreen transition helpers ──

inline void LowerPreviousFullscreen(HWND old_hwnd, BOOL old_valid,
                                    struct SharedState *shared,
                                    struct MainContext *main) {
   if (old_valid == FALSE)
      return;

   Window *old_win = main->windows->FindByHwnd(old_hwnd);
   if ((old_win == nullptr) || (old_win->IsFullscreen() == FALSE))
      return;

   Workspace *old_workspace = FindWorkspaceByHwnd(shared, main, old_hwnd);
   BFWMSetWindowZ(old_hwnd, BFWM_Z_NOTOPMOST);
   BFWMSetWindowZ(old_hwnd, BFWM_Z_BOTTOM);
   if (old_workspace == nullptr)
      return;

   Monitor *old_mon = FindMonitorByWorkspace(shared, main, old_workspace);
   if (old_mon == nullptr)
      return;

   if (old_mon->GetFullscreenCount() > 0)
      old_mon->DecrementFullscreenCount();
   if (old_mon->GetFullscreenCount() == 0 && (old_mon->GetBar() != nullptr))
      old_mon->GetBar()->Show();
}

inline void RaiseNewFullscreen(HWND hwnd, struct SharedState *shared,
                               struct MainContext *main) {
   Window *new_win = main->windows->FindByHwnd(hwnd);
   if ((new_win == nullptr) || (new_win->IsFullscreen() == 0))
      return;

   BFWMSetWindowZ(hwnd, BFWM_Z_TOPMOST);
   Workspace *new_workspace = FindWorkspaceByHwnd(shared, main, hwnd);
   if (new_workspace == nullptr)
      return;

   Monitor *new_mon = FindMonitorByWorkspace(shared, main, new_workspace);
   if (new_mon == nullptr)
      return;

   new_mon->IncrementFullscreenCount();
   if (new_mon->GetFullscreenCount() == 1 && (new_mon->GetBar() != nullptr))
      new_mon->GetBar()->Hide();
}

inline void HandleMouseFollowsFocus(HWND hwnd, struct SharedState *shared) {
   if (shared->config.mouse_follows_focus == 0)
      return;

   RECT r;
   if (GetWindowRect(hwnd, &r) != 0)
      SetCursorPos((r.left + r.right) / 2, (r.top + r.bottom) / 2);
}

inline void CloakInactiveWorkspaces(Workspace *workspace,
                                    struct MainContext *main, Monitor *mon) {
   if ((mon == nullptr) || mon->GetActiveWorkspace() != workspace)
      return;
   for (const auto &j : mon->Workspaces()) {
      if (j == workspace)
         continue;
      for (auto *win : j->Windows()) {
         // Already cloaked in the current state — skip the redundant
         // re-queue (each OP_CLOAK_WINDOW apply is a COM round-trip).
         if (win->IsCloaked() == TRUE)
            continue;
         HWND h = win->GetHwnd();
         if (main->transaction.IsInTransaction() == TRUE) {
            main->transaction.QueueCloak(h, TRUE);
         } else {
            HRESULT const result = SetWindowCloakState(h, TRUE);
            if (FAILED(result)) {
               Warn("CloakInactiveWorkspaces: cloak HWND %p failed "
                    "(result=0x%08lx)",
                    h, static_cast<unsigned long>(result));
            } else {
               win->SetCloaked(TRUE);
            }
         }
      }
   }
}

/**
 * @brief Update inactive-window border colors (single commit-end flush).
 *
 * Keeps the historical main->applying_layout guard around the border-color
 * queueing. Focused window is skipped.
 */
inline void SetInactiveWindowBorders(Workspace *workspace,
                                     struct SharedState *shared,
                                     struct MainContext *main) {
   main->applying_layout = TRUE;
   for (const auto &window : workspace->Windows()) {
      HWND h = window->GetHwnd();
      if ((IsWindowVisible(h) == FALSE) || h == main->focused_hwnd)
         continue;
      main->transaction.QueueBorderColor(h, shared->config.inactive_border);
   }
   main->applying_layout = FALSE;
}

} // namespace

auto Workspace::CreateLayoutEngine(LayoutType type, RECT workspace_rect,
                                   int gap_between, int gap_edge,
                                   int border_width)
    -> std::unique_ptr<LayoutEngine> {
   switch (type) {
   case DWINDLE:
      return DwindleLayoutEngineCreate(workspace_rect, gap_between, gap_edge,
                                       border_width);
   case MONOCLE:
      return MonocleLayoutEngineCreate(workspace_rect, gap_between, gap_edge,
                                       border_width);
   case MASTER:
      return MasterLayoutEngineCreate(workspace_rect, gap_between, gap_edge,
                                      border_width);
   case LAYOUT_NONE:
   case LAYOUT_TYPE_COUNT:
      return nullptr;
   }
   return nullptr;
}

auto CreateWorkspaceForMonitor(SharedState *shared, MainContext *main,
                               size_t target_id, const std::wstring *name,
                               Monitor *mon) -> Workspace * {
   RECT ws_rect = mon->GetWorkArea();
   /* bar_cfg margins are logical; scale them to the monitor's physical
    * pixels. GetBarHeight() is already physical. ScaleForMonitor guards a
    * DPI of zero (registry miss) by falling back to identity scaling. */
   ws_rect.top += main->dpi->ScaleForMonitor(
                      mon->GetHandle(), shared->config.bar_cfg.margin.top) +
                  mon->GetBarHeight() +
                  main->dpi->ScaleForMonitor(
                      mon->GetHandle(), shared->config.bar_cfg.margin.bottom);
   ScaledLayoutConfig const scaled =
       ScaleLayoutConfigForMonitor(shared, main, mon->GetHandle());
   return new Workspace(target_id, name->c_str(), shared->config.default_layout,
                        ws_rect, scaled.gap_between, scaled.gap_edge,
                        scaled.border_width);
}

auto ScaleLayoutConfigForMonitor(struct SharedState *shared,
                                 struct MainContext *main, HMONITOR hmon)
    -> ScaledLayoutConfig {
   if (shared == nullptr)
      return {};
   ScaledLayoutConfig out = {
       .gap_between = shared->config.gap_between,
       .gap_edge = shared->config.gap_edge,
       .border_width = shared->config.border_width,
   };
   if (main->dpi != nullptr) {
      out.gap_between = main->dpi->ScaleForMonitor(hmon, out.gap_between);
      out.gap_edge = main->dpi->ScaleForMonitor(hmon, out.gap_edge);
      out.border_width = main->dpi->ScaleForMonitor(hmon, out.border_width);
   }
   return out;
}

// ---------------------------------------------------------------
// Public API
// ---------------------------------------------------------------

Workspace::Workspace(size_t identifier, const wchar_t *name, LayoutType layout,
                     RECT rect, int gap_between, int gap_edge,
                     int border_width) {
   identifier_ = identifier;
   is_active_ = FALSE;
   origin_monitor_uid_.clear();
   label_.clear();
   name_ = (name != nullptr) ? name : L"";
   workspace_rect_ = rect;
   windows_.reserve(8);
   /* The gap/border values arrive already scaled to the creating monitor's
    * DPI (see ScaleLayoutConfigForMonitor); keep them so ApplyConfig can
    * recreate the engine with the same physical values. */
   gap_between_ = gap_between;
   gap_edge_ = gap_edge;
   border_width_ = border_width;
   /* Usable tiling area = workspace rect inset by the edge gap + border strip
    * on every side. Kept in sync here (creation) and in RecalculateRect (later
    * geometry / DPI / gap changes). See GetUsableRect. */
   int const frame_inset = gap_edge + border_width;
   usable_rect_ = rect;
   usable_rect_.left += frame_inset;
   usable_rect_.top += frame_inset;
   usable_rect_.right -= frame_inset;
   usable_rect_.bottom -= frame_inset;
   engine_ =
       CreateLayoutEngine(layout, rect, gap_between, gap_edge, border_width);
}

Workspace::~Workspace() = default;

void Workspace::ApplyConfig(struct SharedState *shared,
                            struct MainContext *main, size_t identifier) {
   const struct WorkspaceConfig *workspace_config =
       FindWorkspaceConfig(shared, main, identifier);
   if (workspace_config == nullptr)
      return;

   /* Apply label */
   if (!workspace_config->label.empty()) {
      label_ = workspace_config->label;
   }

   /* Apply layout if explicitly set and differs from current */
   if ((workspace_config->has_layout == TRUE) &&
       ((engine_ == nullptr) || engine_->type() != workspace_config->layout)) {
      auto new_engine =
          CreateLayoutEngine(workspace_config->layout, workspace_rect_,
                             gap_between_, gap_edge_, border_width_);
      if (new_engine != nullptr) {
         engine_ = std::move(new_engine);

         /* Re-insert existing managed windows into new engine */
         for (auto *win : windows_) {
            if (IsWindowManagedByLayout(win) == TRUE)
               engine_->insert(win->GetHwnd(), nullptr);
         }
      }
   }
}

void Workspace::DestroyWindows(struct SharedState *shared,
                               struct MainContext *main) {
   if (shared == nullptr)
      return;

   for (auto *win : windows_) {
      if (win != nullptr) {
         main->windows->Remove(win);
      }
   }
   windows_.clear();
}

void Workspace::AddWindow(struct SharedState *shared, struct MainContext *main,
                          Window *window) {

   // Minimized windows should not enter the tree — they get re-added on
   // restore via EVENT_OBJECT_SHOW.
   if (IsIconic(window->GetHwnd()) == TRUE)
      return;

   // Only insert managed windows into the layout tree.  Floating windows
   // (e.g. fullscreen windows detected at registration time) are tracked by
   // the workspace but excluded from tree-based positioning.
   if (IsWindowManagedByLayout(window) == TRUE) {
      if ((engine_ != nullptr) &&
          engine_->insert(window->GetHwnd(), main->focused_hwnd))
         windows_.push_back(window);
   } else {
      windows_.push_back(window);
      if (window->IsFloating() == TRUE) {
         Monitor *mon = FindMonitorByWorkspace(shared, main, this);
         if (mon != nullptr) {
            UINT const dpi = (shared != nullptr && main->dpi != nullptr)
                                 ? main->dpi->GetDpi(mon->GetHandle())
                                 : DpiSystem::BaseDpi();
            RECT const clamped = window->ClampTo(mon->GetWorkArea(), dpi);
            if (EqualRect(&clamped, window->SavedRectPtr()) == FALSE) {
               window->SetSavedRect(clamped);
               BFWMSetWindowPos(window->GetHwnd(), &clamped);
               window->MarkOverlayDirty();
            }
         }
      }
   }

   // Focus the newly added window.
   FocusWindow(window->GetHwnd(), shared, main);
}

void Workspace::RemoveWindow(Window *window) {
   // Always remove from the window array — this must happen regardless of
   // layout-tree membership (auto-fullscreen windows are never inserted).
   auto iterator = std::ranges::find(windows_, window);
   if (iterator != windows_.end())
      windows_.erase(iterator);

   if (engine_ != nullptr)
      engine_->remove(window->GetHwnd());
}

void UpdateFocusTracking(HWND hwnd, struct SharedState *shared,
                         struct MainContext *main) {
   if (IsWindow(hwnd) == FALSE)
      return;

   // Update focus tracking
   main->focused_hwnd = hwnd;

   // Update focused_workspace: find which workspace owns this hwnd
   Workspace *hwnd_workspace = nullptr;
   auto const &monitors = main->monitors->Monitors();
   size_t const mon_count = monitors.size();
   for (size_t m = 0; m < mon_count && (hwnd_workspace == nullptr); m++) {
      Monitor *mon = monitors[m];
      for (size_t j = 0;
           j < mon->Workspaces().size() && (hwnd_workspace == nullptr); j++) {
         if (mon->Workspaces()[j]->ContainsWindow(hwnd)) {
            hwnd_workspace = mon->Workspaces()[j];
            break;
         }
      }
   }

   if (hwnd_workspace == nullptr) {
      BarUpdateRequest(main, BAR_UPDATE_DIRTY);
      return;
   }

   main->focused_workspace = hwnd_workspace;

   // Keep the layout's notion of the active window in sync so layouts like
   // monocle can report the currently-shown window from get_closest_window.
   if (hwnd_workspace->GetEngine() != nullptr) {
      hwnd_workspace->GetEngine()->set_active_window(hwnd);
   }

   // Defer layout to commit time (self-wraps if not in a transaction).
   hwnd_workspace->ApplyLayout(shared, main);

   // When focusing a non-fullscreen window, lower any fullscreen
   // windows on the workspace and raise all managed windows above
   // them so the entire layout is visible.
   LowerFullscreenWindows(hwnd, hwnd_workspace, shared, main);

   BarUpdateRequest(main, BAR_UPDATE_DIRTY);
}

void WorkspaceHandleDesktopClick(struct SharedState *shared,
                                 struct MainContext *main, POINT point) {
   HWND clicked = WindowFromPoint(point);
   if (clicked != nullptr)
      clicked = GetAncestor(clicked, GA_ROOT);

   // Only the desktop and taskbar count as a desktop click. Any real
   // window — managed or unmanaged — must return early: activating the
   // workspace would raise its windows above the clicked window and bury
   // unmanaged ones.
   if ((clicked != nullptr) && !IsShellSurface(clicked))
      return;

   Monitor *mon = FindMonitorByPoint(shared, main, point);
   if ((mon == nullptr) || (mon->GetActiveWorkspace() == nullptr) ||
       mon->GetActiveWorkspace() == main->focused_workspace)
      return;

   main->focused_workspace = mon->GetActiveWorkspace();

   Workspace *workspace = mon->GetActiveWorkspace();
   HWND first = nullptr;
   if (workspace->GetEngine() != nullptr) {
      first = workspace->GetEngine()->get_closest_window(nullptr);
   }

   if (first != nullptr) {
      FocusWindow(first, shared, main);
   } else {
      main->focused_hwnd = nullptr;
   }

   BarUpdateRequest(main, BAR_UPDATE_DIRTY);
}

void WorkspaceHandleMouseHover(struct SharedState *shared,
                               struct MainContext *main, POINT point) {
   HWND hover = WindowFromPoint(point);
   if (hover != nullptr)
      hover = GetAncestor(hover, GA_ROOT);

   if ((hover != nullptr) && hover != main->focused_hwnd &&
       (main->windows->FindByHwnd(hover) != nullptr)) {
      Workspace *workspace = FindWorkspaceByHwnd(shared, main, hover);
      if (workspace != nullptr) {
         Monitor *mon = FindMonitorByWorkspace(shared, main, workspace);
         if ((mon == nullptr) || workspace == mon->GetActiveWorkspace()) {
            FocusWindow(hover, shared, main);
         }
      }
   }
}

namespace {

/// Returns TRUE when the current OS foreground window is fullscreen
/// (exclusive or borderless) and is not `target`. State-based on purpose:
/// the fullscreen flag may be stale when the transition was never detected
/// by the event hooks, but the window geometry/style still report fullscreen.
auto ForegroundIsFullscreen(HWND target) -> BOOL {
   HWND foreground = GetForegroundWindow();
   if ((foreground == nullptr) || (foreground == target))
      return FALSE;
   FullscreenType const type = DetectFullscreenWindow(foreground);
   return ((type & (FS_EXCLUSIVE_FULLSCREEN | FS_BORDERLESS_WINDOW)) != 0)
              ? TRUE
              : FALSE;
}

} // namespace

void FocusWindow(HWND hwnd, struct SharedState *shared,
                 struct MainContext *main) {
   if (shared == nullptr || IsWindow(hwnd) == FALSE)
      return;

   // Never steal the foreground from a window that is currently fullscreen
   // (exclusive or borderless): switching the OS-level foreground kicks an
   // exclusive-fullscreen game out of exclusive mode and minimizes it (e.g.
   // Unity's D3DProxyWindow helper focused during CS2's device init). Skip
   // the whole operation — including the focused_hwnd update — so BFWM
   // keeps treating the fullscreen window as focused. User-initiated
   // switches (mouse click, Alt+Tab) already moved the OS foreground before
   // this runs, so they pass the check.
   if (ForegroundIsFullscreen(hwnd) == TRUE)
      return;

   // Self-wrap: only wrap when not already inside a transaction.
   BOOL wrapped = FALSE;
   if (main->transaction.IsInTransaction() == FALSE) {
      main->transaction.Begin();
      wrapped = TRUE;
   }

   // Focus must always shift to the workspace owning this window: cloak the
   // previously active workspace, uncloak this one, apply layout. No-op when
   // the workspace is already active. Runs inside the transaction so the
   // cloak/relayout ops join the same commit as the focus op below.
   WorkspaceActivateForWindow(hwnd, shared, main);

   HWND old_focus = main->focused_hwnd;

   main->transaction.QueueFocus(hwnd);

   if ((old_focus != nullptr) && old_focus != hwnd &&
       (IsWindowVisible(old_focus) == TRUE)) {
      main->transaction.QueueBorderColor(old_focus,
                                         shared->config.inactive_border);
   }

   // The new focus always gets the active border. Queued here (not only via
   // FocusWindowImmediate at OP_FOCUS_WINDOW apply): the implicit-focus paths
   // (workspace activation, close-refocus, hover-focus) can hit
   // FocusWindowImmediate's already-foregrounded early-return, which would
   // otherwise skip the active color entirely.
   if (IsWindowVisible(hwnd) == TRUE) {
      main->transaction.QueueBorderColor(hwnd, shared->config.border_color);
   }

   // Queue relayout
   Workspace *hwnd_workspace = FindWorkspaceByHwnd(shared, main, hwnd);
   if (hwnd_workspace != nullptr) {
      main->transaction.QueueRelayout(hwnd_workspace);
   }

   // Update the context
   main->focused_hwnd = hwnd;
   if (hwnd_workspace != nullptr) {
      main->focused_workspace = hwnd_workspace;

      /* Restack workspace layers: after an event-driven focus change (click,
       * hover, workspace switch), promote every peer in the focused layer
       * above the opposite layer so the visual z-order matches the layer
       * hierarchy. The cache inside RestackWorkspaceLayers skips when focus
       * stays within the same layer (the common case). */
      RestackWorkspaceLayers(shared, main);
      BarUpdateRequest(main, BAR_UPDATE_DIRTY);
   }

   if (wrapped == TRUE)
      BFWMTransactionCommit(shared, main);
}

auto FocusWindowImmediate(HWND hwnd, struct SharedState *shared,
                          struct MainContext *main) -> BOOL {
   if (IsWindow(hwnd) == FALSE)
      return FALSE;

   // Same guard as FocusWindow(): this entry point is reachable without the
   // wrapper (OP_FOCUS_WINDOW queued directly, e.g. the focus-neighbor
   // action). Returns FALSE so the caller skips the focused_hwnd update.
   if (ForegroundIsFullscreen(hwnd) == TRUE)
      return FALSE;

   // Don't do anything if already focused
   if (GetForegroundWindow() == hwnd && main->focused_hwnd == hwnd)
      return TRUE;

   // Capture old focus at entry — caller (OP_FOCUS_WINDOW apply in
   // transaction.c) sets main->focused_hwnd AFTER we return.
   HWND old_hwnd = main->focused_hwnd;
   BOOL const old_valid =
       static_cast<BOOL>((old_hwnd != nullptr) && old_hwnd != hwnd &&
                         (IsWindowVisible(old_hwnd) == TRUE));

   // Queue border changes; Pass 10 applies them so focus-queued colors
   // win over relayout-queued inactives from Pass 6.
   if (old_valid == TRUE)
      BorderManagerSetInactive(shared, main, old_hwnd);

   if (IsWindowVisible(hwnd) == TRUE)
      BorderManagerSetActive(shared, main, hwnd);

   // Lower the previous fullscreen window (z-order change, not DWM)
   LowerPreviousFullscreen(old_hwnd, old_valid, shared, main);

   // Single arm point for every "focus a specific window" path: the intent
   // suppresses stray Windows-supplied foregrounds until this target lands.
   main->focus_intent.ArmKnown(hwnd);

   FocusWindowReliable(hwnd);
   UpdateFocusTracking(hwnd, shared, main);

   // Raise the new window to topmost if it's fullscreen
   RaiseNewFullscreen(hwnd, shared, main);

   // Single flush + targeted redraws for both windows whose borders changed
   DwmFlush();
   if (old_valid == TRUE)
      BorderManagerRedraw(shared, main, old_hwnd);
   if (IsWindowVisible(hwnd) == TRUE)
      BorderManagerRedraw(shared, main, hwnd);

   HandleMouseFollowsFocus(hwnd, shared);

   return TRUE;
}

auto FindWorkspaceByHwnd(struct SharedState *shared, struct MainContext *main,
                         HWND hwnd) -> Workspace * {
   if (shared == nullptr)
      return nullptr;

   // Fast path: check the focused workspace first.
   // Verify focused_workspace still exists (it may have been freed by
   // ReconcileMonitors between Pass 2 and the focused_workspace update).
   if ((main->focused_workspace != nullptr) &&
       (FindMonitorByWorkspace(shared, main, main->focused_workspace) !=
        nullptr) &&
       main->focused_workspace->ContainsWindow(hwnd)) {
      return main->focused_workspace;
   }

   // Fallback: scan all monitors and their workspaces
   auto const &monitors = main->monitors->Monitors();
   size_t const mon_count = monitors.size();
   for (size_t m = 0; m < mon_count; m++) {
      Monitor *mon = monitors[m];
      for (const auto &workspace : mon->Workspaces()) {
         if (workspace->ContainsWindow(hwnd)) {
            return workspace;
         }
      }
   }

   return nullptr;
}

auto Workspace::ContainsWindow(HWND hwnd) const -> bool {
   return std::ranges::any_of(windows_, [hwnd](const Window *window) -> bool {
      return window->GetHwnd() == hwnd;
   });
}

void Workspace::ApplyLayout(struct SharedState *shared,
                            struct MainContext *main) {
   // Self-wrap: only wrap when not already inside a transaction.
   BOOL wrapped = FALSE;
   if (main->transaction.IsInTransaction() == FALSE) {
      main->transaction.Begin();
      wrapped = TRUE;
   }

   Monitor *mon = FindMonitorByWorkspace(shared, main, this);
   CloakInactiveWorkspaces(this, main, mon);

   if (windows_.empty()) {
      if (wrapped == TRUE)
         BFWMTransactionCommit(shared, main);
      return;
   }

   // Moves pass then inactive-border pass, in exactly this order.
   main->applying_layout = TRUE;
   PlacementApply(this, shared, main);
   SetInactiveWindowBorders(this, shared, main);

   if (wrapped == TRUE)
      BFWMTransactionCommit(shared, main);
}

auto ResolveTargetWorkspace(struct SharedState *shared,
                            struct MainContext *main, HWND hwnd)
    -> Workspace * {
   if ((shared == nullptr) || (hwnd == nullptr) || (IsWindow(hwnd) == FALSE))
      return (shared != nullptr) ? main->focused_workspace : nullptr;

   // Default to focused workspace as fallback
   Workspace *target_workspace = main->focused_workspace;
   RECT win_rect;

   if (GetWindowRect(hwnd, &win_rect) == FALSE)
      return target_workspace;

   if (IsIconic(hwnd) == TRUE || (IsWindowVisible(hwnd) == FALSE))
      return target_workspace;

   // Calculate center with overflow protection
   LONG center_x;
   LONG center_y;
   if (win_rect.right - win_rect.left > 0 &&
       win_rect.bottom - win_rect.top > 0) {
      // Use safe arithmetic
      center_x = win_rect.left + ((win_rect.right - win_rect.left) / 2);
      center_y = win_rect.top + ((win_rect.bottom - win_rect.top) / 2);
   } else {
      return target_workspace; // Invalid rectangle
   }

   POINT const centre = {.x = center_x, .y = center_y};

   BOOL found = FALSE;
   for (auto *mon : main->monitors->Monitors()) {
      if ((mon == nullptr) || (mon->GetActiveWorkspace() == nullptr))
         continue;

      RECT const mon_rect = mon->GetRect();
      if (PtInRect(&mon_rect, centre) == TRUE) {
         target_workspace = mon->GetActiveWorkspace();
         found = TRUE;
         break;
      }
   }

   // If window center isn't on any monitor, try the window corners
   if (found == FALSE) {
      // Check if any corner of the window is on a monitor
      std::array<POINT, 4> corners = {
          {
              {.x = win_rect.left, .y = win_rect.top},
              {.x = win_rect.right, .y = win_rect.top},
              {.x = win_rect.left, .y = win_rect.bottom},
              {.x = win_rect.right, .y = win_rect.bottom},
          },
      };

      for (int i = 0; i < 4; i++) {
         for (auto *mon : main->monitors->Monitors()) {
            if ((mon == nullptr) || (mon->GetActiveWorkspace() == nullptr))
               continue;

            RECT const mon_rect = mon->GetRect();
            if (PtInRect(&mon_rect, corners[i]) == TRUE) {
               target_workspace = mon->GetActiveWorkspace();
               found = TRUE;
               break;
            }
         }
         if (found == TRUE)
            break;
      }
   }

   return target_workspace;
}

auto FindMonitorByPoint(struct SharedState * /*shared*/,
                        struct MainContext *main, POINT point) -> Monitor * {
   auto const &monitors = main->monitors->Monitors();
   size_t const monitor_count = monitors.size();

   for (size_t i = 0; i < monitor_count; i++) {
      if (PointIntersectsRect(point, monitors[i]->GetRect()) == TRUE)
         return monitors[i];
   }

   return nullptr;
}

auto FindMonitorByWorkspace(struct SharedState *shared,
                            struct MainContext *main, Workspace *workspace)
    -> Monitor * {
   if ((shared == nullptr) || (workspace == nullptr))
      return nullptr;
   for (auto *mon : main->monitors->Monitors()) {
      for (size_t j = 0; j < mon->Workspaces().size(); j++) {
         if (mon->Workspaces()[j] == workspace)
            return mon;
      }
   }
   return nullptr;
}

auto FindWorkspaceOnMonitor(const Monitor *mon, size_t identifier)
    -> Workspace * {
   if (mon == nullptr)
      return nullptr;
   for (auto *workspace : mon->Workspaces()) {
      if (workspace->GetIdentifier() == identifier)
         return workspace;
   }
   return nullptr;
}

auto FindWorkspaceById(struct SharedState *shared, struct MainContext *main,
                       size_t identifier) -> Workspace * {
   if (shared == nullptr)
      return nullptr;
   for (auto *mon : main->monitors->Monitors()) {
      for (const auto &workspace : mon->Workspaces()) {
         if (workspace->GetIdentifier() == identifier)
            return workspace;
      }
   }
   return nullptr;
}

void WorkspaceDestroyIfEmpty(Monitor *mon, Workspace *workspace) {
   if ((mon == nullptr) || (workspace == nullptr))
      return;
   if (!workspace->IsEmpty())
      return;
   if (workspace == mon->GetActiveWorkspace())
      return;
   if (mon->Workspaces().size() <= 1)
      return;

   for (size_t j = 0; j < mon->Workspaces().size(); j++) {
      if (mon->Workspaces()[j] == workspace) {
         RECT const mon_rect = mon->GetRect();
         InfoW(L"Destroying empty workspace %zu on monitor at (%ld,%ld)",
               workspace->GetIdentifier(), mon_rect.left, mon_rect.top);
         delete workspace;
         mon->RemoveWorkspaceAt(j);
         return;
      }
   }
}

void WorkspaceActivateSimple(struct SharedState *shared,
                             struct MainContext *main, Monitor *mon,
                             Workspace *old_workspace, Workspace *new_workspace,
                             bool destroy_empty_old_ws) {
   if ((shared == nullptr) || (mon == nullptr) || (old_workspace == nullptr) ||
       (new_workspace == nullptr))
      return;

   BOOL const in_tx = main->transaction.IsInTransaction();

   // Serialize the cloak bookkeeping against the event thread's window
   // add/remove mutations. The lock is recursive, so nesting with the
   // transaction commit (which also takes main->lock) is safe. ApplyLayout
   // must run OUTSIDE the lock (see events.cpp HandleWindowReRegistered).
   {
      ScopedLock const lock(main->lock);
      for (auto *win : old_workspace->Windows()) {
         HWND h = win->GetHwnd();
         if (in_tx == TRUE) {
            main->transaction.QueueCloak(h, TRUE);
         } else {
            HRESULT const result = SetWindowCloakState(h, TRUE);
            if (FAILED(result)) {
               Error("WorkspaceActivateSimple: cloak HWND %p failed "
                     "(result=0x%08lx)",
                     h, static_cast<unsigned long>(result));
            } else {
               win->SetCloaked(TRUE);
            }
         }
      }
      for (auto *win : new_workspace->Windows()) {
         HWND h = win->GetHwnd();
         if (in_tx == TRUE) {
            main->transaction.QueueCloak(h, FALSE);
         } else {
            HRESULT const result = SetWindowCloakState(h, FALSE);
            if (FAILED(result)) {
               Error("WorkspaceActivateSimple: uncloak HWND %p failed "
                     "(result=0x%08lx)",
                     h, static_cast<unsigned long>(result));
            } else {
               win->SetCloaked(FALSE);
            }
         }
      }
   }

   mon->SetActiveWorkspace(new_workspace);
   main->focused_workspace = new_workspace;

   new_workspace->ApplyLayout(shared, main);
   if (destroy_empty_old_ws)
      WorkspaceDestroyIfEmpty(mon, old_workspace);

   BarUpdateRequest(main, BAR_UPDATE_DIRTY);

   // When called outside a transaction, force DWM to re-composite
   // immediately so the cloaking changes take effect.
   if (in_tx == FALSE) {
      DwmFlush();
      InvalidateRect(nullptr, nullptr, TRUE);
   }
}

void WorkspaceActivateForWindow(HWND hwnd, struct SharedState *shared,
                                struct MainContext *main) {
   if ((shared == nullptr) || (IsWindow(hwnd) == FALSE))
      return;

   Workspace *hwnd_ws = FindWorkspaceByHwnd(shared, main, hwnd);
   if (hwnd_ws == nullptr)
      return;

   Monitor *mon = FindMonitorByWorkspace(shared, main, hwnd_ws);
   if ((mon == nullptr) || hwnd_ws == mon->GetActiveWorkspace())
      return;

   WorkspaceActivateSimple(shared, main, mon, mon->GetActiveWorkspace(),
                           hwnd_ws, false);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto WorkspaceActivate(struct SharedState *shared, struct MainContext *main,
                       Monitor *mon, size_t target_id) -> bool {
   if ((shared == nullptr) || (mon == nullptr))
      return false;

   Workspace *target = FindWorkspaceOnMonitor(mon, target_id);

   if (target == nullptr) {
      std::wstring const name = L"ws_" + std::to_wstring(target_id);
      RECT ws_rect = mon->GetWorkArea();
      ws_rect.top += mon->GetBarHeight();
      ScaledLayoutConfig const scaled =
          ScaleLayoutConfigForMonitor(shared, main, mon->GetHandle());
      target = new Workspace(
          target_id, name.c_str(), shared->config.default_layout, ws_rect,
          scaled.gap_between, scaled.gap_edge, scaled.border_width);
      if (target == nullptr)
         return false;
      target->ApplyConfig(shared, main, target_id);
      mon->TrackWorkspace(target);
      RECT const mon_rect = mon->GetRect();
      InfoW(L"Created workspace %zu on monitor at (%ld,%ld)", target_id,
            mon_rect.left, mon_rect.top);
   }

   // Self-wrap: only wrap when not already inside a transaction —
   // a nested Commit would flush the outer batch early.
   BOOL wrapped = FALSE;
   if (main->transaction.IsInTransaction() == FALSE) {
      main->transaction.Begin();
      wrapped = TRUE;
   }

   if (target == mon->GetActiveWorkspace()) {
      // Already active — only a no-op if focus is already here.
      HWND focus_hwnd = main->focused_hwnd;
      if ((focus_hwnd != nullptr) && target->ContainsWindow(focus_hwnd) &&
          (IsWindowVisible(focus_hwnd) == TRUE)) {
         if (wrapped == TRUE)
            BFWMTransactionCommit(shared, main);
         return true;
      }
      // Focus is elsewhere — move it to the target workspace.
      if (target->GetEngine() != nullptr) {
         focus_hwnd = target->GetEngine()->get_closest_window(nullptr);
      } else {
         focus_hwnd = nullptr;
      }
      if (focus_hwnd != nullptr) {
         FocusWindow(focus_hwnd, shared, main);
      } else {
         main->focused_workspace = target;
         if ((main->focused_hwnd != nullptr) &&
             IsWindowVisible(main->focused_hwnd) == TRUE) {
            BorderManagerSetInactive(shared, main, main->focused_hwnd);
            BorderManagerRedraw(shared, main, main->focused_hwnd);
         }
         main->focused_hwnd = nullptr;
         BarUpdateRequest(main, BAR_UPDATE_DIRTY);
      }
      if (wrapped == TRUE)
         BFWMTransactionCommit(shared, main);
      return true;
   }

   Workspace *old = mon->GetActiveWorkspace();

   WorkspaceActivateSimple(shared, main, mon, old, target, true);

   HWND focus_hwnd = main->focused_hwnd;
   if (!target->ContainsWindow(focus_hwnd) ||
       (IsWindowVisible(focus_hwnd) == FALSE)) {
      if (target->GetEngine() != nullptr) {
         focus_hwnd = target->GetEngine()->get_closest_window(nullptr);
      } else {
         focus_hwnd = nullptr;
      }
   }
   if (focus_hwnd != nullptr) {
      FocusWindow(focus_hwnd, shared, main);
   } else {
      if ((main->focused_hwnd != nullptr) &&
          IsWindowVisible(main->focused_hwnd) == TRUE) {
         BorderManagerSetInactive(shared, main, main->focused_hwnd);
      }
      main->focused_hwnd = nullptr;
   }

   if (wrapped == TRUE)
      BFWMTransactionCommit(shared, main);

   return true;
}

void RestoreDesktopState(struct SharedState *shared, struct MainContext *main) {
   if (shared == nullptr)
      return;

   // Self-wrap: batch the border resets in a transaction, then flush
   // DWM once afterwards.
   BOOL wrapped = FALSE;
   if (main->transaction.IsInTransaction() == 0) {
      main->transaction.Begin();
      wrapped = TRUE;
   }

   // Reset borders to inactive and uncloak every managed window.
   for (size_t m = 0; m < main->monitors->Size(); m++) {
      Monitor *mon = main->monitors->At(m);
      for (auto *workspace : mon->Workspaces()) {
         for (auto *win : workspace->Windows()) {
            HWND h = win->GetHwnd();
            if (IsWindowVisible(h) == TRUE)
               BorderManagerSetInactive(shared, main, h);
            BOOL is_cloaked = FALSE;
            DwmGetWindowAttribute(h, DWMWA_CLOAKED, &is_cloaked,
                                  sizeof(is_cloaked));
            if (is_cloaked == TRUE) {
               SetWindowCloakState(h, FALSE);
               win->SetCloaked(FALSE);
            }
         }
      }
   }

   if (wrapped == TRUE)
      BFWMTransactionCommit(shared, main);

   // Force DWM to re-composite with the restored state.
   DwmFlush();
   InvalidateRect(nullptr, nullptr, TRUE);
}

void PushGapConfigToAllWorkspaces(struct SharedState *shared,
                                  struct MainContext *main) {
   if (shared == nullptr)
      return;
   for (size_t m = 0; m < main->monitors->Size(); m++) {
      Monitor *mon = main->monitors->At(m);
      /* Scale the logical gap/border config to this monitor's physical
       * pixels; the gaps_enabled toggle zeroes the gaps before scaling. */
      ScaledLayoutConfig const scaled =
          ScaleLayoutConfigForMonitor(shared, main, mon->GetHandle());
      int const eff_between =
          (shared->config.gaps_enabled != 0) ? scaled.gap_between : 0;
      int const eff_edge =
          (shared->config.gaps_enabled != 0) ? scaled.gap_edge : 0;
      for (size_t j = 0; j < mon->Workspaces().size(); j++) {
         Workspace *workspace = mon->Workspaces()[j];
         if (workspace->GetEngine() != nullptr) {
            LayoutConfig const cfg = {
                .gap_between = eff_between,
                .gap_edge = eff_edge,
                .border_width = scaled.border_width,
                .workspace_rect = workspace->GetWorkspaceRect(),
            };
            workspace->GetEngine()->apply_config(cfg);
         }
         if (workspace == mon->GetActiveWorkspace())
            workspace->ApplyLayout(shared, main);
      }
   }
}

void Workspace::RecalculateRect(Monitor *mon, struct SharedState *shared,
                                struct MainContext *main) {
   if ((mon == nullptr) || (shared == nullptr))
      return;
   RECT new_rect = mon->GetWorkArea();
   /* bar_cfg margins are logical config values; scale them to the monitor's
    * physical pixels. mon->GetBarHeight() is already physical. */
   UINT const dpi = (main->dpi != nullptr) ? main->dpi->GetDpi(mon->GetHandle())
                                           : DpiSystem::BaseDpi();
   new_rect.top += DpiSystem::Scale(shared->config.bar_cfg.margin.top, dpi) +
                   mon->GetBarHeight() +
                   DpiSystem::Scale(shared->config.bar_cfg.margin.bottom, dpi);
   workspace_rect_ = new_rect;
   ScaledLayoutConfig const scaled =
       ScaleLayoutConfigForMonitor(shared, main, mon->GetHandle());
   /* Converge the cached per-monitor-scaled layout values so a later
    * ApplyConfig recreates the engine from this monitor's DPI even after
    * MoveToMonitor reparents the workspace without recreation. */
   gap_between_ = scaled.gap_between;
   gap_edge_ = scaled.gap_edge;
   border_width_ = scaled.border_width;
   int const eff_between =
       (shared->config.gaps_enabled != 0) ? scaled.gap_between : 0;
   int const eff_edge =
       (shared->config.gaps_enabled != 0) ? scaled.gap_edge : 0;
   /* Usable tiling area = workspace rect inset by the effective edge gap plus
    * the border strip on every side. Matches the tiled area each engine derives
    * (dwindle: workspace rect minus border; master/monocle: tiled_rect), so a
    * rect clamped to it is representable by the engine. See GetUsableRect. */
   int const frame_inset = eff_edge + scaled.border_width;
   usable_rect_ = new_rect;
   usable_rect_.left += frame_inset;
   usable_rect_.top += frame_inset;
   usable_rect_.right -= frame_inset;
   usable_rect_.bottom -= frame_inset;
   if (engine_ != nullptr) {
      LayoutConfig const cfg = {
          .gap_between = eff_between,
          .gap_edge = eff_edge,
          .border_width = scaled.border_width,
          .workspace_rect = new_rect,
      };
      engine_->apply_config(cfg);
   }
}

void RecalculateAllWorkspaceRects(struct SharedState *shared,
                                  struct MainContext *main) {
   if (shared == nullptr)
      return;
   for (size_t m = 0; m < main->monitors->Size(); m++) {
      Monitor *mon = main->monitors->At(m);
      for (size_t j = 0; j < mon->Workspaces().size(); j++) {
         mon->Workspaces()[j]->RecalculateRect(mon, shared, main);
      }
   }
}

auto FindNextWorkspaceId(struct SharedState *shared, struct MainContext *main)
    -> size_t {
   size_t identifier = 1;
   while (FindWorkspaceById(shared, main, identifier) != nullptr)
      identifier++;
   return identifier;
}

void Workspace::MoveToMonitor(Monitor *target_mon, struct SharedState *shared,
                              struct MainContext *main) {
   if ((target_mon == nullptr) || (shared == nullptr))
      return;

   Monitor *src_mon = FindMonitorByWorkspace(shared, main, this);
   if ((src_mon == nullptr) || src_mon == target_mon)
      return;

   /* Remove from source monitor's workspace array */
   auto const &src_workspaces = src_mon->Workspaces();
   auto src_it = std::ranges::find(src_workspaces, this);
   if (src_it == src_workspaces.end())
      return;
   src_mon->RemoveWorkspace(this);

   if (src_mon->GetActiveWorkspace() == this) {
      if (!src_mon->Workspaces().empty()) {
         src_mon->SetActiveWorkspace(src_mon->Workspaces()[0]);
      } else {
         src_mon->SetActiveWorkspace(nullptr);
      }
   }

   /* Add to target monitor's workspace array */
   target_mon->TrackWorkspace(this);

   SetActive(FALSE);

   RecalculateRect(target_mon, shared, main);
}

auto FindUnassignedWorkspaceId(struct SharedState *shared,
                               struct MainContext *main, Monitor *mon)
    -> size_t {
   for (int i = 0; i < shared->config.workspace_config_count; i++) {
      const struct WorkspaceConfig *workspace_config =
          &shared->config.workspace_configs[i];
      if (std::cmp_equal(workspace_config->assigned_monitor,
                         mon->GetDisplayNumber()) &&
          (FindWorkspaceById(shared, main, workspace_config->id) == nullptr)) {
         return workspace_config->id;
      }
   }

   size_t identifier = 1;
   for (;;) {
      if (FindWorkspaceById(shared, main, identifier) != nullptr) {
         identifier++;
         continue;
      }
      const struct WorkspaceConfig *workspace_config =
          FindWorkspaceConfig(shared, main, identifier);
      if ((workspace_config != nullptr) &&
          workspace_config->assigned_monitor > 0 &&
          std::cmp_not_equal(workspace_config->assigned_monitor,
                             mon->GetDisplayNumber())) {
         identifier++;
         continue;
      }
      return identifier;
   }
}

/* ── Z-layer restacking ──────────────────────────────────────────────── */

void RestackWorkspaceLayers(SharedState *shared, MainContext *main) {
   if (main->focused_hwnd == nullptr)
      return;

   Window *focused = main->windows->FindByHwnd(main->focused_hwnd);
   if (focused == nullptr || focused->IsFullscreen() != FALSE ||
       focused->IsUnmanagedFullscreen() != FALSE)
      return;

   /* Cache: skip when the focus didn't cross layers (the static cache is
    * safe because this always runs on the main thread). */
   static HWND s_last_hwnd = nullptr;
   static bool s_last_floating = false;
   bool const now_floating = (focused->IsFloating() != FALSE);
   if (main->focused_hwnd == s_last_hwnd && now_floating == s_last_floating)
      return;
   s_last_hwnd = main->focused_hwnd;
   s_last_floating = now_floating;

   Workspace *workspace = FindWorkspaceByHwnd(shared, main, main->focused_hwnd);
   if (workspace == nullptr)
      return;

   /* Raise every sibling in the focused layer above the other layer.
    * Inserting each peer after (below) the focused window keeps the
    * focused window at the top while bringing all its layer peers
    * above the opposite layer. */
   RECT const ignore = {};
   for (Window *win : workspace->Windows()) {
      if (win == focused || win->IsFullscreen() != FALSE ||
          win->IsUnmanagedFullscreen() != FALSE)
         continue;
      if ((win->IsFloating() != FALSE) == now_floating) {
         BFWMSetWindowPosEx(win->GetHwnd(), focused->GetHwnd(), &ignore,
                            SWP_NOMOVE | SWP_NOSIZE);
      }
   }
}
