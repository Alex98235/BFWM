#include "dwindle.h"
#include "../../../core/bfwm_def.h"
#include "../../../logging/logger.h"
#include "../../../math/rect.h"
#include "dwindle_tree.h"
#include <climits>
#include <cstdlib>
#include <minwindef.h>
#include <windef.h>

enum { LEAVES_MAX = 256 };
#define SPLIT_RATIO_MIN 0.1
#define SPLIT_RATIO_MAX 0.9

// ---------------------------------------------------------------------------
// File-scoped helpers
// ---------------------------------------------------------------------------
namespace {
/// Per-side frame inset for a leaf: border_width on every side, plus half the
/// between-gap on any side that faces a sibling.  Uses workspace_rect to
/// detect boundary edges.
void WindowSideInsetsImpl(const RECT &workspace_rect, int gap_between,
                          int border_width, const DwindleNode *node, int *left,
                          int *top, int *right, int *bottom) {
   int const half = gap_between / 2;
   int const border = border_width;
   *left = border + ((node->rect.left > workspace_rect.left) ? half : 0);
   *top = border + ((node->rect.top > workspace_rect.top) ? half : 0);
   *right = border + ((node->rect.right < workspace_rect.right) ? half : 0);
   *bottom = border + ((node->rect.bottom < workspace_rect.bottom) ? half : 0);
}
}

// ---------------------------------------------------------------------------
// SplitRectangle (exposed for testing)
// ---------------------------------------------------------------------------

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
      split_point = parent->left +
                    static_cast<int>((parent->right - parent->left) * ratio);
      first->right = split_point;
      second->left = split_point;
   } else {
      split_point = parent->top +
                    static_cast<int>((parent->bottom - parent->top) * ratio);
      first->bottom = split_point;
      second->top = split_point;
   }
}
#ifndef BFWM_BUILD_TESTING
}
#endif

// ---------------------------------------------------------------------------
// DwindleEngine
// ---------------------------------------------------------------------------

void DwindleEngine::RecalculatePositions(DwindleNode *node) {
   if ((node == nullptr) || (node->is_leaf == TRUE))
      return;

   RECT left_rect;
   RECT right_rect;
   SplitRectangle(&node->rect, node->split_horizontal, node->split_ratio,
                  &left_rect, &right_rect);

   node->first->rect = left_rect;
   node->second->rect = right_rect;

   RecalculatePositions(node->first);
   RecalculatePositions(node->second);
}

void DwindleEngine::ReplaceNode(DwindleNode *old_node, DwindleNode *new_node) {
   if (root == old_node) {
      root = new_node;
      return;
   }
   DwindleNode *parent = FindParent(root, old_node);
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

auto DwindleEngine::UpdateAncestorRatio(DwindleNode *parent, HWND hwnd,
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
      auto const container_w =
          static_cast<double>(parent->rect.right - parent->rect.left);
      if (container_w <= 0.0)
         return FALSE;
      int const inset = (leaf_in_first == TRUE) ? inset_right : inset_left;
      double const split_pos =
          (leaf_in_first == TRUE)
              ? static_cast<double>(new_rect.right + inset - parent->rect.left)
              : static_cast<double>(new_rect.left - inset - parent->rect.left);
      parent->split_ratio = split_pos / container_w;
   } else {
      auto const container_h =
          static_cast<double>(parent->rect.bottom - parent->rect.top);
      if (container_h <= 0.0)
         return FALSE;
      int const inset = (leaf_in_first == TRUE) ? inset_bottom : inset_top;
      double const split_pos =
          (leaf_in_first == TRUE)
              ? static_cast<double>(new_rect.bottom + inset - parent->rect.top)
              : static_cast<double>(new_rect.top - inset - parent->rect.top);
      parent->split_ratio = split_pos / container_h;
   }

   if (parent->split_ratio < SPLIT_RATIO_MIN)
      parent->split_ratio = SPLIT_RATIO_MIN;
   if (parent->split_ratio > SPLIT_RATIO_MAX)
      parent->split_ratio = SPLIT_RATIO_MAX;
   return TRUE;
}

auto DwindleEngine::insert(HWND new_window, HWND focus_window) -> bool {
   // If the window is already in the tree, remove it first and fall
   // through to the normal insert.
   if (FindNodeByWindow(root, new_window) != nullptr) {
      remove(new_window);
   }

   if (window_count == 0) {
      root = CreateLeafNode(new_window, workspace_rect);
      window_count = 1;
      return true;
   }

   DwindleNode *focus_node = FindNodeByWindow(root, focus_window);
   if (focus_node == nullptr) {
      DwindleNode *last = FindLastLeaf(root);
      if (last == nullptr)
         return false;
      focus_node = last;
   }

   int const width = focus_node->rect.right - focus_node->rect.left;
   int const height = focus_node->rect.bottom - focus_node->rect.top;
   BOOL const horz = static_cast<BOOL>(width > height);

   DwindleNode *parent =
       CreateContainerNode(focus_node->rect, horz, split_ratio);
   if (parent == nullptr)
      return false;

   RECT left_rect;
   RECT right_rect;
   SplitRectangle(&parent->rect, horz, split_ratio, &left_rect, &right_rect);

   parent->first = focus_node;
   parent->first->rect = left_rect;
   parent->second = CreateLeafNode(new_window, right_rect);
   if (parent->second == nullptr) {
      free(parent);
      return false;
   }

   ReplaceNode(focus_node, parent);
   window_count++;
   return true;
}

auto DwindleEngine::remove(HWND window) -> bool {
   DwindleNode *node = FindNodeByWindow(root, window);
   if ((node == nullptr) || (node->is_leaf == FALSE))
      return false;

   DwindleNode *parent = FindParent(root, node);
   if (parent == nullptr) {
      free(root);
      root = nullptr;
      window_count = 0;
      return true;
   }

   DwindleNode *survivor =
       (parent->first == node) ? parent->second : parent->first;
   survivor->rect = parent->rect;

   ReplaceNode(parent, survivor);

   if (survivor->is_leaf == FALSE) {
      int const w = survivor->rect.right - survivor->rect.left;
      int const h = survivor->rect.bottom - survivor->rect.top;
      survivor->split_horizontal = static_cast<BOOL>(w > h);
   }

   parent->first = nullptr;
   parent->second = nullptr;
   free(node);
   free(parent);
   window_count--;

   RecalculatePositions(root);
   return true;
}

auto DwindleEngine::get_rect(HWND hwnd, RECT *out_rect) -> bool {
   if (hwnd == nullptr)
      return false;

   DwindleNode *node = FindNodeByWindow(root, hwnd);
   if (node == nullptr) {
      Debug("No node with window handle: %p found", hwnd);
      return false;
   }

   *out_rect = node->rect;

   int left;
   int top;
   int right;
   int bottom;
   WindowSideInsetsImpl(workspace_rect, gap_between, border_width, node, &left,
                        &top, &right, &bottom);
   out_rect->left += left;
   out_rect->top += top;
   out_rect->right -= right;
   out_rect->bottom -= bottom;

   return true;
}

auto DwindleEngine::get_neighbor(HWND hwnd, BFWMDirection direction) -> HWND {
   DwindleNode *source = FindNodeByWindow(root, hwnd);
   if (source == nullptr)
      return nullptr;

   std::array<DwindleNode *, LEAVES_MAX> leaves = {};
   int const count = CollectLeafNodes(root, leaves.data(), LEAVES_MAX);

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

auto DwindleEngine::get_closest_window(HWND reference) -> HWND {
   if (window_count == 0 || root == nullptr)
      return nullptr;

   if (reference != nullptr) {
      DwindleNode *ref_node = FindNodeByWindow(root, reference);
      if (ref_node != nullptr) {
         std::array<DwindleNode *, LEAVES_MAX> leaves = {};
         int const count = CollectLeafNodes(root, leaves.data(), LEAVES_MAX);

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

   // Fallback
   DwindleNode *leaf = FindFirstLeaf(root);
   return (leaf != nullptr) ? leaf->hwnd : nullptr;
}

auto DwindleEngine::resize_window(HWND hwnd, BFWMDirection direction,
                                  int pixels) -> bool {
   if (root == nullptr)
      return false;

   DwindleNode *leaf = FindNodeByWindow(root, hwnd);
   if ((leaf == nullptr) || (leaf->is_leaf == 0))
      return false;

   DwindleNode *node = leaf;
   DwindleNode *parent = FindParent(root, node);

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
                 ? static_cast<double>(parent->rect.right - parent->rect.left)
                 : static_cast<double>(parent->rect.bottom - parent->rect.top);

         if (total_dim <= 0.0)
            return false;

         double const delta = static_cast<double>(pixels) / total_dim;

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

         RecalculatePositions(root);
         return true;
      }

      node = parent;
      parent = FindParent(root, node);
   }
   return false;
}

auto DwindleEngine::resize_window_to_rect(HWND hwnd, RECT new_rect) -> bool {
   if (root == nullptr)
      return false;

   DwindleNode *leaf = FindNodeByWindow(root, hwnd);
   if ((leaf == nullptr) || (leaf->is_leaf == FALSE))
      return false;

   int inset_left;
   int inset_top;
   int inset_right;
   int inset_bottom;
   WindowSideInsetsImpl(workspace_rect, gap_between, border_width, leaf,
                        &inset_left, &inset_top, &inset_right, &inset_bottom);
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
   DwindleNode *parent = FindParent(root, node);
   BOOL updated = FALSE;

   while (parent != nullptr) {
      if (UpdateAncestorRatio(parent, hwnd, new_rect, &width_changed,
                              &height_changed, inset_left, inset_top,
                              inset_right, inset_bottom) == TRUE) {
         updated = TRUE;
         if ((width_changed == FALSE) && (height_changed == FALSE))
            break;
      }
      node = parent;
      parent = FindParent(root, node);
   }

   if (updated == TRUE) {
      RecalculatePositions(root);
      return true;
   }
   return false;
}

auto DwindleEngine::move_window(HWND hwnd, BFWMDirection direction) -> bool {
   if (root == nullptr)
      return false;

   HWND neighbor = get_neighbor(hwnd, direction);
   if (neighbor != nullptr) {
      DwindleNode *node_a = FindNodeByWindow(root, hwnd);
      DwindleNode *node_b = FindNodeByWindow(root, neighbor);
      if ((node_a == nullptr) || (node_b == nullptr))
         return false;

      DwindleNode *parent_a = FindParent(root, node_a);
      DwindleNode *parent_b = FindParent(root, node_b);

      if (parent_a != nullptr && parent_a == parent_b) {
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
         parent_a->split_ratio = static_cast<double>(second_size) / total;

         RecalculatePositions(root);
         return true;
      }

      if (!remove(hwnd)) {
         Error("DwindleMoveWindow: failed to remove window %p", hwnd);
         return false;
      }
      if (!insert(hwnd, neighbor)) {
         Error("DwindleMoveWindow: remove ok but re-insert at %p failed",
               neighbor);
         return false;
      }
      return true;
   }

   DwindleNode *node = FindNodeByWindow(root, hwnd);
   if ((node == nullptr) || (node->is_leaf == FALSE))
      return false;

   DwindleNode *parent = FindParent(root, node);
   if (parent == nullptr)
      return false;

   BOOL const dir_horz =
       static_cast<BOOL>(direction == DirLeft || direction == DirRight);
   if (dir_horz == parent->split_horizontal)
      return false;

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

   RecalculatePositions(root);
   return true;
}

auto DwindleEngine::toggle_split(HWND hwnd) -> bool {
   if (root == nullptr)
      return false;

   DwindleNode *node = FindNodeByWindow(root, hwnd);
   if ((node == nullptr) || (node->is_leaf == FALSE))
      return false;

   DwindleNode *parent = FindParent(root, node);
   if (parent == nullptr)
      return false;

   parent->split_horizontal =
       static_cast<BOOL>(parent->split_horizontal == FALSE);
   RecalculatePositions(root);
   return true;
}

auto DwindleEngine::swap_split(HWND hwnd) -> bool {
   if (root == nullptr)
      return false;

   DwindleNode *node = FindNodeByWindow(root, hwnd);
   if ((node == nullptr) || (node->is_leaf == 0))
      return false;

   DwindleNode *parent = FindParent(root, node);
   if (parent == nullptr)
      return false;

   DwindleNode *tmp = parent->first;
   parent->first = parent->second;
   parent->second = tmp;
   RecalculatePositions(root);
   return true;
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

auto DwindleLayoutEngineCreate(RECT workspace_rect, int gap_between,
                               int gap_edge, int border_width)
    -> std::unique_ptr<LayoutEngine> {
   auto engine = std::make_unique<DwindleEngine>();
   LayoutConfig const cfg = {
       .gap_between = gap_between,
       .gap_edge = gap_edge,
       .border_width = border_width,
       .workspace_rect = workspace_rect,
   };
   engine->apply_config(cfg);
   return engine;
}
