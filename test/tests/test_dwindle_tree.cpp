#include "cbelt.h"
#include <windows.h>

#include "../../src/workspace/layouts/dwindle/dwindle_tree.h"

CBELT_GROUP("dwindle_tree")

/* =========================================================================
 * CreateLeafNode
 * ========================================================================= */

CBELT_TEST(create_leaf_node) {
   RECT rect = {.left = 10, .top = 20, .right = 100, .bottom = 200};
   DwindleNode *node = CreateLeafNode(reinterpret_cast<HWND>(42), rect);

   cbelt_assert_not_null(node);
   cbelt_assert_true(reinterpret_cast<HWND>(42) == node->hwnd);
   cbelt_assert_true(node->is_leaf);
   cbelt_assert_null(node->first);
   cbelt_assert_null(node->second);
   cbelt_assert_true(10 == node->rect.left);
   cbelt_assert_true(20 == node->rect.top);
   cbelt_assert_true(100 == node->rect.right);
   cbelt_assert_true(200 == node->rect.bottom);

   FreeTree(node);
   return TEST_SUCCESS;
}

/* =========================================================================
 * CreateContainerNode
 * ========================================================================= */

CBELT_TEST(create_container_node_horizontal) {
   RECT rect = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   DwindleNode *node = CreateContainerNode(rect, TRUE, 0.5);

   cbelt_assert_not_null(node);
   cbelt_assert_false(node->is_leaf);
   cbelt_assert_true(node->split_horizontal);
   cbelt_assert_true(0.5 == node->split_ratio);
   cbelt_assert_null(node->first);
   cbelt_assert_null(node->second);
   cbelt_assert_true(0 == node->rect.left);
   cbelt_assert_true(1920 == node->rect.right);

   FreeTree(node);
   return TEST_SUCCESS;
}

CBELT_TEST(create_container_node_vertical) {
   RECT rect = {.left = 0, .top = 0, .right = 800, .bottom = 600};
   DwindleNode *node = CreateContainerNode(rect, FALSE, 0.25);

   cbelt_assert_not_null(node);
   cbelt_assert_false(node->is_leaf);
   cbelt_assert_false(node->split_horizontal);
   cbelt_assert_true(0.25 == node->split_ratio);

   FreeTree(node);
   return TEST_SUCCESS;
}

/* =========================================================================
 * FreeTree — null safety
 * ========================================================================= */

CBELT_TEST(free_tree_null) {
   FreeTree(nullptr);
   return TEST_SUCCESS;
}

/* =========================================================================
 * FindNodeByWindow — various cases
 * ========================================================================= */

CBELT_TEST(find_node_single_leaf) {
   RECT rect = {.left = 0, .top = 0, .right = 100, .bottom = 100};
   DwindleNode *root = CreateLeafNode(reinterpret_cast<HWND>(1), rect);

   DwindleNode *found = FindNodeByWindow(root, reinterpret_cast<HWND>(1));
   cbelt_assert_not_null(found);
   cbelt_assert_true(root == found);
   cbelt_assert_true(reinterpret_cast<HWND>(1) == found->hwnd);

   /* Not found */
   found = FindNodeByWindow(root, reinterpret_cast<HWND>(999));
   cbelt_assert_null(found);

   FreeTree(root);
   return TEST_SUCCESS;
}

CBELT_TEST(find_node_in_container) {
   /* Build a small tree: container(leaf(1), leaf(2)) */
   RECT r1 = {.left = 0, .top = 0, .right = 100, .bottom = 100};
   RECT r2 = {.left = 100, .top = 0, .right = 200, .bottom = 100};
   RECT rc = {.left = 0, .top = 0, .right = 200, .bottom = 100};

   DwindleNode *leaf1 = CreateLeafNode(reinterpret_cast<HWND>(1), r1);
   DwindleNode *leaf2 = CreateLeafNode(reinterpret_cast<HWND>(2), r2);
   DwindleNode *root = CreateContainerNode(rc, TRUE, 0.5);
   root->first = leaf1;
   root->second = leaf2;

   DwindleNode *found1 = FindNodeByWindow(root, reinterpret_cast<HWND>(1));
   DwindleNode *found2 = FindNodeByWindow(root, reinterpret_cast<HWND>(2));
   cbelt_assert_true(leaf1 == found1);
   cbelt_assert_true(leaf2 == found2);

   /* Not found */
   cbelt_assert_null(FindNodeByWindow(root, reinterpret_cast<HWND>(3)));

   FreeTree(root);
   return TEST_SUCCESS;
}

CBELT_TEST(find_node_deep_tree) {
   /* Build a deeper tree: container(leaf(1), container(leaf(2), leaf(3))) */
   RECT rc_root = {.left = 0, .top = 0, .right = 200, .bottom = 100};
   RECT rc_right = {.left = 100, .top = 0, .right = 200, .bottom = 100};

   DwindleNode *leaf1 = CreateLeafNode(reinterpret_cast<HWND>(1), rc_root);
   DwindleNode *leaf2 = CreateLeafNode(reinterpret_cast<HWND>(2), rc_right);
   DwindleNode *leaf3 = CreateLeafNode(reinterpret_cast<HWND>(3), rc_right);
   DwindleNode *right = CreateContainerNode(rc_right, FALSE, 0.5);
   right->first = leaf2;
   right->second = leaf3;
   DwindleNode *root = CreateContainerNode(rc_root, TRUE, 0.5);
   root->first = leaf1;
   root->second = right;

   cbelt_assert_true(leaf1 ==
                     FindNodeByWindow(root, reinterpret_cast<HWND>(1)));
   cbelt_assert_true(leaf2 ==
                     FindNodeByWindow(root, reinterpret_cast<HWND>(2)));
   cbelt_assert_true(leaf3 ==
                     FindNodeByWindow(root, reinterpret_cast<HWND>(3)));
   cbelt_assert_null(FindNodeByWindow(root, reinterpret_cast<HWND>(4)));

   FreeTree(root);
   return TEST_SUCCESS;
}

CBELT_TEST(find_node_null_root) {
   cbelt_assert_null(FindNodeByWindow(nullptr, reinterpret_cast<HWND>(1)));
   return TEST_SUCCESS;
}

/* =========================================================================
 * FindParent — various cases
 * ========================================================================= */

CBELT_TEST(find_parent_null_root) {
   DwindleNode *child =
       CreateLeafNode(reinterpret_cast<HWND>(1),
                      RECT{.left = 0, .top = 0, .right = 0, .bottom = 0});
   cbelt_assert_null(FindParent(nullptr, child));
   FreeTree(child);
   return TEST_SUCCESS;
}

CBELT_TEST(find_parent_single_leaf) {
   DwindleNode *root =
       CreateLeafNode(reinterpret_cast<HWND>(1),
                      RECT{.left = 0, .top = 0, .right = 0, .bottom = 0});
   /* No parent for a single-node tree */
   cbelt_assert_null(FindParent(root, root));
   FreeTree(root);
   return TEST_SUCCESS;
}

CBELT_TEST(find_parent_two_levels) {
   RECT rc = {.left = 0, .top = 0, .right = 200, .bottom = 100};
   DwindleNode *leaf1 = CreateLeafNode(reinterpret_cast<HWND>(1), rc);
   DwindleNode *leaf2 = CreateLeafNode(reinterpret_cast<HWND>(2), rc);
   DwindleNode *root = CreateContainerNode(rc, TRUE, 0.5);
   root->first = leaf1;
   root->second = leaf2;

   DwindleNode *parent1 = FindParent(root, leaf1);
   DwindleNode *parent2 = FindParent(root, leaf2);
   cbelt_assert_true(root == parent1);
   cbelt_assert_true(root == parent2);

   /* Root has no parent */
   cbelt_assert_null(FindParent(root, root));

   FreeTree(root);
   return TEST_SUCCESS;
}

CBELT_TEST(find_parent_deep_tree) {
   RECT rc = {.left = 0, .top = 0, .right = 200, .bottom = 100};
   DwindleNode *leaf1 = CreateLeafNode(reinterpret_cast<HWND>(1), rc);
   DwindleNode *leaf2 = CreateLeafNode(reinterpret_cast<HWND>(2), rc);
   DwindleNode *leaf3 = CreateLeafNode(reinterpret_cast<HWND>(3), rc);
   DwindleNode *right = CreateContainerNode(rc, FALSE, 0.5);
   right->first = leaf2;
   right->second = leaf3;
   DwindleNode *root = CreateContainerNode(rc, TRUE, 0.5);
   root->first = leaf1;
   root->second = right;

   cbelt_assert_true(root == FindParent(root, leaf1));
   cbelt_assert_true(root == FindParent(root, right));
   cbelt_assert_true(right == FindParent(root, leaf2));
   cbelt_assert_true(right == FindParent(root, leaf3));
   cbelt_assert_null(FindParent(root, root));

   FreeTree(root);
   return TEST_SUCCESS;
}

CBELT_TEST(find_parent_unrelated_node) {
   RECT rc = {.left = 0, .top = 0, .right = 100, .bottom = 100};
   DwindleNode *leaf1 = CreateLeafNode(reinterpret_cast<HWND>(1), rc);
   DwindleNode *leaf2 = CreateLeafNode(reinterpret_cast<HWND>(2), rc);
   DwindleNode *root = CreateContainerNode(rc, TRUE, 0.5);
   root->first = leaf1;
   root->second = leaf2;

   /* A node not in the tree */
   DwindleNode *orphan = CreateLeafNode(reinterpret_cast<HWND>(99), rc);
   cbelt_assert_null(FindParent(root, orphan));

   FreeTree(root);
   FreeTree(orphan);
   return TEST_SUCCESS;
}

/* =========================================================================
 * FreeTree — recursive free integrity
 * ========================================================================= */

CBELT_TEST(free_tree_full_tree) {
   /* Build a tree, free it, then verify no crash */
   RECT rc = {.left = 0, .top = 0, .right = 400, .bottom = 300};
   DwindleNode *ll = CreateLeafNode(reinterpret_cast<HWND>(1), rc);
   DwindleNode *lr = CreateLeafNode(reinterpret_cast<HWND>(2), rc);
   DwindleNode *l = CreateContainerNode(rc, TRUE, 0.5);
   l->first = ll;
   l->second = lr;

   DwindleNode *rl = CreateLeafNode(reinterpret_cast<HWND>(3), rc);
   DwindleNode *rr = CreateLeafNode(reinterpret_cast<HWND>(4), rc);
   DwindleNode *r = CreateContainerNode(rc, FALSE, 0.5);
   r->first = rl;
   r->second = rr;

   DwindleNode *root = CreateContainerNode(rc, TRUE, 0.5);
   root->first = l;
   root->second = r;

   /* FreeTree should recursively free all 7 nodes without issue */
   FreeTree(root);
   return TEST_SUCCESS;
}
