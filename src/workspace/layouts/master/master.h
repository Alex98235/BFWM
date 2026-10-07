/**
 * @file master.h
 * @brief Master/Stack layout engine.
 *
 * A master/stack tiling layout: one region (the "master" area) holds a
 * configurable number of windows stacked vertically on the left, and the
 * remaining windows are stacked vertically on the right. The master region
 * takes a configurable fraction of the workspace width.
 */

#ifndef BFWM_MASTER_H
#define BFWM_MASTER_H

#include "../../../core/bfwm_def.h"
#include "../layout_api.h"
#include <vector>
#include <windef.h>

/**
 * @brief State for a master/stack layout.
 */
using MasterLayout = struct {
   /// Managed window handles, in insertion order. windows[0..master_count-1]
   /// are masters, the rest form the stack.
   std::vector<HWND> windows;

   /// The workspace area this layout fills
   RECT workspace_rect;
   /// Computed tiled area after applying gaps
   RECT tiled_rect;
   /// Gap between windows
   int gap_between;
   /// Edge gap
   int gap_edge;
   /// Border thickness reserved around the tiled area
   int border_width;
   /// Number of windows in the master region (default 1)
   int master_count;
   /// Fraction of the width used by the master region (default 0.5)
   double master_factor;
   /// Currently-focused window (for get_closest_window(nullptr))
   HWND selected_hwnd;
};

/**
 * @brief Create a new master/stack layout.
 * @param workspace_rect The workspace rectangle
 * @return A new MasterLayout (caller frees with MasterLayoutFree)
 */
auto MasterLayoutCreate(RECT workspace_rect) -> MasterLayout *;

/**
 * @brief Insert a window into the master/stack layout.
 * @param layout Master layout
 * @param window Window handle to insert
 * @param focus  Currently focused window (used for stack ordering)
 * @return true on success
 */
auto MasterInsertWindow(MasterLayout *layout, HWND window, HWND focus) -> bool;

/**
 * @brief Remove a window from the layout.
 * @param layout Master layout
 * @param window Window handle to remove
 * @return true if found and removed
 */
auto MasterRemoveWindow(MasterLayout *layout, HWND window) -> bool;

/**
 * @brief Recalculate window positions.
 * @param layout Master layout
 */
void MasterRecalculate(MasterLayout *layout);

/**
 * @brief Get the tiled rectangle for a window.
 * @param layout  Master layout
 * @param hwnd    Window handle
 * @param out_rect Receives the rectangle
 * @return true if the window was found
 */
auto MasterGetWindowRect(MasterLayout *layout, HWND hwnd, RECT *out_rect)
    -> bool;

/**
 * @brief Move a window in the given direction (swap with neighbor).
 * @param layout    Master layout
 * @param hwnd      Current window
 * @param direction Direction to move
 * @return true on success
 */
auto MasterMoveWindow(MasterLayout *layout, HWND hwnd, BFWMDirection direction)
    -> bool;

/**
 * @brief Get the neighbouring window in the given direction.
 * @param layout    Master layout
 * @param hwnd      Current window
 * @param direction Direction to look
 * @return The neighbouring HWND, or NULL
 */
auto MasterGetNeighbor(MasterLayout *layout, HWND hwnd, BFWMDirection direction)
    -> HWND;

/**
 * @brief Get the closest window to a reference, cycling forward.
 * @param layout    Master layout
 * @param reference Reference window, or NULL for the currently-selected one
 * @return The next HWND in circular order, or NULL if empty
 */
auto MasterGetClosestWindow(MasterLayout *layout, HWND reference) -> HWND;

/**
 * @brief Resize a window in the given direction (no-op in master).
 * @param layout    Master layout
 * @param hwnd      Window handle
 * @param direction Resize direction
 * @param pixels    Pixel delta
 * @return false (unsupported in master)
 */
auto MasterResizeWindow(MasterLayout *layout, HWND hwnd,
                        BFWMDirection direction, int pixels) -> bool;

/**
 * @brief Resize a window to an exact rectangle (no-op in master).
 * @param layout   Master layout
 * @param hwnd     Window handle
 * @param new_rect Target rectangle
 * @return false (unsupported in master)
 */
auto MasterResizeWindowToRect(MasterLayout *layout, HWND hwnd, RECT new_rect)
    -> bool;

/**
 * @brief Toggle the split orientation (no-op in master).
 * @param layout Master layout
 * @param hwnd   Window handle
 * @return false (unsupported in master)
 */
auto MasterToggleSplit(MasterLayout *layout, HWND hwnd) -> bool;

/**
 * @brief Swap the split orientation (no-op in master).
 * @param layout Master layout
 * @param hwnd   Window handle
 * @return false (unsupported in master)
 */
auto MasterSwapSplit(MasterLayout *layout, HWND hwnd) -> bool;

/**
 * @brief Free all resources associated with a master layout.
 * @param layout Layout to free
 */
void MasterLayoutFree(MasterLayout *layout);

/**
 * @brief Apply layout configuration (gaps, etc.).
 * @param layout Master layout
 * @param config Layout configuration to apply
 */
void MasterLayoutApplyConfig(MasterLayout *layout, LayoutConfig config);

#endif
