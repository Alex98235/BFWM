/**
 * @file dwindle_vtable.cpp
 * @brief DwindleEngine: adapter between the LayoutEngine interface and the
 * existing DwindleLayout functions.
 */

#include "dwindle_vtable.h"
#include "../../../core/bfwm_def.h"
#include "../layout_api.h"
#include "dwindle.h"
#include <memory>
#include <windef.h>

DwindleEngine::DwindleEngine(DwindleLayout *layout) : layout_(layout) {}

DwindleEngine::~DwindleEngine() { DwindleLayoutFree(layout_); }

auto DwindleEngine::type() const -> LayoutType { return DWINDLE; }

auto DwindleEngine::insert(HWND window, HWND focus) -> bool {
   return DwindleInsertWindow(layout_, window, focus);
}

auto DwindleEngine::remove(HWND window) -> bool {
   return DwindleRemoveWindow(layout_, window);
}

void DwindleEngine::apply() {
   // dwindle doesn't need the window list; its tree already tracks all
   // managed windows.
   DwindleRecalculate(layout_);
}

auto DwindleEngine::get_rect(HWND window, RECT *out) -> bool {
   return DwindleGetWindowRect(layout_, window, out);
}

auto DwindleEngine::get_neighbor(HWND hwnd, BFWMDirection direction) -> HWND {
   return DwindleGetNeighbor(layout_, hwnd, direction);
}

auto DwindleEngine::move_window(HWND hwnd, BFWMDirection direction) -> bool {
   return DwindleMoveWindow(layout_, hwnd, direction);
}

auto DwindleEngine::get_closest_window(HWND reference) -> HWND {
   return DwindleGetClosestWindow(layout_, reference);
}

auto DwindleEngine::resize_window(HWND hwnd, BFWMDirection direction,
                                  int pixels) -> bool {
   return DwindleResizeWindow(layout_, hwnd, direction, pixels);
}

auto DwindleEngine::resize_window_to_rect(HWND hwnd, RECT new_rect) -> bool {
   return DwindleResizeWindowToRect(layout_, hwnd, new_rect);
}

auto DwindleEngine::toggle_split(HWND hwnd) -> bool {
   return DwindleToggleSplit(layout_, hwnd);
}

auto DwindleEngine::swap_split(HWND hwnd) -> bool {
   return DwindleSwapSplit(layout_, hwnd);
}

void DwindleEngine::apply_config(LayoutConfig config) {
   DwindleLayoutApplyConfig(layout_, config);
}

auto DwindleLayoutEngineCreate(RECT workspace_rect, int gap_between,
                               int gap_edge, int border_width)
    -> std::unique_ptr<LayoutEngine> {
   DwindleLayout *dwindle_layout =
       DwindleLayoutCreate(workspace_rect, DWINDLE_DEFAULT_SPLIT_RATIO);
   if (dwindle_layout == nullptr)
      return nullptr;

   LayoutConfig const cfg = {.gap_between = gap_between,
                             .gap_edge = gap_edge,
                             .border_width = border_width,
                             .workspace_rect = workspace_rect};
   DwindleLayoutApplyConfig(dwindle_layout, cfg);

   return std::make_unique<DwindleEngine>(dwindle_layout);
}
