/**
 * @file wm_worker.c
 * @brief Single worker thread that drives both keyboard and window-event hooks.
 */

#include "wm_worker.h"
#include "../core/bfwm_context.h"
#include "../input/hooks.h"
#include "../logging/logger.h"
#include "../win/com_raii.h"
#include "../win/win_error.h"
#include "../win/win_utils.h"
#include "../window/events/events.h"
#include <handleapi.h>
#include <minwindef.h>
#include <process.h>
#include <processthreadsapi.h>
#include <synchapi.h>
#include <vector>
#include <windows.h>
#include <winerror.h>
#include <winnt.h>

namespace {

const wchar_t *DPI_WATCHER_CLASS = L"BFWMDpiWatcher";
/* Window property that stashes the owning monitor's HMONITOR on each watcher
 * window so DpiWatcherWndProc can forward it in the WM_APP_DPI_CHANGED record
 * (the consumer must not resolve the monitor from the watcher window later —
 * the window may be destroyed and recreated between queue and drain). */
const wchar_t *DPI_WATCHER_MONITOR_PROP = L"BFWMDpiWatcherMonitor";

/* WM_DPICHANGED is a window message (NOT a WinEvent), so each monitor gets a
 * hidden watcher window on the worker thread. When the monitor's DPI changes,
 * the watcher forwards WM_DPICHANGED into the SPSC event queue as
 * WM_APP_DPI_CHANGED for the main thread to consume. */
inline auto CALLBACK DpiWatcherWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                       LPARAM lParam) -> LRESULT {
   if (msg == WM_DPICHANGED) {
      auto *ctx = (BFWMContext *)BFWMGetWindowData(hwnd);
      if (ctx != nullptr) {
         auto *hmon = (HMONITOR)GetPropW(hwnd, DPI_WATCHER_MONITOR_PROP);
         EventRecord const record = {
             .event = WM_APP_DPI_CHANGED,
             .hwnd = reinterpret_cast<HWND>(hmon),
             .idObject = OBJID_WINDOW,
             .idChild = CHILDID_SELF,
             .dwEventThread = 0,
             .dwmsEventTime = 0,
             .dpi = LOWORD(wParam),
         };
         ctx->event_queue.Push(record); // ignore push failure (queue full)
      }
      return 0;
   }
   return DefWindowProcW(hwnd, msg, wParam, lParam);
}

inline auto RegisterDpiWatcherClass(HINSTANCE hInstance) -> BOOL {
   WNDCLASSW window_class = {};
   window_class.lpfnWndProc = DpiWatcherWndProc;
   window_class.hInstance = hInstance;
   window_class.lpszClassName = DPI_WATCHER_CLASS;

   if ((RegisterClassW(&window_class) == 0U) &&
       GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      BFWMLogLastError("RegisterClassW(BFWMDpiWatcher) failed");
      return FALSE;
   }
   return TRUE;
}

/* EnumDisplayMonitors payload for DpiWatcherEnumProc. */
using DpiWatcherEnumData = struct {
   BFWMContext *ctx;
   HINSTANCE hInstance;
   std::vector<HWND> *watchers;
};

/* Create one hidden watcher window per monitor, positioned at the monitor's
 * origin so WM_DPICHANGED is delivered for that monitor. Never shown. */
inline auto CALLBACK DpiWatcherEnumProc(HMONITOR hMonitor, HDC hdcMonitor,
                                        LPRECT lprcMonitor, LPARAM dwData)
    -> BOOL {
   (void)hdcMonitor;

   auto *data = (DpiWatcherEnumData *)dwData;
   if (data == nullptr)
      return FALSE;

   HWND watcher =
       CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, DPI_WATCHER_CLASS,
                       L"", WS_POPUP, lprcMonitor->left, lprcMonitor->top, 1, 1,
                       nullptr, nullptr, data->hInstance, nullptr);
   if (watcher == nullptr) {
      BFWMLogLastError("CreateWindowExW(BFWMDpiWatcher) failed");
      return TRUE; // keep enumerating the remaining monitors
   }

   BFWMSetWindowData(watcher, data->ctx);
   SetPropW(watcher, DPI_WATCHER_MONITOR_PROP, (HANDLE)hMonitor);
   data->watchers->push_back(watcher);
   return TRUE;
}

inline void CreateDpiWatchers(BFWMContext *ctx, HINSTANCE hInstance,
                              std::vector<HWND> *watchers) {
   DpiWatcherEnumData const data = {
       .ctx = ctx, .hInstance = hInstance, .watchers = watchers};
   EnumDisplayMonitors(nullptr, nullptr, DpiWatcherEnumProc, (LPARAM)&data);
}

inline void DestroyDpiWatchers(std::vector<HWND> *watchers) {
   for (HWND watcher : *watchers) {
      if (watcher != nullptr) {
         RemovePropW(watcher, DPI_WATCHER_MONITOR_PROP);
         DestroyWindow(watcher);
      }
   }
   watchers->clear();
}

/**
 * @brief Thread entry point that owns both the keyboard and WinEvent hooks.
 *
 * Uses a blocking GetMessage loop so low-level input hooks (WH_MOUSE_LL,
 * WH_KEYBOARD_LL) get tight message dispatch without artificial delays.
 * Post WM_QUIT to the thread to shut it down.
 */
inline auto __stdcall StartEventWatching(void *param) -> unsigned int {
   auto *ctx = (BFWMContext *)param;
   if (ctx == nullptr)
      return 1;

   ComInitGuard const com_guard;
   if (FAILED(com_guard.hr())) {
      Error("StartEventWatching: CoInitializeEx failed (hr=0x%08lx)",
            (unsigned long)com_guard.hr());
      return 1;
   }

   if (SetupKeyboardHooks(ctx) == FALSE) {
      return 1;
   }

   if (SetupWindowEventHook(ctx) == FALSE) {
      TeardownKeyboardHooks();
      return 1;
   }

   HINSTANCE hInstance = GetModuleHandleW(nullptr);
   if (RegisterDpiWatcherClass(hInstance) == FALSE) {
      TeardownWindowEventHook();
      TeardownKeyboardHooks();
      return 1;
   }

   /* Hidden per-monitor watcher windows that forward WM_DPICHANGED into the
    * SPSC event queue. Recreated on WM_APP_DPI_WATCHER_RECREATE (monitor
    * topology changed). */
   std::vector<HWND> dpi_watchers;
   CreateDpiWatchers(ctx, hInstance, &dpi_watchers);

   /* Blocking message loop. Exits when WM_QUIT is posted from WMWorkerStop */
   MSG msg;
   while (GetMessage(&msg, nullptr, 0, 0) > 0) {
      if (msg.hwnd == nullptr) {
         /* Thread message (posted via PostThreadMessage): no window to
          * dispatch to. */
         if (msg.message == WM_APP_DPI_WATCHER_RECREATE) {
            DestroyDpiWatchers(&dpi_watchers);
            CreateDpiWatchers(ctx, hInstance, &dpi_watchers);
         }
         continue;
      }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
   }

   DestroyDpiWatchers(&dpi_watchers);
   TeardownWindowEventHook();
   TeardownKeyboardHooks();
   return 0;
}

} // namespace

auto WMWorkerRun(BFWMContext *ctx) -> bool {
   ctx->worker_thread =
       _beginthreadex(nullptr, 0, StartEventWatching, ctx, 0, nullptr);
   return ctx->worker_thread != 0;
}

auto WMWorkerStop(BFWMContext *ctx) -> bool {
   if ((ctx == nullptr) || ctx->worker_thread == 0)
      return true;

   /* Post WM_QUIT to the worker thread's message queue so GetMessage returns 0
    */
   PostThreadMessage(GetThreadId((HANDLE)ctx->worker_thread), WM_QUIT, 0, 0);

   WaitForSingleObject((HANDLE)ctx->worker_thread, INFINITE);

   WINBOOL const ret = CloseHandle((HANDLE)ctx->worker_thread);
   ctx->worker_thread = 0;
   return ret != FALSE;
}
