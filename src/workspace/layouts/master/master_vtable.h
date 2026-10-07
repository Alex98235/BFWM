/**
 * @file master_vtable.h
 * @brief Master/Stack LayoutEngine implementation.
 */

#ifndef BFWM_MASTER_VTABLE_H
#define BFWM_MASTER_VTABLE_H

#include "../layout_api.h"
#include "master.h"
#include <memory>
#include <windows.h>

/**
 * @brief Master/Stack layout engine: master region + side stack.
 *
 * Owns a MasterLayout; the destructor frees it via MasterLayoutFree.
 */
class MasterEngine : public LayoutEngine {
 public:
   explicit MasterEngine(MasterLayout *layout);
   ~MasterEngine() override;

   [[nodiscard]] auto type() const -> LayoutType override;
   auto insert(HWND window, HWND focus) -> bool override;
   auto remove(HWND window) -> bool override;
   void apply() override;
   auto get_rect(HWND window, RECT *out) -> bool override;
   auto move_window(HWND hwnd, BFWMDirection direction) -> bool override;
   auto get_neighbor(HWND hwnd, BFWMDirection direction) -> HWND override;
   auto get_closest_window(HWND reference) -> HWND override;
   void set_active_window(HWND hwnd) override;
   auto resize_window(HWND hwnd, BFWMDirection direction, int pixels)
       -> bool override;
   auto resize_window_to_rect(HWND hwnd, RECT new_rect) -> bool override;
   auto toggle_split(HWND hwnd) -> bool override;
   auto swap_split(HWND hwnd) -> bool override;
   void apply_config(LayoutConfig config) override;

 private:
   MasterLayout *layout_;
};

/**
 * @brief Create a MasterEngine wrapping a new master/stack layout.
 *
 * @param workspace_rect The workspace rectangle
 * @param gap_between    Gap between windows
 * @param gap_edge       Edge gap
 * @param border_width   Border thickness reserved around tiled windows
 * @return A new engine, or nullptr on allocation failure
 */
auto MasterLayoutEngineCreate(RECT workspace_rect, int gap_between,
                              int gap_edge, int border_width)
    -> std::unique_ptr<LayoutEngine>;

#endif
