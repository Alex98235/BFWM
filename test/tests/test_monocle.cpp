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
   cbelt_assert(ml->insert(reinterpret_cast<HWND>(1),
                           reinterpret_cast<HWND>(0)) == true);

   /* Remove on non-existent window returns false */
   cbelt_assert(ml->remove(reinterpret_cast<HWND>(99)) == false);

   /* get_rect with null out returns false */
   cbelt_assert(ml->get_rect(reinterpret_cast<HWND>(1), nullptr) == false);

   /* get_rect on valid engine returns true */
   RECT out;
   cbelt_assert(ml->get_rect(reinterpret_cast<HWND>(1), &out) == true);

   /* get_neighbor on non-existent window returns nullptr */
   cbelt_assert(ml->get_neighbor(reinterpret_cast<HWND>(99), DirNext) ==
                nullptr);

   /* get_closest_window on empty engine returns nullptr */
   ml->remove(reinterpret_cast<HWND>(1));
   cbelt_assert(ml->get_closest_window(nullptr) == nullptr);

   /* apply() on empty engine should not crash */
   ml->apply();

   return TEST_SUCCESS;
}

CBELT_TEST(empty_layout_returns_null) {
   auto ml = std::make_unique<TestMonocle>();
   cbelt_assert(ml != nullptr);

   cbelt_assert(ml->get_neighbor(reinterpret_cast<HWND>(1), DirNext) ==
                nullptr);
   cbelt_assert(ml->get_closest_window(nullptr) == nullptr);

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
   cbelt_assert(ml != nullptr);
   cbelt_assert(ml->windows.empty());
   cbelt_assert(ml->workspace_rect.left == 0);
   cbelt_assert(ml->workspace_rect.top == 0);
   cbelt_assert(ml->workspace_rect.right == 1920);
   cbelt_assert(ml->workspace_rect.bottom == 1080);
   cbelt_assert(ml->gap_between == 0);
   cbelt_assert(ml->gap_edge == 0);
   cbelt_assert(ml->tiled_rect.left == 0);
   cbelt_assert(ml->tiled_rect.top == 0);
   cbelt_assert(ml->tiled_rect.right == 1920);
   cbelt_assert(ml->tiled_rect.bottom == 1080);
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
   cbelt_assert(ml != nullptr);
   cbelt_assert(ml->windows.empty());
   return TEST_SUCCESS;
}

/* =========================================================================
 * Insert
 * ========================================================================= */

CBELT_TEST(insert_first_window) {
   auto ml = std::make_unique<TestMonocle>();

   HWND win1 = reinterpret_cast<HWND>(1);
   cbelt_assert(ml->insert(win1, reinterpret_cast<HWND>(0)) == true);
   cbelt_assert(ml->windows.size() == 1);
   cbelt_assert(ml->windows[0] == win1);
   cbelt_assert(ml->get_closest_window(nullptr) == win1);

   return TEST_SUCCESS;
}

CBELT_TEST(insert_multiple) {
   auto ml = std::make_unique<TestMonocle>();

   std::array<HWND, 5> wins = {
       reinterpret_cast<HWND>(1), reinterpret_cast<HWND>(2),
       reinterpret_cast<HWND>(3), reinterpret_cast<HWND>(4),
       reinterpret_cast<HWND>(5)};
   for (int i = 0; i < 5; i++) {
      cbelt_assert(ml->insert(wins[i], reinterpret_cast<HWND>(0)) == true);
   }
   cbelt_assert(ml->windows.size() == 5);
   for (int i = 0; i < 5; i++) {
      cbelt_assert(ml->windows[i] == wins[i]);
   }

   return TEST_SUCCESS;
}

CBELT_TEST(insert_ignores_focus) {
   auto ml = std::make_unique<TestMonocle>();

   cbelt_assert(ml->insert(reinterpret_cast<HWND>(1),
                           reinterpret_cast<HWND>(42)) == true);
   cbelt_assert(ml->insert(reinterpret_cast<HWND>(2),
                           reinterpret_cast<HWND>(99)) == true);
   cbelt_assert(ml->windows.size() == 2);
   cbelt_assert(ml->windows[0] == reinterpret_cast<HWND>(1));
   cbelt_assert(ml->windows[1] == reinterpret_cast<HWND>(2));

   return TEST_SUCCESS;
}

CBELT_TEST(insert_grows_array) {
   auto ml = std::make_unique<TestMonocle>();

   for (size_t i = 0; i < 20; i++) {
      cbelt_assert(ml->insert((HWND)(i + 1), reinterpret_cast<HWND>(0)) ==
                   true);
   }
   cbelt_assert(ml->windows.size() == 20);
   cbelt_assert(ml->windows.capacity() >= 20);
   for (size_t i = 0; i < 20; i++) {
      cbelt_assert(ml->windows[i] == (HWND)(i + 1));
   }

   return TEST_SUCCESS;
}

CBELT_TEST(insert_duplicate_window_keeps_single_entry) {
   auto ml = std::make_unique<TestMonocle>();

   HWND win1 = reinterpret_cast<HWND>(1);
   cbelt_assert(ml->insert(win1, reinterpret_cast<HWND>(0)) == true);
   cbelt_assert(ml->windows.size() == 1);

   /* Re-inserting the same window is a no-op — no duplicate entry */
   cbelt_assert(ml->insert(win1, reinterpret_cast<HWND>(0)) == true);
   cbelt_assert(ml->windows.size() == 1);
   cbelt_assert(ml->windows[0] == win1);

   /* A single remove takes it out entirely */
   cbelt_assert(ml->remove(win1) == true);
   cbelt_assert(ml->windows.empty());

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

   cbelt_assert(ml->remove(reinterpret_cast<HWND>(1)) == true);
   cbelt_assert(ml->windows.empty());

   return TEST_SUCCESS;
}

CBELT_TEST(remove_first) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert(ml->remove(reinterpret_cast<HWND>(1)) == true);
   cbelt_assert(ml->windows.size() == 2);
   cbelt_assert(ml->windows[0] == reinterpret_cast<HWND>(2));
   cbelt_assert(ml->windows[1] == reinterpret_cast<HWND>(3));

   return TEST_SUCCESS;
}

CBELT_TEST(remove_middle) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert(ml->remove(reinterpret_cast<HWND>(2)) == true);
   cbelt_assert(ml->windows.size() == 2);
   cbelt_assert(ml->windows[0] == reinterpret_cast<HWND>(1));
   cbelt_assert(ml->windows[1] == reinterpret_cast<HWND>(3));

   return TEST_SUCCESS;
}

CBELT_TEST(remove_last) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert(ml->remove(reinterpret_cast<HWND>(3)) == true);
   cbelt_assert(ml->windows.size() == 2);
   cbelt_assert(ml->windows[0] == reinterpret_cast<HWND>(1));
   cbelt_assert(ml->windows[1] == reinterpret_cast<HWND>(2));

   return TEST_SUCCESS;
}

CBELT_TEST(remove_nonexistent) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert(ml->remove(reinterpret_cast<HWND>(99)) == false);
   cbelt_assert(ml->windows.size() == 3);

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
   cbelt_assert(ml->get_rect(reinterpret_cast<HWND>(1), &out1) == true);
   cbelt_assert(ml->get_rect(reinterpret_cast<HWND>(2), &out2) == true);
   cbelt_assert(ml->get_rect(reinterpret_cast<HWND>(3), &out3) == true);
   cbelt_assert(out1.left == out2.left);
   cbelt_assert(out1.top == out2.top);
   cbelt_assert(out1.right == out2.right);
   cbelt_assert(out1.bottom == out2.bottom);
   cbelt_assert(memcmp(&out1, &out3, sizeof(RECT)) == 0);

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
   cbelt_assert(ml->get_rect(reinterpret_cast<HWND>(1), &out) == true);
   cbelt_assert(out.left == 100);
   cbelt_assert(out.top == 50);
   cbelt_assert(out.right == 1900);
   cbelt_assert(out.bottom == 1050);

   return TEST_SUCCESS;
}

CBELT_TEST(rect_with_edge_gap) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   auto ml = std::make_unique<TestMonocle>();
   LayoutConfig cfg = {0, 10, 0, workspace};
   ml->apply_config(cfg);

   RECT out;
   cbelt_assert(ml->get_rect(reinterpret_cast<HWND>(1), &out) == true);
   cbelt_assert(out.left == 10);
   cbelt_assert(out.top == 10);
   cbelt_assert(out.right == 1910);
   cbelt_assert(out.bottom == 1070);

   return TEST_SUCCESS;
}

/* =========================================================================
 * GetNeighbor
 * ========================================================================= */

CBELT_TEST(get_neighbor_next_no_wrap) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert(ml->get_neighbor(reinterpret_cast<HWND>(1), DirNext) ==
                reinterpret_cast<HWND>(2));
   cbelt_assert(ml->get_neighbor(reinterpret_cast<HWND>(2), DirNext) ==
                reinterpret_cast<HWND>(3));
   cbelt_assert(ml->get_neighbor(reinterpret_cast<HWND>(3), DirNext) ==
                nullptr);

   return TEST_SUCCESS;
}

CBELT_TEST(get_neighbor_prev_no_wrap) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert(ml->get_neighbor(reinterpret_cast<HWND>(1), DirPrev) ==
                nullptr);
   cbelt_assert(ml->get_neighbor(reinterpret_cast<HWND>(2), DirPrev) ==
                reinterpret_cast<HWND>(1));
   cbelt_assert(ml->get_neighbor(reinterpret_cast<HWND>(3), DirPrev) ==
                reinterpret_cast<HWND>(2));

   return TEST_SUCCESS;
}

CBELT_TEST(get_neighbor_single_window) {
   auto ml = std::make_unique<TestMonocle>();
   ml->insert(reinterpret_cast<HWND>(1), reinterpret_cast<HWND>(0));

   cbelt_assert(ml->get_neighbor(reinterpret_cast<HWND>(1), DirNext) ==
                nullptr);
   cbelt_assert(ml->get_neighbor(reinterpret_cast<HWND>(1), DirPrev) ==
                nullptr);

   return TEST_SUCCESS;
}

CBELT_TEST(get_neighbor_geometric) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert(ml->get_neighbor(reinterpret_cast<HWND>(2), DirLeft) ==
                reinterpret_cast<HWND>(1));
   cbelt_assert(ml->get_neighbor(reinterpret_cast<HWND>(2), DirRight) ==
                reinterpret_cast<HWND>(3));
   cbelt_assert(ml->get_neighbor(reinterpret_cast<HWND>(2), DirUp) == nullptr);
   cbelt_assert(ml->get_neighbor(reinterpret_cast<HWND>(2), DirDown) ==
                nullptr);

   return TEST_SUCCESS;
}

CBELT_TEST(get_neighbor_nonexistent_returns_null) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert(ml->get_neighbor(reinterpret_cast<HWND>(99), DirNext) ==
                nullptr);
   cbelt_assert(ml->get_neighbor(reinterpret_cast<HWND>(99), DirPrev) ==
                nullptr);
   cbelt_assert(ml->get_neighbor(reinterpret_cast<HWND>(99), DirLeft) ==
                nullptr);

   return TEST_SUCCESS;
}

/* =========================================================================
 * GetClosestWindow
 * ========================================================================= */

CBELT_TEST(get_closest_window_returns_first_on_null) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 5);

   cbelt_assert(ml->get_closest_window(nullptr) == reinterpret_cast<HWND>(1));

   return TEST_SUCCESS;
}

CBELT_TEST(get_closest_window_empty_returns_null) {
   auto ml = std::make_unique<TestMonocle>();
   cbelt_assert(ml->get_closest_window(nullptr) == nullptr);
   return TEST_SUCCESS;
}

CBELT_TEST(get_closest_window_returns_selected_window) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3); // win1, win2, win3

   // The first inserted window is the initially-shown one.
   cbelt_assert(ml->get_closest_window(nullptr) == reinterpret_cast<HWND>(1));

   // The currently-shown window is tracked by HWND, so moving focus to a
   // later window changes what get_closest_window(nullptr) reports.
   ml->selected_hwnd = reinterpret_cast<HWND>(2);
   cbelt_assert(ml->get_closest_window(nullptr) == reinterpret_cast<HWND>(2));

   ml->selected_hwnd = reinterpret_cast<HWND>(3);
   cbelt_assert(ml->get_closest_window(nullptr) == reinterpret_cast<HWND>(3));

   return TEST_SUCCESS;
}

CBELT_TEST(removing_shown_window_advances_selection) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3); // win1, win2, win3; selected = win1

   // Focus moves to win2, then win2 is removed.
   ml->selected_hwnd = reinterpret_cast<HWND>(2);
   cbelt_assert(ml->remove(reinterpret_cast<HWND>(2)) == true);
   // Selection advances to the window now at win2's old index (win3).
   cbelt_assert(ml->selected_hwnd == reinterpret_cast<HWND>(3));
   cbelt_assert(ml->get_closest_window(nullptr) == reinterpret_cast<HWND>(3));

   // Remove the currently-shown window again; selection advances to win1.
   cbelt_assert(ml->remove(reinterpret_cast<HWND>(3)) == true);
   cbelt_assert(ml->selected_hwnd == reinterpret_cast<HWND>(1));
   cbelt_assert(ml->get_closest_window(nullptr) == reinterpret_cast<HWND>(1));

   // Remove the last window; selection becomes null.
   cbelt_assert(ml->remove(reinterpret_cast<HWND>(1)) == true);
   cbelt_assert(ml->selected_hwnd == nullptr);
   cbelt_assert(ml->get_closest_window(nullptr) == nullptr);

   return TEST_SUCCESS;
}

/* =========================================================================
 * MoveWindow
 * ========================================================================= */

CBELT_TEST(move_window_next_swap) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert(ml->move_window(reinterpret_cast<HWND>(1), DirNext) == true);
   cbelt_assert(ml->windows[0] == reinterpret_cast<HWND>(2));
   cbelt_assert(ml->windows[1] == reinterpret_cast<HWND>(1));
   cbelt_assert(ml->windows[2] == reinterpret_cast<HWND>(3));

   return TEST_SUCCESS;
}

CBELT_TEST(move_window_prev_swap) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert(ml->move_window(reinterpret_cast<HWND>(2), DirPrev) == true);
   cbelt_assert(ml->windows[0] == reinterpret_cast<HWND>(2));
   cbelt_assert(ml->windows[1] == reinterpret_cast<HWND>(1));
   cbelt_assert(ml->windows[2] == reinterpret_cast<HWND>(3));

   return TEST_SUCCESS;
}

CBELT_TEST(move_window_first_prev_wraps_to_last) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert(ml->move_window(reinterpret_cast<HWND>(1), DirPrev) == true);
   cbelt_assert(ml->windows[0] == reinterpret_cast<HWND>(3));
   cbelt_assert(ml->windows[1] == reinterpret_cast<HWND>(2));
   cbelt_assert(ml->windows[2] == reinterpret_cast<HWND>(1));

   return TEST_SUCCESS;
}

CBELT_TEST(move_window_last_next_wraps_to_first) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert(ml->move_window(reinterpret_cast<HWND>(3), DirNext) == true);
   cbelt_assert(ml->windows[0] == reinterpret_cast<HWND>(3));
   cbelt_assert(ml->windows[1] == reinterpret_cast<HWND>(2));
   cbelt_assert(ml->windows[2] == reinterpret_cast<HWND>(1));

   return TEST_SUCCESS;
}

CBELT_TEST(move_window_geometric_returns_false) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert(ml->move_window(reinterpret_cast<HWND>(2), DirLeft) == false);
   cbelt_assert(ml->move_window(reinterpret_cast<HWND>(2), DirRight) == false);
   cbelt_assert(ml->move_window(reinterpret_cast<HWND>(2), DirUp) == false);
   cbelt_assert(ml->move_window(reinterpret_cast<HWND>(2), DirDown) == false);

   return TEST_SUCCESS;
}

CBELT_TEST(move_window_nonexistent_returns_false) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert(ml->move_window(reinterpret_cast<HWND>(99), DirNext) == false);
   cbelt_assert(ml->move_window(reinterpret_cast<HWND>(99), DirPrev) == false);

   return TEST_SUCCESS;
}

CBELT_TEST(move_window_single_returns_false) {
   auto ml = std::make_unique<TestMonocle>();
   ml->insert(reinterpret_cast<HWND>(1), reinterpret_cast<HWND>(0));

   cbelt_assert(ml->move_window(reinterpret_cast<HWND>(1), DirNext) == false);
   cbelt_assert(ml->move_window(reinterpret_cast<HWND>(1), DirPrev) == false);

   return TEST_SUCCESS;
}

/* =========================================================================
 * N/A operations
 * ========================================================================= */

CBELT_TEST(na_resize) {
   auto ml = std::make_unique<TestMonocle>();
   insert_n(ml.get(), 3);

   cbelt_assert(ml->resize_window(reinterpret_cast<HWND>(1), DirRight, 50) ==
                false);
   cbelt_assert(ml->resize_window_to_rect(reinterpret_cast<HWND>(1),
                                          RECT{0, 0, 100, 100}) == false);
   cbelt_assert(ml->toggle_split(reinterpret_cast<HWND>(1)) == false);
   cbelt_assert(ml->swap_split(reinterpret_cast<HWND>(1)) == false);

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

   cbelt_assert(ml->gap_between == 4);
   cbelt_assert(ml->gap_edge == 8);
   /* apply_config subtracts gap_edge from workspace_rect */
   cbelt_assert(ml->workspace_rect.left == 108);
   cbelt_assert(ml->workspace_rect.top == 108);
   cbelt_assert(ml->workspace_rect.right == 1492);
   cbelt_assert(ml->workspace_rect.bottom == 892);

   return TEST_SUCCESS;
}

CBELT_TEST(apply_config_recalculates_tiled_rect) {
   auto ml = std::make_unique<TestMonocle>();
   RECT new_rect = {.left = 50, .top = 50, .right = 1850, .bottom = 1000};
   LayoutConfig cfg = {0, 5, 0, new_rect};
   ml->apply_config(cfg);

   RECT out;
   ml->get_rect(reinterpret_cast<HWND>(1), &out);
   cbelt_assert(out.left == 55);
   cbelt_assert(out.top == 55);
   cbelt_assert(out.right == 1845);
   cbelt_assert(out.bottom == 995);

   return TEST_SUCCESS;
}

CBELT_TEST(border_compensation_insets_rect) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   auto ml = std::make_unique<TestMonocle>();
   LayoutConfig cfg = {0, 0, 3, workspace};
   ml->apply_config(cfg);

   RECT out;
   cbelt_assert(ml->get_rect(reinterpret_cast<HWND>(1), &out) == true);
   cbelt_assert(out.left == 3);
   cbelt_assert(out.top == 3);
   cbelt_assert(out.right == 1917);
   cbelt_assert(out.bottom == 1077);

   return TEST_SUCCESS;
}

CBELT_TEST(border_compensation_with_edge_gap) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   auto ml = std::make_unique<TestMonocle>();
   LayoutConfig cfg = {0, 10, 3, workspace};
   ml->apply_config(cfg);

   RECT out;
   cbelt_assert(ml->get_rect(reinterpret_cast<HWND>(1), &out) == true);
   cbelt_assert(out.left == 13);
   cbelt_assert(out.top == 13);
   cbelt_assert(out.right == 1907);
   cbelt_assert(out.bottom == 1067);

   return TEST_SUCCESS;
}

/* =========================================================================
 * VTable integration
 * ========================================================================= */

CBELT_TEST(vtable_type_is_monocle) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   std::unique_ptr<LayoutEngine> engine =
       MonocleLayoutEngineCreate(workspace, 0, 0, 0);
   cbelt_assert(engine != nullptr);
   cbelt_assert(engine->type() == MONOCLE);
   return TEST_SUCCESS;
}

CBELT_TEST(vtable_insert_and_get_rect) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   std::unique_ptr<LayoutEngine> engine =
       MonocleLayoutEngineCreate(workspace, 6, 4, 0);
   cbelt_assert(engine != nullptr);

   cbelt_assert(engine->insert(reinterpret_cast<HWND>(1),
                               reinterpret_cast<HWND>(0)) == true);
   cbelt_assert(engine->insert(reinterpret_cast<HWND>(2),
                               reinterpret_cast<HWND>(0)) == true);
   engine->apply();

   RECT r1;
   RECT r2;
   cbelt_assert(engine->get_rect(reinterpret_cast<HWND>(1), &r1) == true);
   cbelt_assert(engine->get_rect(reinterpret_cast<HWND>(2), &r2) == true);
   cbelt_assert(memcmp(&r1, &r2, sizeof(RECT)) == 0);

   return TEST_SUCCESS;
}

CBELT_TEST(vtable_get_neighbor) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   std::unique_ptr<LayoutEngine> engine =
       MonocleLayoutEngineCreate(workspace, 0, 0, 0);
   cbelt_assert(engine != nullptr);

   engine->insert(reinterpret_cast<HWND>(10), reinterpret_cast<HWND>(0));
   engine->insert(reinterpret_cast<HWND>(20), reinterpret_cast<HWND>(0));

   cbelt_assert(engine->get_closest_window(nullptr) ==
                reinterpret_cast<HWND>(10));
   cbelt_assert(engine->get_neighbor(reinterpret_cast<HWND>(10), DirNext) ==
                reinterpret_cast<HWND>(20));
   cbelt_assert(engine->get_neighbor(reinterpret_cast<HWND>(20), DirNext) ==
                nullptr);
   cbelt_assert(engine->get_neighbor(reinterpret_cast<HWND>(10), DirPrev) ==
                nullptr);
   cbelt_assert(engine->get_neighbor(reinterpret_cast<HWND>(10), DirLeft) ==
                nullptr);

   return TEST_SUCCESS;
}

CBELT_TEST(vtable_set_active_window_updates_closest) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   std::unique_ptr<LayoutEngine> engine =
       MonocleLayoutEngineCreate(workspace, 0, 0, 0);
   cbelt_assert(engine != nullptr);

   engine->insert(reinterpret_cast<HWND>(10), reinterpret_cast<HWND>(0));
   engine->insert(reinterpret_cast<HWND>(20), reinterpret_cast<HWND>(0));
   engine->insert(reinterpret_cast<HWND>(30), reinterpret_cast<HWND>(0));

   // Initially the first inserted window is shown.
   cbelt_assert(engine->get_closest_window(nullptr) ==
                reinterpret_cast<HWND>(10));

   // set_active_window mirrors the WM focus; get_closest_window(nullptr)
   // should now report the focused window.
   engine->set_active_window(reinterpret_cast<HWND>(20));
   cbelt_assert(engine->get_closest_window(nullptr) ==
                reinterpret_cast<HWND>(20));

   engine->set_active_window(reinterpret_cast<HWND>(30));
   cbelt_assert(engine->get_closest_window(nullptr) ==
                reinterpret_cast<HWND>(30));

   // An unmanaged window must not change the selection.
   engine->set_active_window(reinterpret_cast<HWND>(999));
   cbelt_assert(engine->get_closest_window(nullptr) ==
                reinterpret_cast<HWND>(30));

   return TEST_SUCCESS;
}

CBELT_TEST(vtable_na_slots) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   std::unique_ptr<LayoutEngine> engine =
       MonocleLayoutEngineCreate(workspace, 0, 0, 0);
   cbelt_assert(engine != nullptr);

   cbelt_assert(
       engine->resize_window(reinterpret_cast<HWND>(1), DirRight, 50) == false);
   cbelt_assert(engine->resize_window_to_rect(reinterpret_cast<HWND>(1),
                                              RECT{0, 0, 100, 100}) == false);
   cbelt_assert(engine->toggle_split(reinterpret_cast<HWND>(1)) == false);
   cbelt_assert(engine->swap_split(reinterpret_cast<HWND>(1)) == false);

   return TEST_SUCCESS;
}

CBELT_TEST(vtable_apply_config) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   std::unique_ptr<LayoutEngine> engine =
       MonocleLayoutEngineCreate(workspace, 0, 0, 0);
   cbelt_assert(engine != nullptr);

   LayoutConfig cfg = {0, 6, 0, workspace};
   engine->apply_config(cfg);

   /* After apply_config, gap should be reflected in get_rect */
   RECT out;
   engine->get_rect(reinterpret_cast<HWND>(1), &out);
   cbelt_assert(out.left == 6);
   cbelt_assert(out.top == 6);
   cbelt_assert(out.right == 1914);
   cbelt_assert(out.bottom == 1074);

   return TEST_SUCCESS;
}