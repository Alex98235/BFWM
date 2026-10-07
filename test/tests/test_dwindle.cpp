#include "cbelt.h"
#include <array>
#include <windef.h>
#include <windows.h>

/* Include the header after windef.h/windows.h for HWND/RECT types */
#include "../../src/workspace/layouts/dwindle/dwindle.h"

/* Helper for floating-point ratio comparisons */
#define ASSERT_RATIO(expected, actual)                                         \
   do {                                                                        \
      double _e = (expected), _a = (actual);                                   \
      double _diff = _e > _a ? _e - _a : _a - _e;                              \
      cbelt_assert(_diff < 0.0001);                                            \
   } while (0)

CBELT_GROUP("dwindle")

/* =========================================================================
 * Edge cases: NULL / zero / one / many
 * ========================================================================= */

CBELT_TEST(null_layout_safety) {
   /* All public API calls should handle NULL layout gracefully */
   cbelt_assert(DwindleInsertWindow(nullptr, reinterpret_cast<HWND>(1),
                                    reinterpret_cast<HWND>(0)) == false);
   cbelt_assert(DwindleRemoveWindow(nullptr, reinterpret_cast<HWND>(1)) ==
                false);

   RECT out;
   cbelt_assert(
       DwindleGetWindowRect(nullptr, reinterpret_cast<HWND>(1), &out) == false);

   DwindleRecalculate(nullptr); /* should not crash */
   DwindleLayoutFree(nullptr);  /* should not crash */

   return TEST_SUCCESS;
}

CBELT_TEST(dwindle_layout_create_and_free) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);
   cbelt_assert(layout != nullptr);
   cbelt_assert(layout->root == nullptr);
   cbelt_assert(layout->window_count == 0);
   cbelt_assert(layout->split_ratio == 0.5);
   cbelt_assert(layout->workspace_rect.left == 0);
   cbelt_assert(layout->workspace_rect.top == 0);
   cbelt_assert(layout->workspace_rect.right == 1920);
   cbelt_assert(layout->workspace_rect.bottom == 1080);
   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(dwindle_insert_first_window) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   /* Insert first window (no focused window yet) */
   HWND win1 = reinterpret_cast<HWND>(1);
   bool result = DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   cbelt_assert(result == true);
   cbelt_assert(layout->window_count == 1);
   cbelt_assert(layout->root != nullptr);
   cbelt_assert(layout->root->hwnd == win1);
   cbelt_assert(layout->root->is_leaf == TRUE);

   /* First window fills the entire workspace */
   cbelt_assert(layout->root->rect.left == 0);
   cbelt_assert(layout->root->rect.top == 0);
   cbelt_assert(layout->root->rect.right == 1920);
   cbelt_assert(layout->root->rect.bottom == 1080);

   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(dwindle_insert_many_windows) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   /* Insert 5 windows in sequence, always focusing the last-inserted */
   std::array<HWND, 5> hwnds = {
       reinterpret_cast<HWND>(1), reinterpret_cast<HWND>(2),
       reinterpret_cast<HWND>(3), reinterpret_cast<HWND>(4),
       reinterpret_cast<HWND>(5)};
   HWND focus = reinterpret_cast<HWND>(0);

   for (int i = 0; i < 5; i++) {
      bool ok = DwindleInsertWindow(layout, hwnds[i], focus);
      cbelt_assert(ok == true);
      focus = hwnds[i];
   }

   cbelt_assert(layout->window_count == 5);
   cbelt_assert(layout->root != nullptr);

   /* The root must be a container (since we have >1 window) */
   cbelt_assert(layout->root->is_leaf == FALSE);

   /* Verify every window exists in the tree */
   for (int i = 0; i < 5; i++) {
      RECT rect;
      bool found = DwindleGetWindowRect(layout, hwnds[i], &rect);
      cbelt_assert(found == true);
      cbelt_assert(rect.right > rect.left);
      cbelt_assert(rect.bottom > rect.top);
   }

   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(insert_duplicate_window_no_duplicate_node) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   cbelt_assert(layout->window_count == 1);

   /* Re-insert the same HWND while it is already in the tree. The dedup
      path removes the existing node first, so only one node remains. */
   bool result = DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   cbelt_assert(result == true);
   cbelt_assert(layout->window_count == 1);

   /* The window must still be addressable after the double insert */
   RECT rect;
   cbelt_assert(DwindleGetWindowRect(layout, win1, &rect) == true);
   cbelt_assert(rect.left == 0);
   cbelt_assert(rect.top == 0);
   cbelt_assert(rect.right == 1920);
   cbelt_assert(rect.bottom == 1080);

   /* A single remove must empty the tree — no duplicate node left behind */
   cbelt_assert(DwindleRemoveWindow(layout, win1) == true);
   cbelt_assert(layout->window_count == 0);
   cbelt_assert(layout->root == nullptr);

   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(reinsert_existing_window_reconciles) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   HWND win3 = reinterpret_cast<HWND>(3);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);
   DwindleInsertWindow(layout, win3, win2);
   cbelt_assert(layout->window_count == 3);

   /* Re-insert win2 (already present) focused on win3 — the old node is
      removed and win2 is placed exactly once at the focus position. */
   bool result = DwindleInsertWindow(layout, win2, win3);
   cbelt_assert(result == true);
   cbelt_assert(layout->window_count == 3);

   /* Every window is still addressable and tiled after the reconcile */
   RECT r1;
   RECT r2;
   RECT r3;
   cbelt_assert(DwindleGetWindowRect(layout, win1, &r1) == true);
   cbelt_assert(DwindleGetWindowRect(layout, win2, &r2) == true);
   cbelt_assert(DwindleGetWindowRect(layout, win3, &r3) == true);
   cbelt_assert(r1.right > r1.left);
   cbelt_assert(r1.bottom > r1.top);
   cbelt_assert(r2.right > r2.left);
   cbelt_assert(r2.bottom > r2.top);
   cbelt_assert(r3.right > r3.left);
   cbelt_assert(r3.bottom > r3.top);

   /* Removing win2 once leaves exactly two windows */
   cbelt_assert(DwindleRemoveWindow(layout, win2) == true);
   cbelt_assert(layout->window_count == 2);

   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

/* =========================================================================
 * DwindleInsertWindow, tree structure verification
 * ========================================================================= */

CBELT_TEST(insert_window_creates_container) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);

   /* After 2 windows, root should be a container node */
   cbelt_assert(layout->root->is_leaf == FALSE);
   cbelt_assert(layout->root->split_horizontal == TRUE ||
                layout->root->split_horizontal == FALSE); /* any direction */

   /* Both children exist */
   cbelt_assert(layout->root->first != nullptr);
   cbelt_assert(layout->root->second != nullptr);
   cbelt_assert(layout->root->first->is_leaf == TRUE);
   cbelt_assert(layout->root->second->is_leaf == TRUE);
   cbelt_assert(layout->root->first->hwnd == win1);
   cbelt_assert(layout->root->second->hwnd == win2);

   /* The two child rects should tile the parent rect */
   RECT *first = &layout->root->first->rect;
   RECT *second = &layout->root->second->rect;
   cbelt_assert(first->left == 0);
   cbelt_assert(first->top == 0);
   cbelt_assert(second->right == 1920);
   cbelt_assert(second->bottom == 1080);

   /* They should be adjacent along the split */
   if (layout->root->split_horizontal) {
      cbelt_assert(first->right == second->left);
      cbelt_assert(first->bottom == second->bottom);
   } else {
      cbelt_assert(first->bottom == second->top);
      cbelt_assert(first->right == second->right);
   }

   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(insert_window_split_ratio_applied) {
   RECT workspace = {.left = 0, .top = 0, .right = 2000, .bottom = 1000};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.25);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);

   cbelt_assert(layout->root->split_ratio == 0.25);

   /* win1 (master) gets 25% of width (500px), win2 gets 75% (1500px) */
   RECT *first = &layout->root->first->rect;
   RECT *second = &layout->root->second->rect;
   int first_width = first->right - first->left;
   int second_width = second->right - second->left;
   cbelt_assert(first_width == 500);
   cbelt_assert(second_width == 1500);
   cbelt_assert(first_width + second_width == 2000);

   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

/* =========================================================================
 * DwindleRemoveWindow, edge cases
 * ========================================================================= */

CBELT_TEST(remove_last_window) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));

   /* Remove the only window */
   bool result = DwindleRemoveWindow(layout, win1);
   cbelt_assert(result == true);
   cbelt_assert(layout->window_count == 0);
   cbelt_assert(layout->root == nullptr);

   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(remove_one_of_two) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);

   /* Remove win1, tree should collapse to a single leaf (win2) */
   bool result = DwindleRemoveWindow(layout, win1);
   cbelt_assert(result == true);
   cbelt_assert(layout->window_count == 1);
   cbelt_assert(layout->root != nullptr);
   cbelt_assert(layout->root->is_leaf == TRUE);
   cbelt_assert(layout->root->hwnd == win2);

   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(remove_focused_leaf_in_deep_tree) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   HWND win3 = reinterpret_cast<HWND>(3);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);
   DwindleInsertWindow(layout, win3, win2);

   cbelt_assert(layout->window_count == 3);

   /* Remove win2 (a leaf somewhere in the middle) */
   bool result = DwindleRemoveWindow(layout, win2);
   cbelt_assert(result == true);
   cbelt_assert(layout->window_count == 2);

   /* Win1 and win3 should still be findable */
   RECT rect;
   cbelt_assert(DwindleGetWindowRect(layout, win1, &rect) == true);
   cbelt_assert(DwindleGetWindowRect(layout, win3, &rect) == true);

   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(remove_nonexistent_window) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));

   /* Try to remove a window that doesn't exist */
   bool result = DwindleRemoveWindow(layout, reinterpret_cast<HWND>(999));
   cbelt_assert(result == false);
   cbelt_assert(layout->window_count == 1);

   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

/* =========================================================================
 * DwindleGetWindowRect, rectangle verification
 * ========================================================================= */

CBELT_TEST(get_window_rect_basic) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));

   RECT out_rect;
   bool found = DwindleGetWindowRect(layout, win1, &out_rect);
   cbelt_assert(found == true);
   cbelt_assert(out_rect.left == 0);
   cbelt_assert(out_rect.top == 0);
   cbelt_assert(out_rect.right == 1920);
   cbelt_assert(out_rect.bottom == 1080);

   /* Non-existent window should return false */
   HWND nonexistent = reinterpret_cast<HWND>(999);
   bool not_found = DwindleGetWindowRect(layout, nonexistent, &out_rect);
   cbelt_assert(not_found == false);

   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(get_window_rect_after_recalculate) {
   RECT workspace = {.left = 0, .top = 0, .right = 2000, .bottom = 1000};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);

   /* Modify the split ratio on the root container */
   layout->root->split_ratio = 0.75;

   /* Recalculate to apply the new split ratio */
   DwindleRecalculate(layout);

   /* Check that both windows have valid rects */
   RECT r1;
   RECT r2;
   cbelt_assert(DwindleGetWindowRect(layout, win1, &r1) == true);
   cbelt_assert(DwindleGetWindowRect(layout, win2, &r2) == true);

   /* Both rects should be within workspace bounds */
   cbelt_assert(r1.left >= 0);
   cbelt_assert(r1.right <= 2000);
   cbelt_assert(r2.left >= 0);
   cbelt_assert(r2.right <= 2000);

   /* The two rects should tile the workspace */
   int width1 = r1.right - r1.left;
   int width2 = r2.right - r2.left;
   cbelt_assert(width1 > 0);
   cbelt_assert(width2 > 0);

   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

/* =========================================================================
 * DwindleRecalculate, rectangle recalculation
 * ========================================================================= */

CBELT_TEST(recalculate_after_tree_change) {
   RECT workspace = {.left = 0, .top = 0, .right = 1200, .bottom = 800};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   HWND win3 = reinterpret_cast<HWND>(3);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);
   DwindleInsertWindow(layout, win3, win2);

   /* Remove one window (win2) to collapse tree */
   DwindleRemoveWindow(layout, win2);

   /* Recalculate after the structural change */
   DwindleRecalculate(layout);

   /* Remaining windows should have valid rects */
   RECT r1;
   RECT r3;
   cbelt_assert(DwindleGetWindowRect(layout, win1, &r1) == true);
   cbelt_assert(DwindleGetWindowRect(layout, win3, &r3) == true);
   cbelt_assert(r1.right > r1.left);
   cbelt_assert(r3.right > r3.left);

   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

/* =========================================================================
 * SplitRectangle
 * ========================================================================= */

CBELT_TEST(split_rectangle_horizontal_50) {
   RECT parent = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   RECT first;
   RECT second;

   SplitRectangle(&parent, TRUE, 0.5, &first, &second);

   /* Horizontal split at 50%, left half and right half */
   cbelt_assert(first.left == 0);
   cbelt_assert(first.right == 960);
   cbelt_assert(first.top == 0);
   cbelt_assert(first.bottom == 1080);

   cbelt_assert(second.left == 960);
   cbelt_assert(second.right == 1920);
   cbelt_assert(second.top == 0);
   cbelt_assert(second.bottom == 1080);

   return TEST_SUCCESS;
}

CBELT_TEST(split_rectangle_vertical_50) {
   RECT parent = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   RECT first;
   RECT second;

   SplitRectangle(&parent, FALSE, 0.5, &first, &second);

   /* Vertical split at 50%, top half and bottom half */
   cbelt_assert(first.left == 0);
   cbelt_assert(first.right == 1920);
   cbelt_assert(first.top == 0);
   cbelt_assert(first.bottom == 540);

   cbelt_assert(second.left == 0);
   cbelt_assert(second.right == 1920);
   cbelt_assert(second.top == 540);
   cbelt_assert(second.bottom == 1080);

   return TEST_SUCCESS;
}

CBELT_TEST(split_rectangle_various_ratios) {
   RECT parent = {.left = 0, .top = 0, .right = 1000, .bottom = 800};

   /* ratio = 0.25 */
   RECT first;
   RECT second;
   SplitRectangle(&parent, TRUE, 0.25, &first, &second);
   cbelt_assert(first.right == 250);
   cbelt_assert(second.left == 250);

   /* ratio = 0.75 */
   SplitRectangle(&parent, TRUE, 0.75, &first, &second);
   cbelt_assert(first.right == 750);
   cbelt_assert(second.left == 750);

   /* ratio = 0.0, first gets 0 width */
   SplitRectangle(&parent, TRUE, 0.0, &first, &second);
   cbelt_assert(first.right == 0);
   cbelt_assert(second.left == 0);

   /* ratio = 1.0, first gets all width */
   SplitRectangle(&parent, TRUE, 1.0, &first, &second);
   cbelt_assert(first.right == 1000);
   cbelt_assert(second.left == 1000);

   return TEST_SUCCESS;
}

/* =========================================================================
 * DwindleLayoutCreate, split_ratio clamping
 * ========================================================================= */

CBELT_TEST(split_ratio_clamping) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};

   /* Clamp above 1.0 */
   DwindleLayout *layout1 = DwindleLayoutCreate(workspace, 2.0);
   cbelt_assert(layout1->split_ratio == 1.0);
   DwindleLayoutFree(layout1);

   /* Clamp below 0.0 */
   DwindleLayout *layout2 = DwindleLayoutCreate(workspace, -1.0);
   cbelt_assert(layout2->split_ratio == 0.0);
   DwindleLayoutFree(layout2);

   /* Exact boundaries */
   DwindleLayout *layout3 = DwindleLayoutCreate(workspace, 0.0);
   cbelt_assert(layout3->split_ratio == 0.0);
   DwindleLayoutFree(layout3);

   DwindleLayout *layout4 = DwindleLayoutCreate(workspace, 1.0);
   cbelt_assert(layout4->split_ratio == 1.0);
   DwindleLayoutFree(layout4);

   return TEST_SUCCESS;
}

/* =========================================================================
 * DwindleGetClosestWindow
 * ========================================================================= */

CBELT_TEST(get_closest_window_empty) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);
   cbelt_assert(DwindleGetClosestWindow(layout, nullptr) == nullptr);
   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(get_closest_window_returns_closest_or_first) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);

   cbelt_assert(DwindleGetClosestWindow(layout, nullptr) == win1);
   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

/* =========================================================================
 * DwindleResizeWindow, keyboard-driven resize
 * =========================================================================
 *
 * Builds a 3‑window tree on a 1600×900 workspace:
 *
 *   C1(H, 0.5) { A(0,0,800,900), C2(V, 0.5) { B(800,0,1600,450),
 *                                               C(800,450,1600,900) } }
 */

CBELT_TEST(resize_window_null_safety) {
   cbelt_assert(DwindleResizeWindow(nullptr, reinterpret_cast<HWND>(1),
                                    DirRight, 50) == false);
   return TEST_SUCCESS;
}

CBELT_TEST(resize_window_nonexistent) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);
   DwindleInsertWindow(layout, reinterpret_cast<HWND>(1),
                       reinterpret_cast<HWND>(0));
   cbelt_assert(DwindleResizeWindow(layout, reinterpret_cast<HWND>(999),
                                    DirRight, 50) == false);
   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(resize_window_first_child_right) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);

   // A (first child) DirRight 80 → split line moves right
   cbelt_assert(DwindleResizeWindow(layout, win1, DirRight, 80) == true);
   ASSERT_RATIO(0.55, layout->root->split_ratio);

   RECT r1;
   DwindleGetWindowRect(layout, win1, &r1);
   cbelt_assert(r1.right - r1.left == 880);
   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(resize_window_first_child_left) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);

   // A (first child) DirLeft 80 → split line moves left
   cbelt_assert(DwindleResizeWindow(layout, win1, DirLeft, 80) == true);
   ASSERT_RATIO(0.45, layout->root->split_ratio);

   RECT r1;
   DwindleGetWindowRect(layout, win1, &r1);
   cbelt_assert(r1.right - r1.left == 720);
   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(resize_window_second_child_left) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);

   // B (second child) DirLeft 80 → split line moves left, B grows
   cbelt_assert(DwindleResizeWindow(layout, win2, DirLeft, 80) == true);
   ASSERT_RATIO(0.45, layout->root->split_ratio);

   RECT r2;
   DwindleGetWindowRect(layout, win2, &r2);
   cbelt_assert(r2.right - r2.left == 880);
   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(resize_window_first_child_down) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   HWND win3 = reinterpret_cast<HWND>(3);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);
   DwindleInsertWindow(layout, win3, win2);

   // B (first child of C2) DirDown 40 → B grows downward
   DwindleNode *c2 = layout->root->second; // C2
   cbelt_assert(c2 != nullptr && c2->is_leaf == FALSE);

   cbelt_assert(DwindleResizeWindow(layout, win2, DirDown, 40) == true);
   ASSERT_RATIO(0.5 + 40.0 / 900.0, c2->split_ratio);

   RECT r2;
   DwindleGetWindowRect(layout, win2, &r2);

   // B and C should tile vertically within C2
   RECT r3;
   DwindleGetWindowRect(layout, win3, &r3);
   cbelt_assert(r2.bottom == r3.top);
   cbelt_assert((r2.bottom - r2.top) + (r3.bottom - r3.top) == 900);

   // Rects still tile properly
   RECT r1;
   DwindleGetWindowRect(layout, win1, &r1);
   cbelt_assert(r1.left == 0 && r1.top == 0);
   cbelt_assert(r3.right == 1600 && r3.bottom == 900);
   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(resize_window_second_child_up) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   HWND win3 = reinterpret_cast<HWND>(3);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);
   DwindleInsertWindow(layout, win3, win2);

   // C (second child of C2) DirUp 40 → C grows upward
   DwindleNode *c2 = layout->root->second;
   cbelt_assert(c2 != nullptr && c2->is_leaf == FALSE);

   cbelt_assert(DwindleResizeWindow(layout, win3, DirUp, 40) == true);
   ASSERT_RATIO(0.5 - 40.0 / 900.0, c2->split_ratio);

   RECT r3;
   DwindleGetWindowRect(layout, win3, &r3);
   cbelt_assert(r3.bottom - r3.top == 490);
   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(resize_window_walks_up_ancestors) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   HWND win3 = reinterpret_cast<HWND>(3);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);
   DwindleInsertWindow(layout, win3, win2);

   // B (inside C2, vertical) DirRight → no horizontal match at C2,
   // walk up to C1 (horizontal).  DirRight increases C1 ratio.
   cbelt_assert(DwindleResizeWindow(layout, win2, DirRight, 80) == true);
   ASSERT_RATIO(0.55, layout->root->split_ratio);

   RECT r1;
   DwindleGetWindowRect(layout, win1, &r1);
   cbelt_assert(r1.right - r1.left == 880);
   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(resize_window_clamps_min) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);

   // Aggressive left shrink, ratio must not go below 0.1
   cbelt_assert(DwindleResizeWindow(layout, win1, DirLeft, 999999) == true);
   ASSERT_RATIO(0.1, layout->root->split_ratio);
   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(resize_window_clamps_max) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);

   // Aggressive right grow, ratio must not go above 0.9
   cbelt_assert(DwindleResizeWindow(layout, win1, DirRight, 999999) == true);
   ASSERT_RATIO(0.9, layout->root->split_ratio);
   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

/* =========================================================================
 * DwindleResizeWindowToRect, mouse‑driven resize
 * ========================================================================= */

CBELT_TEST(resize_to_rect_null_safety) {
   RECT r = {.left = 0, .top = 0, .right = 100, .bottom = 100};
   cbelt_assert(DwindleResizeWindowToRect(nullptr, reinterpret_cast<HWND>(1),
                                          r) == false);
   return TEST_SUCCESS;
}

CBELT_TEST(resize_to_rect_first_child) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);

   // Set A's rect to 960px wide → ratio should become 960/1600 = 0.6
   RECT new_a = {.left = 0, .top = 0, .right = 960, .bottom = 900};
   cbelt_assert(DwindleResizeWindowToRect(layout, win1, new_a) == true);
   ASSERT_RATIO(0.6, layout->root->split_ratio);

   RECT r1;
   DwindleGetWindowRect(layout, win1, &r1);
   cbelt_assert(r1.right - r1.left == 960);
   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(resize_to_rect_second_child) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);

   // Set B's left edge to 640 → split line should be at 640:
   // ratio = 640/1600 = 0.4
   RECT new_b = {.left = 640, .top = 0, .right = 1600, .bottom = 900};
   cbelt_assert(DwindleResizeWindowToRect(layout, win2, new_b) == true);
   ASSERT_RATIO(0.4, layout->root->split_ratio);

   RECT r2;
   DwindleGetWindowRect(layout, win2, &r2);
   cbelt_assert(r2.left == 640);
   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(resize_to_rect_walks_up) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   HWND win3 = reinterpret_cast<HWND>(3);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);
   DwindleInsertWindow(layout, win3, win2);

   DwindleNode *c2 = layout->root->second;
   cbelt_assert(c2->is_leaf == FALSE);

   // Move C's top edge from 450 to 360 (C grows up)
   RECT new_c = {.left = 800, .top = 360, .right = 1600, .bottom = 900};
   cbelt_assert(DwindleResizeWindowToRect(layout, win3, new_c) == true);
   ASSERT_RATIO(360.0 / 900.0, c2->split_ratio);

   RECT r3;
   DwindleGetWindowRect(layout, win3, &r3);
   cbelt_assert(r3.top == 360);
   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(resize_to_rect_nonexistent) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);
   DwindleInsertWindow(layout, reinterpret_cast<HWND>(1),
                       reinterpret_cast<HWND>(0));

   RECT r = {.left = 0, .top = 0, .right = 100, .bottom = 100};
   cbelt_assert(DwindleResizeWindowToRect(layout, reinterpret_cast<HWND>(999),
                                          r) == false);
   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

/* =========================================================================
 * DwindleMoveWindow — tree-based promotion (the "promotion" fallback)
 * ========================================================================= */

CBELT_TEST(move_window_at_edge_returns_false) {
   // A single window has no neighbor in any direction and cannot be
   // promoted further — returns false.
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);
   DwindleInsertWindow(layout, reinterpret_cast<HWND>(1),
                       reinterpret_cast<HWND>(0));
   cbelt_assert(DwindleMoveWindow(layout, reinterpret_cast<HWND>(1), DirLeft) ==
                false);
   cbelt_assert(
       DwindleMoveWindow(layout, reinterpret_cast<HWND>(1), DirRight) == false);
   cbelt_assert(DwindleMoveWindow(layout, reinterpret_cast<HWND>(1), DirUp) ==
                false);
   cbelt_assert(DwindleMoveWindow(layout, reinterpret_cast<HWND>(1), DirDown) ==
                false);
   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(move_window_at_edge_in_deep_tree) {
   // Rightmost leaf in a deep tree — no leaf to the right, returns false.
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   HWND win3 = reinterpret_cast<HWND>(3);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);
   DwindleInsertWindow(layout, win3, win2);

   // Tree: C1(H, 0.5) { A(0,0,800,900), C2(V, 0.5) { B(800,0,1600,450),
   // C(800,450,1600,900) } } C at bottom-right: DirDown is parallel to C2's V
   // split → genuine edge.
   cbelt_assert(DwindleMoveWindow(layout, win3, DirDown) == false);
   // DirRight is orthogonal to C2's V split → restructures C2 to H{B,C}
   // so C moves to the right half: (1200,0,1600,900).
   cbelt_assert(DwindleMoveWindow(layout, win3, DirRight) == true);
   RECT rc;
   DwindleGetWindowRect(layout, win3, &rc);
   cbelt_assert(rc.left == 1200);
   cbelt_assert(rc.top == 0);
   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

/* =========================================================================
 * DwindleMoveWindow, sticky swap
 * ========================================================================= */

CBELT_TEST(move_window_same_parent) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);

   // Move A right → swaps with B (same parent C1)
   cbelt_assert(DwindleMoveWindow(layout, win1, DirRight) == true);

   // After swap: win1 should be in B's old position (right)
   RECT rect;
   DwindleGetWindowRect(layout, win1, &rect);
   cbelt_assert(rect.left == 800);

   // win2 should be in A's old position (left)
   DwindleGetWindowRect(layout, win2, &rect);
   cbelt_assert(rect.left == 0);

   // Split ratio unchanged
   ASSERT_RATIO(0.5, layout->root->split_ratio);
   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(move_window_same_parent_preserves_size) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   HWND win3 = reinterpret_cast<HWND>(3);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);
   DwindleInsertWindow(layout, win3, win2);

   // Tree: C1(H, 0.5) { A(0,0,800,900), C2(V, 0.5) { B(800,0,1600,450),
   // C(800,450,1600,900) } }

   // Resize B (first child of C2) DirDown 40px -> B larger, ratio ~0.54444
   DwindleResizeWindow(layout, win2, DirDown, 40);

   RECT rb;
   DwindleGetWindowRect(layout, win2, &rb);
   int b_height_before = rb.bottom - rb.top;

   // Move B DirDown -> swaps with C within C2 (same parent)
   cbelt_assert(DwindleMoveWindow(layout, win2, DirDown) == true);

   // B should still have the same height (sticky size preserved)
   DwindleGetWindowRect(layout, win2, &rb);
   cbelt_assert(rb.bottom - rb.top == b_height_before);

   // C should now be in B's old position with the complement height
   RECT rc;
   DwindleGetWindowRect(layout, win3, &rc);
   cbelt_assert(rc.bottom - rc.top == 900 - b_height_before);
   cbelt_assert(rb.top == rc.bottom);

   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(move_window_different_parent_reinsert) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   HWND win3 = reinterpret_cast<HWND>(3);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);
   DwindleInsertWindow(layout, win3, win2);

   // Tree: C1(H, 0.5) { A(0,0,800,900), C2(V, 0.5) { B(800,0,1600,450),
   // C(800,450,1600,900) } }

   // Move C left → neighbor is A (geometric, different parents).
   // C is removed, B takes full right half, then C re-inserted at A.
   // A is split vertically, A stays first (top-left), C goes second
   // (bottom-left).
   cbelt_assert(DwindleMoveWindow(layout, win3, DirLeft) == true);

   // After re-insert: win3 (C) is at bottom-left (splitting A)
   RECT rect;
   DwindleGetWindowRect(layout, win3, &rect);
   cbelt_assert(rect.left == 0);
   cbelt_assert(rect.top > 0);

   // win1 (A) is at top-left (stays first child)
   DwindleGetWindowRect(layout, win1, &rect);
   cbelt_assert(rect.left == 0);
   cbelt_assert(rect.top == 0);

   // win2 (B) is still at right (full height after C was removed)
   DwindleGetWindowRect(layout, win2, &rect);
   cbelt_assert(rect.left == 800);

   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

/* =========================================================================
 * Insert, fallback when focus is not in the tree
 * ========================================================================= */

CBELT_TEST(insert_focus_not_found_splits_last_leaf) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   HWND win3 = reinterpret_cast<HWND>(3);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);
   DwindleInsertWindow(layout, win3, win2);

   // Remove win2, then re‑insert with focus=win2 (not in tree).
   // The fallback picks the last leaf (win3) and splits it so win2
   // lands at the edge.
   DwindleRemoveWindow(layout, win2);
   cbelt_assert(layout->window_count == 2);

   // Insert win2 back with focus=win2 (not in tree)
   DwindleInsertWindow(layout, win2, win2);
   cbelt_assert(layout->window_count == 3);

   // win2 should be on the right half (sibling of last leaf)
   RECT r2;
   DwindleGetWindowRect(layout, win2, &r2);
   int halfway = 800;
   cbelt_assert(r2.left >= halfway);

   // win1 and win3 tile the left and split the right half
   RECT r1;
   RECT r3;
   DwindleGetWindowRect(layout, win1, &r1);
   DwindleGetWindowRect(layout, win3, &r3);
   cbelt_assert(r1.left == 0);
   cbelt_assert(r3.left >= halfway);
   cbelt_assert(r3.bottom == r2.top);
   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

/* =========================================================================
 * Gap size
 * ========================================================================= */

CBELT_TEST(gap_size_applied) {
   RECT workspace = {.left = 0, .top = 0, .right = 1600, .bottom = 900};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);
   DwindleLayoutSetGaps(layout, 10, 0);

   // Single window should be inset by gap/2 = 5px on all edges
   HWND win1 = reinterpret_cast<HWND>(1);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));

   RECT r1;
   DwindleGetWindowRect(layout, win1, &r1);
   cbelt_assert(r1.left == 5);
   cbelt_assert(r1.top == 5);
   cbelt_assert(r1.right == 1595);
   cbelt_assert(r1.bottom == 895);

   // Two windows: horizontal split at 0.5, each inset by 5px
   HWND win2 = reinterpret_cast<HWND>(2);
   DwindleInsertWindow(layout, win2, win1);

   DwindleGetWindowRect(layout, win1, &r1);
   RECT r2;
   DwindleGetWindowRect(layout, win2, &r2);

   // Outer edges inset by 5px
   cbelt_assert(r1.left == 5);
   cbelt_assert(r1.top == 5);
   cbelt_assert(r2.right == 1595);
   cbelt_assert(r2.bottom == 895);

   // 10px gap between the two windows
   cbelt_assert(r2.left - r1.right == 10);

   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}

CBELT_TEST(border_compensation_insets_each_window) {
   RECT workspace = {.left = 0, .top = 0, .right = 800, .bottom = 600};
   DwindleLayout *layout = DwindleLayoutCreate(workspace, 0.5);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   DwindleInsertWindow(layout, win1, reinterpret_cast<HWND>(0));
   DwindleInsertWindow(layout, win2, win1);

   LayoutConfig cfg = {.gap_between = 0,
                       .gap_edge = 0,
                       .border_width = 4,
                       .workspace_rect = workspace};
   DwindleLayoutApplyConfig(layout, cfg);

   // Seam at x=400; each window inset 4px from the seam and the edges
   RECT r1;
   RECT r2;
   cbelt_assert(DwindleGetWindowRect(layout, win1, &r1) == true);
   cbelt_assert(r1.left == 4);
   cbelt_assert(r1.top == 4);
   cbelt_assert(r1.right == 396);
   cbelt_assert(r1.bottom == 596);

   cbelt_assert(DwindleGetWindowRect(layout, win2, &r2) == true);
   cbelt_assert(r2.left == 404);
   cbelt_assert(r2.top == 4);
   cbelt_assert(r2.right == 796);
   cbelt_assert(r2.bottom == 596);

   // Drag win1's right edge to x=496 → the split ratio accounts for the
   // border inset so the window's right edge lands exactly on 496.
   cbelt_assert(DwindleResizeWindowToRect(layout, win1, RECT{4, 4, 496, 596}) ==
                true);
   RECT r1b;
   cbelt_assert(DwindleGetWindowRect(layout, win1, &r1b) == true);
   cbelt_assert(r1b.right == 496);

   DwindleLayoutFree(layout);
   return TEST_SUCCESS;
}
