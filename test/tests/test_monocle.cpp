#include "cbelt.h"
#include <array>
#include <memory>
#include <windef.h>
#include <windows.h>

#include "layout_test_access.h"

CBELT_GROUP("monocle")

/* =========================================================================
 * Null / boundary safety
 * ========================================================================= */

CBELT_TEST(null_safety) {
   auto ml = std::make_unique<TestMonocle>();

   /* Insert should work on an empty engine */
   cbelt_assert_true(
       ml->insert(reinterpret_cast<HWND>(1), reinterpret_cast<HWND>(0)));

   /* Remove on non-existent window returns false */
   cbelt_assert_false(ml->remove(reinterpret_cast<HWND>(99)));

   /* get_rect with null out returns false */
   cbelt_assert_false(ml->get_rect(reinterpret_cast<HWND>(1), nullptr));

   /* get_rect on valid engine returns true */
   RECT out;
   cbelt_assert_true(ml->get_rect(reinterpret_cast<HWND>(1), &out));

   /* get_neighbor on non-existent window returns nullptr */
   cbelt_assert_null(ml->get_neighbor(reinterpret_cast<HWND>(99), DirNext));

   /* get_closest_window on empty engine returns nullptr */
   ml->remove(reinterpret_cast<HWND>(1));
   cbelt_assert_null(ml->get_closest_window(nullptr));

   /* apply() on empty engine should not crash */
   ml->apply();

   return TEST_SUCCESS;
}

CBELT_TEST(empty_layout_returns_null) {
   auto ml = std::make_unique<TestMonocle>();
   cbelt_assert_not_null(ml);

   cbelt_assert_null(ml->get_neighbor(reinterpret_cast<HWND>(1), DirNext));
   cbelt_assert_null(ml->get_closest_window(nullptr));

   return TEST_SUCCESS;
}

/* =========================================================================
 * Create / lifecycle
 * ========================================================================= */

CBELT_TEST(create_free) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   auto ml = std::make_unique<TestMonocle>();
   ml->apply_config(LayoutConfig{.gap_between = 0,
                                 .gap_edge = 0,
                                 .border_width = 0,
                                 .workspace_rect = workspace});
   cbelt_assert_not_null(ml);
   cbelt_assert_true(ml->windows.empty());
   cbelt_assert_true(0 == ml->workspace_rect.left);
   cbelt_assert_true(0 == ml->workspace_rect.top);
   cbelt_assert_true(1920 == ml->workspace_rect.right);
   cbelt_assert_true(1080 == ml->workspace_rect.bottom);
   cbelt_assert_true(0 == ml->gap_between);
   cbelt_assert_true(0 == ml->gap_edge);
   cbelt_assert_true(0 == ml->tiled_rect.left);
   cbelt_assert_true(0 == ml->tiled_rect.top);
   cbelt_assert_true(1920 == ml->tiled_rect.right);
   cbelt_assert_true(1080 == ml->tiled_rect.bottom);
   return TEST_SUCCESS;
}

CBELT_TEST(create_after_free) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   auto ml = std::make_unique<TestMonocle>();
   ml->apply_config(LayoutConfig{.gap_between = 0,
                                 .gap_edge = 0,
                                 .border_width = 0,
                                 .workspace_rect = workspace});
   ml = std::make_unique<TestMonocle>();
   ml->apply_config(LayoutConfig{.gap_between = 0,
                                 .gap_edge = 0,
                                 .border_width = 0,
                                 .workspace_rect = workspace});
   cbelt_assert_not_null(ml);
   cbelt_assert_true(ml->windows.empty());
   return TEST_SUCCESS;
}

/* =========================================================================
 * Insert
 * ========================================================================= */

CBELT_TEST(insert_first_window) {
   auto ml = std::make_unique<TestMonocle>();

   HWND win1 = reinterpret_cast<HWND>(1);
   cbelt_assert_true(ml->insert(win1, reinterpret_cast<HWND>(0)));
   cbelt_assert_true(1 == ml->windows.size());
   cbelt_assert_true(win1 == ml->windows[0]);
   cbelt_assert_true(win1 == ml->get_closest_window(nullptr));

   return TEST_SUCCESS;
}

CBELT_TEST(insert_multiple) {
   auto ml = std::make_unique<TestMonocle>();

   std::array<HWND, 5> wins = {
       reinterpret_cast<HWND>(1), reinterpret_cast<HWND>(2),
       reinterpret_cast<HWND>(3), reinterpret_cast<HWND>(4),
       reinterpret_cast<HWND>(5)};
   for (int i = 0; i < 5; i++) {
      cbelt_assert_true(ml->insert(wins[i], reinterpret_cast<HWND>(0)));
   }
   cbelt_assert_true(5 == ml->windows.size());
   for (int i = 0; i < 5; i++) {
      cbelt_assert_true(wins[i] == ml->windows[i]);
   }

   return TEST_SUCCESS;
}

CBELT_TEST(insert_ignores_focus) {
   auto ml = std::make_unique<TestMonocle>();

   cbelt_assert_true(
       ml->insert(reinterpret_cast<HWND>(1), reinterpret_cast<HWND>(42)));
   cbelt_assert_true(
       ml->insert(reinterpret_cast<HWND>(2), reinterpret_cast<HWND>(99)));
   cbelt_assert_true(2 == ml->windows.size());
   cbelt_assert_true(reinterpret_cast<HWND>(1) == ml->windows[0]);
   cbelt_assert_true(reinterpret_cast<HWND>(2) == ml->windows[1]);

   return TEST_SUCCESS;
}

CBELT_TEST(insert_grows_array) {
   auto ml = std::make_unique<TestMonocle>();

   for (size_t i = 0; i < 20; i++) {
      cbelt_assert_true(ml->insert((HWND)(i + 1), reinterpret_cast<HWND>(0)));
   }
   cbelt_assert_true(20 == ml->windows.size());
   cbelt_assert_true(ml->windows.capacity() >= 20);
   for (size_t i = 0; i < 20; i++) {
      cbelt_assert_true((HWND)(i + 1) == ml->windows[i]);
   }

   return TEST_SUCCESS;
}

CBELT_TEST(insert_duplicate_window_keeps_single_entry) {
   auto ml = std::make_unique<TestMonocle>();

   HWND win1 = reinterpret_cast<HWND>(1);
   cbelt_assert_true(ml->insert(win1, reinterpret_cast<HWND>(0)));
   cbelt_assert_true(1 == ml->windows.size());

   /* Re-inserting the same window is a no-op — no duplicate entry */
   cbelt_assert_true(ml->insert(win1, reinterpret_cast<HWND>(0)));
   cbelt_assert_true(1 == ml->windows.size());
   cbelt_assert_true(win1 == ml->windows[0]);

   /* A single remove takes it out entirely */
   cbelt_assert_true(ml->remove(win1));
   cbelt_assert_true(ml->windows.empty());

   return TEST_SUCCESS;
}

/* =========================================================================
 * Remove
 * ========================================================================= */

namespace {

inline void insert_n(TestMonocle *ml, int n) {
   for (int i = 1; i <= n; i++) {
      ml->insert(reinterpret_cast<HWND>(static_cast<uintptr_t>(i)),
                 reinterpret_cast<HWND>(0));
   }
}
} // namespace

CBELT_TEST(remove_only_window) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 1);

   cbelt_assert_true(ml->remove(reinterpret_cast<HWND>(1)));
   cbelt_assert_true(ml->windows.empty());

   return TEST_SUCCESS;
}

CBELT_TEST(remove_first) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert_true(ml->remove(reinterpret_cast<HWND>(1)));
   cbelt_assert_true(2 == ml->windows.size());
   cbelt_assert_true(reinterpret_cast<HWND>(2) == ml->windows[0]);
   cbelt_assert_true(reinterpret_cast<HWND>(3) == ml->windows[1]);

   return TEST_SUCCESS;
}

CBELT_TEST(remove_middle) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert_true(ml->remove(reinterpret_cast<HWND>(2)));
   cbelt_assert_true(2 == ml->windows.size());
   cbelt_assert_true(reinterpret_cast<HWND>(1) == ml->windows[0]);
   cbelt_assert_true(reinterpret_cast<HWND>(3) == ml->windows[1]);

   return TEST_SUCCESS;
}

CBELT_TEST(remove_last) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert_true(ml->remove(reinterpret_cast<HWND>(3)));
   cbelt_assert_true(2 == ml->windows.size());
   cbelt_assert_true(reinterpret_cast<HWND>(1) == ml->windows[0]);
   cbelt_assert_true(reinterpret_cast<HWND>(2) == ml->windows[1]);

   return TEST_SUCCESS;
}

CBELT_TEST(remove_nonexistent) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert_false(ml->remove(reinterpret_cast<HWND>(99)));
   cbelt_assert_true(3 == ml->windows.size());

   return TEST_SUCCESS;
}

/* =========================================================================
 * GetWindowRect
 * ========================================================================= */

CBELT_TEST(all_windows_same_rect) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   auto ml = std::make_unique<TestMonocle>();
   ml->apply_config(LayoutConfig{.gap_between = 0,
                                 .gap_edge = 0,
                                 .border_width = 0,
                                 .workspace_rect = workspace});
   insert_n(ml.get(), 3);
   ml->apply();

   RECT out1;
   RECT out2;
   RECT out3;
   cbelt_assert_true(ml->get_rect(reinterpret_cast<HWND>(1), &out1));
   cbelt_assert_true(ml->get_rect(reinterpret_cast<HWND>(2), &out2));
   cbelt_assert_true(ml->get_rect(reinterpret_cast<HWND>(3), &out3));
   cbelt_assert_true(out2.left == out1.left);
   cbelt_assert_true(out2.top == out1.top);
   cbelt_assert_true(out2.right == out1.right);
   cbelt_assert_true(out2.bottom == out1.bottom);
   cbelt_assert_true(0 == memcmp(&out1, &out3, sizeof(RECT)));

   return TEST_SUCCESS;
}

CBELT_TEST(rect_no_gap) {
   RECT workspace = {.left = 100, .top = 50, .right = 1900, .bottom = 1050};
   auto ml = std::make_unique<TestMonocle>();
   ml->apply_config(LayoutConfig{.gap_between = 0,
                                 .gap_edge = 0,
                                 .border_width = 0,
                                 .workspace_rect = workspace});
   ml->apply();

   RECT out;
   cbelt_assert_true(ml->get_rect(reinterpret_cast<HWND>(1), &out));
   cbelt_assert_true(100 == out.left);
   cbelt_assert_true(50 == out.top);
   cbelt_assert_true(1900 == out.right);
   cbelt_assert_true(1050 == out.bottom);

   return TEST_SUCCESS;
}

CBELT_TEST(rect_with_edge_gap) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   auto ml = std::make_unique<TestMonocle>();
   LayoutConfig cfg = {0, 10, 0, workspace};
   ml->apply_config(cfg);

   RECT out;
   cbelt_assert_true(ml->get_rect(reinterpret_cast<HWND>(1), &out));
   cbelt_assert_true(10 == out.left);
   cbelt_assert_true(10 == out.top);
   cbelt_assert_true(1910 == out.right);
   cbelt_assert_true(1070 == out.bottom);

   return TEST_SUCCESS;
}

/* =========================================================================
 * GetNeighbor
 * ========================================================================= */

CBELT_TEST(get_neighbor_next_no_wrap) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert_true(reinterpret_cast<HWND>(2) ==
                     ml->get_neighbor(reinterpret_cast<HWND>(1), DirNext));
   cbelt_assert_true(reinterpret_cast<HWND>(3) ==
                     ml->get_neighbor(reinterpret_cast<HWND>(2), DirNext));
   cbelt_assert_null(ml->get_neighbor(reinterpret_cast<HWND>(3), DirNext));

   return TEST_SUCCESS;
}

CBELT_TEST(get_neighbor_prev_no_wrap) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert_null(ml->get_neighbor(reinterpret_cast<HWND>(1), DirPrev));
   cbelt_assert_true(reinterpret_cast<HWND>(1) ==
                     ml->get_neighbor(reinterpret_cast<HWND>(2), DirPrev));
   cbelt_assert_true(reinterpret_cast<HWND>(2) ==
                     ml->get_neighbor(reinterpret_cast<HWND>(3), DirPrev));

   return TEST_SUCCESS;
}

CBELT_TEST(get_neighbor_single_window) {
   auto ml = std::make_unique<TestMonocle>();
   ml->insert(reinterpret_cast<HWND>(1), reinterpret_cast<HWND>(0));

   cbelt_assert_null(ml->get_neighbor(reinterpret_cast<HWND>(1), DirNext));
   cbelt_assert_null(ml->get_neighbor(reinterpret_cast<HWND>(1), DirPrev));

   return TEST_SUCCESS;
}

CBELT_TEST(get_neighbor_geometric) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert_true(reinterpret_cast<HWND>(1) ==
                     ml->get_neighbor(reinterpret_cast<HWND>(2), DirLeft));
   cbelt_assert_true(reinterpret_cast<HWND>(3) ==
                     ml->get_neighbor(reinterpret_cast<HWND>(2), DirRight));
   cbelt_assert_null(ml->get_neighbor(reinterpret_cast<HWND>(2), DirUp));
   cbelt_assert_null(ml->get_neighbor(reinterpret_cast<HWND>(2), DirDown));

   return TEST_SUCCESS;
}

CBELT_TEST(get_neighbor_nonexistent_returns_null) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert_null(ml->get_neighbor(reinterpret_cast<HWND>(99), DirNext));
   cbelt_assert_null(ml->get_neighbor(reinterpret_cast<HWND>(99), DirPrev));
   cbelt_assert_null(ml->get_neighbor(reinterpret_cast<HWND>(99), DirLeft));

   return TEST_SUCCESS;
}

/* =========================================================================
 * GetClosestWindow
 * ========================================================================= */

CBELT_TEST(get_closest_window_returns_first_on_null) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 5);

   cbelt_assert_true(reinterpret_cast<HWND>(1) ==
                     ml->get_closest_window(nullptr));

   return TEST_SUCCESS;
}

CBELT_TEST(get_closest_window_empty_returns_null) {
   auto ml = std::make_unique<TestMonocle>();
   cbelt_assert_null(ml->get_closest_window(nullptr));
   return TEST_SUCCESS;
}

CBELT_TEST(get_closest_window_returns_selected_window) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3); // win1, win2, win3

   // The first inserted window is the initially-shown one.
   cbelt_assert_true(reinterpret_cast<HWND>(1) ==
                     ml->get_closest_window(nullptr));

   // The currently-shown window is tracked by HWND, so moving focus to a
   // later window changes what get_closest_window(nullptr) reports.
   ml->selected_hwnd = reinterpret_cast<HWND>(2);
   cbelt_assert_true(reinterpret_cast<HWND>(2) ==
                     ml->get_closest_window(nullptr));

   ml->selected_hwnd = reinterpret_cast<HWND>(3);
   cbelt_assert_true(reinterpret_cast<HWND>(3) ==
                     ml->get_closest_window(nullptr));

   return TEST_SUCCESS;
}

CBELT_TEST(removing_shown_window_advances_selection) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3); // win1, win2, win3; selected = win1

   // Focus moves to win2, then win2 is removed.
   ml->selected_hwnd = reinterpret_cast<HWND>(2);
   cbelt_assert_true(ml->remove(reinterpret_cast<HWND>(2)));
   // Selection advances to the window now at win2's old index (win3).
   cbelt_assert_true(reinterpret_cast<HWND>(3) == ml->selected_hwnd);
   cbelt_assert_true(reinterpret_cast<HWND>(3) ==
                     ml->get_closest_window(nullptr));

   // Remove the currently-shown window again; selection advances to win1.
   cbelt_assert_true(ml->remove(reinterpret_cast<HWND>(3)));
   cbelt_assert_true(reinterpret_cast<HWND>(1) == ml->selected_hwnd);
   cbelt_assert_true(reinterpret_cast<HWND>(1) ==
                     ml->get_closest_window(nullptr));

   // Remove the last window; selection becomes null.
   cbelt_assert_true(ml->remove(reinterpret_cast<HWND>(1)));
   cbelt_assert_null(ml->selected_hwnd);
   cbelt_assert_null(ml->get_closest_window(nullptr));

   return TEST_SUCCESS;
}

/* =========================================================================
 * MoveWindow
 * ========================================================================= */

CBELT_TEST(move_window_next_swap) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert_true(ml->move_window(reinterpret_cast<HWND>(1), DirNext));
   cbelt_assert_true(reinterpret_cast<HWND>(2) == ml->windows[0]);
   cbelt_assert_true(reinterpret_cast<HWND>(1) == ml->windows[1]);
   cbelt_assert_true(reinterpret_cast<HWND>(3) == ml->windows[2]);

   return TEST_SUCCESS;
}

CBELT_TEST(move_window_prev_swap) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert_true(ml->move_window(reinterpret_cast<HWND>(2), DirPrev));
   cbelt_assert_true(reinterpret_cast<HWND>(2) == ml->windows[0]);
   cbelt_assert_true(reinterpret_cast<HWND>(1) == ml->windows[1]);
   cbelt_assert_true(reinterpret_cast<HWND>(3) == ml->windows[2]);

   return TEST_SUCCESS;
}

CBELT_TEST(move_window_first_prev_wraps_to_last) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert_true(ml->move_window(reinterpret_cast<HWND>(1), DirPrev));
   cbelt_assert_true(reinterpret_cast<HWND>(3) == ml->windows[0]);
   cbelt_assert_true(reinterpret_cast<HWND>(2) == ml->windows[1]);
   cbelt_assert_true(reinterpret_cast<HWND>(1) == ml->windows[2]);

   return TEST_SUCCESS;
}

CBELT_TEST(move_window_last_next_wraps_to_first) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert_true(ml->move_window(reinterpret_cast<HWND>(3), DirNext));
   cbelt_assert_true(reinterpret_cast<HWND>(3) == ml->windows[0]);
   cbelt_assert_true(reinterpret_cast<HWND>(2) == ml->windows[1]);
   cbelt_assert_true(reinterpret_cast<HWND>(1) == ml->windows[2]);

   return TEST_SUCCESS;
}

CBELT_TEST(move_window_geometric_returns_false) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert_false(ml->move_window(reinterpret_cast<HWND>(2), DirLeft));
   cbelt_assert_false(ml->move_window(reinterpret_cast<HWND>(2), DirRight));
   cbelt_assert_false(ml->move_window(reinterpret_cast<HWND>(2), DirUp));
   cbelt_assert_false(ml->move_window(reinterpret_cast<HWND>(2), DirDown));

   return TEST_SUCCESS;
}

CBELT_TEST(move_window_nonexistent_returns_false) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert_false(ml->move_window(reinterpret_cast<HWND>(99), DirNext));
   cbelt_assert_false(ml->move_window(reinterpret_cast<HWND>(99), DirPrev));

   return TEST_SUCCESS;
}

CBELT_TEST(move_window_single_returns_false) {
   auto ml = std::make_unique<TestMonocle>();
   ml->insert(reinterpret_cast<HWND>(1), reinterpret_cast<HWND>(0));

   cbelt_assert_false(ml->move_window(reinterpret_cast<HWND>(1), DirNext));
   cbelt_assert_false(ml->move_window(reinterpret_cast<HWND>(1), DirPrev));

   return TEST_SUCCESS;
}

/* =========================================================================
 * N/A operations
 * ========================================================================= */

CBELT_TEST(na_resize) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert_false(
       ml->resize_window(reinterpret_cast<HWND>(1), DirRight, 50));
   cbelt_assert_false(ml->resize_window_to_rect(reinterpret_cast<HWND>(1),
                                                RECT{0, 0, 100, 100}));
   cbelt_assert_false(ml->toggle_split(reinterpret_cast<HWND>(1)));
   cbelt_assert_false(ml->swap_split(reinterpret_cast<HWND>(1)));

   return TEST_SUCCESS;
}

/* =========================================================================
 * ApplyConfig
 * ========================================================================= */

CBELT_TEST(apply_config_updates_config) {
   auto ml = std::make_unique<TestMonocle>();
   RECT new_rect = {.left = 100, .top = 100, .right = 1500, .bottom = 900};
   LayoutConfig cfg = {4, 8, 0, new_rect};
   ml->apply_config(cfg);

   cbelt_assert_true(4 == ml->gap_between);
   cbelt_assert_true(8 == ml->gap_edge);
   /* apply_config subtracts gap_edge from workspace_rect */
   cbelt_assert_true(108 == ml->workspace_rect.left);
   cbelt_assert_true(108 == ml->workspace_rect.top);
   cbelt_assert_true(1492 == ml->workspace_rect.right);
   cbelt_assert_true(892 == ml->workspace_rect.bottom);

   return TEST_SUCCESS;
}

CBELT_TEST(apply_config_recalculates_tiled_rect) {
   auto ml = std::make_unique<TestMonocle>();
   RECT new_rect = {.left = 50, .top = 50, .right = 1850, .bottom = 1000};
   LayoutConfig cfg = {0, 5, 0, new_rect};
   ml->apply_config(cfg);

   RECT out;
   ml->get_rect(reinterpret_cast<HWND>(1), &out);
   cbelt_assert_true(55 == out.left);
   cbelt_assert_true(55 == out.top);
   cbelt_assert_true(1845 == out.right);
   cbelt_assert_true(995 == out.bottom);

   return TEST_SUCCESS;
}

CBELT_TEST(border_compensation_insets_rect) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   auto ml = std::make_unique<TestMonocle>();
   LayoutConfig cfg = {0, 0, 3, workspace};
   ml->apply_config(cfg);

   RECT out;
   cbelt_assert_true(ml->get_rect(reinterpret_cast<HWND>(1), &out));
   cbelt_assert_true(3 == out.left);
   cbelt_assert_true(3 == out.top);
   cbelt_assert_true(1917 == out.right);
   cbelt_assert_true(1077 == out.bottom);

   return TEST_SUCCESS;
}

CBELT_TEST(border_compensation_with_edge_gap) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   auto ml = std::make_unique<TestMonocle>();
   LayoutConfig cfg = {0, 10, 3, workspace};
   ml->apply_config(cfg);

   RECT out;
   cbelt_assert_true(ml->get_rect(reinterpret_cast<HWND>(1), &out));
   cbelt_assert_true(13 == out.left);
   cbelt_assert_true(13 == out.top);
   cbelt_assert_true(1907 == out.right);
   cbelt_assert_true(1067 == out.bottom);

   return TEST_SUCCESS;
}

/* =========================================================================
 * VTable integration
 * ========================================================================= */

CBELT_TEST(vtable_type_is_monocle) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   std::unique_ptr<LayoutEngine> engine =
       MonocleLayoutEngineCreate(workspace, 0, 0, 0);
   cbelt_assert_not_null(engine);
   cbelt_assert_true(MONOCLE == engine->type());
   return TEST_SUCCESS;
}

CBELT_TEST(vtable_insert_and_get_rect) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   std::unique_ptr<LayoutEngine> engine =
       MonocleLayoutEngineCreate(workspace, 6, 4, 0);
   cbelt_assert_not_null(engine);

   cbelt_assert_true(
       engine->insert(reinterpret_cast<HWND>(1), reinterpret_cast<HWND>(0)));
   cbelt_assert_true(
       engine->insert(reinterpret_cast<HWND>(2), reinterpret_cast<HWND>(0)));
   engine->apply();

   RECT r1;
   RECT r2;
   cbelt_assert_true(engine->get_rect(reinterpret_cast<HWND>(1), &r1));
   cbelt_assert_true(engine->get_rect(reinterpret_cast<HWND>(2), &r2));
   cbelt_assert_true(0 == memcmp(&r1, &r2, sizeof(RECT)));

   return TEST_SUCCESS;
}

CBELT_TEST(vtable_get_neighbor) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   std::unique_ptr<LayoutEngine> engine =
       MonocleLayoutEngineCreate(workspace, 0, 0, 0);
   cbelt_assert_not_null(engine);

   engine->insert(reinterpret_cast<HWND>(10), reinterpret_cast<HWND>(0));
   engine->insert(reinterpret_cast<HWND>(20), reinterpret_cast<HWND>(0));

   cbelt_assert_true(reinterpret_cast<HWND>(10) ==
                     engine->get_closest_window(nullptr));
   cbelt_assert_true(reinterpret_cast<HWND>(20) ==
                     engine->get_neighbor(reinterpret_cast<HWND>(10), DirNext));
   cbelt_assert_null(engine->get_neighbor(reinterpret_cast<HWND>(20), DirNext));
   cbelt_assert_null(engine->get_neighbor(reinterpret_cast<HWND>(10), DirPrev));
   cbelt_assert_null(engine->get_neighbor(reinterpret_cast<HWND>(10), DirLeft));

   return TEST_SUCCESS;
}

CBELT_TEST(vtable_set_active_window_updates_closest) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   std::unique_ptr<LayoutEngine> engine =
       MonocleLayoutEngineCreate(workspace, 0, 0, 0);
   cbelt_assert_not_null(engine);

   engine->insert(reinterpret_cast<HWND>(10), reinterpret_cast<HWND>(0));
   engine->insert(reinterpret_cast<HWND>(20), reinterpret_cast<HWND>(0));
   engine->insert(reinterpret_cast<HWND>(30), reinterpret_cast<HWND>(0));

   // Initially the first inserted window is shown.
   cbelt_assert_true(reinterpret_cast<HWND>(10) ==
                     engine->get_closest_window(nullptr));

   // set_active_window mirrors the WM focus; get_closest_window(nullptr)
   // should now report the focused window.
   engine->set_active_window(reinterpret_cast<HWND>(20));
   cbelt_assert_true(reinterpret_cast<HWND>(20) ==
                     engine->get_closest_window(nullptr));

   engine->set_active_window(reinterpret_cast<HWND>(30));
   cbelt_assert_true(reinterpret_cast<HWND>(30) ==
                     engine->get_closest_window(nullptr));

   // An unmanaged window must not change the selection.
   engine->set_active_window(reinterpret_cast<HWND>(999));
   cbelt_assert_true(reinterpret_cast<HWND>(30) ==
                     engine->get_closest_window(nullptr));

   return TEST_SUCCESS;
}

CBELT_TEST(vtable_na_slots) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   std::unique_ptr<LayoutEngine> engine =
       MonocleLayoutEngineCreate(workspace, 0, 0, 0);
   cbelt_assert_not_null(engine);

   cbelt_assert_false(
       engine->resize_window(reinterpret_cast<HWND>(1), DirRight, 50));
   cbelt_assert_false(engine->resize_window_to_rect(reinterpret_cast<HWND>(1),
                                                    RECT{0, 0, 100, 100}));
   cbelt_assert_false(engine->toggle_split(reinterpret_cast<HWND>(1)));
   cbelt_assert_false(engine->swap_split(reinterpret_cast<HWND>(1)));

   return TEST_SUCCESS;
}

CBELT_TEST(vtable_apply_config) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   std::unique_ptr<LayoutEngine> engine =
       MonocleLayoutEngineCreate(workspace, 0, 0, 0);
   cbelt_assert_not_null(engine);

   LayoutConfig cfg = {0, 6, 0, workspace};
   engine->apply_config(cfg);

   /* After apply_config, gap should be reflected in get_rect */
   RECT out;
   engine->get_rect(reinterpret_cast<HWND>(1), &out);
   cbelt_assert_true(6 == out.left);
   cbelt_assert_true(6 == out.top);
   cbelt_assert_true(1914 == out.right);
   cbelt_assert_true(1074 == out.bottom);

   return TEST_SUCCESS;
}