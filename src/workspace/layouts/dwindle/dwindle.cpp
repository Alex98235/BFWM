#include "dwindle.h"
#include "../../../core/bfwm_def.h"
#include "../../../logging/logger.h"
#include "../../../math/rect.h"
#include "../../../memory/safe.h"
#include "../layout_api.h"
#include "dwindle_tree.h"
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <minwindef.h>
#include <windef.h>

enum { LEAVES_MAX = 256 };
#define SPLIT_RATIO_MIN 0.1
#define SPLIT_RATIO_MAX 0.9

auto DwindleLayoutCreate(RECT workspace_rect, double split_ratio)
    -> DwindleLayout * {
   auto *layout = (DwindleLayout *)safe_malloc(
       sizeof(DwindleLayout), "Could not allocate space for dwindle layout");

   layout->workspace_rect = workspace_rect;

   split_ratio = std::min(split_ratio, 1.0);
   split_ratio = std::max(split_ratio, 0.0);

   layout->split_ratio = split_ratio;
   layout->root = nullptr;
   layout->window_count = 0;
   layout->gap_between = 0;
   layout->gap_edge = 0;
   layout->border_width = 0;

   return layout;
}

namespace {

/// Per-side frame inset for a leaf: border_width on every side, plus half the
/// between-gap on any side that faces a sibling. A side whose node edge lies on
/// the workspace boundary faces no sibling — gap_edge already spaced the
/// workspace rect there — so it gets no between-gap inset. This keeps
/// gap_between strictly between adjacent windows and gap_edge at the boundary.
inline void WindowSideInsets(const DwindleLayout *layout,
                             const DwindleNode *node, int *left, int *top,
                             int *right, int *bottom) {
   int const half = layout->gap_between / 2;
   int const border = layout->border_width;
   *left =
       border + ((node->rect.left > layout->workspace_rect.left) ? half : 0);
   *top = border + ((node->rect.top > layout->workspace_rect.top) ? half : 0);
   *right =
       border + ((node->rect.right < layout->workspace_rect.right) ? half : 0);
   *bottom = border +
             ((node->rect.bottom < layout->workspace_rect.bottom) ? half : 0);
}

} // namespace

auto DwindleGetWindowRect(DwindleLayout *layout, HWND hwnd, RECT *out_rect)
    -> bool {
   if ((layout == nullptr) || (hwnd == nullptr))
      return false;

   DwindleNode *node = FindNodeByWindow(layout->root, hwnd);
   if (node == nullptr) {
      Debug("No node with window handle: %p found", hwnd);
      return false;
   }

   *out_rect = node->rect;

   int left;
   int top;
   int right;
   int bottom;
   WindowSideInsets(layout, node, &left, &top, &right, &bottom);
   out_rect->left += left;
   out_rect->top += top;
   out_rect->right -= right;
   out_rect->bottom -= bottom;

   return true;
}

#ifdef BFWM_BUILD_TESTING
void
#else
namespace {
inline void
#endif
SplitRectangle(RECT *parent, BOOL horizontal, double ratio, RECT *first,
               RECT *second) {
   if ((parent == nullptr) || (first == nullptr) || (second == nullptr)) {
      Fatal("Invalid RECT argument in SplitRectangle");
   }

   *first = *parent;
   *second = *parent;

   int split_point;
   if (horizontal == TRUE) {
      // Left/right split
      split_point =
          parent->left + (int)((parent->right - parent->left) * ratio);
      first->right = split_point;
      second->left = split_point;
   } else {
      // Top/bottom split
      split_point = parent->top + (int)((parent->bottom - parent->top) * ratio);
      first->bottom = split_point;
      second->top = split_point;
   }
}
#ifndef BFWM_BUILD_TESTING
}
#endif

namespace {

inline void ReplaceNode(DwindleLayout *layout, DwindleNode *old_node,
                        DwindleNode *new_node) {
   // Case 1: old_node is the root
   if (layout->root == old_node) {
      layout->root = new_node;
      return;
   }

   // Case 2: old_node is a child of some parent
   DwindleNode *parent = FindParent(layout->root, old_node);
   if (parent != nullptr) {
      if (parent->first == old_node) {
         parent->first = new_node;
      } else if (parent->second == old_node) {
         parent->second = new_node;
      }
   } else {
      Error("No parent for node: %p", old_node);
   }
}

inline void RecalculatePositions(DwindleNode *node) {
   if ((node == nullptr) || (node->is_leaf == TRUE))
      return;

   // Split this node's rectangle between children
   RECT left_rect;
   RECT right_rect;
   SplitRectangle(&node->rect, node->split_horizontal, node->split_ratio,
                  &left_rect, &right_rect);

   node->first->rect = left_rect;
   node->second->rect = right_rect;

   // Recurse down the tree
   RecalculatePositions(node->first);
   RecalculatePositions(node->second);
}

inline auto UpdateDwindleAncestorRatio(DwindleNode *parent, HWND hwnd,
                                       RECT new_rect, BOOL *width_changed,
                                       BOOL *height_changed, int inset_left,
                                       int inset_top, int inset_right,
                                       int inset_bottom) -> BOOL {
   BOOL match = FALSE;
   if (((*width_changed) == TRUE) && (parent->split_horizontal == TRUE)) {
      match = TRUE;
      *width_changed = FALSE;
   } else if (((*height_changed) == TRUE) &&
              (parent->split_horizontal == FALSE)) {
      match = TRUE;
      *height_changed = FALSE;
   }
   if (match == FALSE)
      return FALSE;

   BOOL const leaf_in_first =
       static_cast<BOOL>(FindNodeByWindow(parent->first, hwnd) != nullptr);

   if (parent->split_horizontal == TRUE) {
      double const container_w = parent->rect.right - parent->rect.left;
      if (container_w <= 0.0)
         return FALSE;
      /* The split line sits at the leaf's sibling-facing edge; that side's
       * frame inset separates the requested frame edge from the node edge the
       * ratio is computed from. */
      int const inset = (leaf_in_first == TRUE) ? inset_right : inset_left;
      double const split_pos =
          (leaf_in_first == TRUE)
              ? (double)(new_rect.right + inset - parent->rect.left)
              : (double)(new_rect.left - inset - parent->rect.left);
      parent->split_ratio = split_pos / container_w;
   } else {
      double const container_h = parent->rect.bottom - parent->rect.top;
      if (container_h <= 0.0)
         return FALSE;
      int const inset = (leaf_in_first == TRUE) ? inset_bottom : inset_top;
      double const split_pos =
          (leaf_in_first == TRUE)
              ? (double)(new_rect.bottom + inset - parent->rect.top)
              : (double)(new_rect.top - inset - parent->rect.top);
      parent->split_ratio = split_pos / container_h;
   }

   if (parent->split_ratio < SPLIT_RATIO_MIN)
      parent->split_ratio = SPLIT_RATIO_MIN;
   if (parent->split_ratio > SPLIT_RATIO_MAX)
      parent->split_ratio = SPLIT_RATIO_MAX;
   return TRUE;
}

} // namespace

void DwindleRecalculate(DwindleLayout *layout) {
   if (layout == nullptr)
      return;
   if (layout->root != nullptr)
      layout->root->rect = layout->workspace_rect;
   RecalculatePositions(layout->root);
}

auto DwindleInsertWindow(DwindleLayout *layout, HWND new_window,
                         HWND focus_window) -> bool {
   if (layout == nullptr)
      return false;

   // If the window is already in the tree, remove it first and fall
   // through to the normal insert. This dedups (no second leaf) and
   // reconciles (the window is re-inserted exactly once at the focus
   // position). Without this, a duplicate insert leaves a stale node
   // whose space is never filled after re-tiling — a permanent gap.
   // DwindleRemoveWindow decrements window_count and recalculates
   // positions; the insert below increments it back.
   if (FindNodeByWindow(layout->root, new_window) != nullptr) {
      DwindleRemoveWindow(layout, new_window);
   }

   // special case for first window
   if (layout->window_count == 0) {
      layout->root = CreateLeafNode(new_window, layout->workspace_rect);
      layout->window_count = 1;
      return true;
   }

   DwindleNode *focus_node = FindNodeByWindow(layout->root, focus_window);
   if (focus_node == nullptr) {
      // Hint window not in tree — split the last leaf (rightmost/
      // bottommost in the tree) so the new window lands at the edge
      // rather than unpredictably inside the layout.
      DwindleNode *last = FindLastLeaf(layout->root);
      if (last == nullptr)
         return false;
      focus_node = last;
   }

   int const width = focus_node->rect.right - focus_node->rect.left;
   int const height = focus_node->rect.bottom - focus_node->rect.top;
   BOOL const horz = static_cast<BOOL>(width > height);

   DwindleNode *parent =
       CreateContainerNode(focus_node->rect, horz, layout->split_ratio);
   if (parent == nullptr)
      return false;

   RECT left_rect;
   RECT right_rect;
   SplitRectangle(&parent->rect, horz, layout->split_ratio, &left_rect,
                  &right_rect);

   parent->first = focus_node;
   parent->first->rect = left_rect;
   parent->second = CreateLeafNode(new_window, right_rect);
   if (parent->second == nullptr) {
      free(parent);
      return false;
   }

   ReplaceNode(layout, focus_node, parent);

   layout->window_count++;
   return true;
}

auto DwindleRemoveWindow(DwindleLayout *layout, HWND window) -> bool {
   if (layout == nullptr)
      return false;

   DwindleNode *node = FindNodeByWindow(layout->root, window);
   if ((node == nullptr) || (node->is_leaf == FALSE))
      return false;

   DwindleNode *parent = FindParent(layout->root, node);
   if (parent == nullptr) {
      // Last window - just free it
      free(layout->root);
      layout->root = nullptr;
      layout->window_count = 0;
      return true;
   }

   // Determine which child is being removed and which survives
   DwindleNode *survivor =
       (parent->first == node) ? parent->second : parent->first;

   // The survivor takes the full space of the container
   survivor->rect = parent->rect;

   // Replace parent with survivor in the tree
   ReplaceNode(layout, parent, survivor);

   // The survivor now occupies a rect of different dimensions than
   // before (it inherited the parent's rect).  Re-evaluate its split
   // orientation so the subtree is laid out sensibly under the new
   // aspect ratio — without this a container promoted from a tall
   // subspace into a wide one keeps its old vertical split, producing
   // short-wide strips instead of a normal dwindle arrangement.
   if (survivor->is_leaf == FALSE) {
      int const w = survivor->rect.right - survivor->rect.left;
      int const h = survivor->rect.bottom - survivor->rect.top;
      survivor->split_horizontal = static_cast<BOOL>(w > h);
   }

   // Clean up - free the removed window's node AND the parent container
   parent->first = nullptr;
   parent->second = nullptr;
   free(node);
   free(parent);
   layout->window_count--;

   // Recalculate positions
   RecalculatePositions(layout->root);
   return true;
}

auto DwindleGetNeighbor(DwindleLayout *layout, HWND hwnd,
                        BFWMDirection direction) -> HWND {
   if (layout == nullptr)
      return nullptr;

   DwindleNode *source = FindNodeByWindow(layout->root, hwnd);
   if (source == nullptr)
      return nullptr;

   std::array<DwindleNode *, LEAVES_MAX> leaves = {};
   int const count = CollectLeafNodes(layout->root, leaves.data(), LEAVES_MAX);

   RECT const src = source->rect;
   HWND best = nullptr;
   int best_dist = INT_MAX;

   for (int i = 0; i < count; i++) {
      if (leaves[i] == source)
         continue;

      int dist;
      if ((RectIsNeighbor(src, leaves[i]->rect, direction, &dist) == TRUE) &&
          dist < best_dist) {
         best_dist = dist;
         best = leaves[i]->hwnd;
      }
   }

   return best;
}

auto DwindleGetClosestWindow(DwindleLayout *layout, HWND reference) -> HWND {
   if ((layout == nullptr) || layout->window_count == 0 ||
       (layout->root == nullptr))
      return nullptr;

   // If reference is non-nullptr and exists in tree, find closest by
   // Euclidean center-to-center distance.
   if (reference != nullptr) {
      DwindleNode *ref_node = FindNodeByWindow(layout->root, reference);
      if (ref_node != nullptr) {
         std::array<DwindleNode *, LEAVES_MAX> leaves = {};
         int const count =
             CollectLeafNodes(layout->root, leaves.data(), LEAVES_MAX);

         int const cx1 = (ref_node->rect.left + ref_node->rect.right) / 2;
         int const cy1 = (ref_node->rect.top + ref_node->rect.bottom) / 2;

         HWND best = nullptr;
         int best_dist_sq = INT_MAX;

         for (int i = 0; i < count; i++) {
            if (leaves[i] == ref_node)
               continue;

            int const cx2 = (leaves[i]->rect.left + leaves[i]->rect.right) / 2;
            int const cy2 = (leaves[i]->rect.top + leaves[i]->rect.bottom) / 2;
            int const delta_x = cx2 - cx1;
            int const delta_y = cy2 - cy1;
            int const dist_sq = (delta_x * delta_x) + (delta_y * delta_y);

            if (dist_sq < best_dist_sq) {
               best_dist_sq = dist_sq;
               best = leaves[i]->hwnd;
            }
         }

         return best;
      }
   }

   // Fallback: reference was nullptr or not found in the tree
   DwindleNode *leaf = FindFirstLeaf(layout->root);
   return (leaf != nullptr) ? leaf->hwnd : nullptr;
}

auto DwindleResizeWindow(DwindleLayout *layout, HWND hwnd,
                         BFWMDirection direction, int pixels) -> bool {
   if ((layout == nullptr) || (layout->root == nullptr))
      return false;

   DwindleNode *leaf = FindNodeByWindow(layout->root, hwnd);
   if ((leaf == nullptr) || (leaf->is_leaf == 0))
      return false;

   // Walk up the tree to find the nearest ancestor with matching split
   DwindleNode *node = leaf;
   DwindleNode *parent = FindParent(layout->root, node);

   while (parent != nullptr) {
      BOOL match = FALSE;
      if (((direction == DirLeft || direction == DirRight) &&
           (parent->split_horizontal == TRUE)) ||
          ((direction == DirUp || direction == DirDown) &&
           (parent->split_horizontal == FALSE))) {
         match = TRUE;
      }

      if (match == TRUE) {
         double const total_dim =
             (parent->split_horizontal != 0)
                 ? (double)(parent->rect.right - parent->rect.left)
                 : (double)(parent->rect.bottom - parent->rect.top);

         if (total_dim <= 0.0)
            return false;

         // Convert pixel delta to a ratio delta
         double const delta = (double)pixels / total_dim;

         // Move the split line in the direction pressed.
         // DirRight/DirDown increase the ratio (split line moves right/down).
         // DirLeft/DirUp decrease the ratio (split line moves left/up).
         // The focused window grows if it is on the expanding side of the
         // split and shrinks if it is on the contracting side.
         switch (direction) {
         case DirRight:
         case DirDown:
            parent->split_ratio += delta;
            break;
         case DirLeft:
         case DirUp:
            parent->split_ratio -= delta;
            break;
         default:
            return false;
         }

         if (parent->split_ratio < SPLIT_RATIO_MIN)
            parent->split_ratio = SPLIT_RATIO_MIN;
         if (parent->split_ratio > SPLIT_RATIO_MAX)
            parent->split_ratio = SPLIT_RATIO_MAX;

         RecalculatePositions(layout->root);
         return true;
      }

      node = parent;
      parent = FindParent(layout->root, node);
   }

   return false;
}

auto DwindleResizeWindowToRect(DwindleLayout *layout, HWND hwnd, RECT new_rect)
    -> bool {
   if ((layout == nullptr) || (layout->root == nullptr))
      return false;

   DwindleNode *leaf = FindNodeByWindow(layout->root, hwnd);
   if ((leaf == nullptr) || (leaf->is_leaf == FALSE))
      return false;

   int inset_left;
   int inset_top;
   int inset_right;
   int inset_bottom;
   WindowSideInsets(layout, leaf, &inset_left, &inset_top, &inset_right,
                    &inset_bottom);
   int const tiled_w =
       (leaf->rect.right - leaf->rect.left) - inset_left - inset_right;
   int const tiled_h =
       (leaf->rect.bottom - leaf->rect.top) - inset_top - inset_bottom;
   int const new_w = new_rect.right - new_rect.left;
   int const new_h = new_rect.bottom - new_rect.top;

   BOOL width_changed = static_cast<BOOL>(abs(new_w - tiled_w) > 2);
   BOOL height_changed = static_cast<BOOL>(abs(new_h - tiled_h) > 2);

   if ((width_changed == FALSE) && (height_changed == FALSE))
      return false;

   DwindleNode *node = leaf;
   DwindleNode *parent = FindParent(layout->root, node);
   BOOL updated = FALSE;

   while (parent != nullptr) {
      if (UpdateDwindleAncestorRatio(parent, hwnd, new_rect, &width_changed,
                                     &height_changed, inset_left, inset_top,
                                     inset_right, inset_bottom) == TRUE) {
         updated = TRUE;
         if ((width_changed == FALSE) && (height_changed == FALSE))
            break;
      }
      node = parent;
      parent = FindParent(layout->root, node);
   }

   if (updated == TRUE) {
      RecalculatePositions(layout->root);
      return true;
   }
   return false;
}

auto DwindleMoveWindow(DwindleLayout *layout, HWND hwnd,
                       BFWMDirection direction) -> bool {
   if ((layout == nullptr) || (layout->root == nullptr))
      return false;

   HWND neighbor = DwindleGetNeighbor(layout, hwnd, direction);
   if (neighbor != nullptr) {
      DwindleNode *node_a = FindNodeByWindow(layout->root, hwnd);
      DwindleNode *node_b = FindNodeByWindow(layout->root, neighbor);
      if ((node_a == nullptr) || (node_b == nullptr))
         return false;

      DwindleNode *parent_a = FindParent(layout->root, node_a);
      DwindleNode *parent_b = FindParent(layout->root, node_b);

      if (parent_a != nullptr && parent_a == parent_b) {
         // Same parent: swap HWNDs but keep each window's screen dimensions.
         HWND temp = node_a->hwnd;
         node_a->hwnd = node_b->hwnd;
         node_b->hwnd = temp;

         int const second_size =
             (parent_a->split_horizontal == TRUE)
                 ? (parent_a->second->rect.right - parent_a->second->rect.left)
                 : (parent_a->second->rect.bottom - parent_a->second->rect.top);
         int const total = (parent_a->split_horizontal == TRUE)
                               ? (parent_a->rect.right - parent_a->rect.left)
                               : (parent_a->rect.bottom - parent_a->rect.top);
         parent_a->split_ratio = (double)second_size / (double)total;

         RecalculatePositions(layout->root);
         return true;
      }

      // Different parents: remove from current position and re-insert
      // at the neighbor's position, splitting the neighbor's space.
      // This reshapes the tree topology with each cross-branch move.
      if (!DwindleRemoveWindow(layout, hwnd)) {
         Error("DwindleMoveWindow: failed to remove window %p", hwnd);
         return false;
      }

      if (!DwindleInsertWindow(layout, hwnd, neighbor)) {
         Error("DwindleMoveWindow: remove ok but re-insert at %p failed",
               neighbor);
         return false;
      }

      return true;
   }

   // No geometric neighbor — try restructuring the parent split so the
   // window can move in an orthogonal direction.  If the movement is
   // parallel to the parent's split it is a genuine edge → workspace move.
   DwindleNode *node = FindNodeByWindow(layout->root, hwnd);
   if ((node == nullptr) || (node->is_leaf == FALSE))
      return false;

   DwindleNode *parent = FindParent(layout->root, node);
   if (parent == nullptr)
      return false;

   BOOL const dir_horz =
       static_cast<BOOL>(direction == DirLeft || direction == DirRight);
   if (dir_horz == parent->split_horizontal)
      return false; // parallel direction → genuine edge

   // Restructure: toggle parent's split, put the moving window on the
   // leading edge, and toggle the sibling subtree's split as well.
   parent->split_horizontal =
       static_cast<BOOL>(parent->split_horizontal == FALSE);

   BOOL const go_first =
       static_cast<BOOL>(direction == DirLeft || direction == DirUp);
   BOOL const node_is_first = static_cast<BOOL>(parent->first == node);
   if (go_first != node_is_first) {
      DwindleNode *tmp = parent->first;
      parent->first = parent->second;
      parent->second = tmp;
   }

   DwindleNode *sibling =
       (parent->first == node) ? parent->second : parent->first;
   if ((sibling != nullptr) && (sibling->is_leaf == FALSE)) {
      sibling->split_horizontal =
          static_cast<BOOL>(sibling->split_horizontal == FALSE);
      if (go_first == FALSE) {
         DwindleNode *tmp = sibling->first;
         sibling->first = sibling->second;
         sibling->second = tmp;
      }
   }

   RecalculatePositions(layout->root);
   return true;
}

auto DwindleToggleSplit(DwindleLayout *layout, HWND hwnd) -> bool {
   if ((layout == nullptr) || (layout->root == nullptr))
      return false;

   DwindleNode *node = FindNodeByWindow(layout->root, hwnd);
   if ((node == nullptr) || (node->is_leaf == FALSE))
      return false;

   DwindleNode *parent = FindParent(layout->root, node);
   if (parent == nullptr)
      return false;

   parent->split_horizontal =
       static_cast<BOOL>(parent->split_horizontal == FALSE);
   RecalculatePositions(layout->root);
   return true;
}

auto DwindleSwapSplit(DwindleLayout *layout, HWND hwnd) -> bool {
   if ((layout == nullptr) || (layout->root == nullptr))
      return false;

   DwindleNode *node = FindNodeByWindow(layout->root, hwnd);
   if ((node == nullptr) || (node->is_leaf == 0))
      return false;

   DwindleNode *parent = FindParent(layout->root, node);
   if (parent == nullptr)
      return false;

   DwindleNode *tmp = parent->first;
   parent->first = parent->second;
   parent->second = tmp;
   RecalculatePositions(layout->root);
   return true;
}

void DwindleLayoutFree(DwindleLayout *layout) {
   if (layout == nullptr)
      return;

   FreeTree(layout->root);

   free(layout);
   layout = nullptr;
}

void DwindleLayoutSetGaps(DwindleLayout *layout, int gap_between,
                          int gap_edge) {
   if (layout == nullptr)
      return;
   layout->gap_between = gap_between;
   layout->gap_edge = gap_edge;
}

void DwindleLayoutApplyConfig(DwindleLayout *dwindle_layout,
                              LayoutConfig config) {
   if (dwindle_layout == nullptr)
      return;
   dwindle_layout->gap_between = config.gap_between;
   dwindle_layout->gap_edge = config.gap_edge;
   dwindle_layout->border_width =
       config.border_width > 0 ? config.border_width : 0;
   RECT r = config.workspace_rect;
   r.left += dwindle_layout->gap_edge;
   r.top += dwindle_layout->gap_edge;
   r.right -= dwindle_layout->gap_edge;
   r.bottom -= dwindle_layout->gap_edge;
   dwindle_layout->workspace_rect = r;
   DwindleRecalculate(dwindle_layout);
}
