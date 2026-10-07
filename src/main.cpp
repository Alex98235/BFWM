#include "bar/bar.h"
#ifdef DEBUG
#include "debug/crash_dump.h"
#endif
#include "config/action.h"
#include "config/config_paths.h"
#include "config/keybinds.h"
#include "config/lua/parser.h"
#include "core/action_dispatcher.h"
#include "core/bfwm_context.h"
#include "core/bfwm_def.h"
#include "core/sync.h"
#include "dpi/dpi.h"
#include "input/actions/handlers.h"
#include "input/keystroke.h"
#include "input/queue.h"
#include "logging/logger.h"
#include "monitor/monitor.h"
#include "notification/snackbar.h"
#include "system/system_status.h"
#include "transaction/transaction.h"
#include "tray/tray.h"
#include "win/com_raii.h"
#include "win/win_error.h"
#include "window/border/overlay.h"
#include "window/events/events.h"
#include "wm/wm_worker.h"
#include "workspace/workspace.h"
#include <clocale>
#include <consoleapi.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <errhandlingapi.h>
#include <handleapi.h>
#include <memory>
#include <minwindef.h>
#include <processthreadsapi.h>
#include <stringapiset.h>
#include <synchapi.h>
#include <sysinfoapi.h>
#include <windef.h>
#include <windows.h>
#include <winerror.h>
#include <winnls.h>
#include <winnt.h>

namespace {

/* ------------------------------------------------------------------ */
/*  Console control handler. Traps Ctrl+C so the main loop can      */
/*  exit cleanly instead of the process being killed.                 */
/* ------------------------------------------------------------------ */
inline BFWMContext *ctrl_handler_ctx = nullptr;

/* Next deadline for the periodic overlay reconcile pass (OverlayReconcileAll).
 * Mirrors the monitor-reconcile deadline pattern; the pass itself lives in
 * MainLoop and is gated on this tick, skipping drags/suspend. */
inline ULONGLONG overlay_reconcile_deadline = 0;

// NOLINTNEXTLINE
inline BOOL WINAPI CtrlHandler(DWORD dwCtrlType) {
   if (dwCtrlType == CTRL_C_EVENT) {
      Debug("Ctrl+C received, signalling main loop to exit");
      ctrl_handler_ctx->running = FALSE;
      /* Enqueue a dummy keystroke to unblock DequeueKeystroke()
         which may be waiting on the semaphore. */
      Keystroke const dummy = {};
      ctrl_handler_ctx->keystroke_queue.Enqueue(dummy);
      return TRUE;
   }
   return FALSE; /* let the default handler deal with other events */
}

/* ------------------------------------------------------------------ */
/*  Register every action handler with the ActionDispatcher.          */
/*  Called once during startup before the main event loop.            */
/* ------------------------------------------------------------------ */
inline void RegisterActionHandlers() {
   ActionDispatcherRegister(ActionSpawn, HandleSpawn);
   ActionDispatcherRegister(ActionExec, HandleExec);
   ActionDispatcherRegister(ActionKillActive, HandleKillActive);
   ActionDispatcherRegister(ActionMinimize, HandleMinimize);
   ActionDispatcherRegister(ActionFullscreen, HandleFullscreen);
   ActionDispatcherRegister(ActionToggleFloat, HandleToggleFloat);
   ActionDispatcherRegister(ActionMoveWindow, HandleMoveWindow);
   ActionDispatcherRegister(ActionFocus, HandleFocus);
   ActionDispatcherRegister(ActionWorkspace, HandleWorkspace);
   ActionDispatcherRegister(ActionMoveToWorkspace, HandleMoveToWorkspace);
   ActionDispatcherRegister(ActionResizeWindow, HandleResizeWindow);
   ActionDispatcherRegister(ActionSplit, HandleSplit);
   ActionDispatcherRegister(ActionSwapSplit, HandleSwapSplit);
   ActionDispatcherRegister(ActionLayout, HandleLayout);
   ActionDispatcherRegister(ActionCycleLayout, HandleCycleLayout);
   ActionDispatcherRegister(ActionToggleGaps, HandleToggleGaps);
   ActionDispatcherRegister(ActionCustom, HandleCustom);
   ActionDispatcherRegister(ActionReloadConfig, HandleReloadConfig);
   ActionDispatcherRegister(ActionMoveWorkspaceToMonitor,
                            HandleMoveWorkspaceToMonitor);
}

/* ------------------------------------------------------------------ */
/*  Look up the keystroke in the dictionary and execute all actions   */
/*  via the pluggable ActionDispatcher.                               */
/* ------------------------------------------------------------------ */
enum {
   MAIN_BUF_SIZE = 1024,
   RECONCILE_INTERVAL_MS = 500,
   RECONCILE_RESUME_DELAY_MS = 1500,
   RECONCILE_MAX_WAIT_MS = 5000,
};

/// Throttled cadence of the async-move convergence pass (see
/// ConvergeAsyncMoves).
#define MOVE_CHECK_INTERVAL_MS 33U

/// After a keyboard-driven resize action, suppress the periodic overlay
/// reconcile tick for this long so it cannot inject an extra commit mid-resize.
#define RESIZE_RECONCILE_GUARD_MS 100U

/**
 * @brief Converge async moves at a throttled cadence while any layout move is
 *        in flight or pending.
 *
 * When a layout move was issued but has not yet landed (app slower than the
 * keystroke cadence), re-runs the layout for the focused workspace so the gate
 * in PlacementApply issues the latest desired rect as soon as the in-flight
 * move lands, guaranteeing the window ends exactly where the layout says even
 * after the last keystroke. Stops automatically: moves_in_flight is cleared
 * once every window has settled. ApplyLayout self-wraps a transaction
 * when not already in one, so no explicit transaction calls are needed here.
 *
 * @param ctx The BFWM context
 */
inline void ConvergeAsyncMoves(BFWMContext *ctx) {
   if ((ctx->moves_in_flight != 0) &&
       GetTickCount64() >= ctx->move_check_deadline) {
      ctx->move_check_deadline = GetTickCount64() + MOVE_CHECK_INTERVAL_MS;
      if (ctx->focused_workspace != nullptr) {
         ctx->focused_workspace->ApplyLayout(ctx);
      }
   }
}

/**
 * @brief Present the bars: destroy + recreate every bar window to match the
 *        current config (composition root — the only module that may depend on
 *        bar.h besides bar.c and BFWM_context.c).
 *
 * Runs when a BAR_UPDATE_RECREATE request is consumed; destroyed bars are
 * rebuilt from ctx->config.bar_cfg, disabled bars are torn down. Called before
 * the first full render, so a RECREATE raised during init is consumed on the
 * first MainLoop iteration.
 *
 * @param ctx The BFWM context
 */
inline void PresentBars(struct BFWMContext *ctx) {
   for (size_t index = 0; index < ctx->monitors->Size(); index++) {
      Monitor *mon = ctx->monitors->At(index);
      if (ctx->config.bar_cfg.enabled) {
         if (mon->GetBar() != nullptr)
            mon->ResetBar();
         mon->SetBar(std::make_unique<Bar>(mon, &ctx->config.bar_cfg, ctx));
         if (!mon->GetBar()->Init()) {
            /* keep object alive for retry */
         }
      } else {
         if (mon->GetBar() != nullptr) {
            mon->ResetBar();
         }
      }
   }
}

inline void DispatchKeybind(BFWMContext *ctx, Keystroke *queued) {
   const KBDLLHOOKSTRUCT *key = &queued->key;

   if (key->vkCode == VK_CONTROL || key->vkCode == VK_SHIFT ||
       key->vkCode == VK_MENU) {
      return; // skip bare modifiers
   }

   auto *actions = KBDFind(ctx->keybinds, *queued);
   if (actions == nullptr)
      return;

   // The generation check after each dispatch catches actions becoming
   // stale when the dictionary is rebuilt mid-loop (e.g. HandleReloadConfig).
   size_t const gen = ctx->keybinds.generation;
   for (const auto &action : *actions) {
      if ((queued->repeat == TRUE) && (action->repeatable == FALSE))
         continue;
      ActionDispatcherDispatch(ctx, action.get());
      if (ctx->keybinds.generation != gen)
         break;
   }
}

using BFWMCliArgs = struct {
   BOOL no_bar;
   BOOL notify_mode;
   const char *notify_text;
   const char *custom_config_path;
};

// NOLINTNEXTLINE
inline auto ParseCliArgs(int argc, char *argv[]) -> BFWMCliArgs {
   BFWMCliArgs args = {
       .no_bar = FALSE,
       .notify_mode = FALSE,
       .notify_text = nullptr,
       .custom_config_path = nullptr,
   };
   for (int index = 1; index < argc; index++) {
      if (strcmp(argv[index], "--no-bar") == 0) {
         args.no_bar = TRUE;
      } else if (strcmp(argv[index], "--notify") == 0 && index + 1 < argc) {
         args.notify_mode = TRUE;
         args.notify_text = argv[++index];
      } else if (strcmp(argv[index], "--config") == 0 && index + 1 < argc) {
         args.custom_config_path = argv[++index];
      }
   }
   return args;
}

inline auto HandleInstanceGuard(HANDLE mutex, BOOL notify_mode,
                                const char *notify_text) -> int {
   (void)mutex;
   if (GetLastError() != ERROR_ALREADY_EXISTS)
      return 0;

   if ((notify_mode == TRUE) && notify_text != nullptr) {
      std::wstring wtext;
      int const wlen =
          MultiByteToWideChar(CP_UTF8, 0, notify_text, -1, nullptr, 0);
      if (wlen > 0) {
         wtext.resize(static_cast<size_t>(wlen));
         MultiByteToWideChar(CP_UTF8, 0, notify_text, -1, wtext.data(), wlen);
      }
      HWND notify_window = FindWindowW(SNACKBAR_NOTIFY_CLASS, nullptr);
      if (notify_window != nullptr) {
         COPYDATASTRUCT cds = {
             .dwData = 0,
             .cbData = static_cast<DWORD>(wtext.size() * sizeof(wchar_t)),
             .lpData = wtext.data(),
         };
         SendMessageW(notify_window, WM_COPYDATA, 0,
                      reinterpret_cast<LPARAM>(&cds));
      } else {
         ErrorW(L"BFWM is running but notify window not found\n");
      }
   } else {
      ErrorW(L"BFWM is already running\n");
   }
   return 1;
}

/** Default max log file size before truncation on next launch. */
#define MAX_LOG_SIZE (1LL * 1024 * 1024) /* 1 MiB */

inline void InitLogger() {
   setlocale(LC_ALL, "");
   Logger &logger = Logger::Instance();
#ifdef DEBUG
   logger.SetLevel(LogLevel::Debug);
#else
   logger.SetLevel(LogLevel::Warn);
#endif
   logger.UseColor(true);
   logger.UseTimestamps(true);

   /* Log to %APPDATA%\BFWM\bfwm.log (per-user install). */
   std::string log_path;
   log_path.resize(MAIN_BUF_SIZE, '\0');
   DWORD const ret = ExpandEnvironmentStringsA(
       "%APPDATA%\\BFWM\\bfwm.log", log_path.data(), log_path.size());
   if (ret == 0 || ret > log_path.size()) {
      Error("ExpandEnvironmentStringsA failed for log path: %lu",
            GetLastError());
      return;
   }
   log_path.resize(ret - 1);

   /* Ensure the log directory exists (it may be missing if the user
      deleted their config). ERROR_ALREADY_EXISTS means it is present. */
   std::string log_dir;
   log_dir.resize(MAIN_BUF_SIZE, '\0');
   DWORD const dir_ret = ExpandEnvironmentStringsA(
       "%APPDATA%\\BFWM", log_dir.data(), log_dir.size());
   if (dir_ret == 0 || dir_ret > log_dir.size()) {
      Error("ExpandEnvironmentStringsA failed for log directory: %lu",
            GetLastError());
      return;
   }
   log_dir.resize(dir_ret - 1);
   if (CreateDirectoryA(log_dir.c_str(), nullptr) == FALSE &&
       GetLastError() != ERROR_ALREADY_EXISTS) {
      Error("Could not create log directory: %s", log_dir.c_str());
      return;
   }

   const char *mode = "a";

   /* Truncate the log file if it exceeds MAX_LOG_SIZE */
   WIN32_FILE_ATTRIBUTE_DATA file_info;
   if (GetFileAttributesExA(log_path.c_str(), GetFileExInfoStandard,
                            &file_info) == TRUE) {
      LARGE_INTEGER file_size;
      file_size.HighPart = static_cast<LONG>(file_info.nFileSizeHigh);
      file_size.LowPart = file_info.nFileSizeLow;
      if (file_size.QuadPart > MAX_LOG_SIZE) {
         mode = "w";
      }
   }

   if (logger.OpenLogFile(log_path.c_str(), mode)) {
      logger.SetLogToFile(true);
      if (mode[0] == 'w') {
         Info("Log file exceeded %lld byte limit, truncated", MAX_LOG_SIZE);
      }
   } else {
      Error("Could not open log file for writing");
   }
}

inline auto PopulateBFWMContext(struct BFWMContext *ctx,
                                const BFWMCliArgs *args) -> BOOL {
   std::string config_path;
   if (args->custom_config_path != nullptr) {
      config_path = args->custom_config_path;
   } else {
      config_path.resize(MAIN_BUF_SIZE, '\0');
      if (ResolveConfigPath("config.lua", config_path.data(),
                            config_path.size())) {
         config_path.resize(strlen(config_path.c_str()));
      } else {
         Snackbar::Warn(ctx, L"No custom config found. Using default values");
         config_path.clear();
      }
   }
   LuaConfigLoad(&ctx->lua, ctx, config_path.data());

   if (args->no_bar == TRUE)
      ctx->config.bar_cfg.enabled = FALSE;

   ctx->running = TRUE;

   ctrl_handler_ctx = ctx;
   if (SetConsoleCtrlHandler(CtrlHandler, TRUE) == FALSE)
      BFWMLogLastError("SetConsoleCtrlHandler failed");

   if (!WMWorkerRun(ctx)) {
      BFWMLogLastError("Could not start worker thread");
      return FALSE;
   }
   BFWMContextSetup(ctx);
   return TRUE;
}

inline void ProcessWindowMessages(struct BFWMContext *ctx) {
   MSG msg;
   while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) == TRUE) {
      if (msg.message == WM_APP_DISPLAYCHANGE) {
         ctx->reconcile_pending = TRUE;
         ctx->reconcile_deadline = GetTickCount64() + RECONCILE_INTERVAL_MS;
         if (ctx->reconcile_attempt == 0U)
            ctx->reconcile_attempt = ctx->reconcile_deadline;
         continue;
      }

      if (msg.message == WM_APP_SUSPEND) {
         ctx->suspended = TRUE;
         ctx->pre_sleep_monitor_count = ctx->monitors->Size();
         continue;
      }

      if (msg.message == WM_APP_RESUME) {
         ctx->suspended = FALSE;
         SystemStatusShutdown();
         SystemStatusInit();
         Bar::ResetPollTimers(ctx);
         ctx->reconcile_pending = TRUE;
         ctx->reconcile_deadline = GetTickCount64() + RECONCILE_RESUME_DELAY_MS;
         ctx->reconcile_attempt = ctx->reconcile_deadline;
         Bar::UpdateAll(ctx);
         continue;
      }
      // force-focus a specific window (from auto-focus guard)
      if (msg.message == WM_FORCE_FOCUS) {
         FocusWindow(reinterpret_cast<HWND>(msg.wParam), ctx);
         continue;
      }
      // desktop click (from mouse hook): focus the closest window of the
      // clicked monitor's active workspace.
      if (msg.message == WM_APP_DESKTOP_CLICK) {
         POINT point;
         point.x = static_cast<LONG>(msg.lParam & 0xFFFFFFFFLL);
         point.y = static_cast<LONG>((msg.lParam >> 32) & 0xFFFFFFFFLL);
         WorkspaceHandleDesktopClick(ctx, point);
         continue;
      }
      // focus-follows-mouse hover (from mouse hook).
      if (msg.message == WM_APP_MOUSE_HOVER) {
         POINT point;
         point.x = static_cast<LONG>(msg.lParam & 0xFFFFFFFFLL);
         point.y = static_cast<LONG>((msg.lParam >> 32) & 0xFFFFFFFFLL);
         WorkspaceHandleMouseHover(ctx, point);
         continue;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
   }
}

inline void ProcessReconcile(struct BFWMContext *ctx) {
   if ((ctx->reconcile_pending == 0) ||
       GetTickCount64() < ctx->reconcile_deadline)
      return;

   // Purge windows whose HWND died or was reused (e.g. across sleep/wake)
   // before any cloak/layout work touches them.
   Overlay::ReclaimStaleWindows(ctx);

   // Exclusive-fullscreen games switch display modes without firing any
   // window event (only WM_DISPLAYCHANGE), so the transition is invisible to
   // the event hooks and the window stays marked as managed. Refresh
   // fullscreen/maximize state for all windows before the reconcile's window
   // handling so a genuinely fullscreen window is not cloaked or re-tiled —
   // re-tiling an exclusive-fullscreen window forces it out of exclusive mode
   // (e.g. CS2 minimizing on every launch).
   ctx->transaction.Begin();
   {
      ScopedLock const lock(ctx->lock);
      RefreshFullscreenStates(ctx);
   }
   BFWMTransactionCommit(ctx);

   ReconcileMonitors(ctx);
   ULONGLONG const waited = GetTickCount64() - ctx->reconcile_attempt;
   if (ctx->pre_sleep_monitor_count > 0 &&
       ctx->monitors->Size() < ctx->pre_sleep_monitor_count &&
       waited < RECONCILE_MAX_WAIT_MS) {
      ctx->reconcile_deadline = GetTickCount64() + RECONCILE_INTERVAL_MS;
   } else {
      ctx->reconcile_pending = FALSE;
      ctx->reconcile_attempt = 0;
      ctx->pre_sleep_monitor_count = 0;
      if (!ctx->monitors->Empty())
         Bar::UpdateAll(ctx);
   }
}

inline void ProcessKeystrokes(struct BFWMContext *ctx) {
   Keystroke queued;
   BOOL processed = FALSE;
   // Drain the queue fully: every key still applies (repeats and distinct keys
   // alike), each through the same dispatch path, but only ONE transaction
   // commit at the end. Previously each key committed its own transaction,
   // which throttled key rate to 1/commit-time and let the cap-8 drop-oldest
   // queue drop distinct keys during a slow hold.
   while (ctx->keystroke_queue.Dequeue(queued)) {
      if (processed == FALSE) {
         ctx->transaction.Begin();
         processed = TRUE;
      }
      DispatchKeybind(ctx, &queued);
   }
   if (processed == TRUE) {
      BFWMTransactionCommit(ctx);
   } else {
      Sleep(1);
   }
}

inline void MainLoop(struct BFWMContext *ctx) {
   while (ctx->running == TRUE) {
      DrainEventQueue(ctx);
      ProcessWindowMessages(ctx);
      ProcessReconcile(ctx);
      /* Periodic overlay reconcile: guarantees ring convergence within
       * RECONCILE_INTERVAL_MS regardless of dropped events, async z-ops, or
       * missed fullscreen transitions. Skipped during drags/suspend and for a
       * short window after a keyboard resize key (resize_hwnd only tracks
       * mouse drags, so without this guard the tick can inject an extra commit
       * mid keyboard-resize). */
      if (GetTickCount64() >= overlay_reconcile_deadline &&
          ctx->resize_hwnd == nullptr && (ctx->suspended == FALSE) &&
          (GetTickCount64() - ctx->last_keyboard_resize) >=
              RESIZE_RECONCILE_GUARD_MS) {
         Overlay::OverlayReconcileAll(ctx);
         overlay_reconcile_deadline = GetTickCount64() + RECONCILE_INTERVAL_MS;
      }
      ProcessKeystrokes(ctx);

      /* Converge async moves at a throttled cadence while any layout move is
       * in flight or pending. Stops automatically: moves_in_flight is cleared
       * once every window has settled. */
      ConvergeAsyncMoves(ctx);

      CheckPendingKills(ctx);

      Snackbar::Tick(ctx);

      /* Consume the DPI-update port: a monitor's DPI changed, so refresh every
       * monitor's reserved bar height (config is logical; the stored value is
       * physical), re-derive every workspace rect, and recreate the bars. */
      if (ctx->dpi_update == TRUE) {
         for (size_t i = 0; i < ctx->monitors->Size(); i++) {
            Monitor *mon = ctx->monitors->At(i);
            mon->SetBarHeight(ctx->dpi->ScaleForMonitor(
                mon->GetHandle(), ctx->config.bar_height));
         }
         RecalculateAllWorkspaceRects(ctx);
         /* Rects changed; relayout the active workspace on every monitor so
          * PlacementApply issues the rescaled window moves. Active-only scope
          * mirrors ReloadConfig / PushGapConfigToAllWorkspaces. */
         for (size_t i = 0; i < ctx->monitors->Size(); i++) {
            Monitor *mon = ctx->monitors->At(i);
            Workspace *active = mon->GetActiveWorkspace();
            if (active != nullptr)
               active->ApplyLayout(ctx);
         }
         BarUpdateRequest(ctx, BAR_UPDATE_RECREATE);
         ctx->dpi_update = FALSE;
      }

      /* Consume the bar-update port: recreate bars first (RECREATE), then
       * invalidate so the fresh bars paint immediately. Deferred to the end of
       * the iteration because BarUpdate/BarUpdateAll only invalidate — the
       * paint happens on the next WM_PAINT pump. */
      if (ctx->bar_update != BAR_UPDATE_NONE) {
         if (ctx->bar_update == BAR_UPDATE_RECREATE) {
            PresentBars(ctx);
         }
         Bar::UpdateAll(ctx);
         ctx->bar_update = BAR_UPDATE_NONE;
      }

      {
         ScopedLock const lock(ctx->lock);
         if (ctx->running == 0) {
            break;
         }
      }
   }
}

inline void ShutdownBFWM(struct BFWMContext *ctx) {
   if (!WMWorkerStop(ctx))
      BFWMLogLastError("Could not stop worker thread");

   RestoreFullscreenWindows(ctx);
   RestoreDesktopState(ctx);
   BFWMContextFree(ctx);
   SystemStatusShutdown();
   /* COM teardown (CoUninitialize) is owned by the ComInitGuard in main(), so
    * it is balanced on every exit path — including early returns. */

   /* The Logger singleton flushes and closes its log file on destruction. */
}

} // namespace

auto main(int argc, char *argv[]) -> int {
   // Declare Per-Monitor V2 DPI awareness FIRST, before any window creation
   // or GDI use. The manifest also declares it, so
   // SetProcessDpiAwarenessContext legitimately fails with ERROR_ACCESS_DENIED
   // when the loader already applied awareness from the manifest — capture the
   // error code before InitLogger() clobbers the thread's last-error state.
   BOOL const dpi_aware = DpiSystem::Init();
   DWORD const dpi_init_error = (dpi_aware == FALSE) ? GetLastError() : 0;

   InitLogger();

   if (dpi_aware == FALSE) {
      if (DpiSystem::IsPerMonitorAware() != FALSE) {
         // Expected: the manifest already made the process per-monitor aware
         // before main(), so the runtime declaration is a redundant no-op.
         Debug("DpiSystem::Init skipped - already per-monitor aware via "
               "manifest (SetProcessDpiAwarenessContext error %lu)",
               static_cast<unsigned long>(dpi_init_error));
      } else {
         Error("DpiSystem::Init failed (error %lu); process is not per-monitor "
               "DPI aware - display scaling will be incorrect",
               static_cast<unsigned long>(dpi_init_error));
      }
   }

#ifdef DEBUG
   CrashDumpInit();
#endif

   ComInitGuard const com_guard;
   if (FAILED(com_guard.hr())) {
      Error("CoInitializeEx failed (hr=0x%08lx)",
            static_cast<unsigned long>(com_guard.hr()));
   }

   BFWMCliArgs const args = ParseCliArgs(argc, argv);

   HANDLE instance_mutex = CreateMutexW(nullptr, TRUE, L"Local\\BFWM-Instance");
   if (HandleInstanceGuard(instance_mutex, args.notify_mode,
                           args.notify_text) == TRUE) {
      CloseHandle(instance_mutex);
      return 1;
   }

   if (args.notify_mode == TRUE) {
      ErrorW(L"BFWM is not running\n");
      CloseHandle(instance_mutex);
      return 1;
   }

   RegisterActionHandlers();

   BFWMContext *ctx = BFWMContextInit();
   ctx->main_thread_id = GetCurrentThreadId();
   ctx->instance_mutex = instance_mutex;

   if (PopulateBFWMContext(ctx, &args) == FALSE) {
      BFWMContextFree(ctx);
      return 1;
   }

   TrayInit(ctx);

   MainLoop(ctx);
   TrayShutdown();
   ShutdownBFWM(ctx);
   return 0;
}
