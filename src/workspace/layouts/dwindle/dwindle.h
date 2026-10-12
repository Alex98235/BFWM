/**
 * @file dwindle.h
 * @brief Dwindle tiling layout engine.
 *
 * Implements a binary-space-partitioning (BSP) tiling layout algorithm where
 * each new window splits the currently focused window's space.
 *
 * Inherits common fields and apply_config() from BaseLayout.
 * Only what differs from other layouts lives here.
 */

#ifndef BFWM_DWINDLE_H
#define BFWM_DWINDLE_H

#include "../layout_base.h"
#include "dwindle_tree.h"
#include <memory>
#include <windef.h>

#define DWINDLE_DEFAULT_SPLIT_RATIO 0.5

/** @brief Direction in which to split a window. */
using SplitDirection = enum {
   SPLIT_LEFT,
   SPLIT_RIGHT,
   SPLIT_TOP,
   SPLIT_BOTTOM,
};

#ifdef BFWM_BUILD_TESTING
void SplitRectangle(RECT *parent, BOOL horizontal, double ratio, RECT *first,
                    RECT *second);
#endif

/**
 * @brief Binary-space-partitioning (dwindle) layout engine.
 *
 * State:
 *   - root, window_count, split_ratio  (tree-specific)
 *   - workspace_rect, gap_between, gap_edge, border_width  (inherited)
 */
class DwindleEngine : public BaseLayout {
 public:
   DwindleEngine() = default;
   ~DwindleEngine() override { FreeTree(root); }

   // -- LayoutEngine interface ----------------------------------------------
   [[nodiscard]] auto type() const -> LayoutType override { return DWINDLE; }
   auto insert(HWND new_window, HWND focus_window) -> bool override;
   auto remove(HWND window) -> bool override;
   void apply() override { Recalculate(); }
   auto get_rect(HWND hwnd, RECT *out_rect) -> bool override;
   auto move_window(HWND hwnd, BFWMDirection direction) -> bool override;
   auto get_neighbor(HWND hwnd, BFWMDirection direction) -> HWND override;
   auto get_closest_window(HWND reference) -> HWND override;
   auto resize_window(HWND hwnd, BFWMDirection direction, int pixels)
       -> bool override;
   auto resize_window_to_rect(HWND hwnd, RECT new_rect) -> bool override;
   auto toggle_split(HWND hwnd) -> bool override;
   auto swap_split(HWND hwnd) -> bool override;

   // -- Dwindle-specific ---------------------------------------------------

   /// Set the default split ratio for new splits, clamped to [0, 1].
   void SetSplitRatio(double ratio) {
      split_ratio = std::clamp(ratio, 0.0, 1.0);
   }

 protected:
   /// Reset the tree to fill workspace_rect and recurse.
   void Recalculate() override {
      if (root != nullptr)
         root->rect = workspace_rect;
      RecalculatePositions(root);
   }

   DwindleNode *root = nullptr;
   int window_count = 0;
   double split_ratio = DWINDLE_DEFAULT_SPLIT_RATIO;

 private:
   /// Recursive position recalculation from the given node downward.
   static void RecalculatePositions(DwindleNode *node);

   /// Per-side frame inset for a leaf node.
   void WindowSideInsets(const DwindleNode *node, int *left, int *top,
                         int *right, int *bottom) const;
   /// Replace old_node with new_node in the tree.
   void ReplaceNode(DwindleNode *old_node, DwindleNode *new_node);
   /// Update ancestor split ratios after a resize-to-rect.
   static auto UpdateAncestorRatio(DwindleNode *parent, HWND hwnd,
                                   RECT new_rect, BOOL *width_changed,
                                   BOOL *height_changed, int inset_left,
                                   int inset_top, int inset_right,
                                   int inset_bottom) -> BOOL;
};

/// Factory: create a DwindleEngine, apply workspace/gaps, return as Engine.
auto DwindleLayoutEngineCreate(RECT workspace_rect, int gap_between,
                               int gap_edge, int border_width)
    -> std::unique_ptr<LayoutEngine>;

#endif /* BFWM_DWINDLE_H */
