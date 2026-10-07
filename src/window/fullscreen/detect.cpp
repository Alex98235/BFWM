#include "detect.h"
#include <array>
#include <cwchar>
#include <dwmapi.h>
#include <minwindef.h>
#include <shellapi.h>
#include <windef.h>
#include <windows.h>
#include <winerror.h>
#include <winnt.h>

enum {
   CLASS_NAME_BUF = 256,
   MIN_CHROME_HEIGHT = 8,
};

auto DetectFullscreenWindow(HWND hwnd) -> FullscreenType {
   if ((IsWindow(hwnd) == FALSE) || (IsWindowVisible(hwnd) == FALSE))
      return FS_NOT_FULLSCREEN;

   std::array<WCHAR, CLASS_NAME_BUF> className = {};
   GetClassNameW(hwnd, className.data(), CLASS_NAME_BUF);
   if (wcscmp(className.data(), L"Progman") == 0 ||
       wcscmp(className.data(), L"WorkerW") == 0 ||
       wcscmp(className.data(), L"Shell_TrayWnd") == 0)
      return FS_NOT_FULLSCREEN;

   LONG const style = GetWindowLongA(hwnd, GWL_STYLE);

   HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
   MONITORINFO monitor_info = {.cbSize = sizeof(monitor_info),
                               .rcMonitor = {},
                               .rcWork = {},
                               .dwFlags = {}};
   if (GetMonitorInfoW(mon, &monitor_info) == FALSE)
      return FS_NOT_FULLSCREEN;
   RECT window_rect;
   if (GetWindowRect(hwnd, &window_rect) == FALSE)
      return FS_NOT_FULLSCREEN;

   // 2px tolerance for Windows 11 rounded corners
   BOOL const covers_monitor = static_cast<BOOL>(
       window_rect.left <= monitor_info.rcMonitor.left + 2 &&
       window_rect.top <= monitor_info.rcMonitor.top + 2 &&
       window_rect.right >= monitor_info.rcMonitor.right - 2 &&
       window_rect.bottom >= monitor_info.rcMonitor.bottom - 2);

   if (covers_monitor == 0)
      return (IsZoomed(hwnd) == TRUE) ? FS_MAXIMIZED_WINDOW : FS_NOT_FULLSCREEN;

   // Maximized windows that happen to cover the monitor are not exclusive
   // fullscreen
   if (IsZoomed(hwnd) == TRUE)
      return FS_MAXIMIZED_WINDOW;

   // QUNS_RUNNING_D3D_FULL_SCREEN is set by D3D/DXGI exclusive fullscreen
   QUERY_USER_NOTIFICATION_STATE state;
   if (SUCCEEDED(SHQueryUserNotificationState(&state))) {
      if (state == QUNS_RUNNING_D3D_FULL_SCREEN ||
          state == QUNS_PRESENTATION_MODE)
         return FS_EXCLUSIVE_FULLSCREEN;
   }

   // Fullscreen windows suppress non-client rendering (no title bar / borders)
   BOOL ncRendering = TRUE;
   if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_NCRENDERING_ENABLED,
                                       &ncRendering, sizeof(ncRendering)))) {
      if (ncRendering == FALSE)
         return FS_EXCLUSIVE_FULLSCREEN;
   }

   // < 8px chrome + WS_POPUP = no visible title bar
   RECT client_rect;
   if (GetClientRect(hwnd, &client_rect) == TRUE) {
      int const chromeHeight = (window_rect.bottom - window_rect.top) -
                               (client_rect.bottom - client_rect.top);
      if (chromeHeight < MIN_CHROME_HEIGHT && ((style & WS_POPUP) != 0U))
         return FS_EXCLUSIVE_FULLSCREEN;
   }

   // WDA_MONITOR means the window renders exclusively on its monitor
   DWORD affinity = 0;
   if ((GetWindowDisplayAffinity(hwnd, &affinity) == TRUE) &&
       ((affinity & WDA_MONITOR) != 0U))
      return FS_EXCLUSIVE_FULLSCREEN;

   // Covers the monitor but is not foreground → background borderless window
   if (hwnd != GetForegroundWindow())
      return FS_BORDERLESS_WINDOW;

   return FS_EXCLUSIVE_FULLSCREEN;
}
