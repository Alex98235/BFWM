/**
 * @file dwindle.h
 * @brief Dwindle tiling layout implementation.
 *
 * Implements a binary-space-partitioning (BSP) tiling layout algorithm where
 * each new window splits the currently focused window's space. Supports
 * configurable split ratios, directions, and layout options.
 */

#ifndef BFWM_DWINDLE_H
#define BFWM_DWINDLE_H

#include "../../../core/bfwm_def.h"
#include "../layout_api.h"
#include "dwindle_tree.h"
#include <windef.h>

#define DWINDLE_DEFAULT_SPLIT_RATIO 0.5
#define DWINDLE_DEFAULT_SPLIT_WIDTH_MULTIPLIER 1.0

/**
 * @brief Direction in which to split a window.
 */
using SplitDirection = enum {
   /// Split to the left
   SPLIT_LEFT,
   /// Split to the right
   SPLIT_RIGHT,
   /// Split to the top
   SPLIT_TOP,
   /// Split to the bottom
   SPLIT_BOTTOM
};

#ifdef BFWM_BUILD_TESTING
/* Exposed for unit testing — not part of the public API */
void SplitRectangle(RECT *parent, BOOL horizontal, double ratio, RECT *first,
                    RECT *second);
#endif

/**
 * @brief The main dwindle layout state.
 *
 * Holds the BSP tree root, window count, split preferences, and the
 * available screen area for the workspace.
 */
using DwindleLayout = struct {
   /// Root node of the BSP tree
   DwindleNode *root;
   /// Number of windows in the layout
   int window_count;

   /// Default ratio for new splits (0.0 to 1.0)
   double split_ratio;

   /// Pixel gap between adjacent tiled windows
   int gap_between;
   /// Pixel gap at workspace edges
   int gap_edge;
   /// Border thickness reserved around each tiled window (layout inset)
   int border_width;
   /// Available area (monitor minus bars)
   RECT workspace_rect;
};

/**
 * @brief Allocate memory for a new dwindle layout
 *
 * @param workspace_rect The available area (monitor minus bars)
 * @param split_ratio    The ratio between a parent and childs largest window
 * dimension. Clamps the value between 0.0 and 1.0.
 * @param options        One or more DwindleOption or'd together
 * @return DwindleLayout* A pointer to the dwindle layout
 */
auto DwindleLayoutCreate(RECT workspace_rect, double split_ratio)
    -> DwindleLayout *;

/**
 * @brief Insert a window into the dwindle tree.
 *        Splits the available space if necessary.
 *
 * @param layout       A dwindle layout
 * @param new_window   The window to insert
 * @param focus_window The currently focused window
 * @return true if the window was successfully inserted, else false
 */
auto DwindleInsertWindow(DwindleLayout *layout, HWND new_window,
                         HWND focus_window) -> bool;

/**
 * @brief Remove a window in the dwindle layout.
 *
 * @param layout A dwindle layout
 * @param window The window to remove
 * @return true if the window was successfully removed, else false
 * (including if the window doesn't exist).
 */
auto DwindleRemoveWindow(DwindleLayout *layout, HWND window) -> bool;

/**
 * @brief Recalculate window positions for the windows in the layout.
 *
 * @param layout A dwindle layout
 */
void DwindleRecalculate(DwindleLayout *layout);

/**
 * @brief Get the window RECT for the given window
 *
 * @param layout   A dwindle layout
 * @param hwnd     The window to find
 * @param out_rect The position and size for the given window
 * @return true if the RECT exists, else false
 */
auto DwindleGetWindowRect(DwindleLayout *layout, HWND hwnd, RECT *out_rect)
    -> bool;

/**
 * @brief Find the closest window in the given direction using geometric
 *        comparison of window rectangles.
 *
 * @param layout    A dwindle layout
 * @param hwnd      The source window
 * @param direction Direction to look (DirLeft/DirRight/DirUp/DirDown)
 * @return HWND     The neighbor window handle, or NULL if none found
 */
auto DwindleGetNeighbor(DwindleLayout *layout, HWND hwnd,
                        BFWMDirection direction) -> HWND;

/**
 * @brief Find the window closest (by Euclidean center distance) to a reference
 *        window.  Falls back to the first leaf if @p reference is NULL or not
 *        found in the tree.
 *
 * @param layout    A dwindle layout
 * @param reference Reference window, or NULL for first-leaf fallback
 * @return HWND     The closest window, or NULL if none exist
 */
auto DwindleGetClosestWindow(DwindleLayout *layout, HWND reference) -> HWND;

/**
 * @brief Resize a window in the given direction by the given pixel delta.
 *
 * Adjusts the split ratio of the nearest matching ancestor container
 * so the size change persists. Clamps ratio to [0.1, 0.9].
 *
 * @param layout    A dwindle layout
 * @param hwnd      The window to resize
 * @param direction Direction to expand (DirLeft/DirRight/DirUp/DirDown)
 * @param pixels    Pixel delta (positive = grow, negative = shrink)
 * @return true if the window was resized, false otherwise
 */
auto DwindleResizeWindow(DwindleLayout *layout, HWND hwnd,
                         BFWMDirection direction, int pixels) -> bool;

/**
 * @brief Resize a window to a specific rectangle (mouse drag resize).
 *
 * Computes new split ratios from the desired rect so the tree matches
 * the window's actual on-screen position. Clamps ratio to [0.1, 0.9].
 *
 * @param layout  A dwindle layout
 * @param hwnd    The window to resize
 * @param new_rect The window's new desired rect
 * @return true if the tree was adjusted, false otherwise
 */
auto DwindleResizeWindowToRect(DwindleLayout *layout, HWND hwnd, RECT new_rect)
    -> bool;

/**
 * @brief Move the given window in the given direction.
 *
 * First tries swapping with the nearest geometric neighbor. If no
 * neighbor exists in that direction, walks the BSP tree to promote
 * the window to a leaf of the first matching ancestor split (the
 * tree-based "promotion" fallback).
 *
 * @param layout    A dwindle layout
 * @param hwnd      The window to move
 * @param direction Direction to move (DirLeft/DirRight/DirUp/DirDown)
 * @return true     if the window was successfully moved, else false
 */
auto DwindleMoveWindow(DwindleLayout *layout, HWND hwnd,
                       BFWMDirection direction) -> bool;

/**
 * @brief Toggle the split direction (horizontal↔vertical) of the container
 *        that holds the given window.
 *
 * @param layout A dwindle layout
 * @param hwnd   A window managed by the layout
 * @return true if the split was toggled, false if no parent container exists
 */
auto DwindleToggleSplit(DwindleLayout *layout, HWND hwnd) -> bool;

/**
 * @brief Swap the two children of the container that holds the given window.
 *
 * @param layout A dwindle layout
 * @param hwnd   A window managed by the layout
 * @return true if the children were swapped, false if no parent container
 * exists
 */
auto DwindleSwapSplit(DwindleLayout *layout, HWND hwnd) -> bool;

/**
 * @brief Deallocate a dwindle layout
 *
 * @param layout A dwindle layout
 */
void DwindleLayoutFree(DwindleLayout *layout);

/**
 * @brief Set the pixel gaps on a dwindle layout.
 *
 * gap_between is applied in DwindleGetWindowRect as an inset of gap/2
 * pixels on each edge. gap_edge is applied by insetting workspace_rect
 * before the split tree is recalculated.
 *
 * @param layout      A dwindle layout
 * @param gap_between Pixel gap between adjacent tiled windows
 * @param gap_edge    Pixel gap at workspace edges
 */
void DwindleLayoutSetGaps(DwindleLayout *layout, int gap_between, int gap_edge);

/**
 * @brief Apply a hot-reloaded configuration to a dwindle layout.
 *
 * Updates the gap values and workspace rectangle, then recalculates
 * all node positions.
 */
void DwindleLayoutApplyConfig(DwindleLayout *dwindle_layout,
                              LayoutConfig config);
#endif
