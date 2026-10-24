#include "cbelt.h"
#include <array>
#include <memory>
#include <windef.h>
#include <windows.h>

#include "layout_test_access.h"

CBELT_GROUP("master")

/* =========================================================================
 * Null / boundary safety
 * ========================================================================= */

CBELT_TEST(null_safety) {
   /* Default-constructed empty TestMaster. */
   TestMaster engine;

   /* get_closest_window on an empty engine returns nullptr. */
   cbelt_assert_null(engine.get_closest_window(nullptr));

   /* Recalculate (triggered through apply_config) on an empty engine must
    * not crash. */
   const LayoutConfig empty_config = {};
   engine.apply_config(empty_config);

   /* Insert on an empty engine succeeds. */
   HWND const win1 = reinterpret_cast<HWND>(1);
   cbelt_assert_true(engine.insert(win1, reinterpret_cast<HWND>(0)));

   /* remove on a nonexistent window returns false. */
   HWND const win2 = reinterpret_cast<HWND>(2);
   cbelt_assert_false(engine.remove(win2));

   /* get_rect on a nonexistent window returns false. */
   RECT out;
   cbelt_assert_false(engine.get_rect(win2, &out));

   /* get_neighbor on a nonexistent window returns nullptr. */
   cbelt_assert_null(engine.get_neighbor(win2, DirNext));
   return TEST_SUCCESS;
}

/* =========================================================================
 * Geometry
 * ========================================================================= */

CBELT_TEST(single_window_fills_area) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   auto ml = std::make_unique<TestMaster>();
   ml->apply_config(LayoutConfig{.gap_between = 0,
                                 .gap_edge = 0,
                                 .border_width = 0,
                                 .workspace_rect = workspace});

   HWND win1 = reinterpret_cast<HWND>(1);
   cbelt_assert_true(ml->insert(win1, reinterpret_cast<HWND>(0)));

   RECT out;
   cbelt_assert_true(ml->get_rect(win1, &out));
   cbelt_assert_true(0 == out.left);
   cbelt_assert_true(0 == out.top);
   cbelt_assert_true(1920 == out.right);
   cbelt_assert_true(1080 == out.bottom);

   return TEST_SUCCESS;
}

CBELT_TEST(master_and_stack_geometry) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   auto ml = std::make_unique<TestMaster>();
   ml->apply_config(LayoutConfig{.gap_between = 0,
                                 .gap_edge = 0,
                                 .border_width = 0,
                                 .workspace_rect = workspace});

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   HWND win3 = reinterpret_cast<HWND>(3);
   cbelt_assert_true(ml->insert(win1, reinterpret_cast<HWND>(0)));
   cbelt_assert_true(ml->insert(win2, reinterpret_cast<HWND>(0)));
   cbelt_assert_true(ml->insert(win3, reinterpret_cast<HWND>(0)));

   RECT r1;
   RECT r2;
   RECT r3;
   cbelt_assert_true(ml->get_rect(win1, &r1));
   cbelt_assert_true(ml->get_rect(win2, &r2));
   cbelt_assert_true(ml->get_rect(win3, &r3));

   /* Defaults: master_count=1, master_factor=0.5, gaps=0. */
   cbelt_assert_true(0 == r1.left);
   cbelt_assert_true(0 == r1.top);
   cbelt_assert_true(960 == r1.right);
   cbelt_assert_true(1080 == r1.bottom);

   cbelt_assert_true(960 == r2.left);
   cbelt_assert_true(0 == r2.top);
   cbelt_assert_true(1920 == r2.right);
   cbelt_assert_true(540 == r2.bottom);

   cbelt_assert_true(960 == r3.left);
   cbelt_assert_true(540 == r3.top);
   cbelt_assert_true(1920 == r3.right);
   cbelt_assert_true(1080 == r3.bottom);

   return TEST_SUCCESS;
}

/* =========================================================================
 * GetNeighbor
 * ========================================================================= */

namespace {

inline void insert_n(TestMaster *ml, int n) {
   for (int i = 1; i <= n; i++) {
      ml->insert(reinterpret_cast<HWND>(static_cast<uintptr_t>(i)),
                 reinterpret_cast<HWND>(0));
   }
}
} // namespace

CBELT_TEST(neighbor_edges) {
   auto ml = std::make_unique<TestMaster>();
   insert_n(ml.get(), 3);

   HWND const win1 = reinterpret_cast<HWND>(1);
   HWND const win2 = reinterpret_cast<HWND>(2);
   HWND const win3 = reinterpret_cast<HWND>(3);

   /* Left of the master is the outer edge. */
   cbelt_assert_null(ml->get_neighbor(win1, DirLeft));
   /* Master -> first stack window. */
   cbelt_assert_true(win2 == ml->get_neighbor(win1, DirRight));
   /* Stack -> master. */
   cbelt_assert_true(win1 == ml->get_neighbor(win2, DirLeft));
   /* Right of the stack is the outer edge. */
   cbelt_assert_null(ml->get_neighbor(win2, DirRight));
   /* Top of the stack is the edge. */
   cbelt_assert_null(ml->get_neighbor(win2, DirUp));
   /* Bottom of the stack is the edge. */
   cbelt_assert_null(ml->get_neighbor(win3, DirDown));
   /* Within the stack. */
   cbelt_assert_true(win3 == ml->get_neighbor(win2, DirDown));

   return TEST_SUCCESS;
}

/* =========================================================================
 * GetClosestWindow
 * ========================================================================= */

CBELT_TEST(closest_window_returns_selected) {
   auto ml = std::make_unique<TestMaster>();
   insert_n(ml.get(), 3);

   HWND const win1 = reinterpret_cast<HWND>(1);
   HWND const win3 = reinterpret_cast<HWND>(3);

   cbelt_assert_true(win1 == ml->get_closest_window(nullptr));

   ml->selected_hwnd = win3;
   cbelt_assert_true(win3 == ml->get_closest_window(nullptr));

   return TEST_SUCCESS;
}

/* =========================================================================
 * VTable integration
 * ========================================================================= */

CBELT_TEST(vtable_type_is_master) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   std::unique_ptr<LayoutEngine> engine =
       MasterLayoutEngineCreate(workspace, 0, 0, 0);
   cbelt_assert_not_null(engine);
   cbelt_assert_true(MASTER == engine->type());
   return TEST_SUCCESS;
}

CBELT_TEST(vtable_insert_and_focus) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   std::unique_ptr<LayoutEngine> engine =
       MasterLayoutEngineCreate(workspace, 0, 0, 0);
   cbelt_assert_not_null(engine);

   HWND const win10 = reinterpret_cast<HWND>(10);
   HWND const win20 = reinterpret_cast<HWND>(20);
   HWND const win30 = reinterpret_cast<HWND>(30);
   cbelt_assert_true(engine->insert(win10, reinterpret_cast<HWND>(0)));
   cbelt_assert_true(engine->insert(win20, reinterpret_cast<HWND>(0)));
   cbelt_assert_true(engine->insert(win30, reinterpret_cast<HWND>(0)));

   /* First insert becomes the selection. */
   cbelt_assert_true(win10 == engine->get_closest_window(nullptr));

   /* Focus tracking moves the selection. */
   engine->set_active_window(win20);
   cbelt_assert_true(win20 == engine->get_closest_window(nullptr));

   /* Master|stack boundary sits at the master's right edge (gaps are 0). */
   RECT r1;
   RECT r2;
   cbelt_assert_true(engine->get_rect(win10, &r1));
   cbelt_assert_true(engine->get_rect(win20, &r2));
   cbelt_assert_true(r2.left == r1.right);

   return TEST_SUCCESS;
}