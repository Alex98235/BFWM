#include "dwindle_tree.h"
#include "../../../memory/safe.h"
#include <cstdlib>
#include <minwindef.h>
#include <windef.h>

auto CreateLeafNode(HWND hwnd, RECT rect) -> DwindleNode * {
   auto *node = (DwindleNode *)safe_calloc(
       1, sizeof(DwindleNode), "Could not allocate space for dwindle node");

   node->hwnd = hwnd;
   node->rect = rect;
   node->is_leaf = TRUE;

   return node;
}

auto CreateContainerNode(RECT rect, BOOL horizontal, double ratio)
    -> DwindleNode * {
   auto *node = (DwindleNode *)safe_calloc(
       1, sizeof(DwindleNode), "Could not allocate space for new dwindle node");

   node->is_leaf = FALSE;
   node->rect = rect;
   node->split_horizontal = horizontal;
   node->split_ratio = ratio;

   return node;
}

auto FindNodeByWindow(DwindleNode *root, HWND hwnd) -> DwindleNode * {
   if (root == nullptr)
      return nullptr;

   if (root->hwnd == hwnd)
      return root;

   DwindleNode *child_node = nullptr;

   if (root->first != nullptr)
      child_node = FindNodeByWindow(root->first, hwnd);

   if (child_node == nullptr) {
      if (root->second != nullptr) {
         return FindNodeByWindow(root->second, hwnd);
      }
      return nullptr;
   }

   return child_node;
}

auto FindParent(DwindleNode *root, DwindleNode *child) -> DwindleNode * {
   if (root == nullptr)
      return nullptr;

   if (root->first == child || root->second == child)
      return root;

   DwindleNode *child_node = nullptr;

   if (root->first != nullptr)
      child_node = FindParent(root->first, child);

   if (child_node == nullptr) {
      if (root->second != nullptr) {
         return FindParent(root->second, child);
      }
      return nullptr;
   }

   return child_node;
}

auto FindFirstLeaf(DwindleNode *root) -> DwindleNode * {
   if (root == nullptr)
      return nullptr;
   if (root->is_leaf == TRUE)
      return root;
   DwindleNode *found = FindFirstLeaf(root->first);
   if (found != nullptr)
      return found;
   return FindFirstLeaf(root->second);
}

auto FindLastLeaf(DwindleNode *root) -> DwindleNode * {
   if (root == nullptr)
      return nullptr;
   if (root->is_leaf == TRUE)
      return root;
   DwindleNode *found = FindLastLeaf(root->second);
   if (found != nullptr)
      return found;
   return FindLastLeaf(root->first);
}

auto CollectLeafNodes(DwindleNode *root, DwindleNode **out, int max_count)
    -> int {
   if ((root == nullptr) || (out == nullptr))
      return 0;

   int collected = 0;

   if (root->is_leaf == TRUE) {
      if (max_count > 0) {
         out[0] = root;
         return 1;
      }
      return 0;
   }

   // Recurse into children
   if (root->first != nullptr) {
      collected +=
          CollectLeafNodes(root->first, out + collected, max_count - collected);
   }

   if ((root->second != nullptr) && collected < max_count) {
      collected += CollectLeafNodes(root->second, out + collected,
                                    max_count - collected);
   }

   return collected;
}

void FreeTree(DwindleNode *root) {
   if (root == nullptr)
      return;

   FreeTree(root->first);
   FreeTree(root->second);

   free(root);
   root = nullptr;
}
