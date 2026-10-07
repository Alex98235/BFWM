#include "master.h"
#include "../../../core/bfwm_def.h"
#include "../layout_api.h"
#include <algorithm>
#include <utility>
#include <vector>
#include <windef.h>

namespace {

/**
 * @brief Split a region into `count` equal-height vertical slices.
 *
 * Each slice spans the full width of `region`; slices are stacked top to
 * bottom with `gap` pixels between them. The last slice absorbs any
 * rounding remainder. If count == 1, out[0] = region unchanged.
 */
void SplitVertical(RECT region, int count, int gap, std::vector<RECT> &out) {
   out.clear();
   if (count <= 0)
      return;
   if (count == 1) {
      out.push_back(region);
      return;
   }
   out.reserve(static_cast<size_t>(count));
   LONG const total_gap = static_cast<LONG>(count - 1) * gap;
   LONG const usable = region.bottom - region.top - total_gap;
   LONG const slice_h = usable / count;
   LONG y = region.top;
   for (int i = 0; i < count; i++) {
      RECT r = region;
      r.top = y;
      if (i == count - 1) {
         // Last slice absorbs the rounding remainder.
         r.bottom = region.bottom;
      } else {
         r.bottom = y + slice_h;
         y = r.bottom + gap;
      }
      out.push_back(r);
   }
}

} // namespace

auto MasterLayoutCreate(RECT workspace_rect) -> MasterLayout * {
   auto *master_layout = new MasterLayout;
   master_layout->workspace_rect = workspace_rect;
   master_layout->gap_between = 0;
   master_layout->gap_edge = 0;
   master_layout->border_width = 0;
   master_layout->master_count = 1;
   master_layout->master_factor = 0.5;
   master_layout->tiled_rect = workspace_rect;
   master_layout->selected_hwnd = nullptr;
   return master_layout;
}

auto MasterInsertWindow(MasterLayout *layout, HWND window, HWND focus) -> bool {
   (void)focus;
   if (layout == nullptr)
      return false;

   // No-op if the window is already present — a duplicate entry would
   // otherwise leave stale space after re-tiling.
   if (std::ranges::find(layout->windows, window) != layout->windows.end()) {
      return true;
   }

   layout->windows.push_back(window);
   if (layout->selected_hwnd == nullptr)
      layout->selected_hwnd = window;
   return true;
}

auto MasterRemoveWindow(MasterLayout *layout, HWND window) -> bool {
   if (layout == nullptr)
      return false;

   for (size_t i = 0; i < layout->windows.size(); i++) {
      if (layout->windows[i] == window) {
         bool const was_selected =
             (layout->windows[i] == layout->selected_hwnd);
         layout->windows.erase(layout->windows.begin() +
                               static_cast<ptrdiff_t>(i));
         if (was_selected) {
            if (layout->windows.empty()) {
               layout->selected_hwnd = nullptr;
            } else {
               layout->selected_hwnd =
                   layout->windows[i % layout->windows.size()];
            }
         }
         return true;
      }
   }
   return false;
}

void MasterRecalculate(MasterLayout *layout) {
   if (layout == nullptr)
      return;

   int const edge = layout->gap_edge;
   RECT r = layout->workspace_rect;
   r.left += edge;
   r.top += edge;
   r.right -= edge;
   r.bottom -= edge;
   if (layout->border_width > 0) {
      r.left += layout->border_width;
      r.top += layout->border_width;
      r.right -= layout->border_width;
      r.bottom -= layout->border_width;
   }
   layout->tiled_rect = r;
}

auto MasterGetWindowRect(MasterLayout *layout, HWND hwnd, RECT *out_rect)
    -> bool {
   if ((layout == nullptr) || (out_rect == nullptr))
      return false;

   int idx = -1;
   for (size_t i = 0; i < layout->windows.size(); i++) {
      if (layout->windows[i] == hwnd) {
         idx = static_cast<int>(i);
         break;
      }
   }
   if (idx < 0)
      return false;

   size_t const n = layout->windows.size();
   int const master_count = layout->master_count > 0 ? layout->master_count : 1;

   if (std::cmp_less_equal(n, master_count))) {
         // Everything is a master: split the whole tiled area.
         std::vector<RECT> slices;
         SplitVertical(layout->tiled_rect, static_cast<int>(n),
                       layout->gap_between, slices);
         if (static_cast<size_t>(idx) < slices.size()) {
            *out_rect = slices[static_cast<size_t>(idx)];
            return true;
         }
         return false;
      }

   int const master_w =
       static_cast<int>((layout->tiled_rect.right - layout->tiled_rect.left) *
                        layout->master_factor);
   RECT const master_region = {
       .left = layout->tiled_rect.left,
       .top = layout->tiled_rect.top,
       .right = layout->tiled_rect.left + master_w,
       .bottom = layout->tiled_rect.bottom,
   };
   RECT const stack_region = {
       .left = master_region.right + layout->gap_between,
       .top = layout->tiled_rect.top,
       .right = layout->tiled_rect.right,
       .bottom = layout->tiled_rect.bottom,
   };

   if (idx < master_count) {
      // This window is a master.
      std::vector<RECT> slices;
      SplitVertical(master_region, master_count, layout->gap_between, slices);
      if (static_cast<size_t>(idx) < slices.size()) {
         *out_rect = slices[static_cast<size_t>(idx)];
         return true;
      }
      return false;
   }

   // This window is in the stack.
   int const stack_count = static_cast<int>(n) - master_count;
   std::vector<RECT> slices;
   SplitVertical(stack_region, stack_count, layout->gap_between, slices);
   auto const sidx = static_cast<size_t>(idx - master_count);
   if (sidx < slices.size()) {
      *out_rect = slices[sidx];
      return true;
   }
   return false;
}

auto MasterMoveWindow(MasterLayout *layout, HWND hwnd, BFWMDirection direction)
    -> bool {
   if ((layout == nullptr) || layout->windows.size() < 2)
      return false;

   int idx = -1;
   for (size_t i = 0; i < layout->windows.size(); i++) {
      if (layout->windows[i] == hwnd) {
         idx = (int)i;
         break;
      }
   }
   if (idx < 0)
      return false;

   int swap_idx = -1;
   switch (direction) {
   case DirPrev:
      swap_idx =
          (idx == 0) ? static_cast<int>(layout->windows.size()) - 1 : idx - 1;
      break;
   case DirNext:
      swap_idx =
          (idx == static_cast<int>(layout->windows.size()) - 1) ? 0 : idx + 1;
      break;
   case DirLeft:
   case DirRight:
   case DirUp:
   case DirDown:
      break;
   }

   if (swap_idx < 0 || std::cmp_greater_equal(swap_idx, layout->windows.size()))
      return false;

   HWND tmp = layout->windows[idx];
   layout->windows[idx] = layout->windows[swap_idx];
   layout->windows[swap_idx] = tmp;
   return true;
}

auto MasterGetNeighbor(MasterLayout *layout, HWND hwnd, BFWMDirection direction)
    -> HWND {
   if ((layout == nullptr) || layout->windows.empty())
      return nullptr;

   int idx = -1;
   for (size_t i = 0; i < layout->windows.size(); i++) {
      if (layout->windows[i] == hwnd) {
         idx = (int)i;
         break;
      }
   }
   if (idx < 0)
      return nullptr;

   int const master_count = layout->master_count > 0 ? layout->master_count : 1;
   bool const is_master = idx < master_count;
   int const sidx = idx - master_count;
   int const stack_count =
       static_cast<int>(layout->windows.size()) - master_count;

   switch (direction) {
   case DirLeft:
   case DirPrev:
      // Left of the master is the outer edge; the stack sits to the right.
      if (is_master)
         return nullptr;
      return layout->windows[0];
   case DirRight:
   case DirNext:
      // Right of the stack is the outer edge; the master sits to the left.
      if (!is_master)
         return nullptr;
      return (stack_count > 0) ? layout->windows[master_count] : nullptr;
   case DirUp:
      if (is_master) {
         return (master_count > 1 && idx > 0) ? layout->windows[idx - 1]
                                              : nullptr;
      }
      return (sidx > 0) ? layout->windows[idx - 1] : nullptr;
   case DirDown:
      if (is_master) {
         return (master_count > 1 && idx < master_count - 1)
                    ? layout->windows[idx + 1]
                    : nullptr;
      }
      return (sidx < stack_count - 1) ? layout->windows[idx + 1] : nullptr;
   }
   return nullptr;
}

auto MasterGetClosestWindow(MasterLayout *layout, HWND reference) -> HWND {
   if ((layout == nullptr) || layout->windows.empty())
      return nullptr;

   if (reference != nullptr) {
      for (size_t i = 0; i < layout->windows.size(); i++) {
         if (layout->windows[i] == reference)
            return layout->windows[(i + 1) % layout->windows.size()];
      }
   }

   // Default: return the currently-selected window. Fall back to the first
   // window if selection is unset/stale.
   if (layout->selected_hwnd != nullptr) {
      for (size_t i = 0; i < layout->windows.size(); i++) {
         if (layout->windows[i] == layout->selected_hwnd)
            return layout->selected_hwnd;
      }
   }
   return layout->windows[0];
}

auto MasterResizeWindow(MasterLayout *layout, HWND hwnd,
                        BFWMDirection direction, int pixels) -> bool {
   (void)layout;
   (void)hwnd;
   (void)direction;
   (void)pixels;
   return false;
}

auto MasterResizeWindowToRect(MasterLayout *layout, HWND hwnd, RECT new_rect)
    -> bool {
   (void)layout;
   (void)hwnd;
   (void)new_rect;
   return false;
}

auto MasterToggleSplit(MasterLayout *layout, HWND hwnd) -> bool {
   (void)layout;
   (void)hwnd;
   return false;
}

auto MasterSwapSplit(MasterLayout *layout, HWND hwnd) -> bool {
   (void)layout;
   (void)hwnd;
   return false;
}

void MasterLayoutFree(MasterLayout *layout) {
   if (layout == nullptr)
      return;
   delete layout;
}

void MasterLayoutApplyConfig(MasterLayout *layout, LayoutConfig config) {
   if (layout == nullptr)
      return;
   layout->gap_between = config.gap_between;
   layout->gap_edge = config.gap_edge;
   layout->border_width = config.border_width > 0 ? config.border_width : 0;
   layout->workspace_rect = config.workspace_rect;
   MasterRecalculate(layout);
}
