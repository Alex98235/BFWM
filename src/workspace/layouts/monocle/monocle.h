/**
 * @file monocle.h
 * @brief Monocle layout engine.
 *
 * A full-screen stacking layout where only one window is visible at a
 * time. Windows are stacked in insertion order and the user cycles
 * through them.
 */

#ifndef BFWM_MONOCLE_H
#define BFWM_MONOCLE_H

#include "../../../core/bfwm_def.h"
#include "../layout_api.h"
#include <vector>
#include <windef.h>

/**
 * @brief State for a monocle (full-screen stacking) layout.
 */
using MonocleLayout = struct {
   /// Managed window handles, in insertion order
   std::vector<HWND> windows;

   /// The currently-shown window (monocle displays one window at a time).
   /// Tracked by HWND (not index) so it survives stack reordering/swaps.
   HWND selected_hwnd;

   /// The workspace area this layout fills
   RECT workspace_rect;
   /// Gap between windows (unused in monocle)
   int gap_between;
   /// Edge gap (unused in monocle)
   int gap_edge;
   /// Border thickness reserved around the tiled area
   int border_width;
   /// Computed tiled area after applying gaps
   RECT tiled_rect;
};

/**
 * @brief Create a new monocle layout.
 * @param workspace_rect The workspace rectangle
 * @return A new MonocleLayout (caller frees with MonocleLayoutFree)
 */
auto MonocleLayoutCreate(RECT workspace_rect) -> MonocleLayout *;

/**
 * @brief Insert a window into the monocle stack.
 * @param layout Monocle layout
 * @param window Window handle to insert
 * @param focus  Currently focused window (used for stack ordering)
 * @return true on success
 */
auto MonocleInsertWindow(MonocleLayout *layout, HWND window, HWND focus)
    -> bool;

/**
 * @brief Remove a window from the monocle stack.
 * @param layout Monocle layout
 * @param window Window handle to remove
 * @return true if found and removed
 */
auto MonocleRemoveWindow(MonocleLayout *layout, HWND window) -> bool;

/**
 * @brief Recalculate window positions (trivial for monocle).
 * @param layout Monocle layout
 */
void MonocleRecalculate(MonocleLayout *layout);

/**
 * @brief Get the tiled rectangle for a window.
 * @param layout  Monocle layout
 * @param hwnd    Window handle
 * @param out_rect Receives the rectangle
 * @return true if the window was found
 */
auto MonocleGetWindowRect(MonocleLayout *layout, HWND hwnd, RECT *out_rect)
    -> bool;

/**
 * @brief Move focus to the next/previous window in the stack.
 * @param layout    Monocle layout
 * @param hwnd      Current window
 * @param direction DirNext or DirPrev
 * @return true on success
 */
auto MonocleMoveWindow(MonocleLayout *layout, HWND hwnd,
                       BFWMDirection direction) -> bool;

/**
 * @brief Get the neighbouring window in the given direction.
 * @param layout    Monocle layout
 * @param hwnd      Current window
 * @param direction Direction to look
 * @return The neighbouring HWND, or NULL
 */
auto MonocleGetNeighbor(MonocleLayout *layout, HWND hwnd,
                        BFWMDirection direction) -> HWND;

/**
 * @brief Get the closest window to a reference, cycling forward.
 * @param layout    Monocle layout
 * @param reference Reference window, or NULL for first
 * @return The next HWND in circular order, or NULL if empty
 */
auto MonocleGetClosestWindow(MonocleLayout *layout, HWND reference) -> HWND;

/**
 * @brief Resize a window in the given direction (no-op in monocle).
 * @param layout    Monocle layout
 * @param hwnd      Window handle
 * @param direction Resize direction
 * @param pixels    Pixel delta
 * @return false (unsupported in monocle)
 */
auto MonocleResizeWindow(MonocleLayout *layout, HWND hwnd,
                         BFWMDirection direction, int pixels) -> bool;

/**
 * @brief Resize a window to an exact rectangle.
 * @param layout   Monocle layout
 * @param hwnd     Window handle
 * @param new_rect Target rectangle
 * @return true on success
 */
auto MonocleResizeWindowToRect(MonocleLayout *layout, HWND hwnd, RECT new_rect)
    -> bool;

/**
 * @brief Toggle the split orientation (no-op in monocle).
 * @param layout Monocle layout
 * @param hwnd   Window handle
 * @return false (unsupported in monocle)
 */
auto MonocleToggleSplit(MonocleLayout *layout, HWND hwnd) -> bool;

/**
 * @brief Swap the split orientation (no-op in monocle).
 * @param layout Monocle layout
 * @param hwnd   Window handle
 * @return false (unsupported in monocle)
 */
auto MonocleSwapSplit(MonocleLayout *layout, HWND hwnd) -> bool;

/**
 * @brief Free all resources associated with a monocle layout.
 * @param layout Layout to free
 */
void MonocleLayoutFree(MonocleLayout *layout);

/**
 * @brief Apply layout configuration (gaps, etc.).
 * @param layout Monocle layout
 * @param config Layout configuration to apply
 */
void MonocleLayoutApplyConfig(MonocleLayout *layout, LayoutConfig config);

#endif
