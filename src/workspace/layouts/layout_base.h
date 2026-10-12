/**
 * @file layout_base.h
 * @brief Shared base classes for layout engines.
 *
 * Provides BaseLayout (common fields + apply_config + border-width inset)
 * and SequenceLayout (vector-based window list operations) so each
 * concrete engine only implements what truly differs.
 *
 * All layouts share the same apply_config behaviour:
 *   1. Copy gap fields from the config.
 *   2. Subtract gap_edge from workspace_rect (the available canvas).
 *   3. Call the virtual Recalculate() hook.
 *
 * Border-width is handled inside Recalculate via ApplyBorderInset().
 */

#ifndef BFWM_LAYOUT_BASE_H
#define BFWM_LAYOUT_BASE_H

#include "../../core/bfwm_def.h"
#include "layout_api.h"
#include <algorithm>
#include <utility>
#include <vector>
#include <windef.h>

// ---------------------------------------------------------------------------
// BaseLayout: shared by ALL layout engines
// ---------------------------------------------------------------------------

/**
 * @brief Intermediate base that holds common fields and implements shared
 *        behaviour for all layout engines.
 *
 * Owned fields (public for direct layout-method access):
 *   - workspace_rect, tiled_rect
 *   - gap_between, gap_edge, border_width
 *
 * Provides:
 *   - apply_config()  — standardised gap-edge handling (single place)
 *   - ApplyBorderInset() — subtract border_width from tiled_rect
 *   - Stubs for resize_window, resize_window_to_rect, toggle_split,
 *     swap_split (all return false).
 *
 * Derived engines override:
 *   - Recalculate()  — layout-specific geometry computation
 *   - insert(), remove(), get_rect(), move_window(), get_neighbor(),
 *     get_closest_window() — as appropriate per layout
 */
class BaseLayout : public LayoutEngine {
 protected:
   // -- Common fields -------------------------------------------------------
   RECT workspace_rect = {};
   RECT tiled_rect = {};
   int gap_between = 0;
   int gap_edge = 0;
   int border_width = 0;

 public:
   // -- Shared methods ------------------------------------------------------

   /// Subtract gap_edge from workspace_rect, then call the virtual
   /// Recalculate() hook.  Every layout uses the same gap-edge policy.
   void apply_config(LayoutConfig config) override {
      gap_between = config.gap_between;
      gap_edge = config.gap_edge;
      border_width = config.border_width > 0 ? config.border_width : 0;
      RECT r = config.workspace_rect;
      r.left += gap_edge;
      r.top += gap_edge;
      r.right -= gap_edge;
      r.bottom -= gap_edge;
      workspace_rect = r;
      Recalculate();
   }

   /// Subtract border_width from tiled_rect (call from Recalculate).
   void ApplyBorderInset() {
      tiled_rect = workspace_rect;
      if (border_width > 0) {
         tiled_rect.left += border_width;
         tiled_rect.top += border_width;
         tiled_rect.right -= border_width;
         tiled_rect.bottom -= border_width;
      }
   }

   // -- Stubs for operations most layouts don't support ---------------------
   auto resize_window(HWND /*hwnd*/, BFWMDirection /*direction*/,
                      int /*pixels*/) -> bool override {
      return false;
   }
   auto resize_window_to_rect(HWND /*hwnd*/, RECT /*new_rect*/)
       -> bool override {
      return false;
   }
   auto toggle_split(HWND /*hwnd*/) -> bool override { return false; }
   auto swap_split(HWND /*hwnd*/) -> bool override { return false; }

 protected:
   /// Layout-specific geometry recalculaton.  Called by apply_config() after
   /// gap_edge has been subtracted and by apply().
   virtual void Recalculate() = 0;
};

// ---------------------------------------------------------------------------
// SequenceLayout — shared by vector-based layouts (master, monocle)
// ---------------------------------------------------------------------------

/**
 * @brief Intermediate base for layouts that manage windows as an ordered
 *        sequence (std::vector).
 *
 * Provides:
 *   - insert()  — dedup + push_back + set selected_hwnd
 *   - remove()  — find + erase + re-select fallback
 *   - move_window()   — circular prev/next swap
 *   - get_neighbor()  — circular prev/next neighbour
 *   - get_closest_window() — next-in-list or fallback to selected
 *   - set_active_window()  — set selected_hwnd if hwnd is in the list
 */
class SequenceLayout : public BaseLayout {
 protected:
   std::vector<HWND> windows;
   HWND selected_hwnd = nullptr;

 public:
   // -- Access helpers ------------------------------------------------------

   /// Find the index of @p hwnd in @c windows, or -1.
   [[nodiscard]] auto FindIndex(HWND hwnd) const -> int {
      for (size_t i = 0; i < windows.size(); i++) {
         if (windows[i] == hwnd)
            return static_cast<int>(i);
      }
      return -1;
   }

   // -- Insert / Remove -----------------------------------------------------

   auto insert(HWND window, HWND focus) -> bool override {
      (void)focus;
      // No-op if the window is already present — a duplicate entry would
      // otherwise leave stale space after re-tiling.
      if (std::ranges::find(windows, window) != windows.end())
         return true;
      windows.push_back(window);
      if (selected_hwnd == nullptr)
         selected_hwnd = window;
      return true;
   }

   auto remove(HWND window) -> bool override {
      int const idx = FindIndex(window);
      if (idx < 0)
         return false;
      bool const was_selected = (windows[idx] == selected_hwnd);
      windows.erase(windows.begin() + idx);
      if (was_selected) {
         selected_hwnd =
             windows.empty()
                 ? nullptr
                 : windows[static_cast<size_t>(idx) % windows.size()];
      }
      return true;
   }

   // -- Navigation ----------------------------------------------------------

   auto move_window(HWND hwnd, BFWMDirection direction) -> bool override {
      if (windows.size() < 2)
         return false;
      int const idx = FindIndex(hwnd);
      if (idx < 0)
         return false;

      int swap_idx = -1;
      switch (direction) {
      case DirPrev:
         swap_idx = (idx == 0) ? static_cast<int>(windows.size()) - 1 : idx - 1;
         break;
      case DirNext:
         swap_idx = (idx == static_cast<int>(windows.size()) - 1) ? 0 : idx + 1;
         break;
      default:
         break;
      }
      if (swap_idx < 0)
         return false;
      std::swap(windows[idx], windows[swap_idx]);
      return true;
   }

   auto get_neighbor(HWND hwnd, BFWMDirection direction) -> HWND override {
      if (windows.empty())
         return nullptr;
      int const idx = FindIndex(hwnd);
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
             (idx == static_cast<int>(windows.size()) - 1) ? -1 : idx + 1;
         break;
      default:
         break;
      }
      if (neighbor < 0 || static_cast<size_t>(neighbor) >= windows.size())
         return nullptr;
      return windows[static_cast<size_t>(neighbor)];
   }

   auto get_closest_window(HWND reference) -> HWND override {
      if (windows.empty())
         return nullptr;

      if (reference != nullptr) {
         for (size_t i = 0; i < windows.size(); i++) {
            if (windows[i] == reference)
               return windows[(i + 1) % windows.size()];
         }
      }

      // Default: return the currently-selected window.
      if (selected_hwnd != nullptr) {
         for (auto &window : windows) {
            if (window == selected_hwnd)
               return selected_hwnd;
         }
      }
      return windows[0];
   }

   void set_active_window(HWND hwnd) override {
      if (hwnd == nullptr)
         return;
      for (auto &w : windows) {
         if (w == hwnd) {
            selected_hwnd = hwnd;
            return;
         }
      }
   }
};

#endif /* BFWM_LAYOUT_BASE_H */
