#include "monocle.h"
#include "../../../core/bfwm_def.h"
#include "../layout_api.h"
#include <algorithm>
#include <utility>
#include <vector>
#include <windef.h>

auto MonocleLayoutCreate(RECT workspace_rect) -> MonocleLayout * {
   auto *monocle_layout = new MonocleLayout;
   monocle_layout->workspace_rect = workspace_rect;
   monocle_layout->gap_between = 0;
   monocle_layout->gap_edge = 0;
   monocle_layout->border_width = 0;
   monocle_layout->tiled_rect = workspace_rect;
   monocle_layout->selected_hwnd = nullptr;
   return monocle_layout;
}

auto MonocleInsertWindow(MonocleLayout *layout, HWND window, HWND focus)
    -> bool {
   (void)focus;
   if (layout == nullptr)
      return false;

   // No-op if the window is already in the stack — a duplicate entry
   // would otherwise leave stale space (and a stale placeholder) after
   // re-tiling.
   if (std::ranges::find(layout->windows, window) != layout->windows.end()) {
      return true;
   }

   layout->windows.push_back(window);
   if (layout->selected_hwnd == nullptr)
      layout->selected_hwnd = window;
   return true;
}

auto MonocleRemoveWindow(MonocleLayout *layout, HWND window) -> bool {
   if (layout == nullptr)
      return false;

   for (size_t i = 0; i < layout->windows.size(); i++) {
      if (layout->windows[i] == window) {
         bool const was_selected =
             (layout->windows[i] == layout->selected_hwnd);
         layout->windows.erase(layout->windows.begin() +
                               static_cast<ptrdiff_t>(i));
         if (was_selected) {
            if (layout->windows.empty())
               layout->selected_hwnd = nullptr;
            else
               layout->selected_hwnd =
                   layout->windows[i % layout->windows.size()];
         }
         return true;
      }
   }
   return false;
}

void MonocleRecalculate(MonocleLayout *layout) {
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

auto MonocleGetWindowRect(MonocleLayout *layout, HWND hwnd, RECT *out_rect)
    -> bool {
   (void)hwnd;
   if ((layout == nullptr) || (out_rect == nullptr))
      return false;
   *out_rect = layout->tiled_rect;
   (void)hwnd;
   return true;
}

auto MonocleMoveWindow(MonocleLayout *layout, HWND hwnd,
                       BFWMDirection direction) -> bool {
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

auto MonocleGetNeighbor(MonocleLayout *layout, HWND hwnd,
                        BFWMDirection direction) -> HWND {
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

   int neighbor = -1;
   switch (direction) {
   case DirLeft:
   case DirPrev:
      neighbor = (idx == 0) ? -1 : idx - 1;
      break;
   case DirRight:
   case DirNext:
      neighbor =
          (idx == static_cast<int>(layout->windows.size()) - 1) ? -1 : idx + 1;
      break;
   case DirUp:
   case DirDown:
      break;
   }

   if (neighbor < 0 || std::cmp_greater_equal(neighbor, layout->windows.size()))
      return nullptr;
   return layout->windows[neighbor];
}

auto MonocleGetClosestWindow(MonocleLayout *layout, HWND reference) -> HWND {
   if ((layout == nullptr) || layout->windows.empty())
      return nullptr;

   if (reference != nullptr) {
      for (size_t i = 0; i < layout->windows.size(); i++) {
         if (layout->windows[i] == reference)
            return layout->windows[(i + 1) % layout->windows.size()];
      }
   }

   // Default: return the currently-shown window (monocle shows one at a
   // time). Fall back to the first window if selection is unset/stale.
   if (layout->selected_hwnd != nullptr) {
      for (size_t i = 0; i < layout->windows.size(); i++) {
         if (layout->windows[i] == layout->selected_hwnd)
            return layout->selected_hwnd;
      }
   }
   return layout->windows[0];
}

auto MonocleResizeWindow(MonocleLayout *layout, HWND hwnd,
                         BFWMDirection direction, int pixels) -> bool {
   (void)layout;
   (void)hwnd;
   (void)direction;
   (void)pixels;
   return false;
}

auto MonocleResizeWindowToRect(MonocleLayout *layout, HWND hwnd, RECT new_rect)
    -> bool {
   (void)layout;
   (void)hwnd;
   (void)new_rect;
   return false;
}

auto MonocleToggleSplit(MonocleLayout *layout, HWND hwnd) -> bool {
   (void)layout;
   (void)hwnd;
   return false;
}

auto MonocleSwapSplit(MonocleLayout *layout, HWND hwnd) -> bool {
   (void)layout;
   (void)hwnd;
   return false;
}

void MonocleLayoutFree(MonocleLayout *layout) {
   if (layout == nullptr)
      return;
   delete layout;
}

void MonocleLayoutApplyConfig(MonocleLayout *layout, LayoutConfig config) {
   if (layout == nullptr)
      return;
   layout->gap_between = config.gap_between;
   layout->gap_edge = config.gap_edge;
   layout->border_width = config.border_width > 0 ? config.border_width : 0;
   layout->workspace_rect = config.workspace_rect;
   MonocleRecalculate(layout);
}
