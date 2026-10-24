#include "cbelt.h"
#include <array>
#include <memory>
#include <vector>
#include <windows.h>

#include "../../src/core/main_context.h"

#include "../../src/core/shared_state.h"
#include "../../src/monitor/monitor.h"
#include "../../src/window/window.h"
#include "../../src/workspace/workspace.h"

CBELT_GROUP("workspace_core")

/* =========================================================================
 * Helpers
 * ========================================================================= */

namespace {

/* Creates a Window owned by `owned` (a per-test vector) and returns the raw
 * pointer to hand to the workspace. Scope destroys the Window for us. */
inline auto make_win(std::vector<std::unique_ptr<Window>> &owned, HWND hwnd)
    -> Window * {
   owned.push_back(std::make_unique<Window>(hwnd, L"W"));
   return owned.back().get();
}

/* The workspace only stores raw pointers — the Window objects live in the
 * test's `owned` vector. Release just clears the workspace's view. */
inline void release_ws_windows(Workspace *ws) { ws->ClearWindows(); }

/* The stack-declared MainContext used by these tests only needs a heap
 * monitor registry (ctx->monitors is a unique_ptr member). */
inline void init_test_monitors(MainContext *ctx) {
   ctx->monitors = std::make_unique<MonitorRegistry>();
}
inline void free_test_monitors(MainContext *ctx) { ctx->monitors.reset(); }
} // namespace

CBELT_TEST(create_and_destroy_workspace) {
   RECT rect = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   Workspace *ws = new Workspace(1, L"Main", DWINDLE, rect, 0, 0, 0);

   cbelt_assert_not_null(ws);
   // Intentional: test fixture, never freed.
   // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
   cbelt_assert_true(1 == ws->GetIdentifier());
   cbelt_assert(ws->GetName() == L"Main");
   cbelt_assert_true(ws->IsEmpty());
   cbelt_assert_false(ws->IsActive());

   delete ws;
   return TEST_SUCCESS;
}

CBELT_TEST(create_workspace_with_different_ids) {
   Workspace *ws1 =
       new Workspace(0, L"Zero", DWINDLE, RECT{0, 0, 800, 600}, 0, 0, 0);
   Workspace *ws2 =
       new Workspace(42, L"Answer", DWINDLE, RECT{0, 0, 800, 600}, 0, 0, 0);

   cbelt_assert_not_null(ws1);
   cbelt_assert_not_null(ws2);
   // Intentional: test fixture, never freed.
   // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
   cbelt_assert_true(0 == ws1->GetIdentifier());
   cbelt_assert_true(42 == ws2->GetIdentifier());
   cbelt_assert_not_equal(ws2, ws1);

   delete ws1;
   delete ws2;
   return TEST_SUCCESS;
}

CBELT_TEST(destroy_null_workspace_is_safe) {
   delete static_cast<Workspace *>(nullptr);
   return TEST_SUCCESS;
}

CBELT_TEST(contains_window_in_empty_workspace) {
   Workspace *ws =
       new Workspace(1, L"Empty", DWINDLE, RECT{0, 0, 1920, 1080}, 0, 0, 0);

   bool found = ws->ContainsWindow(reinterpret_cast<HWND>(1));
   cbelt_assert_false(found);

   delete ws;
   return TEST_SUCCESS;
}

CBELT_TEST(contains_window_with_one_window) {
   Workspace *ws =
       new Workspace(1, L"Test", DWINDLE, RECT{0, 0, 1920, 1080}, 0, 0, 0);

   std::vector<std::unique_ptr<Window>> owned;
   Window *win = make_win(owned, reinterpret_cast<HWND>(42));

   ws->TrackWindow(win);

   cbelt_assert_true(ws->ContainsWindow(reinterpret_cast<HWND>(42)));
   cbelt_assert_false(ws->ContainsWindow(reinterpret_cast<HWND>(99)));

   ws->ClearWindows();
   delete ws;
   return TEST_SUCCESS;
}

CBELT_TEST(contains_window_with_multiple_windows) {
   Workspace *ws =
       new Workspace(1, L"Multi", DWINDLE, RECT{0, 0, 1920, 1080}, 0, 0, 0);

   std::vector<std::unique_ptr<Window>> owned;
   std::array<Window *, 4> wins{};
   for (int i = 0; i < 4; i++) {
      wins[i] = make_win(
          owned, reinterpret_cast<HWND>(static_cast<intptr_t>(100 + i)));
      ws->TrackWindow(wins[i]);
   }

   for (int i = 0; i < 4; i++) {
      cbelt_assert_true(ws->ContainsWindow(
          reinterpret_cast<HWND>(static_cast<intptr_t>(100 + i))));
   }

   cbelt_assert_false(ws->ContainsWindow(reinterpret_cast<HWND>(999)));

   ws->ClearWindows();
   delete ws;
   return TEST_SUCCESS;
}

/* =========================================================================
 * FindWorkspaceByHwnd
 * ========================================================================= */

CBELT_TEST(find_workspace_by_hwnd_found) {
   Workspace *ws =
       new Workspace(1, L"Test", DWINDLE, RECT{0, 0, 800, 600}, 0, 0, 0);
   std::vector<std::unique_ptr<Window>> owned;
   Window *win = make_win(owned, reinterpret_cast<HWND>(42));
   ws->TrackWindow(win);

   Monitor mon = {};
   mon.TrackWorkspace(ws);
   Monitor *mon_ptr = &mon;

   SharedState shared = {};
   MainContext main = {};
   init_test_monitors(&main);
   main.monitors->Add(mon_ptr);

   Workspace *result =
       FindWorkspaceByHwnd(&shared, &main, reinterpret_cast<HWND>(42));
   cbelt_assert_true(ws == result);

   release_ws_windows(ws);
   delete ws;
   free_test_monitors(&main);
   return TEST_SUCCESS;
}

CBELT_TEST(find_workspace_by_hwnd_not_found) {
   Workspace *ws =
       new Workspace(1, L"Test", DWINDLE, RECT{0, 0, 800, 600}, 0, 0, 0);
   std::vector<std::unique_ptr<Window>> owned;
   Window *win = make_win(owned, reinterpret_cast<HWND>(42));
   ws->TrackWindow(win);

   Monitor mon = {};
   mon.TrackWorkspace(ws);
   Monitor *mon_ptr = &mon;

   SharedState shared = {};
   MainContext main = {};
   init_test_monitors(&main);
   main.monitors->Add(mon_ptr);

   Workspace *result =
       FindWorkspaceByHwnd(&shared, &main, reinterpret_cast<HWND>(99));
   cbelt_assert_null(result);

   release_ws_windows(ws);
   delete ws;
   free_test_monitors(&main);
   return TEST_SUCCESS;
}

CBELT_TEST(find_workspace_by_hwnd_multiple_monitors) {
   Workspace *ws1 =
       new Workspace(1, L"W1", DWINDLE, RECT{0, 0, 800, 600}, 0, 0, 0);
   std::vector<std::unique_ptr<Window>> owned;
   Window *win1 = make_win(owned, reinterpret_cast<HWND>(10));
   ws1->TrackWindow(win1);

   Workspace *ws2 =
       new Workspace(2, L"W2", DWINDLE, RECT{800, 0, 1600, 600}, 0, 0, 0);
   Window *win2 = make_win(owned, reinterpret_cast<HWND>(20));
   ws2->TrackWindow(win2);

   Monitor m1 = {};
   Monitor m2 = {};
   m1.TrackWorkspace(ws1);
   m2.TrackWorkspace(ws2);

   SharedState shared = {};
   MainContext main = {};
   init_test_monitors(&main);
   main.monitors->Add(&m1);
   main.monitors->Add(&m2);

   cbelt_assert_true(
       ws1 == FindWorkspaceByHwnd(&shared, &main, reinterpret_cast<HWND>(10)));
   cbelt_assert_true(
       ws2 == FindWorkspaceByHwnd(&shared, &main, reinterpret_cast<HWND>(20)));
   cbelt_assert_null(
       FindWorkspaceByHwnd(&shared, &main, reinterpret_cast<HWND>(30)));

   release_ws_windows(ws1);
   release_ws_windows(ws2);
   delete ws1;
   delete ws2;
   free_test_monitors(&main);
   return TEST_SUCCESS;
}

/* =========================================================================
 * FindWorkspaceById
 * ========================================================================= */

CBELT_TEST(find_workspace_by_id_found) {
   SharedState shared = {};
   MainContext main = {};
   init_test_monitors(&main);
   Workspace *ws1 =
       new Workspace(5, L"A", DWINDLE, RECT{0, 0, 800, 600}, 0, 0, 0);
   Workspace *ws2 =
       new Workspace(10, L"B", DWINDLE, RECT{0, 0, 800, 600}, 0, 0, 0);
   Monitor mon = {};
   mon.TrackWorkspace(ws1);
   mon.TrackWorkspace(ws2);
   Monitor *mon_ptr = &mon;

   main.monitors->Add(mon_ptr);

   cbelt_assert_true(ws1 == FindWorkspaceById(&shared, &main, 5));
   cbelt_assert_true(ws2 == FindWorkspaceById(&shared, &main, 10));
   cbelt_assert_null(FindWorkspaceById(&shared, &main, 99));

   delete ws1;
   delete ws2;
   free_test_monitors(&main);
   return TEST_SUCCESS;
}

CBELT_TEST(find_workspace_by_id_null_ctx) {
   cbelt_assert_null(FindWorkspaceById(nullptr, nullptr, 1));
   return TEST_SUCCESS;
}

/* =========================================================================
 * FindWorkspaceOnMonitor
 * ========================================================================= */

CBELT_TEST(find_workspace_on_monitor_found) {
   Workspace *ws1 =
       new Workspace(1, L"A", DWINDLE, RECT{0, 0, 800, 600}, 0, 0, 0);
   Workspace *ws2 =
       new Workspace(2, L"B", DWINDLE, RECT{0, 0, 800, 600}, 0, 0, 0);
   Monitor mon = {};
   mon.TrackWorkspace(ws1);
   mon.TrackWorkspace(ws2);

   cbelt_assert_true(ws1 == FindWorkspaceOnMonitor(&mon, 1));
   cbelt_assert_true(ws2 == FindWorkspaceOnMonitor(&mon, 2));
   cbelt_assert_null(FindWorkspaceOnMonitor(&mon, 3));

   delete ws1;
   delete ws2;
   return TEST_SUCCESS;
}

CBELT_TEST(find_workspace_on_monitor_null) {
   cbelt_assert_null(FindWorkspaceOnMonitor(nullptr, 1));
   return TEST_SUCCESS;
}

/* =========================================================================
 * FindMonitorByWorkspace
 * ========================================================================= */

CBELT_TEST(find_monitor_by_workspace_found) {
   Workspace *ws1 =
       new Workspace(1, L"A", DWINDLE, RECT{0, 0, 800, 600}, 0, 0, 0);
   Workspace *ws2 =
       new Workspace(2, L"B", DWINDLE, RECT{0, 0, 800, 600}, 0, 0, 0);
   Monitor m1 = {};
   Monitor m2 = {};
   m1.TrackWorkspace(ws1);
   m2.TrackWorkspace(ws2);

   SharedState shared = {};
   MainContext main = {};
   init_test_monitors(&main);
   main.monitors->Add(&m1);
   main.monitors->Add(&m2);

   cbelt_assert_true(&m1 == FindMonitorByWorkspace(&shared, &main, ws1));
   cbelt_assert_true(&m2 == FindMonitorByWorkspace(&shared, &main, ws2));
   cbelt_assert_null(FindMonitorByWorkspace(&shared, &main, nullptr));

   delete ws1;
   delete ws2;
   free_test_monitors(&main);
   return TEST_SUCCESS;
}

/* =========================================================================
 * FindMonitorByPoint
 * ========================================================================= */

CBELT_TEST(find_monitor_by_point_inside) {
   Monitor m1 = {};
   Monitor m2 = {};
   m1.SetRect(RECT{0, 0, 800, 600});
   m2.SetRect(RECT{800, 0, 1600, 600});

   SharedState shared = {};
   MainContext main = {};
   init_test_monitors(&main);
   main.monitors->Add(&m1);
   main.monitors->Add(&m2);

   cbelt_assert_true(&m1 ==
                     FindMonitorByPoint(&shared, &main, POINT{400, 300}));
   cbelt_assert_true(&m2 ==
                     FindMonitorByPoint(&shared, &main, POINT{1200, 300}));
   cbelt_assert_null(FindMonitorByPoint(&shared, &main, POINT{-50, -50}));

   free_test_monitors(&main);
   return TEST_SUCCESS;
}

/* =========================================================================
 * WorkspaceDestroyIfEmpty
 * ========================================================================= */

CBELT_TEST(destroy_if_empty_destroys) {
   Workspace *ws =
       new Workspace(1, L"WS", DWINDLE, RECT{0, 0, 800, 600}, 0, 0, 0);
   Workspace *ws_other =
       new Workspace(2, L"Other", DWINDLE, RECT{0, 0, 800, 600}, 0, 0, 0);
   Monitor mon = {};
   mon.TrackWorkspace(ws);
   mon.TrackWorkspace(ws_other);

   WorkspaceDestroyIfEmpty(&mon, ws);
   cbelt_assert_true(1 == mon.Workspaces().size());
   cbelt_assert_true(ws_other == mon.Workspaces()[0]);

   delete ws_other;
   return TEST_SUCCESS;
}

CBELT_TEST(destroy_if_empty_keeps_nonempty) {
   Workspace *ws =
       new Workspace(1, L"WS", DWINDLE, RECT{0, 0, 800, 600}, 0, 0, 0);
   std::vector<std::unique_ptr<Window>> owned;
   Window *win = make_win(owned, reinterpret_cast<HWND>(42));
   ws->TrackWindow(win);
   Monitor mon = {};
   mon.TrackWorkspace(ws);

   WorkspaceDestroyIfEmpty(&mon, ws);
   cbelt_assert_true(1 == mon.Workspaces().size());
   cbelt_assert_true(ws == mon.Workspaces()[0]);

   release_ws_windows(ws);
   delete ws;
   return TEST_SUCCESS;
}
