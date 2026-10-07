#include "master_vtable.h"
#include "../../../core/bfwm_def.h"
#include "../layout_api.h"
#include "master.h"
#include <memory>
#include <windef.h>

MasterEngine::MasterEngine(MasterLayout *layout) : layout_(layout) {}

MasterEngine::~MasterEngine() { MasterLayoutFree(layout_); }

auto MasterEngine::type() const -> LayoutType { return MASTER; }

auto MasterEngine::insert(HWND window, HWND focus) -> bool {
   return MasterInsertWindow(layout_, window, focus);
}

auto MasterEngine::remove(HWND window) -> bool {
   return MasterRemoveWindow(layout_, window);
}

void MasterEngine::apply() {}

auto MasterEngine::get_rect(HWND window, RECT *out) -> bool {
   return MasterGetWindowRect(layout_, window, out);
}

auto MasterEngine::get_neighbor(HWND hwnd, BFWMDirection direction) -> HWND {
   return MasterGetNeighbor(layout_, hwnd, direction);
}

auto MasterEngine::move_window(HWND hwnd, BFWMDirection direction) -> bool {
   return MasterMoveWindow(layout_, hwnd, direction);
}

auto MasterEngine::get_closest_window(HWND reference) -> HWND {
   return MasterGetClosestWindow(layout_, reference);
}

void MasterEngine::set_active_window(HWND hwnd) {
   if (layout_ == nullptr)
      return;
   for (auto &window : layout_->windows) {
      if (window == hwnd) {
         layout_->selected_hwnd = hwnd;
         return;
      }
   }
   // hwnd not managed by this layout (e.g. an unmanaged window gained
   // focus): leave the selection unchanged.
}

/** @brief Not applicable — master/stack has no free-form resize */
auto MasterEngine::resize_window(HWND hwnd, BFWMDirection direction,
                                 int pixels) -> bool {
   (void)hwnd;
   (void)direction;
   (void)pixels;
   return false;
}

/** @brief Not applicable — master/stack has no free-form resize */
auto MasterEngine::resize_window_to_rect(HWND hwnd, RECT new_rect) -> bool {
   (void)hwnd;
   (void)new_rect;
   return false;
}

/** @brief Not applicable — master/stack has no splits to toggle */
auto MasterEngine::toggle_split(HWND hwnd) -> bool {
   (void)hwnd;
   return false;
}

/** @brief Not applicable — master/stack has no splits to swap */
auto MasterEngine::swap_split(HWND hwnd) -> bool {
   (void)hwnd;
   return false;
}

void MasterEngine::apply_config(LayoutConfig config) {
   MasterLayoutApplyConfig(layout_, config);
}

auto MasterLayoutEngineCreate(RECT workspace_rect, int gap_between,
                              int gap_edge, int border_width)
    -> std::unique_ptr<LayoutEngine> {
   MasterLayout *master_layout = MasterLayoutCreate(workspace_rect);
   if (master_layout == nullptr)
      return nullptr;

   LayoutConfig const cfg = {.gap_between = gap_between,
                             .gap_edge = gap_edge,
                             .border_width = border_width,
                             .workspace_rect = workspace_rect};
   MasterLayoutApplyConfig(master_layout, cfg);

   return std::make_unique<MasterEngine>(master_layout);
}
