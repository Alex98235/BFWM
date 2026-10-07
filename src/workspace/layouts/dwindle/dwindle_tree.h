/**
 * @file dwindle_tree.h
 * @brief Binary space partition (BSP) tree for the dwindle layout algorithm.
 *
 * Defines the tree data structure used to partition screen space among
 * tiled windows. Supports leaf nodes (windows) and container nodes (splits).
 */

#ifndef BFWM_DWINDLE_TREE
#define BFWM_DWINDLE_TREE

#include "windef.h"

// Binary Space Partition (BSP)
using DwindleNode = struct DwindleNode {
   HWND hwnd;
   BOOL is_leaf;

   BOOL split_horizontal; // TRUE = left/right, FALSE = top/bottom
   double split_ratio;    // Position of split (0.0 to 1.0)

   struct DwindleNode *first;  // top-left child
   struct DwindleNode *second; // bottom-right

   RECT rect;
};

/**
 * @brief Create a Leaf Node object. This represents a single window.
 *
 * @param hwnd          A window handle
 * @param rect          The position and size on screen for this window
 * @return DwindleNode* The leaf node
 */
auto CreateLeafNode(HWND hwnd, RECT rect) -> DwindleNode *;

/**
 * @brief Create a Container Node object. This represents an area on the screen
 * to be further divided.
 *
 * @param rect          The position and size on screen for this node
 * @param horizontal    TRUE if this node should split windows horizontally,
 *                      FALSE if this should split vertically
 * @param ratio         The ratio between the left/top and right/bottom nodes
 *                      when split (in the larger dimension)
 * @return DwindleNode* The container node
 */
auto CreateContainerNode(RECT rect, BOOL horizontal, double ratio)
    -> DwindleNode *;

/**
 * @brief Find an existing dwindle node in the tree with the given window
 * handle.
 *
 * @param root          The root node of the tree
 * @param hwnd          A window handle
 * @return DwindleNode* The node if found, else null
 */
auto FindNodeByWindow(DwindleNode *root, HWND hwnd) -> DwindleNode *;

/**
 * @brief Find the parent node of this child node.
 *
 * @param root          The root node of the tree
 * @param child         The child whose parent to find
 * @return DwindleNode* The node if found, else null
 */
auto FindParent(DwindleNode *root, DwindleNode *child) -> DwindleNode *;

/**
 * @brief Find the first leaf (preferring the first branch) of the given tree.
 *
 * @param root  The root node of the tree
 * @return DwindleNode* The first leaf node, or NULL
 */
auto FindFirstLeaf(DwindleNode *root) -> DwindleNode *;

/**
 * @brief Find the last leaf (preferring the second branch) of the given tree.
 *
 * @param root  The root node of the tree
 * @return DwindleNode* The last leaf node, or NULL
 */
auto FindLastLeaf(DwindleNode *root) -> DwindleNode *;

/**
 * @brief Recursively collect all leaf (window) nodes into an array.
 *
 * @param root      The root node of the tree
 * @param out       Output array to hold collected leaf pointers
 * @param max_count Maximum number of leaves to collect (array capacity)
 * @return int      Number of leaves collected
 */
auto CollectLeafNodes(DwindleNode *root, DwindleNode **out, int max_count)
    -> int;

/**
 * @brief Deallocate the tree
 *
 * @param root The root node of the tree
 */
void FreeTree(DwindleNode *root);

#endif
