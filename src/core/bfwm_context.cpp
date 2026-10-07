#include "bfwm_context.h"
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
      auto *ctx = (BFWMContext *)BFWMGetWindowData(hwnd);
      if (ctx != nullptr)
         PostThreadMessageW(ctx->main_thread_id, WM_APP_DISPLAYCHANGE, 0, 0);
      return 0;
   }
   if (msg == WM_POWERBROADCAST) {
      auto *ctx = (BFWMContext *)BFWMGetWindowData(hwnd);
      if (ctx == nullptr)
         return TRUE;
      if (wParam == PBT_APMSUSPEND) {
         PostThreadMessageW(ctx->main_thread_id, WM_APP_SUSPEND, 0, 0);
      } else if ((wParam == PBT_APMRESUMEAUTOMATIC) ||
                 (wParam == PBT_APMRESUMESUSPEND) ||
                 (wParam == PBT_APMRESUMECRITICAL) ||
                 (wParam == PBT_APMRESUMESTANDBY)) {
         // Any resume broadcast must clear the suspend flag. Only posting
         // for PBT_APMRESUMEAUTOMATIC would leave ctx->suspended stuck TRUE
         // after a resume delivered as another type, permanently gating
         // OverlayReconcileAll (and the stale-window sweep) in the main loop.
         PostThreadMessageW(ctx->main_thread_id, WM_APP_RESUME, 0, 0);
      }
      return TRUE;
   }
   return DefWindowProcW(hwnd, msg, wParam, lParam);
}

inline auto CreateMonitorHelperWindow(BFWMContext *ctx) -> BOOL {
   WNDCLASSW window_class = {};
   window_class.lpfnWndProc = MonitorHelperWndProc;
   window_class.hInstance = GetModuleHandleW(nullptr);
   window_class.lpszClassName = MONITOR_HELPER_CLASS;

   if ((RegisterClassW(&window_class) == 0U) &&
       GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      BFWMLogLastError("RegisterClassW(BFWMMonitorHelper) failed");
      return FALSE;
   }

   ctx->monitor_helper_hwnd =
       CreateWindowExW(0, MONITOR_HELPER_CLASS, L"", WS_POPUP, 0, 0, 0, 0,
                       nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
   if (ctx->monitor_helper_hwnd == nullptr) {
      BFWMLogLastError("CreateWindowExW(BFWMMonitorHelper) failed");
      return FALSE;
   }

   BFWMSetWindowData(ctx->monitor_helper_hwnd, ctx);
   return TRUE;
}

// ============================================================
// STATIC INIT FUNCTIONS
// ============================================================
inline void InitMonitorReg(BFWMContext *context) {
   context->monitors = std::make_unique<MonitorRegistry>();
}

inline void InitWindowReg(BFWMContext *context) {
   context->windows = std::make_unique<WindowRegistry>();
}

inline void InitLuaConfig(BFWMContext *context) {
   context->lua.L = nullptr;
   context->lua.config_path = {};
}

inline void InitWindowRules(BFWMContext *context) {
   context->window_rule_count = 0;
}

// ============================================================
// STATIC FREE FUNCTIONS
// ============================================================
inline void FreeMonitorReg(BFWMContext *context) {
   if (context->monitors == nullptr)
      return;
   for (auto *mon : context->monitors->Monitors()) {
      if (mon->GetBar() != nullptr)
         mon->ResetBar();
      for (Workspace *workspace : mon->Workspaces()) {
         delete workspace;
      }
      delete mon;
   }
   context->monitors.reset();
}

inline void FreeWindowReg(BFWMContext *context) { context->windows.reset(); }

inline void FreeLuaConfig(BFWMContext *context) {
   if (context->lua.L != nullptr) {
      lua_close(context->lua.L);
      context->lua.L = nullptr;
   }
}

inline void FreeWindowRules(BFWMContext *context) {
   for (WindowRule *rule : context->window_rules)
      WindowRuleDestroy(rule);
   context->window_rules.clear();
   context->window_rule_count = 0;
}

// ============================================================
// SETUP. Post-init enumeration and workspace creation
// ============================================================

inline void AssignWindowToWorkspace(BFWMContext *ctx, Window *win) {
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
          FindWorkspaceById(ctx, (size_t)win->RuleTargetWorkspace());
      if (target == nullptr) {
         Monitor *monitor = nullptr;
         const struct WorkspaceConfig *ws_config =
             FindWorkspaceConfig(ctx, (size_t)win->RuleTargetWorkspace());
         if ((ws_config != nullptr) && ws_config->assigned_monitor > 0) {
            monitor = FindMonitorByDisplayNumber(
                ctx, (UINT)ws_config->assigned_monitor);
         }
         if (monitor == nullptr)
            monitor = FindMonitorByWorkspace(ctx, ctx->focused_workspace);
         if (monitor != nullptr) {
            size_t const target_id = win->RuleTargetWorkspace();
            std::wstring const name = L"ws_" + std::to_wstring(target_id);
            target = CreateWorkspaceForMonitor(ctx, target_id, &name, monitor);
            if (target != nullptr) {
               target->ApplyConfig(ctx, (size_t)win->RuleTargetWorkspace());
               monitor->TrackWorkspace(target);
            }
         }
      }
      if (target != nullptr) {
         target->AddWindow(ctx, win);
         return;
      }
   }

   Workspace *workspace = ResolveTargetWorkspace(ctx, win->GetHwnd());
   if (workspace == nullptr)
      workspace = ctx->focused_workspace;
   workspace->AddWindow(ctx, win);
}

inline auto FilterDisabledMonitors(BFWMContext *ctx, MonitorRegistry *reg)
    -> size_t {
   ctx->disabled_uid_count = 0;
   size_t dd_dst = 0;
   auto const &monitors = reg->Monitors();
   size_t mon_count = monitors.size();

   for (size_t index = 0; index < mon_count; index++) {
      Monitor *monitor = monitors[index];
      BOOL disabled = FALSE;
      for (int d = 0; d < ctx->config.disabled_monitor_count; d++) {
         if (std::cmp_equal(monitor->GetDisplayNumber(),
                            ctx->config.disabled_monitors[d])) {
            disabled = TRUE;
            break;
         }
      }
      if (disabled != 0) {
         if (!monitor->GetUid().empty()) {
            ctx->disabled_uids[ctx->disabled_uid_count++] = monitor->GetUid();
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

inline auto SetupBarHeights(BFWMContext *ctx,
                            const std::vector<Monitor *> &monitors) -> void {
   if (ctx->config.bar_cfg.enabled) {
      Bar::Setup();
      ctx->config.bar_height = ctx->config.bar_cfg.height;
      for (const auto &monitor : monitors) {
         /* bar_height config is logical; store it scaled to each monitor's
          * physical pixels (the value reserved from the work area). */
         monitor->SetBarHeight(ctx->dpi->ScaleForMonitor(
             monitor->GetHandle(), ctx->config.bar_height));
      }
   }
}

inline auto CreateDefaultWorkspaces(BFWMContext *ctx,
                                    const std::vector<Monitor *> &monitors)
    -> void {
   for (auto *mon : monitors) {
      size_t const target_id = FindUnassignedWorkspaceId(ctx, mon);
      std::wstring const name = L"workspace_" + std::to_wstring(target_id);
      auto *workspace = CreateWorkspaceForMonitor(ctx, target_id, &name, mon);
      workspace->ApplyConfig(ctx, target_id);

      mon->TrackWorkspace(workspace);
      mon->SetActiveWorkspace(workspace);
   }
}

inline auto AssignWindowsToWorkspaces(BFWMContext *ctx, HWND pre_focus)
    -> void {
   const auto &all_windows = ctx->windows->Windows();
   size_t const win_count = all_windows.size();

   for (size_t index = 0; index < win_count; index++) {
      if (all_windows[index]->GetHwnd() == pre_focus)
         AssignWindowToWorkspace(ctx, all_windows[index].get());
   }

   for (size_t index = 0; index < win_count; index++) {
      if (all_windows[index]->GetHwnd() != pre_focus)
         AssignWindowToWorkspace(ctx, all_windows[index].get());
   }
}

inline auto CreateBarWindows(BFWMContext *ctx,
                             const std::vector<Monitor *> &monitors) -> void {
   if (ctx->config.bar_cfg.enabled) {
      for (auto *mon : monitors) {
         mon->SetBar(std::make_unique<Bar>(mon, &ctx->config.bar_cfg, ctx));
         if (!mon->GetBar()->Init()) {
            /* keep object alive for retry */
         }
      }
   }
}

inline auto ApplyLayoutsOnAllWorkspaces(BFWMContext *ctx,
                                        const std::vector<Monitor *> &monitors)
    -> void {
   for (auto *mon : monitors) {
      for (const auto &workspace : mon->Workspaces()) {
         workspace->ApplyLayout(ctx);
      }
   }
}

} // namespace

auto BFWMContextSetup(BFWMContext *ctx) -> bool {
   // Enumerate monitors into ctx->monitors
   Debug("Enumerating displays...");
   MonitorEnumData const enum_data = {.reg = ctx->monitors.get(), .ctx = ctx};
   EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, (LPARAM)&enum_data);
   Debug("Display enumeration complete (%zu monitors found)",
         ctx->monitors->Size());

   auto const &monitors = ctx->monitors->Monitors();

   // Sort monitors by position for deterministic navigation order
   // regardless of EnumDisplayMonitors callback order.
   ctx->monitors->SortByPosition();
   // Populate disabled_uids from config and filter disabled monitors
   // out before workspace creation.
   size_t const dd_dst = FilterDisabledMonitors(ctx, ctx->monitors.get());
   size_t const mon_count = dd_dst;
   ctx->monitors->Resize(mon_count);

   // Set bar height on monitors *before* workspace creation so
   // workspaces reserve the correct top-margin.  Bar window creation
   // is deferred until after EnumWindows (see below) to avoid
   // triggering Windows 11 display-detection logic that can
   // spontaneously open the Settings app on multi-monitor setups.
   SetupBarHeights(ctx, monitors);

   CreateDefaultWorkspaces(ctx, monitors);

   // Set the first monitor's first workspace as active on the context
   if (mon_count > 0)
      ctx->focused_workspace = monitors[0]->GetActiveWorkspace();

   // Capture the window focused before setup so we can restore it as the
   // first-inserted window (anchors the dwindle layout) and refocus it.
   HWND pre_focus = GetForegroundWindow();

   ctx->setup_in_progress = TRUE;

   // Enumerate existing windows
   if (EnumWindows(EnumWindowsProc, (LPARAM)ctx) == 0) {
      BFWMLogLastError("EnumWindows failed");
      return false;
   }

   // Insert pre_focus first (anchors the layout as the largest window),
   // then all remaining windows (each splits whatever is currently
   // focused, which after the first pass is the most recently inserted).
   AssignWindowsToWorkspaces(ctx, pre_focus);

   ctx->setup_in_progress = FALSE;

   // Create bar windows after EnumWindows to avoid triggering
   // Windows 11 display-detection logic that can spontaneously
   // open the Settings app on multi-monitor setups.
   // (ApplicationFrameWindow is also blacklisted in window_filter.c
   // as a secondary defense.)
   CreateBarWindows(ctx, monitors);

   // Initialise the notification snackbar
   ctx->snackbar = new Snackbar(ctx);
   if (!ctx->snackbar->Init())
      Error("Snackbar init failed");

   ApplyLayoutsOnAllWorkspaces(ctx, monitors);

   // Restore focus to the window that was foreground before setup.
   // AddWindow cycles focus through every enlisted window,
   // leaving it on an arbitrary last-enumerated window.
   if ((pre_focus != nullptr) &&
       (ctx->windows->FindByHwnd(pre_focus) != nullptr))
      FocusWindow(pre_focus, ctx);

   CreateMonitorHelperWindow(ctx);

   return true;
}

// ============================================================
// BFWM CONTEXT
// ============================================================
auto BFWMContextInit() -> BFWMContext * {
   auto *context = new BFWMContext();

   // Initialize synchronization
   InitializeCriticalSection(&context->lock);

   // Initialize sub-registries in dependency-safe order
   context->dpi = std::make_unique<DpiSystem>();
   InitMonitorReg(context);
   InitWindowReg(context);
   InitLuaConfig(context);
   InitWindowRules(context);

   // Populate config with defaults (must happen after sub-registries are
   // allocated)
   ConfigLoadDefaults(context);

   return context;
}

void BFWMContextFree(BFWMContext *context) {
   if (context == nullptr)
      return;

   // Destroy monitor helper window
   if (context->monitor_helper_hwnd != nullptr) {
      DestroyWindow(context->monitor_helper_hwnd);
      context->monitor_helper_hwnd = nullptr;
   }
   UnregisterClassW(MONITOR_HELPER_CLASS, GetModuleHandleW(nullptr));

   // Destroy notification snackbar
   delete context->snackbar;
   context->snackbar = nullptr;

   // Release single-instance mutex
   if (context->instance_mutex != nullptr) {
      CloseHandle(context->instance_mutex);
      context->instance_mutex = nullptr;
   }

   // Free in reverse dependency order
   FreeWindowRules(context);
   FreeLuaConfig(context);
   FreeWindowReg(context);
   context->dpi.reset();
   FreeMonitorReg(context);

   DeleteCriticalSection(&context->lock);

   delete context;
}
