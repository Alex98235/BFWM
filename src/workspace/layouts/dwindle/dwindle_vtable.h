/**
 * @file dwindle_vtable.h
 * @brief Dwindle LayoutEngine implementation.
 */

#ifndef BFWM_DWINDLE_VTABLE_H
#define BFWM_DWINDLE_VTABLE_H

#include "../layout_api.h"
#include "dwindle.h"
#include <memory>
#include <windows.h>

/**
 * @brief Dwindle layout engine: binary-split tiling.
 *
 * Owns a DwindleLayout; the destructor frees it via DwindleLayoutFree.
 */
class DwindleEngine : public LayoutEngine {
 public:
   explicit DwindleEngine(DwindleLayout *layout);
   ~DwindleEngine() override;

   [[nodiscard]] auto type() const -> LayoutType override;
   auto insert(HWND window, HWND focus) -> bool override;
   auto remove(HWND window) -> bool override;
   void apply() override;
   auto get_rect(HWND window, RECT *out) -> bool override;
   auto move_window(HWND hwnd, BFWMDirection direction) -> bool override;
   auto get_neighbor(HWND hwnd, BFWMDirection direction) -> HWND override;
   auto get_closest_window(HWND reference) -> HWND override;
   auto resize_window(HWND hwnd, BFWMDirection direction, int pixels)
       -> bool override;
   auto resize_window_to_rect(HWND hwnd, RECT new_rect) -> bool override;
   auto toggle_split(HWND hwnd) -> bool override;
   auto swap_split(HWND hwnd) -> bool override;
   void apply_config(LayoutConfig config) override;

 private:
   DwindleLayout *layout_;
};

/**
 * @brief Create a DwindleEngine wrapping a new dwindle layout.
 *
 * @param workspace_rect The available screen area for this workspace.
 * @param gap_between    Pixel gap between adjacent tiled windows.
 * @param gap_edge       Pixel gap at workspace edges.
 * @param border_width   Border thickness reserved around tiled windows.
 * @return A new engine, or nullptr on allocation failure.
 */
auto DwindleLayoutEngineCreate(RECT workspace_rect, int gap_between,
                               int gap_edge, int border_width)
    -> std::unique_ptr<LayoutEngine>;

#endif /* BFWM_DWINDLE_VTABLE_H */