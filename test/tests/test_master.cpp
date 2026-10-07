#include "cbelt.h"
#include <array>
#include <memory>
#include <windef.h>
#include <windows.h>

#include "../../src/workspace/layouts/master/master.h"
#include "../../src/workspace/layouts/master/master_vtable.h"

CBELT_GROUP("master")

/* =========================================================================
 * Null / boundary safety
 * ========================================================================= */

CBELT_TEST(null_safety) {
   cbelt_assert(MasterInsertWindow(nullptr, reinterpret_cast<HWND>(1), reinterpret_cast<HWND>(0)) == false);
   cbelt_assert(MasterRemoveWindow(nullptr, reinterpret_cast<HWND>(1)) == false);

   RECT out;
   cbelt_assert(MasterGetWindowRect(nullptr, reinterpret_cast<HWND>(1), &out) == false);
   cbelt_assert(MasterGetWindowRect(nullptr, reinterpret_cast<HWND>(1), nullptr) == false);

   cbelt_assert(MasterGetNeighbor(nullptr, reinterpret_cast<HWND>(1), DirNext) == nullptr);
   cbelt_assert(MasterGetClosestWindow(nullptr, nullptr) == nullptr);

   MasterRecalculate(nullptr);
   MasterLayoutFree(nullptr);
   const LayoutConfig empty_config = {};
   MasterLayoutApplyConfig(nullptr, empty_config);
   return TEST_SUCCESS;
}

/* =========================================================================
 * Geometry
 * ========================================================================= */

CBELT_TEST(single_window_fills_area) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   MasterLayout *ml = MasterLayoutCreate(workspace);

   HWND win1 = reinterpret_cast<HWND>(1);
   cbelt_assert(MasterInsertWindow(ml, win1, reinterpret_cast<HWND>(0)) == true);

   RECT out;
   cbelt_assert(MasterGetWindowRect(ml, win1, &out) == true);
   cbelt_assert(out.left == 0);
   cbelt_assert(out.top == 0);
   cbelt_assert(out.right == 1920);
   cbelt_assert(out.bottom == 1080);

   MasterLayoutFree(ml);
   return TEST_SUCCESS;
}

CBELT_TEST(master_and_stack_geometry) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   MasterLayout *ml = MasterLayoutCreate(workspace);

   HWND win1 = reinterpret_cast<HWND>(1);
   HWND win2 = reinterpret_cast<HWND>(2);
   HWND win3 = reinterpret_cast<HWND>(3);
   cbelt_assert(MasterInsertWindow(ml, win1, reinterpret_cast<HWND>(0)) == true);
   cbelt_assert(MasterInsertWindow(ml, win2, reinterpret_cast<HWND>(0)) == true);
   cbelt_assert(MasterInsertWindow(ml, win3, reinterpret_cast<HWND>(0)) == true);

   RECT r1;
   RECT r2;
   RECT r3;
   cbelt_assert(MasterGetWindowRect(ml, win1, &r1) == true);
   cbelt_assert(MasterGetWindowRect(ml, win2, &r2) == true);
   cbelt_assert(MasterGetWindowRect(ml, win3, &r3) == true);

   /* Defaults: master_count=1, master_factor=0.5, gaps=0. */
   cbelt_assert(r1.left == 0);
   cbelt_assert(r1.top == 0);
   cbelt_assert(r1.right == 960);
   cbelt_assert(r1.bottom == 1080);

   cbelt_assert(r2.left == 960);
   cbelt_assert(r2.top == 0);
   cbelt_assert(r2.right == 1920);
   cbelt_assert(r2.bottom == 540);

   cbelt_assert(r3.left == 960);
   cbelt_assert(r3.top == 540);
   cbelt_assert(r3.right == 1920);
   cbelt_assert(r3.bottom == 1080);

   MasterLayoutFree(ml);
   return TEST_SUCCESS;
}

/* =========================================================================
 * GetNeighbor
 * ========================================================================= */

namespace {

inline void insert_n(MasterLayout *ml, int n) {
   for (int i = 1; i <= n; i++)
      MasterInsertWindow(ml, reinterpret_cast<HWND>(static_cast<uintptr_t>(i)),
                         reinterpret_cast<HWND>(0));
}
} // namespace

CBELT_TEST(neighbor_edges) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   MasterLayout *ml = MasterLayoutCreate(workspace);
   insert_n(ml, 3);

   HWND const win1 = reinterpret_cast<HWND>(1);
   HWND const win2 = reinterpret_cast<HWND>(2);
   HWND const win3 = reinterpret_cast<HWND>(3);

   /* Left of the master is the outer edge. */
   cbelt_assert(MasterGetNeighbor(ml, win1, DirLeft) == nullptr);
   /* Master -> first stack window. */
   cbelt_assert(MasterGetNeighbor(ml, win1, DirRight) == win2);
   /* Stack -> master. */
   cbelt_assert(MasterGetNeighbor(ml, win2, DirLeft) == win1);
   /* Right of the stack is the outer edge. */
   cbelt_assert(MasterGetNeighbor(ml, win2, DirRight) == nullptr);
   /* Top of the stack is the edge. */
   cbelt_assert(MasterGetNeighbor(ml, win2, DirUp) == nullptr);
   /* Bottom of the stack is the edge. */
   cbelt_assert(MasterGetNeighbor(ml, win3, DirDown) == nullptr);
   /* Within the stack. */
   cbelt_assert(MasterGetNeighbor(ml, win2, DirDown) == win3);

   MasterLayoutFree(ml);
   return TEST_SUCCESS;
}

/* =========================================================================
 * GetClosestWindow
 * ========================================================================= */

CBELT_TEST(closest_window_returns_selected) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   MasterLayout *ml = MasterLayoutCreate(workspace);
   insert_n(ml, 3);

   HWND const win1 = reinterpret_cast<HWND>(1);
   HWND const win3 = reinterpret_cast<HWND>(3);

   cbelt_assert(MasterGetClosestWindow(ml, nullptr) == win1);

   ml->selected_hwnd = win3;
   cbelt_assert(MasterGetClosestWindow(ml, nullptr) == win3);

   MasterLayoutFree(ml);
   return TEST_SUCCESS;
}

/* =========================================================================
 * VTable integration
 * ========================================================================= */

CBELT_TEST(vtable_type_is_master) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   std::unique_ptr<LayoutEngine> engine = MasterLayoutEngineCreate(workspace, 0, 0, 0);
   cbelt_assert(engine != nullptr);
   cbelt_assert(engine->type() == MASTER);
   return TEST_SUCCESS;
}

CBELT_TEST(vtable_insert_and_focus) {
   RECT workspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   std::unique_ptr<LayoutEngine> engine = MasterLayoutEngineCreate(workspace, 0, 0, 0);
   cbelt_assert(engine != nullptr);

   HWND const win10 = reinterpret_cast<HWND>(10);
   HWND const win20 = reinterpret_cast<HWND>(20);
   HWND const win30 = reinterpret_cast<HWND>(30);
   cbelt_assert(engine->insert(win10, reinterpret_cast<HWND>(0)) == true);
   cbelt_assert(engine->insert(win20, reinterpret_cast<HWND>(0)) == true);
   cbelt_assert(engine->insert(win30, reinterpret_cast<HWND>(0)) == true);

   /* First insert becomes the selection. */
   cbelt_assert(engine->get_closest_window(nullptr) == win10);

   /* Focus tracking moves the selection. */
   engine->set_active_window(win20);
   cbelt_assert(engine->get_closest_window(nullptr) == win20);

   /* Master|stack boundary sits at the master's right edge (gaps are 0). */
   RECT r1;
   RECT r2;
   cbelt_assert(engine->get_rect(win10, &r1) == true);
   cbelt_assert(engine->get_rect(win20, &r2) == true);
   cbelt_assert(r1.right == r2.left);

   return TEST_SUCCESS;
}
