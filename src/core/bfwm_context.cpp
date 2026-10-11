#include "../bar/bar.h"
#include "../config/defaults.h"
#include "../dpi/dpi.h"
#include "../logging/logger.h"
#include "../win/win_error.h"
#include "../window/events/events.h"
#include "../window/rules/rules.h"
#include "../window/window.h"
#include "../workspace/workspace.h"
#include "bfwm_def.h"
#include "main_context.h"
#include "shared_state.h"
#include "worker_context.h"

#include "../monitor/monitor.h"
#include "../notification/snackbar.h"
#include "../transaction/transaction.h"
#include "../win/win_utils.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "lua.h"
#ifdef __cplusplus
}
#endif
#include <cstdlib>
#include <cwchar>
#include <errhandlingapi.h>
#include <handleapi.h>
#include <libloaderapi.h>
#include <memory>
#include <minwindef.h>
#include <synchapi.h>
#include <utility>
#include <windef.h>
#include <windows.h>
#include <winerror.h>

// ============================================================
// MONITOR HELPER WINDOW — receives WM_DISPLAYCHANGE broadcasts
// and forwards them as WM_APP_DISPLAYCHANGE (WM_APP+2) to the main thread.
// ============================================================

namespace {

const wchar_t *MONITOR_HELPER_CLASS = L"BFWMMonitorHelper";

inline auto CALLBACK MonitorHelperWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                          LPARAM lParam) -> LRESULT {
   if (msg == WM_DISPLAYCHANGE) {
      auto *shared = (SharedState *)BFWMGetWindowData(hwnd);
      if (shared != nullptr) {
         PostThreadMessageW(shared->main_thread_id, WM_APP_DISPLAYCHANGE, 0, 0);
      }
      return 0;
   }
   if (msg == WM_POWERBROADCAST) {
      auto *shared = (SharedState *)BFWMGetWindowData(hwnd);
      if (shared == nullptr)
         return TRUE;
      if (wParam == PBT_APMSUSPEND) {
         PostThreadMessageW(shared->main_thread_id, WM_APP_SUSPEND, 0, 0);
      } else if ((wParam == PBT_APMRESUMEAUTOMATIC) ||
                 (wParam == PBT_APMRESUMESUSPEND) ||
                 (wParam == PBT_APMRESUMECRITICAL) ||
                 (wParam == PBT_APMRESUMESTANDBY)) {
         // Any resume broadcast must clear the suspend flag. Only posting
         // for PBT_APMRESUMEAUTOMATIC would leave main->suspended stuck
         // TRUE after a resume delivered as another type, permanently gating
         // OverlayReconcileAll (and the stale-window sweep) in the main loop.
         PostThreadMessageW(shared->main_thread_id, WM_APP_RESUME, 0, 0);
      }
      return TRUE;
   }
   return DefWindowProcW(hwnd, msg, wParam, lParam);
}

inline auto CreateMonitorHelperWindow(SharedState *shared, MainContext *main)
    -> BOOL {
   WNDCLASSW window_class = {};
   window_class.lpfnWndProc = MonitorHelperWndProc;
   window_class.hInstance = GetModuleHandleW(nullptr);
   window_class.lpszClassName = MONITOR_HELPER_CLASS;

   if ((RegisterClassW(&window_class) == 0U) &&
       GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      BFWMLogLastError("RegisterClassW(BFWMMonitorHelper) failed");
      return FALSE;
   }

   main->monitor_helper_hwnd =
       CreateWindowExW(0, MONITOR_HELPER_CLASS, L"", WS_POPUP, 0, 0, 0, 0,
                       nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
   if (main->monitor_helper_hwnd == nullptr) {
      BFWMLogLastError("CreateWindowExW(BFWMMonitorHelper) failed");
      return FALSE;
   }

   BFWMSetWindowData(main->monitor_helper_hwnd, shared);
   return TRUE;
}

// ============================================================
// STATIC INIT FUNCTIONS
// ============================================================
inline void InitMonitorReg(MainContext *main) {
   main->monitors = std::make_unique<MonitorRegistry>();
}

inline void InitWindowReg(MainContext *main) {
   main->windows = std::make_unique<WindowRegistry>();
}

inline void InitLuaConfig(MainContext *main) {
   main->lua.L = nullptr;
   main->lua.config_path = {};
}

inline void InitWindowRules(MainContext *main) { main->window_rule_count = 0; }

// ============================================================
// STATIC FREE FUNCTIONS
// ============================================================
inline void FreeMonitorReg(MainContext *main) {
   if (main->monitors == nullptr)
      return;
   for (auto *mon : main->monitors->Monitors()) {
      if (mon->GetBar() != nullptr)
         mon->ResetBar();
      for (Workspace *workspace : mon->Workspaces()) {
         delete workspace;
      }
      delete mon;
   }
   main->monitors.reset();
}

inline void FreeWindowReg(MainContext *main) { main->windows.reset(); }

inline void FreeLuaConfig(MainContext *main) {
   if (main->lua.L != nullptr) {
      lua_close(main->lua.L);
      main->lua.L = nullptr;
   }
}

inline void FreeWindowRules(MainContext *main) {
   for (WindowRule *rule : main->window_rules)
      WindowRuleDestroy(rule);
   main->window_rules.clear();
   main->window_rule_count = 0;
}

// ============================================================
// SETUP. Post-init enumeration and workspace creation
// ============================================================

inline void AssignWindowToWorkspace(SharedState *shared, MainContext *main,
                                    Window *win) {
   // Skip windows that are minimized (will be added on restore via
   // HandleMinimizeEnd / HandleWindowReRegistered) or hidden (will be
   // added on first EVENT_OBJECT_SHOW via HandleObjectShow).
   if (IsIconic(win->GetHwnd()) != 0)
      return;
   if (IsWindowVisible(win->GetHwnd()) == 0)
      return;

   // Honour rule-targeted workspace if one was set by WindowRulesApply
   // during NewWindowTrigger (e.g. "move Spotify to workspace 4").
   if (win->RuleTargetWorkspace() > 0) {
      Workspace *target =
          FindWorkspaceById(shared, main, (size_t)win->RuleTargetWorkspace());
      if (target == nullptr) {
         Monitor *monitor = nullptr;
         const struct WorkspaceConfig *ws_config = FindWorkspaceConfig(
             shared, main, (size_t)win->RuleTargetWorkspace());
         if ((ws_config != nullptr) && ws_config->assigned_monitor > 0) {
            monitor = FindMonitorByDisplayNumber(
                shared, main, (UINT)ws_config->assigned_monitor);
         }
         if (monitor == nullptr) {
            monitor =
                FindMonitorByWorkspace(shared, main, main->focused_workspace);
         }
         if (monitor != nullptr) {
            size_t const target_id = win->RuleTargetWorkspace();
            std::wstring const name = L"ws_" + std::to_wstring(target_id);
            target = CreateWorkspaceForMonitor(shared, main, target_id, &name,
                                               monitor);
            if (target != nullptr) {
               target->ApplyConfig(shared, main,
                                   (size_t)win->RuleTargetWorkspace());
               monitor->TrackWorkspace(target);
            }
         }
      }
      if (target != nullptr) {
         target->AddWindow(shared, main, win);
         return;
      }
   }

   Workspace *workspace = ResolveTargetWorkspace(shared, main, win->GetHwnd());
   if (workspace == nullptr)
      workspace = main->focused_workspace;
   workspace->AddWindow(shared, main, win);
}

inline auto FilterDisabledMonitors(SharedState *shared, MainContext *main,
                                   MonitorRegistry *reg) -> size_t {
   main->disabled_uid_count = 0;
   size_t dd_dst = 0;
   auto const &monitors = reg->Monitors();
   size_t mon_count = monitors.size();

   for (size_t index = 0; index < mon_count; index++) {
      Monitor *monitor = monitors[index];
      BOOL disabled = FALSE;
      for (int d = 0; d < shared->config.disabled_monitor_count; d++) {
         if (std::cmp_equal(monitor->GetDisplayNumber(),
                            shared->config.disabled_monitors[d])) {
            disabled = TRUE;
            break;
         }
      }
      if (disabled != 0) {
         if (!monitor->GetUid().empty()) {
            main->disabled_uids[main->disabled_uid_count++] = monitor->GetUid();
         }
         delete monitor;
      } else {
         reg->SetAt(dd_dst++, monitor);
      }
   }

   mon_count = dd_dst;
   reg->Resize(mon_count);
   return mon_count;
}

inline auto SetupBarHeights(SharedState *shared, MainContext *main,
                            const std::vector<Monitor *> &monitors) -> void {
   if (shared->config.bar_cfg.enabled) {
      Bar::Setup();
      shared->config.bar_height = shared->config.bar_cfg.height;
      for (const auto &monitor : monitors) {
         /* bar_height config is logical; store it scaled to each monitor's
          * physical pixels (the value reserved from the work area). */
         monitor->SetBarHeight(main->dpi->ScaleForMonitor(
             monitor->GetHandle(), shared->config.bar_height));
      }
   }
}

inline auto CreateDefaultWorkspaces(SharedState *shared, MainContext *main,
                                    const std::vector<Monitor *> &monitors)
    -> void {
   for (auto *mon : monitors) {
      size_t const target_id = FindUnassignedWorkspaceId(shared, main, mon);
      std::wstring const name = L"workspace_" + std::to_wstring(target_id);
      auto *workspace =
          CreateWorkspaceForMonitor(shared, main, target_id, &name, mon);
      workspace->ApplyConfig(shared, main, target_id);

      mon->TrackWorkspace(workspace);
      mon->SetActiveWorkspace(workspace);
   }
}

inline auto AssignWindowsToWorkspaces(SharedState *shared, MainContext *main,
                                      HWND pre_focus) -> void {
   const auto &all_windows = main->windows->Windows();
   size_t const win_count = all_windows.size();

   for (size_t index = 0; index < win_count; index++) {
      if (all_windows[index]->GetHwnd() == pre_focus)
         AssignWindowToWorkspace(shared, main, all_windows[index].get());
   }

   for (size_t index = 0; index < win_count; index++) {
      if (all_windows[index]->GetHwnd() != pre_focus)
         AssignWindowToWorkspace(shared, main, all_windows[index].get());
   }
}

inline auto CreateBarWindows(SharedState *shared, MainContext *main,
                             const std::vector<Monitor *> &monitors) -> void {
   if (shared->config.bar_cfg.enabled) {
      for (auto *mon : monitors) {
         mon->SetBar(
             std::make_unique<Bar>(mon, &shared->config.bar_cfg, shared, main));
         if (!mon->GetBar()->Init()) {
            /* keep object alive for retry */
         }
      }
   }
}

inline auto ApplyLayoutsOnAllWorkspaces(SharedState *shared, MainContext *main,
                                        const std::vector<Monitor *> &monitors)
    -> void {
   for (auto *mon : monitors) {
      for (const auto &workspace : mon->Workspaces()) {
         workspace->ApplyLayout(shared, main);
      }
   }
}

} // namespace

auto BFWMSetup(SharedState *shared, WorkerContext *worker, MainContext *main)
    -> bool {
   // Enumerate monitors into main->monitors
   Debug("Enumerating displays...");
   MonitorEnumData const enum_data = {
       .reg = main->monitors.get(), .shared = shared, .main = main};
   EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, (LPARAM)&enum_data);
   Debug("Display enumeration complete (%zu monitors found)",
         main->monitors->Size());

   auto const &monitors = main->monitors->Monitors();

   // Sort monitors by position for deterministic navigation order
   // regardless of EnumDisplayMonitors callback order.
   main->monitors->SortByPosition();
   // Populate disabled_uids from config and filter disabled monitors
   // out before workspace creation.
   size_t const dd_dst =
       FilterDisabledMonitors(shared, main, main->monitors.get());
   size_t const mon_count = dd_dst;
   main->monitors->Resize(mon_count);

   // Set bar height on monitors *before* workspace creation so
   // workspaces reserve the correct top-margin.  Bar window creation
   // is deferred until after EnumWindows (see below) to avoid
   // triggering Windows 11 display-detection logic that can
   // spontaneously open the Settings app on multi-monitor setups.
   SetupBarHeights(shared, main, monitors);

   CreateDefaultWorkspaces(shared, main, monitors);

   // Set the first monitor's first workspace as active on the context
   if (mon_count > 0)
      main->focused_workspace = monitors[0]->GetActiveWorkspace();

   // Capture the window focused before setup so we can restore it as the
   // first-inserted window (anchors the dwindle layout) and refocus it.
   HWND pre_focus = GetForegroundWindow();

   worker->setup_in_progress = TRUE;

   // Enumerate existing windows
   WindowEnumData enum_win_data = {.shared = shared, .main = main};
   if (EnumWindows(EnumWindowsProc, (LPARAM)&enum_win_data) == 0) {
      BFWMLogLastError("EnumWindows failed");
      return false;
   }

   // Insert pre_focus first (anchors the layout as the largest window),
   // then all remaining windows (each splits whatever is currently
   // focused, which after the first pass is the most recently inserted).
   AssignWindowsToWorkspaces(shared, main, pre_focus);

   worker->setup_in_progress = FALSE;

   // Create bar windows after EnumWindows to avoid triggering
   // Windows 11 display-detection logic that can spontaneously
   // open the Settings app on multi-monitor setups.
   // (ApplicationFrameWindow is also blacklisted in window_filter.c
   // as a secondary defense.)
   CreateBarWindows(shared, main, monitors);

   // Initialise the notification snackbar
   main->snackbar = new Snackbar(shared, main);
   if (!main->snackbar->Init())
      Error("Snackbar init failed");

   ApplyLayoutsOnAllWorkspaces(shared, main, monitors);

   // Restore focus to the window that was foreground before setup.
   // AddWindow cycles focus through every enlisted window,
   // leaving it on an arbitrary last-enumerated window.
   if ((pre_focus != nullptr) &&
       (main->windows->FindByHwnd(pre_focus) != nullptr))
      FocusWindow(pre_focus, shared, main);

   CreateMonitorHelperWindow(shared, main);

   return true;
}

// ============================================================
// BFWM CONTEXT
// ============================================================
void BFWMInit(SharedState *shared, MainContext *main) {
   // Initialize synchronization
   InitializeCriticalSection(&main->lock);

   // Initialize sub-registries in dependency-safe order
   main->dpi = std::make_unique<DpiSystem>();
   InitMonitorReg(main);
   InitWindowReg(main);
   InitLuaConfig(main);
   InitWindowRules(main);
   main->exec_cache = ExecCacheCreate();

   // Populate config with defaults (must happen after sub-registries are
   // allocated)
   ConfigLoadDefaults(shared);
}

void BFWMFree(SharedState *shared, MainContext *main) {
   if (shared == nullptr || main == nullptr)
      return;

   // Destroy monitor helper window
   if (main->monitor_helper_hwnd != nullptr) {
      DestroyWindow(main->monitor_helper_hwnd);
      main->monitor_helper_hwnd = nullptr;
   }
   UnregisterClassW(MONITOR_HELPER_CLASS, GetModuleHandleW(nullptr));

   // Destroy notification snackbar
   delete main->snackbar;
   main->snackbar = nullptr;

   // Release single-instance mutex
   if (main->instance_mutex != nullptr) {
      CloseHandle(main->instance_mutex);
      main->instance_mutex = nullptr;
   }

   // Terminate/close any in-flight exec_cache children before tearing down Lua
   // (the BFWM closures capture ctx and the cache).
   ExecCacheDestroy(main->exec_cache);
   main->exec_cache = nullptr;

   // Free in reverse dependency order
   FreeWindowRules(main);
   FreeLuaConfig(main);
   FreeWindowReg(main);
   main->dpi.reset();
   FreeMonitorReg(main);

   DeleteCriticalSection(&main->lock);
}
