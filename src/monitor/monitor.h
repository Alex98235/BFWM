/**
 * @file monitor.h
 * @brief Monitor detection, tracking, and management.
 *
 * Provides functions for enumerating displays, registering monitors,
 * and managing per-monitor workspace and window state.
 */

#ifndef BFWM_MONITOR_H
#define BFWM_MONITOR_H

#include "../core/bfwm_def.h"
#include "../workspace/workspace.h"
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>

class Bar;

/**
 * @brief Represents a physical display monitor.
 *
 * Tracks the monitor's dimensions, work area, associated workspaces,
 * managed windows, and layout defaults.
 */
class Monitor {
 public:
   Monitor();  // defined in monitor.cpp (Bar must be complete to destroy
               // unique_ptr<Bar>)
   ~Monitor(); // defined in monitor.cpp

   /// Windows monitor handle
   [[nodiscard]] auto GetHandle() const -> HMONITOR { return handle_; }
   /// Full monitor dimensions
   [[nodiscard]] auto GetRect() const -> RECT { return rect_; }
   /// Usable area (excluding taskbar)
   [[nodiscard]] auto GetWorkArea() const -> RECT { return work_area_; }
   /// Display number matching Windows Settings
   [[nodiscard]] auto GetDisplayNumber() const -> UINT {
      return display_number_;
   }
   /// Stable PnP device instance ID (e.g. MONITOR\DEL4049\5&3a7e8e2&0&UID256)
   [[nodiscard]] auto GetUid() const -> std::wstring const & { return uid_; }

   /** @name Workspace management
    * @{ */
   /// Array of workspaces on this monitor (read-only iteration)
   [[nodiscard]] auto Workspaces() const -> std::vector<Workspace *> const & {
      return workspaces_;
   }
   /// Currently visible workspace
   [[nodiscard]] auto GetActiveWorkspace() const -> Workspace * {
      return active_workspace_;
   }
   /** @} */

   /** @name Bar
    * @{ */
   /// This monitor's bar, or null when no bar exists
   [[nodiscard]] auto GetBar() const -> Bar * { return bar_.get(); }
   /// Reserved bar height in pixels
   [[nodiscard]] auto GetBarHeight() const -> int { return bar_height_; }
   /// Number of fullscreen windows on this monitor (bar hidden when >0)
   [[nodiscard]] auto GetFullscreenCount() const -> int {
      return fullscreen_count_;
   }
   /** @} */

   // Mutators
   void SetHandle(HMONITOR v) { handle_ = v; }
   void SetRect(RECT v) { rect_ = v; }
   void SetWorkArea(RECT v) { work_area_ = v; }
   void SetDisplayNumber(UINT v) { display_number_ = v; }
   void SetUid(std::wstring v) { uid_ = std::move(v); }
   void TrackWorkspace(Workspace *workspace) {
      workspaces_.push_back(workspace);
   }
   void ClearWorkspaces() { workspaces_.clear(); }
   void RemoveWorkspaceAt(size_t index) {
      workspaces_.erase(workspaces_.begin() +
                        static_cast<std::ptrdiff_t>(index));
   }
   void RemoveWorkspace(Workspace *workspace) {
      auto const found = std::ranges::find(workspaces_, workspace);
      if (found != workspaces_.end())
         workspaces_.erase(found);
   }
   void SetActiveWorkspace(Workspace *workspace) {
      active_workspace_ = workspace;
   }
   /* Out-of-line in monitor.cpp (move-assign and reset need Bar complete). */
   void SetBar(std::unique_ptr<Bar> b); // bar_ = std::move(b)
   void ResetBar();                     // bar_.reset()
   void SetBarHeight(int v) { bar_height_ = v; }
   void SetFullscreenCount(int v) { fullscreen_count_ = v; }
   void IncrementFullscreenCount() { ++fullscreen_count_; }
   void DecrementFullscreenCount() { --fullscreen_count_; }

   // Per-monitor operations
   void Init(struct BFWMContext *ctx); // was InitNewMonitor
   [[nodiscard]] auto CreateWorkspace(struct BFWMContext *ctx,
                                      size_t identifier) -> Workspace *;
   [[nodiscard]] auto SetupNewWorkspaces(struct BFWMContext *ctx)
       -> BOOL; // was SetupNewMonitorWorkspaces
   [[nodiscard]] auto IsDisabled(struct BFWMContext *ctx) const
       -> BOOL; // was IsMonitorDisabled
 private:
   auto TryPinnedWorkspaces(struct BFWMContext *ctx) -> BOOL;
   auto TryOriginWorkspaces(struct BFWMContext *ctx) -> BOOL;
   auto TryFreshWorkspace(struct BFWMContext *ctx) -> BOOL;

   HMONITOR handle_ = nullptr;
   RECT rect_ = {};
   RECT work_area_ = {};
   UINT display_number_ = 0;
   std::wstring uid_;
   std::vector<Workspace *> workspaces_;
   Workspace *active_workspace_ = nullptr;
   std::unique_ptr<Bar> bar_;
   int bar_height_ = 0;
   int fullscreen_count_ = 0;
};

/**
 * @brief Registry of all physical monitors.
 */
class MonitorRegistry {
 public:
   [[nodiscard]] auto Monitors() const -> std::vector<Monitor *> const & {
      return monitors_;
   }
   [[nodiscard]] auto At(size_t index) const -> Monitor * {
      return monitors_[index];
   }
   [[nodiscard]] auto Size() const -> size_t { return monitors_.size(); }
   [[nodiscard]] auto Empty() const -> bool { return monitors_.empty(); }
   void Add(Monitor *monitor) { monitors_.push_back(monitor); }
   void SetAt(size_t index, Monitor *monitor) { monitors_[index] = monitor; }
   void Reserve(size_t capacity) { monitors_.reserve(capacity); }
   void Resize(size_t count) { monitors_.resize(count); }
   void Clear() { monitors_.clear(); }
   void SortByPosition(); // defined in monitor.cpp
   void ReplaceWith(std::vector<Monitor *> monitors) {
      monitors_ = std::move(monitors);
   }
   [[nodiscard]] auto TakeMonitors() -> std::vector<Monitor *> {
      return std::move(monitors_);
   }

 private:
   std::vector<Monitor *> monitors_;
};

/**
 * @brief Register a monitor in the registry.
 * @param reg     The monitor registry
 * @param monitor The monitor to register
 */
void RegisterMonitor(MonitorRegistry *reg, Monitor *monitor);

/**
 * @brief Sort monitors by position (top→bottom, then left→right).
 *
 * Produces a stable ordering regardless of EnumDisplayMonitors callback
 * order.  Used after enumeration to assign deterministic display numbers.
 *
 * @param a First monitor
 * @param b Second monitor
 * @return true if a sorts before b
 */
auto SortMonitorByPosition(Monitor const *a, Monitor const *b) -> bool;

/**
 * @brief EnumDisplayMonitors callback payload: target registry + context.
 *
 * MonitorEnumProc needs both the registry to fill and the BFWMContext so it
 * can register each monitor's effective DPI with ctx->dpi.
 */
using MonitorEnumData = struct {
   MonitorRegistry *reg;
   struct BFWMContext *ctx;
};

/**
 * @brief EnumDisplayMonitors callback for discovering connected displays.
 *
 * @param hMonitor    Handle to the display monitor
 * @param hdcMonitor  Handle to the monitor DC
 * @param lprcMonitor Intersection rectangle
 * @param dwData      Application-defined data (MonitorEnumData*)
 * @return BOOL TRUE to continue enumeration, FALSE to stop
 */
auto CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor,
                              LPRECT lprcMonitor, LPARAM dwData) -> BOOL;

/**
 * @brief Find the adjacent workspace in a given direction relative to
 *        the focused window.
 *
 * Searches across all monitors for the nearest workspace in the specified
 * direction. For DirNext/DirPrev, cycles through the monitors in order.
 *
 * @param ctx       The BFWM context
 * @param direction Direction to search
 * (DirLeft/DirRight/DirUp/Down/DirNext/DirPrev)
 * @return Workspace* Pointer to the neighbouring workspace, or NULL
 */
auto FindNeighbouringWorkspace(struct BFWMContext *ctx, BFWMDirection direction)
    -> Workspace *;

/**
 * @brief Find the monitor for a given HWND.
 *
 * Convenience that chains FindWorkspaceByHwnd + FindMonitorByWorkspace.
 *
 * @param ctx  The BFWM context
 * @param hwnd The window handle
 * @return Monitor* Pointer to the monitor, or NULL if the window
 *         is not managed or not found on any workspace.
 */
auto FindMonitorForHwnd(struct BFWMContext *ctx, HWND hwnd) -> Monitor *;

auto FindNeighbouringMonitor(struct BFWMContext *ctx, Monitor *src,
                             BFWMDirection direction) -> Monitor *;

auto FindMonitorByDisplayNumber(struct BFWMContext *ctx, UINT display_number)
    -> Monitor *;

void ReconcileMonitors(struct BFWMContext *ctx);

#endif
