#include "cbelt.h"
#include <array>
#include <memory>
#include <vector>
#include <windows.h>

#include "../../src/core/bfwm_context.h"
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
inline auto make_win(std::vector<std::unique_ptr<Window>> &owned,
                     HWND hwnd) -> Window * {
   owned.push_back(std::make_unique<Window>(hwnd, L"W"));
   return owned.back().get();
}

/* The workspace only stores raw pointers — the Window objects live in the
 * test's `owned` vector. Release just clears the workspace's view. */
inline void release_ws_windows(Workspace *ws) {
   ws->ClearWindows();
}

/* The stack-declared BFWMContext used by these tests only needs a heap
 * monitor registry (ctx->monitors is now a unique_ptr member). */
inline void init_test_monitors(BFWMContext *ctx) {
   ctx->monitors = std::make_unique<MonitorRegistry>();
}
inline void free_test_monitors(BFWMContext *ctx) {
   ctx->monitors.reset();
}
} // namespace

CBELT_TEST(create_and_destroy_workspace) {
   RECT rect = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   Workspace *ws = new Workspace(1, L"Main", DWINDLE, rect, 0, 0, 0);

   cbelt_assert(ws != nullptr);
   cbelt_assert(ws->GetIdentifier() == 1);
   cbelt_assert(ws->GetName() == L"Main");
   cbelt_assert(ws->IsEmpty());
   cbelt_assert(ws->IsActive() == FALSE);

   delete ws;
   return TEST_SUCCESS;
}

CBELT_TEST(create_workspace_with_different_ids) {
   Workspace *ws1 =
       new Workspace(0, L"Zero", DWINDLE, RECT{0, 0, 800, 600}, 0, 0, 0);
   Workspace *ws2 =
       new Workspace(42, L"Answer", DWINDLE, RECT{0, 0, 800, 600}, 0, 0, 0);

   cbelt_assert(ws1 != nullptr);
   cbelt_assert(ws2 != nullptr);
   cbelt_assert(ws1->GetIdentifier() == 0);
   cbelt_assert(ws2->GetIdentifier() == 42);
   cbelt_assert(ws1 != ws2);

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
   cbelt_assert(found == false);

   delete ws;
   return TEST_SUCCESS;
}

CBELT_TEST(contains_window_with_one_window) {
   Workspace *ws =
       new Workspace(1, L"Test", DWINDLE, RECT{0, 0, 1920, 1080}, 0, 0, 0);

   std::vector<std::unique_ptr<Window>> owned;
   Window *win = make_win(owned, reinterpret_cast<HWND>(42));

   ws->TrackWindow(win);

   cbelt_assert(ws->ContainsWindow(reinterpret_cast<HWND>(42)) == true);
   cbelt_assert(ws->ContainsWindow(reinterpret_cast<HWND>(99)) == false);

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
      wins[i] = make_win(owned, reinterpret_cast<HWND>(static_cast<intptr_t>(100 + i)));
      ws->TrackWindow(wins[i]);
   }

   for (int i = 0; i < 4; i++) {
      cbelt_assert(ws->ContainsWindow(reinterpret_cast<HWND>(static_cast<intptr_t>(100 + i))) ==
                   true);
   }

   cbelt_assert(ws->ContainsWindow(reinterpret_cast<HWND>(999)) == false);

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

   BFWMContext ctx = {};
   init_test_monitors(&ctx);
   ctx.monitors->Add(mon_ptr);

   Workspace *result = FindWorkspaceByHwnd(&ctx, reinterpret_cast<HWND>(42));
   cbelt_assert(result == ws);

   release_ws_windows(ws);
   delete ws;
   free_test_monitors(&ctx);
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

   BFWMContext ctx = {};
   init_test_monitors(&ctx);
   ctx.monitors->Add(mon_ptr);

   Workspace *result = FindWorkspaceByHwnd(&ctx, reinterpret_cast<HWND>(99));
   cbelt_assert(result == nullptr);

   release_ws_windows(ws);
   delete ws;
   free_test_monitors(&ctx);
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

   BFWMContext ctx = {};
   init_test_monitors(&ctx);
   ctx.monitors->Add(&m1);
   ctx.monitors->Add(&m2);

   cbelt_assert(FindWorkspaceByHwnd(&ctx, reinterpret_cast<HWND>(10)) == ws1);
   cbelt_assert(FindWorkspaceByHwnd(&ctx, reinterpret_cast<HWND>(20)) == ws2);
   cbelt_assert(FindWorkspaceByHwnd(&ctx, reinterpret_cast<HWND>(30)) == nullptr);

   release_ws_windows(ws1);
   release_ws_windows(ws2);
   delete ws1;
   delete ws2;
   free_test_monitors(&ctx);
   return TEST_SUCCESS;
}

/* =========================================================================
 * FindWorkspaceById
 * ========================================================================= */

CBELT_TEST(find_workspace_by_id_found) {
   BFWMContext ctx = {};
   init_test_monitors(&ctx);
   Workspace *ws1 =
       new Workspace(5, L"A", DWINDLE, RECT{0, 0, 800, 600}, 0, 0, 0);
   Workspace *ws2 =
       new Workspace(10, L"B", DWINDLE, RECT{0, 0, 800, 600}, 0, 0, 0);
   Monitor mon = {};
   mon.TrackWorkspace(ws1);
   mon.TrackWorkspace(ws2);
   Monitor *mon_ptr = &mon;

   ctx.monitors->Add(mon_ptr);

   cbelt_assert(FindWorkspaceById(&ctx, 5) == ws1);
   cbelt_assert(FindWorkspaceById(&ctx, 10) == ws2);
   cbelt_assert(FindWorkspaceById(&ctx, 99) == nullptr);

   delete ws1;
   delete ws2;
   free_test_monitors(&ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(find_workspace_by_id_null_ctx) {
   cbelt_assert(FindWorkspaceById(nullptr, 1) == nullptr);
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

   cbelt_assert(FindWorkspaceOnMonitor(&mon, 1) == ws1);
   cbelt_assert(FindWorkspaceOnMonitor(&mon, 2) == ws2);
   cbelt_assert(FindWorkspaceOnMonitor(&mon, 3) == nullptr);

   delete ws1;
   delete ws2;
   return TEST_SUCCESS;
}

CBELT_TEST(find_workspace_on_monitor_null) {
   cbelt_assert(FindWorkspaceOnMonitor(nullptr, 1) == nullptr);
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

   BFWMContext ctx = {};
   init_test_monitors(&ctx);
   ctx.monitors->Add(&m1);
   ctx.monitors->Add(&m2);

   cbelt_assert(FindMonitorByWorkspace(&ctx, ws1) == &m1);
   cbelt_assert(FindMonitorByWorkspace(&ctx, ws2) == &m2);
   cbelt_assert(FindMonitorByWorkspace(&ctx, nullptr) == nullptr);

   delete ws1;
   delete ws2;
   free_test_monitors(&ctx);
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

   BFWMContext ctx = {};
   init_test_monitors(&ctx);
   ctx.monitors->Add(&m1);
   ctx.monitors->Add(&m2);

   cbelt_assert(FindMonitorByPoint(&ctx, POINT{400, 300}) == &m1);
   cbelt_assert(FindMonitorByPoint(&ctx, POINT{1200, 300}) == &m2);
   cbelt_assert(FindMonitorByPoint(&ctx, POINT{-50, -50}) == nullptr);

   free_test_monitors(&ctx);
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
   cbelt_assert(mon.Workspaces().size() == 1);
   cbelt_assert(mon.Workspaces()[0] == ws_other);

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
   cbelt_assert(mon.Workspaces().size() == 1);
   cbelt_assert(mon.Workspaces()[0] == ws);

   release_ws_windows(ws);
   delete ws;
   return TEST_SUCCESS;
}
