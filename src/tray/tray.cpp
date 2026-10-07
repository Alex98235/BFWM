#include "tray.h"

#include "../core/bfwm_context.h"
#include "../core/bfwm_def.h"
#include "../input/keystroke.h"
#include "../logging/logger.h"
#include "../win/win_error.h"

namespace {

/* Context-menu command ids. */
#define ID_TRAY_EXIT 1

const wchar_t *TRAY_CLASS = L"BFWMTrayHelper";

/*
 * Singleton state — there is exactly one tray icon per process.
 * (Should only ever be one)
 */
BFWMContext *g_ctx = nullptr;
HWND g_hwnd = nullptr;
HICON g_icon = nullptr;
NOTIFYICONDATA g_nid = {};

inline auto CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                 LPARAM lParam) -> LRESULT {
   if (msg == WM_TRAY_CALLBACK) {
      /* lParam is the mouse event that triggered the callback. Show the menu
       * on right-click (and left-click for convenience); ignore the
       * balloon/hovertime notifications. */
      if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP ||
          lParam == WM_CONTEXTMENU) {
         POINT cursor;
         GetCursorPos(&cursor);
         /* Required so the menu dismisses on an outside click. */
         SetForegroundWindow(hwnd);

         HMENU menu = CreatePopupMenu();
         if (menu != nullptr) {
            AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");
            UINT const cmd_id = TrackPopupMenu(
                menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, cursor.x,
                cursor.y, 0, hwnd, nullptr);
            DestroyMenu(menu);

            if (cmd_id == ID_TRAY_EXIT && g_ctx != nullptr) {
               /* Mirror the Ctrl+C handler: signal the loop and enqueue a dummy
                * keystroke so a blocking DequeueKeystroke() wakes and re-checks
                * ctx->running. */
               g_ctx->running = FALSE;
               Keystroke const dummy = {};
               g_ctx->keystroke_queue.Enqueue(dummy);
            }
         }
      }
      return 0;
   }
   return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

auto TrayInit(struct BFWMContext *ctx) -> BOOL {
   g_ctx = ctx;

   WNDCLASSW window_class = {};
   window_class.lpfnWndProc = TrayWndProc;
   window_class.hInstance = GetModuleHandleW(nullptr);
   window_class.lpszClassName = TRAY_CLASS;

   if ((RegisterClassW(&window_class) == 0U) &&
       GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      BFWMLogLastError("TrayInit: RegisterClassW failed");
      return FALSE;
   }

   g_hwnd = CreateWindowExW(0, TRAY_CLASS, L"", WS_POPUP, 0, 0, 0, 0, nullptr,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
   if (g_hwnd == nullptr) {
      BFWMLogLastError("TrayInit: CreateWindowExW failed");
      return FALSE;
   }

   /* Load the embedded icon (resource ID 1 from app.rc) at tray size. */
   g_icon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCE(1),
                              IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
   if (g_icon == nullptr) {
      Warn("TrayInit: LoadImage failed to load icon resource (error %lu) — "
           "tray icon will be blank",
           (unsigned long)GetLastError());
      /* Continue without an icon; the tray entry still works. */
   }

   g_nid = {};
   g_nid.cbSize = sizeof(g_nid);
   g_nid.hWnd = g_hwnd;
   g_nid.uID = 1;
   g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
   g_nid.uCallbackMessage = WM_TRAY_CALLBACK;
   g_nid.hIcon = g_icon;
   lstrcpyW(g_nid.szTip, L"BFWM");

   if (Shell_NotifyIconW(NIM_ADD, &g_nid) == FALSE) {
      BFWMLogLastError("TrayInit: Shell_NotifyIcon(NIM_ADD) failed");
      return FALSE;
   }
   return TRUE;
}

void TrayShutdown() {
   if (g_hwnd != nullptr) {
      Shell_NotifyIconW(NIM_DELETE, &g_nid);
   }
   if (g_icon != nullptr) {
      DestroyIcon(g_icon);
      g_icon = nullptr;
   }
   if (g_hwnd != nullptr) {
      DestroyWindow(g_hwnd);
      g_hwnd = nullptr;
   }
   UnregisterClassW(TRAY_CLASS, GetModuleHandleW(nullptr));
   g_ctx = nullptr;
}
