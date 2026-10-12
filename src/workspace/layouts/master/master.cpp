#include "master.h"
#include "../../../core/bfwm_def.h"
#include "../layout_base.h"
#include <algorithm>
#include <vector>
#include <windef.h>

namespace {

/**
 * @brief Split a region into `count` equal-height vertical slices.
 *
 * Each slice spans the full width of `region`; slices are stacked top to
 * bottom with `gap` pixels between them. The last slice absorbs any
 * rounding remainder.
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
         r.bottom = region.bottom;
      } else {
         r.bottom = y + slice_h;
         y = r.bottom + gap;
      }
      out.push_back(r);
   }
}

} // namespace

// ---------------------------------------------------------------------------
// MasterEngine
// ---------------------------------------------------------------------------

auto MasterEngine::get_rect(HWND hwnd, RECT *out_rect) -> bool {
   if (out_rect == nullptr)
      return false;

   int const idx = FindIndex(hwnd);
   if (idx < 0)
      return false;

   size_t const n = windows.size();
   int const mcnt = master_count > 0 ? master_count : 1;

   if (std::cmp_less_equal(n, mcnt)) {
      // Everything is a master: split the whole tiled area.
      std::vector<RECT> slices;
      SplitVertical(tiled_rect, static_cast<int>(n), gap_between, slices);
      if (static_cast<size_t>(idx) < slices.size()) {
         *out_rect = slices[static_cast<size_t>(idx)];
         return true;
      }
      return false;
   }

   int const master_w =
       static_cast<int>((tiled_rect.right - tiled_rect.left) * master_factor);
   RECT const master_region = {
       .left = tiled_rect.left,
       .top = tiled_rect.top,
       .right = tiled_rect.left + master_w,
       .bottom = tiled_rect.bottom,
   };
   RECT const stack_region = {
       .left = master_region.right + gap_between,
       .top = tiled_rect.top,
       .right = tiled_rect.right,
       .bottom = tiled_rect.bottom,
   };

   if (idx < mcnt) {
      std::vector<RECT> slices;
      SplitVertical(master_region, mcnt, gap_between, slices);
      if (static_cast<size_t>(idx) < slices.size()) {
         *out_rect = slices[static_cast<size_t>(idx)];
         return true;
      }
      return false;
   }

   int const stack_count = static_cast<int>(n) - mcnt;
   std::vector<RECT> slices;
   SplitVertical(stack_region, stack_count, gap_between, slices);
   auto const sidx = static_cast<size_t>(idx - mcnt);
   if (sidx < slices.size()) {
      *out_rect = slices[sidx];
      return true;
   }
   return false;
}

auto MasterEngine::get_neighbor(HWND hwnd, BFWMDirection direction) -> HWND {
   if (windows.empty())
      return nullptr;

   int const idx = FindIndex(hwnd);
   if (idx < 0)
      return nullptr;

   int const mcnt = master_count > 0 ? master_count : 1;
   bool const is_master = idx < mcnt;
   int const sidx = idx - mcnt;
   int const stack_cnt = static_cast<int>(windows.size()) - mcnt;

   switch (direction) {
   case DirLeft:
   case DirPrev:
      // Left of the master is the outer edge; the stack sits to the right.
      if (is_master)
         return nullptr;
      return windows[0];
   case DirRight:
   case DirNext:
      // Right of the stack is the outer edge; the master sits to the left.
      if (!is_master)
         return nullptr;
      return (stack_cnt > 0) ? windows[mcnt] : nullptr;
   case DirUp:
      if (is_master)
         return (mcnt > 1 && idx > 0) ? windows[idx - 1] : nullptr;
      return (sidx > 0) ? windows[idx - 1] : nullptr;
   case DirDown:
      if (is_master)
         return (mcnt > 1 && idx < mcnt - 1) ? windows[idx + 1] : nullptr;
      return (sidx < stack_cnt - 1) ? windows[idx + 1] : nullptr;
   }
   return nullptr;
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

auto MasterLayoutEngineCreate(RECT workspace_rect, int gap_between,
                              int gap_edge, int border_width)
    -> std::unique_ptr<LayoutEngine> {
   auto engine = std::make_unique<MasterEngine>();
   LayoutConfig const cfg = {.gap_between = gap_between,
                             .gap_edge = gap_edge,
                             .border_width = border_width,
                             .workspace_rect = workspace_rect};
   engine->apply_config(cfg);
   return engine;
}