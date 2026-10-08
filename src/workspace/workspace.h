/**
 * @file workspace.h
 * @brief Workspace management.
 *
 * Defines the workspace abstraction used to group windows into virtual
 * desktops.  Layout-specific logic is handled via the LayoutEngine
 * VTable interface (see layout_api.h).
 */

#ifndef BFWM_WORKSPACE_H
#define BFWM_WORKSPACE_H

#include "../window/window.h"
#include "layouts/layout_api.h"
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>

class Monitor;
struct BFWMContext;
struct WorkspaceConfig;

class Workspace {
 public:
   Workspace() = default;
   Workspace(size_t identifier, const wchar_t *name, LayoutType layout,
             RECT rect, int gap_between, int gap_edge, int border_width);
   ~Workspace();

   /// Unique workspace identifier
   [[nodiscard]] auto GetIdentifier() const -> size_t { return identifier_; }
   /// Display name
   [[nodiscard]] auto GetName() const -> std::wstring const & { return name_; }
   /// User-defined label from Lua config (empty if unset)
   [[nodiscard]] auto GetLabel() const -> std::string const & { return label_; }
   void SetLabel(std::string label) { label_ = std::move(label); }

   /// Active layout engine (null when the workspace has no layout)
   [[nodiscard]] auto GetEngine() const -> LayoutEngine * {
      return engine_.get();
   }
   void SetEngine(std::unique_ptr<LayoutEngine> engine) {
      engine_ = std::move(engine);
   }

   /// TRUE while this workspace is the monitor's active workspace
   [[nodiscard]] auto IsActive() const -> BOOL { return is_active_; }
   void SetActive(BOOL value) { is_active_ = value; }

   /// UID of the monitor this workspace was originally created on. Set during
   /// fallback migration in HandleDisconnectedMonitors so SetupNewMonitors can
   /// move the workspace back on reconnection.
   [[nodiscard]] auto GetOriginMonitorUid() const -> std::wstring const & {
      return origin_monitor_uid_;
   }
   void SetOriginMonitorUid(std::wstring uid) {
      origin_monitor_uid_ = std::move(uid);
   }
   void ClearOriginMonitorUid() { origin_monitor_uid_.clear(); }

   /// Raw workspace area (before edge-gap inset)
   [[nodiscard]] auto GetWorkspaceRect() const -> RECT {
      return workspace_rect_;
   }
   void SetWorkspaceRect(RECT value) { workspace_rect_ = value; }

   /// Usable tiling area: the workspace rect inset by the effective edge gap
   /// plus the border strip on every side. This is the outermost frame a tiled
   /// window can occupy (it matches the tiled_rect each engine derives), so the
   /// placement failure path clamps adopted rects to it — keeping them
   /// representable instead of asking the engine for an unreachable rect.
   /// Recomputed in RecalculateRect.
   [[nodiscard]] auto GetUsableRect() const -> RECT { return usable_rect_; }

   /// Managed windows in this workspace (read-only iteration)
   [[nodiscard]] auto Windows() const -> std::vector<Window *> const & {
      return windows_;
   }
   [[nodiscard]] auto WindowCount() const -> size_t { return windows_.size(); }
   [[nodiscard]] auto IsEmpty() const -> bool { return windows_.empty(); }
   /// Check if this workspace contains a window with the given HWND
   [[nodiscard]] auto ContainsWindow(HWND hwnd) const -> bool;
   /// Low-level membership append (used by the events pipeline after it has
   /// already inserted the window into the layout engine itself)
   void TrackWindow(Window *window) { windows_.push_back(window); }
   void ClearWindows() { windows_.clear(); }

   /// Apply label/layout from the matching WorkspaceConfig, if any
   void ApplyConfig(struct BFWMContext *ctx, size_t identifier);
   /// Add a window to the workspace (layout-tree insert, tracking, focus)
   void AddWindow(struct BFWMContext *ctx, Window *window);
   /// Remove a window from the workspace (tracking + layout-tree removal)
   void RemoveWindow(Window *window);
   /// Recalculate and apply the layout, then set border colours
   void ApplyLayout(struct BFWMContext *ctx);
   /// Remove every managed window from the registry and clear the list
   void DestroyWindows(struct BFWMContext *ctx);
   /// Recompute this workspace's rect from its monitor's geometry
   void RecalculateRect(Monitor *mon, struct BFWMContext *ctx);
   /// Move this workspace from its current monitor to another
   void MoveToMonitor(Monitor *target_mon, struct BFWMContext *ctx);

   /// Factory: create a layout engine for the given type, or nullptr
   static auto CreateLayoutEngine(LayoutType type, RECT workspace_rect,
                                  int gap_between, int gap_edge,
                                  int border_width)
       -> std::unique_ptr<LayoutEngine>;

 private:
   size_t identifier_ = 0;
   std::wstring name_;
   std::string label_;
   std::unique_ptr<LayoutEngine> engine_;
   BOOL is_active_ = FALSE;
   std::wstring origin_monitor_uid_;
   RECT workspace_rect_{};
   std::vector<Window *> windows_;
   /// Scaled (physical-pixel) gap values from the creating monitor's DPI;
   /// reused by ApplyConfig when it recreates the engine for a config layout.
   int gap_between_ = 0;
   int gap_edge_ = 0;
   int border_width_ = 0;
   /// Workspace rect inset by (effective edge gap + border width): the frame
   /// bound for placement/adoption. See GetUsableRect.
   RECT usable_rect_{};
};

/**
 * @brief Per-monitor layout values, scaled from logical config to physical
 *        pixels for a monitor's effective DPI.
 *
 * Layout engines stay DPI-pure: every consumer scales the logical
 * gap_between / gap_edge / border_width config once, for the target monitor,
 * and passes these physical values into Workspace / CreateLayoutEngine.
 */
struct ScaledLayoutConfig {
   int gap_between;
   int gap_edge;
   int border_width;
};

auto CreateWorkspaceForMonitor(BFWMContext *ctx, size_t target_id,
                               const std::wstring *name, Monitor *mon)
    -> Workspace *;

/**
 * @brief Scale the logical gap/border config for a monitor's effective DPI.
 *
 * @param ctx  The BFWM context (ctx->dpi must be initialized)
 * @param hmon The monitor handle to scale for
 * @return Physical-pixel gap_between / gap_edge / border_width for @p hmon
 */
auto ScaleLayoutConfigForMonitor(struct BFWMContext *ctx, HMONITOR hmon)
    -> ScaledLayoutConfig;

/**
 * @brief Find which workspace contains the given HWND.
 *
 * Searches all monitors and their workspaces for a window with the
 * given handle.  Intended to avoid duplicating this scan in multiple
 * event handlers.
 *
 * @param ctx  The BFWM context (must be non-NULL)
 * @param hwnd The window handle to look for
 * @return Workspace* Pointer to the workspace, or NULL if not found
 */
auto FindWorkspaceByHwnd(struct BFWMContext *ctx, HWND hwnd) -> Workspace *;

/**
 * @brief Public API for focusing a window - checks transaction state.
 *
 * If a transaction is active, queues focus operation.
 * Otherwise, applies focus immediately.
 *
 * @param hwnd The window to focus
 * @param ctx The BFWM context
 */
void FocusWindow(HWND hwnd, struct BFWMContext *ctx);

/**
 * @brief Immediately apply focus without transaction checking.
 *
 * Used by OP_FOCUS_WINDOW during transaction commit.
 * Always applies focus immediately, regardless of transaction state.
 *
 * @param hwnd The window to focus
 * @param ctx The BFWM context
 */
auto FocusWindowImmediate(HWND hwnd, struct BFWMContext *ctx) -> BOOL;

/**
 * @brief Update focus tracking, workspace, and border colours without
 *        calling SetForegroundWindow.
 *
 * @param hwnd The HWND that now has foreground focus
 * @param ctx  The BFWM context (updated immediately)
 */
void UpdateFocusTracking(HWND hwnd, struct BFWMContext *ctx);

/**
 * @brief Handle a desktop click (from the mouse hook via WM_APP_DESKTOP_CLICK).
 *
 * Focuses the closest window of the clicked monitor's active workspace when
 * the click landed on the desktop (no managed window under the cursor) and the
 * workspace is not already focused. Lives here so input/hooks.c does not need
 * to depend on workspace.h.
 *
 * @param ctx   The BFWM context
 * @param point The cursor position at click time
 */
void WorkspaceHandleDesktopClick(struct BFWMContext *ctx, POINT point);

/**
 * @brief Handle focus-follows-mouse hover (from the mouse hook via
 *        WM_APP_MOUSE_HOVER).
 *
 * Focuses a managed window under the cursor when its workspace is the active
 * one. Lives here so input/hooks.c does not need to depend on workspace.h.
 *
 * @param ctx   The BFWM context
 * @param point The cursor position at hover time
 */
void WorkspaceHandleMouseHover(struct BFWMContext *ctx, POINT point);

auto FindMonitorByPoint(struct BFWMContext *ctx, POINT point) -> Monitor *;

auto FindMonitorByWorkspace(struct BFWMContext *ctx, Workspace *workspace)
    -> Monitor *;

auto FindWorkspaceOnMonitor(const Monitor *mon, size_t identifier)
    -> Workspace *;

auto FindWorkspaceById(struct BFWMContext *ctx, size_t identifier)
    -> Workspace *;

void WorkspaceDestroyIfEmpty(Monitor *mon, Workspace *workspace);

void WorkspaceActivateSimple(struct BFWMContext *ctx, Monitor *mon,
                             Workspace *old_workspace, Workspace *new_workspace,
                             bool destroy_empty_old_ws);

/**
 * @brief Activate the workspace containing @p hwnd, if it is not already
 * active on its monitor.
 *
 * Focus events (foreground, hover, force-focus, close-refocus) must always
 * shift to the workspace owning the focused window: cloak the previously
 * active workspace's windows, uncloak the target's, and apply layout.
 * No-op when the window's workspace is already the monitor's active one.
 *
 * @param hwnd The HWND whose workspace should be activated
 * @param ctx  The BFWM context
 */
void WorkspaceActivateForWindow(HWND hwnd, struct BFWMContext *ctx);

auto WorkspaceActivate(struct BFWMContext *ctx, Monitor *mon, size_t target_id)
    -> bool;

void RestoreDesktopState(struct BFWMContext *ctx);

void PushGapConfigToAllWorkspaces(struct BFWMContext *ctx);

void RecalculateAllWorkspaceRects(struct BFWMContext *ctx);

auto FindNextWorkspaceId(struct BFWMContext *ctx) -> size_t;

auto FindUnassignedWorkspaceId(struct BFWMContext *ctx, Monitor *mon) -> size_t;

auto FindWorkspaceConfig(struct BFWMContext *ctx, size_t workspace_id) -> const
    struct WorkspaceConfig *;

/**
 * @brief Force DWM to re-composite immediately.
 *
 * Must be called after cloaking changes, layout passes, or any
 * visual update that needs to be flushed to the screen right away.
 */
static inline void ForceDwmComposite() { DwmFlush(); }

/**
 * @brief Resolve the target workspace for a given HWND based on its
 *        on-screen position.
 *
 * Scans all monitors and returns the active workspace of the monitor
 * whose work area contains the window's centre point.  Returns NULL if
 * the centre falls outside all monitor work areas.
 *
 * @param ctx  The BFWM context
 * @param hwnd The window handle to locate
 * @return Workspace* Pointer to the workspace, or NULL
 */
auto ResolveTargetWorkspace(struct BFWMContext *ctx, HWND hwnd) -> Workspace *;

#endif
