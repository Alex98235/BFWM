#include "workspace.h"
#include "../bar/bar.h"
#include "../config/lua/parser.h"
#include "../core/bfwm_context.h"
#include "../core/sync.h"
#include "../dpi/dpi.h"
#include "../logging/logger.h"
#include "../math/rect.h"
#include "../monitor/monitor.h"
#include "../transaction/transaction.h"
#include "../win/win_utils.h"
#include "../window/border/border_manager.h"
#include "../window/border/overlay.h"
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

auto FindWorkspaceConfig(struct BFWMContext *ctx, size_t workspace_id) -> const
    struct WorkspaceConfig * {
   if (ctx == nullptr)
      return nullptr;
   for (int i = 0; i < ctx->config.workspace_config_count; i++) {
      if (ctx->config.workspace_configs[i].id == workspace_id)
         return &ctx->config.workspace_configs[i];
   }
   return nullptr;
}

// ---------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------

namespace {

inline void LowerFullscreenWindows(HWND hwnd, Workspace *hwnd_workspace,
                                   struct BFWMContext *ctx) {
   Window *target = ctx->windows->FindByHwnd(hwnd);
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
   Monitor *bar_mon = FindMonitorByWorkspace(ctx, hwnd_workspace);
   if ((bar_mon != nullptr) && (bar_mon->GetBar() != nullptr))
      bar_mon->GetBar()->EnsureTopZ();
}

// ── Fullscreen transition helpers ──

inline void LowerPreviousFullscreen(HWND old_hwnd, BOOL old_valid,
                                    struct BFWMContext *ctx) {
   if (old_valid == FALSE)
      return;

   Window *old_win = ctx->windows->FindByHwnd(old_hwnd);
   if ((old_win == nullptr) || (old_win->IsFullscreen() == FALSE))
      return;

   Workspace *old_workspace = FindWorkspaceByHwnd(ctx, old_hwnd);
   BFWMSetWindowZ(old_hwnd, BFWM_Z_NOTOPMOST);
   BFWMSetWindowZ(old_hwnd, BFWM_Z_BOTTOM);
   if (old_workspace == nullptr)
      return;

   Monitor *old_mon = FindMonitorByWorkspace(ctx, old_workspace);
   if (old_mon == nullptr)
      return;

   if (old_mon->GetFullscreenCount() > 0)
      old_mon->DecrementFullscreenCount();
   if (old_mon->GetFullscreenCount() == 0 && (old_mon->GetBar() != nullptr))
      old_mon->GetBar()->Show();
}

inline void RaiseNewFullscreen(HWND hwnd, struct BFWMContext *ctx) {
   Window *new_win = ctx->windows->FindByHwnd(hwnd);
   if ((new_win == nullptr) || (new_win->IsFullscreen() == 0))
      return;

   BFWMSetWindowZ(hwnd, BFWM_Z_TOPMOST);
   Workspace *new_workspace = FindWorkspaceByHwnd(ctx, hwnd);
   if (new_workspace == nullptr)
      return;

   Monitor *new_mon = FindMonitorByWorkspace(ctx, new_workspace);
   if (new_mon == nullptr)
      return;

   new_mon->IncrementFullscreenCount();
   if (new_mon->GetFullscreenCount() == 1 && (new_mon->GetBar() != nullptr))
      new_mon->GetBar()->Hide();
}

inline void HandleMouseFollowsFocus(HWND hwnd, struct BFWMContext *ctx) {
   if (ctx->config.mouse_follows_focus == 0)
      return;

   RECT r;
   if (GetWindowRect(hwnd, &r) != 0)
      SetCursorPos((r.left + r.right) / 2, (r.top + r.bottom) / 2);
}

inline void CloakInactiveWorkspaces(Workspace *workspace,
                                    struct BFWMContext *ctx, Monitor *mon) {
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
         if (ctx->transaction.IsInTransaction() == TRUE) {
            ctx->transaction.QueueCloak(h, TRUE);
         } else {
            HRESULT const result = SetWindowCloakState(h, TRUE);
            if (FAILED(result)) {
               Warn("CloakInactiveWorkspaces: cloak HWND %p failed "
                    "(result=0x%08lx)",
                    h, (unsigned long)result);
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
 * Keeps the historical ctx->applying_layout guard around the border-color
 * queueing. Focused window is skipped.
 */
inline void SetInactiveWindowBorders(Workspace *workspace,
                                     struct BFWMContext *ctx) {
   ctx->applying_layout = TRUE;
   for (const auto &window : workspace->Windows()) {
      HWND h = window->GetHwnd();
      if ((IsWindowVisible(h) == FALSE) || h == ctx->focused_hwnd)
         continue;
      ctx->transaction.QueueBorderColor(h, ctx->config.inactive_border);
   }
   ctx->applying_layout = FALSE;
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

auto CreateWorkspaceForMonitor(BFWMContext *ctx, size_t target_id,
                               const std::wstring *name, Monitor *mon)
    -> Workspace * {
   RECT ws_rect = mon->GetWorkArea();
   /* bar_cfg margins are logical; scale them to the monitor's physical
    * pixels. GetBarHeight() is already physical. ScaleForMonitor guards a
    * DPI of zero (registry miss) by falling back to identity scaling. */
   ws_rect.top += ctx->dpi->ScaleForMonitor(mon->GetHandle(),
                                            ctx->config.bar_cfg.margin.top) +
                  mon->GetBarHeight() +
                  ctx->dpi->ScaleForMonitor(mon->GetHandle(),
                                            ctx->config.bar_cfg.margin.bottom);
   ScaledLayoutConfig const scaled =
       ScaleLayoutConfigForMonitor(ctx, mon->GetHandle());
   return new Workspace(target_id, name->c_str(), ctx->config.default_layout,
                        ws_rect, scaled.gap_between, scaled.gap_edge,
                        scaled.border_width);
}

auto ScaleLayoutConfigForMonitor(struct BFWMContext *ctx, HMONITOR hmon)
    -> ScaledLayoutConfig {
   if (ctx == nullptr)
      return {};
   ScaledLayoutConfig out = {
       .gap_between = ctx->config.gap_between,
       .gap_edge = ctx->config.gap_edge,
       .border_width = ctx->config.border_width,
   };
   if (ctx->dpi != nullptr) {
      out.gap_between = ctx->dpi->ScaleForMonitor(hmon, out.gap_between);
      out.gap_edge = ctx->dpi->ScaleForMonitor(hmon, out.gap_edge);
      out.border_width = ctx->dpi->ScaleForMonitor(hmon, out.border_width);
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

void Workspace::ApplyConfig(struct BFWMContext *ctx, size_t identifier) {
   const struct WorkspaceConfig *workspace_config =
       FindWorkspaceConfig(ctx, identifier);
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

void Workspace::DestroyWindows(struct BFWMContext *ctx) {
   if (ctx == nullptr)
      return;

   for (auto *win : windows_) {
      if (win != nullptr) {
         ctx->windows->Remove(win);
      }
   }
   windows_.clear();
}

void Workspace::AddWindow(struct BFWMContext *ctx, Window *window) {

   // Minimized windows should not enter the tree — they get re-added on
   // restore via EVENT_OBJECT_SHOW.
   if (IsIconic(window->GetHwnd()) == TRUE)
      return;

   // Only insert managed windows into the layout tree.  Floating windows
   // (e.g. fullscreen windows detected at registration time) are tracked by
   // the workspace but excluded from tree-based positioning.
   if (IsWindowManagedByLayout(window) == TRUE) {
      if ((engine_ != nullptr) &&
          engine_->insert(window->GetHwnd(), ctx->focused_hwnd))
         windows_.push_back(window);
   } else {
      windows_.push_back(window);
      if (window->IsFloating() == TRUE) {
         Monitor *mon = FindMonitorByWorkspace(ctx, this);
         if (mon != nullptr) {
            UINT const dpi = (ctx != nullptr && ctx->dpi != nullptr)
                                 ? ctx->dpi->GetDpi(mon->GetHandle())
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
   FocusWindow(window->GetHwnd(), ctx);
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

void UpdateFocusTracking(HWND hwnd, struct BFWMContext *ctx) {
   if (IsWindow(hwnd) == FALSE)
      return;

   // Update focus tracking
   ctx->focused_hwnd = hwnd;

   // Update focused_workspace: find which workspace owns this hwnd
   Workspace *hwnd_workspace = nullptr;
   auto const &monitors = ctx->monitors->Monitors();
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
      BarUpdateRequest(ctx, BAR_UPDATE_DIRTY);
      return;
   }

   ctx->focused_workspace = hwnd_workspace;

   // Keep the layout's notion of the active window in sync so layouts like
   // monocle can report the currently-shown window from get_closest_window.
   if (hwnd_workspace->GetEngine() != nullptr) {
      hwnd_workspace->GetEngine()->set_active_window(hwnd);
   }

   // Defer layout to commit time (self-wraps if not in a transaction).
   hwnd_workspace->ApplyLayout(ctx);

   // When focusing a non-fullscreen window, lower any fullscreen
   // windows on the workspace and raise all managed windows above
   // them so the entire layout is visible.
   LowerFullscreenWindows(hwnd, hwnd_workspace, ctx);

   BarUpdateRequest(ctx, BAR_UPDATE_DIRTY);
}

void WorkspaceHandleDesktopClick(struct BFWMContext *ctx, POINT point) {
   HWND clicked = WindowFromPoint(point);
   if (clicked != nullptr)
      clicked = GetAncestor(clicked, GA_ROOT);

   // Only the desktop and taskbar count as a desktop click. Any real
   // window — managed or unmanaged — must return early: activating the
   // workspace would raise its windows above the clicked window and bury
   // unmanaged ones.
   if ((clicked != nullptr) && !IsShellSurface(clicked))
      return;

   Monitor *mon = FindMonitorByPoint(ctx, point);
   if ((mon == nullptr) || (mon->GetActiveWorkspace() == nullptr) ||
       mon->GetActiveWorkspace() == ctx->focused_workspace)
      return;

   ctx->focused_workspace = mon->GetActiveWorkspace();

   Workspace *workspace = mon->GetActiveWorkspace();
   HWND first = nullptr;
   if (workspace->GetEngine() != nullptr) {
      first = workspace->GetEngine()->get_closest_window(nullptr);
   }

   if (first != nullptr) {
      FocusWindow(first, ctx);
   } else {
      ctx->focused_hwnd = nullptr;
   }

   BarUpdateRequest(ctx, BAR_UPDATE_DIRTY);
}

void WorkspaceHandleMouseHover(struct BFWMContext *ctx, POINT point) {
   HWND hover = WindowFromPoint(point);
   if (hover != nullptr)
      hover = GetAncestor(hover, GA_ROOT);

   if ((hover != nullptr) && hover != ctx->focused_hwnd &&
       (ctx->windows->FindByHwnd(hover) != nullptr)) {
      Workspace *workspace = FindWorkspaceByHwnd(ctx, hover);
      if (workspace != nullptr) {
         Monitor *mon = FindMonitorByWorkspace(ctx, workspace);
         if ((mon == nullptr) || workspace == mon->GetActiveWorkspace()) {
            FocusWindow(hover, ctx);
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

void FocusWindow(HWND hwnd, struct BFWMContext *ctx) {
   if (IsWindow(hwnd) == FALSE)
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
   if (ctx->transaction.IsInTransaction() == FALSE) {
      ctx->transaction.Begin();
      wrapped = TRUE;
   }

   // Focus must always shift to the workspace owning this window: cloak the
   // previously active workspace, uncloak this one, apply layout. No-op when
   // the workspace is already active. Runs inside the transaction so the
   // cloak/relayout ops join the same commit as the focus op below.
   WorkspaceActivateForWindow(hwnd, ctx);

   HWND old_focus = ctx->focused_hwnd;

   ctx->transaction.QueueFocus(hwnd);

   if ((old_focus != nullptr) && old_focus != hwnd &&
       (IsWindowVisible(old_focus) == TRUE)) {
      ctx->transaction.QueueBorderColor(old_focus, ctx->config.inactive_border);
   }

   // The new focus always gets the active border. Queued here (not only via
   // FocusWindowImmediate at OP_FOCUS_WINDOW apply): the implicit-focus paths
   // (workspace activation, close-refocus, hover-focus) can hit
   // FocusWindowImmediate's already-foregrounded early-return, which would
   // otherwise skip the active color entirely.
   if (IsWindowVisible(hwnd) == TRUE) {
      ctx->transaction.QueueBorderColor(hwnd, ctx->config.border_color);
   }

   // Queue relayout
   Workspace *hwnd_workspace = FindWorkspaceByHwnd(ctx, hwnd);
   if (hwnd_workspace != nullptr) {
      ctx->transaction.QueueRelayout(hwnd_workspace);
   }

   // Update the context
   ctx->focused_hwnd = hwnd;
   if (hwnd_workspace != nullptr) {
      ctx->focused_workspace = hwnd_workspace;
      BarUpdateRequest(ctx, BAR_UPDATE_DIRTY);
   }

   if (wrapped == TRUE)
      BFWMTransactionCommit(ctx);
}

auto FocusWindowImmediate(HWND hwnd, struct BFWMContext *ctx) -> BOOL {
   if (IsWindow(hwnd) == FALSE)
      return FALSE;

   // Same guard as FocusWindow(): this entry point is reachable without the
   // wrapper (OP_FOCUS_WINDOW queued directly, e.g. the focus-neighbor
   // action). Returns FALSE so the caller skips the focused_hwnd update.
   if (ForegroundIsFullscreen(hwnd) == TRUE)
      return FALSE;

   // Don't do anything if already focused
   if (GetForegroundWindow() == hwnd && ctx->focused_hwnd == hwnd)
      return TRUE;

   // Capture old focus at entry — caller (OP_FOCUS_WINDOW apply in
   // transaction.c) sets ctx->focused_hwnd AFTER we return.
   HWND old_hwnd = ctx->focused_hwnd;
   BOOL const old_valid =
       static_cast<BOOL>((old_hwnd != nullptr) && old_hwnd != hwnd &&
                         (IsWindowVisible(old_hwnd) == TRUE));

   // Queue border changes; Pass 10 applies them so focus-queued colors
   // win over relayout-queued inactives from Pass 6.
   if (old_valid == TRUE)
      BorderManagerSetInactive(ctx, old_hwnd);

   if (IsWindowVisible(hwnd) == TRUE)
      BorderManagerSetActive(ctx, hwnd);

   // Lower the previous fullscreen window (z-order change, not DWM)
   LowerPreviousFullscreen(old_hwnd, old_valid, ctx);

   // Single arm point for every "focus a specific window" path: the intent
   // suppresses stray Windows-supplied foregrounds until this target lands.
   ctx->focus_intent.ArmKnown(hwnd);

   FocusWindowReliable(hwnd);
   UpdateFocusTracking(hwnd, ctx);

   // Raise the new window to topmost if it's fullscreen
   RaiseNewFullscreen(hwnd, ctx);

   // Single flush + targeted redraws for both windows whose borders changed
   DwmFlush();
   if (old_valid == TRUE)
      BorderManagerRedraw(ctx, old_hwnd);
   if (IsWindowVisible(hwnd) == TRUE)
      BorderManagerRedraw(ctx, hwnd);

   HandleMouseFollowsFocus(hwnd, ctx);

   return TRUE;
}

auto FindWorkspaceByHwnd(struct BFWMContext *ctx, HWND hwnd) -> Workspace * {
   if (ctx == nullptr)
      return nullptr;

   // Fast path: check the focused workspace first.
   // Verify focused_workspace still exists (it may have been freed by
   // ReconcileMonitors between Pass 2 and the focused_workspace update).
   if ((ctx->focused_workspace != nullptr) &&
       (FindMonitorByWorkspace(ctx, ctx->focused_workspace) != nullptr) &&
       ctx->focused_workspace->ContainsWindow(hwnd)) {
      return ctx->focused_workspace;
   }

   // Fallback: scan all monitors and their workspaces
   auto const &monitors = ctx->monitors->Monitors();
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

void Workspace::ApplyLayout(struct BFWMContext *ctx) {
   // Self-wrap: only wrap when not already inside a transaction.
   BOOL wrapped = FALSE;
   if (ctx->transaction.IsInTransaction() == FALSE) {
      ctx->transaction.Begin();
      wrapped = TRUE;
   }

   Monitor *mon = FindMonitorByWorkspace(ctx, this);
   CloakInactiveWorkspaces(this, ctx, mon);

   if (windows_.empty()) {
      if (wrapped == TRUE)
         BFWMTransactionCommit(ctx);
      return;
   }

   // Moves pass then inactive-border pass, in exactly this order.
   ctx->applying_layout = TRUE;
   PlacementApply(this, ctx);
   SetInactiveWindowBorders(this, ctx);

   if (wrapped == TRUE)
      BFWMTransactionCommit(ctx);
}

auto ResolveTargetWorkspace(struct BFWMContext *ctx, HWND hwnd) -> Workspace * {
   if ((ctx == nullptr) || (hwnd == nullptr) || (IsWindow(hwnd) == FALSE))
      return (ctx != nullptr) ? ctx->focused_workspace : nullptr;

   // Default to focused workspace as fallback
   Workspace *target_workspace = ctx->focused_workspace;
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
   for (auto *mon : ctx->monitors->Monitors()) {
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
          {{.x = win_rect.left, .y = win_rect.top},
           {.x = win_rect.right, .y = win_rect.top},
           {.x = win_rect.left, .y = win_rect.bottom},
           {.x = win_rect.right, .y = win_rect.bottom}}};

      for (int i = 0; i < 4; i++) {
         for (auto *mon : ctx->monitors->Monitors()) {
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

auto FindMonitorByPoint(struct BFWMContext *ctx, POINT point) -> Monitor * {
   auto const &monitors = ctx->monitors->Monitors();
   size_t const monitor_count = monitors.size();

   for (size_t i = 0; i < monitor_count; i++) {
      if (PointIntersectsRect(point, monitors[i]->GetRect()) == TRUE)
         return monitors[i];
   }

   return nullptr;
}

auto FindMonitorByWorkspace(struct BFWMContext *ctx, Workspace *workspace)
    -> Monitor * {
   if ((ctx == nullptr) || (workspace == nullptr))
      return nullptr;
   for (auto *mon : ctx->monitors->Monitors()) {
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

auto FindWorkspaceById(struct BFWMContext *ctx, size_t identifier)
    -> Workspace * {
   if (ctx == nullptr)
      return nullptr;
   for (auto *mon : ctx->monitors->Monitors()) {
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

void WorkspaceActivateSimple(struct BFWMContext *ctx, Monitor *mon,
                             Workspace *old_workspace, Workspace *new_workspace,
                             bool destroy_empty_old_ws) {
   if ((ctx == nullptr) || (mon == nullptr) || (old_workspace == nullptr) ||
       (new_workspace == nullptr))
      return;

   BOOL const in_tx = ctx->transaction.IsInTransaction();

   // Serialize the cloak bookkeeping against the event thread's window
   // add/remove mutations. The lock is recursive, so nesting with the
   // transaction commit (which also takes ctx->lock) is safe. ApplyLayout
   // must run OUTSIDE the lock (see events.cpp HandleWindowReRegistered).
   {
      ScopedLock const lock(ctx->lock);
      for (auto *win : old_workspace->Windows()) {
         HWND h = win->GetHwnd();
         if (in_tx == TRUE) {
            ctx->transaction.QueueCloak(h, TRUE);
         } else {
            HRESULT const result = SetWindowCloakState(h, TRUE);
            if (FAILED(result)) {
               Error("WorkspaceActivateSimple: cloak HWND %p failed "
                     "(result=0x%08lx)",
                     h, (unsigned long)result);
            } else {
               win->SetCloaked(TRUE);
            }
         }
      }
      for (auto *win : new_workspace->Windows()) {
         HWND h = win->GetHwnd();
         if (in_tx == TRUE) {
            ctx->transaction.QueueCloak(h, FALSE);
         } else {
            HRESULT const result = SetWindowCloakState(h, FALSE);
            if (FAILED(result)) {
               Error("WorkspaceActivateSimple: uncloak HWND %p failed "
                     "(result=0x%08lx)",
                     h, (unsigned long)result);
            } else {
               win->SetCloaked(FALSE);
            }
         }
      }
   }

   mon->SetActiveWorkspace(new_workspace);
   ctx->focused_workspace = new_workspace;

   new_workspace->ApplyLayout(ctx);
   if (destroy_empty_old_ws)
      WorkspaceDestroyIfEmpty(mon, old_workspace);

   BarUpdateRequest(ctx, BAR_UPDATE_DIRTY);

   // When called outside a transaction, force DWM to re-composite
   // immediately so the cloaking changes take effect.
   if (in_tx == FALSE) {
      DwmFlush();
      InvalidateRect(nullptr, nullptr, TRUE);
   }
}

void WorkspaceActivateForWindow(HWND hwnd, struct BFWMContext *ctx) {
   if ((ctx == nullptr) || (IsWindow(hwnd) == FALSE))
      return;

   Workspace *hwnd_ws = FindWorkspaceByHwnd(ctx, hwnd);
   if (hwnd_ws == nullptr)
      return;

   Monitor *mon = FindMonitorByWorkspace(ctx, hwnd_ws);
   if ((mon == nullptr) || hwnd_ws == mon->GetActiveWorkspace())
      return;

   WorkspaceActivateSimple(ctx, mon, mon->GetActiveWorkspace(), hwnd_ws, false);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto WorkspaceActivate(struct BFWMContext *ctx, Monitor *mon, size_t target_id)
    -> bool {
   if ((ctx == nullptr) || (mon == nullptr))
      return false;

   Workspace *target = FindWorkspaceOnMonitor(mon, target_id);

   if (target == nullptr) {
      std::wstring const name = L"ws_" + std::to_wstring(target_id);
      RECT ws_rect = mon->GetWorkArea();
      ws_rect.top += mon->GetBarHeight();
      ScaledLayoutConfig const scaled =
          ScaleLayoutConfigForMonitor(ctx, mon->GetHandle());
      target = new Workspace(
          target_id, name.c_str(), ctx->config.default_layout, ws_rect,
          scaled.gap_between, scaled.gap_edge, scaled.border_width);
      if (target == nullptr)
         return false;
      target->ApplyConfig(ctx, target_id);
      mon->TrackWorkspace(target);
      RECT const mon_rect = mon->GetRect();
      InfoW(L"Created workspace %zu on monitor at (%ld,%ld)", target_id,
            mon_rect.left, mon_rect.top);
   }

   // Self-wrap: only wrap when not already inside a transaction —
   // a nested Commit would flush the outer batch early.
   BOOL wrapped = FALSE;
   if (ctx->transaction.IsInTransaction() == FALSE) {
      ctx->transaction.Begin();
      wrapped = TRUE;
   }

   if (target == mon->GetActiveWorkspace()) {
      // Already active — only a no-op if focus is already here.
      HWND focus_hwnd = ctx->focused_hwnd;
      if ((focus_hwnd != nullptr) && target->ContainsWindow(focus_hwnd) &&
          (IsWindowVisible(focus_hwnd) == TRUE)) {
         if (wrapped == TRUE)
            BFWMTransactionCommit(ctx);
         return true;
      }
      // Focus is elsewhere — move it to the target workspace.
      if (target->GetEngine() != nullptr) {
         focus_hwnd = target->GetEngine()->get_closest_window(nullptr);
      } else {
         focus_hwnd = nullptr;
      }
      if (focus_hwnd != nullptr) {
         FocusWindow(focus_hwnd, ctx);
      } else {
         ctx->focused_workspace = target;
         if ((ctx->focused_hwnd != nullptr) &&
             IsWindowVisible(ctx->focused_hwnd) == TRUE) {
            BorderManagerSetInactive(ctx, ctx->focused_hwnd);
            BorderManagerRedraw(ctx, ctx->focused_hwnd);
         }
         ctx->focused_hwnd = nullptr;
         BarUpdateRequest(ctx, BAR_UPDATE_DIRTY);
      }
      if (wrapped == TRUE)
         BFWMTransactionCommit(ctx);
      return true;
   }

   Workspace *old = mon->GetActiveWorkspace();

   WorkspaceActivateSimple(ctx, mon, old, target, true);

   HWND focus_hwnd = ctx->focused_hwnd;
   if (!target->ContainsWindow(focus_hwnd) ||
       (IsWindowVisible(focus_hwnd) == FALSE)) {
      if (target->GetEngine() != nullptr) {
         focus_hwnd = target->GetEngine()->get_closest_window(nullptr);
      } else {
         focus_hwnd = nullptr;
      }
   }
   if (focus_hwnd != nullptr) {
      FocusWindow(focus_hwnd, ctx);
   } else {
      if ((ctx->focused_hwnd != nullptr) &&
          IsWindowVisible(ctx->focused_hwnd) == TRUE) {
         BorderManagerSetInactive(ctx, ctx->focused_hwnd);
      }
      ctx->focused_hwnd = nullptr;
   }

   if (wrapped == TRUE)
      BFWMTransactionCommit(ctx);

   return true;
}

void RestoreDesktopState(struct BFWMContext *ctx) {
   if (ctx == nullptr)
      return;

   // Self-wrap: batch the border resets in a transaction, then flush
   // DWM once afterwards.
   BOOL wrapped = FALSE;
   if (ctx->transaction.IsInTransaction() == 0) {
      ctx->transaction.Begin();
      wrapped = TRUE;
   }

   // Reset borders to inactive and uncloak every managed window.
   for (size_t m = 0; m < ctx->monitors->Size(); m++) {
      Monitor *mon = ctx->monitors->At(m);
      for (auto *workspace : mon->Workspaces()) {
         for (auto *win : workspace->Windows()) {
            HWND h = win->GetHwnd();
            if (IsWindowVisible(h) == TRUE)
               BorderManagerSetInactive(ctx, h);
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
      BFWMTransactionCommit(ctx);

   // Force DWM to re-composite with the restored state.
   DwmFlush();
   InvalidateRect(nullptr, nullptr, TRUE);
}

void PushGapConfigToAllWorkspaces(struct BFWMContext *ctx) {
   if (ctx == nullptr)
      return;
   for (size_t m = 0; m < ctx->monitors->Size(); m++) {
      Monitor *mon = ctx->monitors->At(m);
      /* Scale the logical gap/border config to this monitor's physical
       * pixels; the gaps_enabled toggle zeroes the gaps before scaling. */
      ScaledLayoutConfig const scaled =
          ScaleLayoutConfigForMonitor(ctx, mon->GetHandle());
      int const eff_between =
          (ctx->config.gaps_enabled != 0) ? scaled.gap_between : 0;
      int const eff_edge =
          (ctx->config.gaps_enabled != 0) ? scaled.gap_edge : 0;
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
            workspace->ApplyLayout(ctx);
      }
   }
}

void Workspace::RecalculateRect(Monitor *mon, struct BFWMContext *ctx) {
   if ((mon == nullptr) || (ctx == nullptr))
      return;
   RECT new_rect = mon->GetWorkArea();
   /* bar_cfg margins are logical config values; scale them to the monitor's
    * physical pixels. mon->GetBarHeight() is already physical. */
   UINT const dpi = (ctx->dpi != nullptr) ? ctx->dpi->GetDpi(mon->GetHandle())
                                          : DpiSystem::BaseDpi();
   new_rect.top += DpiSystem::Scale(ctx->config.bar_cfg.margin.top, dpi) +
                   mon->GetBarHeight() +
                   DpiSystem::Scale(ctx->config.bar_cfg.margin.bottom, dpi);
   workspace_rect_ = new_rect;
   ScaledLayoutConfig const scaled =
       ScaleLayoutConfigForMonitor(ctx, mon->GetHandle());
   /* Converge the cached per-monitor-scaled layout values so a later
    * ApplyConfig recreates the engine from this monitor's DPI even after
    * MoveToMonitor reparents the workspace without recreation. */
   gap_between_ = scaled.gap_between;
   gap_edge_ = scaled.gap_edge;
   border_width_ = scaled.border_width;
   int const eff_between =
       (ctx->config.gaps_enabled != 0) ? scaled.gap_between : 0;
   int const eff_edge = (ctx->config.gaps_enabled != 0) ? scaled.gap_edge : 0;
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

void RecalculateAllWorkspaceRects(struct BFWMContext *ctx) {
   if (ctx == nullptr)
      return;
   for (size_t m = 0; m < ctx->monitors->Size(); m++) {
      Monitor *mon = ctx->monitors->At(m);
      for (size_t j = 0; j < mon->Workspaces().size(); j++) {
         mon->Workspaces()[j]->RecalculateRect(mon, ctx);
      }
   }
}

auto FindNextWorkspaceId(struct BFWMContext *ctx) -> size_t {
   size_t identifier = 1;
   while (FindWorkspaceById(ctx, identifier) != nullptr)
      identifier++;
   return identifier;
}

void Workspace::MoveToMonitor(Monitor *target_mon, struct BFWMContext *ctx) {
   if ((target_mon == nullptr) || (ctx == nullptr))
      return;

   Monitor *src_mon = FindMonitorByWorkspace(ctx, this);
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

   RecalculateRect(target_mon, ctx);
}

auto FindUnassignedWorkspaceId(struct BFWMContext *ctx, Monitor *mon)
    -> size_t {
   for (int i = 0; i < ctx->config.workspace_config_count; i++) {
      const struct WorkspaceConfig *workspace_config =
          &ctx->config.workspace_configs[i];
      if (std::cmp_equal(workspace_config->assigned_monitor,
                         mon->GetDisplayNumber()) &&
          (FindWorkspaceById(ctx, workspace_config->id) == nullptr)) {
         return workspace_config->id;
      }
   }

   size_t identifier = 1;
   for (;;) {
      if (FindWorkspaceById(ctx, identifier) != nullptr) {
         identifier++;
         continue;
      }
      const struct WorkspaceConfig *workspace_config =
          FindWorkspaceConfig(ctx, identifier);
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
