/**
 * @file layout_api.h
 * @brief Virtual interface for layout engines.
 *
 * New layout algorithms are self-contained modules that implement the
 * LayoutEngine interface.  The workspace is layout-agnostic and delegates
 * all layout-specific work through virtual calls.
 */

#ifndef BFWM_LAYOUT_API_H
#define BFWM_LAYOUT_API_H

#include "../../core/bfwm_def.h"
#include <windows.h>

// ------------------------------------------------------------------
// Layout type enum. Identifies which concrete layout is in use.
// ------------------------------------------------------------------
using LayoutType = enum {
   LAYOUT_NONE = -1,
   DWINDLE,
   MONOCLE,
   MASTER,
   LAYOUT_TYPE_COUNT,
};

/** Configuration pushed to a layout engine during hot-reload. */
using LayoutConfig = struct {
   int gap_between;
   int gap_edge;
   int border_width; /* per-window border thickness reserved around each
                        tiled window */
   RECT workspace_rect;
};

// ------------------------------------------------------------------
// LayoutEngine – abstract interface implemented by each layout.
// ------------------------------------------------------------------
class LayoutEngine {
 public:
   virtual ~LayoutEngine() = default;

   /// Identifies the concrete layout (for logging etc.)
   // NOLINTNEXTLINE
   [[nodiscard]] virtual auto type() const -> LayoutType = 0;

   /** Insert a new window into the layout.  Returns true on success. */
   virtual auto insert(HWND window, HWND focus) -> bool = 0;

   /** Remove a window from the layout.  Returns true on success. */
   virtual auto remove(HWND window) -> bool = 0;

   /** Apply the layout: position every managed window. */
   virtual void apply() = 0;

   /** Retrieve the RECT for a given window managed by this layout. */
   virtual auto get_rect(HWND window, RECT *out) -> bool = 0;

   /** Move window 'hwnd' in the given direction (swap with neighbor).
    * Returns true on success. */
   virtual auto move_window(HWND hwnd, BFWMDirection direction) -> bool = 0;

   /** Get the HWND of the window adjacent to 'hwnd' in the given direction.
    * Returns NULL if no neighbor exists. */
   virtual auto get_neighbor(HWND hwnd, BFWMDirection direction) -> HWND = 0;

   /** Get the HWND closest to @p reference.
    *  @param reference  Reference window, or NULL to return a sensible default
    *                    (first window / top of stack).
    *  Returns NULL if no windows exist. */
   virtual auto get_closest_window(HWND reference) -> HWND = 0;

   /** Notify the layout which window is currently focused/active.
    *  Layouts that track an active selection (e.g. monocle) use this so
    *  get_closest_window(nullptr) can return the visible window. Default
    *  is a no-op for layouts that don't need it. */
   virtual void set_active_window(HWND hwnd) { (void)hwnd; }

   /** Resize a window in the given direction by the given pixel delta.
    *  Returns true on success. */
   virtual auto resize_window(HWND hwnd, BFWMDirection direction, int pixels)
       -> bool = 0;

   /** Resize a window to an absolute rectangle (mouse-drag resize).
    *  Updates the split ratio so neighbours accommodate the new size.
    *  Returns true on success. */
   virtual auto resize_window_to_rect(HWND hwnd, RECT new_rect) -> bool = 0;

   /** Toggle the split direction (horizontal<->vertical) of the container
    *  that holds hwnd. Returns true on success. */
   virtual auto toggle_split(HWND hwnd) -> bool = 0;

   /** Swap the two children of the container that holds hwnd.
    *  Returns true on success. */
   virtual auto swap_split(HWND hwnd) -> bool = 0;

   /** Push updated config (gap, workspace area) to the layout. */
   virtual void apply_config(LayoutConfig config) = 0;
};

/** Layout type of an engine, or LAYOUT_NONE when the engine is null. */
static inline auto EngineType(const LayoutEngine *engine) -> LayoutType {
   return (engine != nullptr) ? engine->type() : LAYOUT_NONE;
}

#endif /* BFWM_LAYOUT_API_H */
