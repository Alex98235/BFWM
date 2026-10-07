#include "monocle_vtable.h"
#include "../../../core/bfwm_def.h"
#include "../layout_api.h"
#include "monocle.h"
#include <memory>
#include <windef.h>

MonocleEngine::MonocleEngine(MonocleLayout *layout) : layout_(layout) {}

MonocleEngine::~MonocleEngine() { MonocleLayoutFree(layout_); }

auto MonocleEngine::type() const -> LayoutType { return MONOCLE; }

auto MonocleEngine::insert(HWND window, HWND focus) -> bool {
   return MonocleInsertWindow(layout_, window, focus);
}

auto MonocleEngine::remove(HWND window) -> bool {
   return MonocleRemoveWindow(layout_, window);
}

void MonocleEngine::apply() { MonocleRecalculate(layout_); }

auto MonocleEngine::get_rect(HWND window, RECT *out) -> bool {
   return MonocleGetWindowRect(layout_, window, out);
}

/** @brief Not applicable — monocle has no geometric neighbors; use
 * get_closest_window/get_neighbor */
auto MonocleEngine::get_neighbor(HWND hwnd, BFWMDirection direction) -> HWND {
   return MonocleGetNeighbor(layout_, hwnd, direction);
}

auto MonocleEngine::move_window(HWND hwnd, BFWMDirection direction) -> bool {
   return MonocleMoveWindow(layout_, hwnd, direction);
}

auto MonocleEngine::get_closest_window(HWND reference) -> HWND {
   return MonocleGetClosestWindow(layout_, reference);
}

void MonocleEngine::set_active_window(HWND hwnd) {
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

/** @brief Not applicable — all windows share the same full rect */
auto MonocleEngine::resize_window(HWND hwnd, BFWMDirection direction,
                                  int pixels) -> bool {
   (void)hwnd;
   (void)direction;
   (void)pixels;
   return false;
}

/** @brief Not applicable — all windows share the same full rect */
auto MonocleEngine::resize_window_to_rect(HWND hwnd, RECT new_rect) -> bool {
   (void)hwnd;
   (void)new_rect;
   return false;
}

/** @brief Not applicable — monocle has no splits to toggle */
auto MonocleEngine::toggle_split(HWND hwnd) -> bool {
   (void)hwnd;
   return false;
}

/** @brief Not applicable — monocle has no splits to swap */
auto MonocleEngine::swap_split(HWND hwnd) -> bool {
   (void)hwnd;
   return false;
}

void MonocleEngine::apply_config(LayoutConfig config) {
   MonocleLayoutApplyConfig(layout_, config);
}

auto MonocleLayoutEngineCreate(RECT workspace_rect, int gap_between,
                               int gap_edge, int border_width)
    -> std::unique_ptr<LayoutEngine> {
   MonocleLayout *monocle_layout = MonocleLayoutCreate(workspace_rect);
   if (monocle_layout == nullptr)
      return nullptr;

   LayoutConfig const cfg = {.gap_between = gap_between,
                             .gap_edge = gap_edge,
                             .border_width = border_width,
                             .workspace_rect = workspace_rect};
   MonocleLayoutApplyConfig(monocle_layout, cfg);

   return std::make_unique<MonocleEngine>(monocle_layout);
}
