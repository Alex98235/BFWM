#include "win_utils.h"

#include "../core/bfwm_context.h"
#include "../dpi/dpi.h"

#include <dwmapi.h>
#include <minwindef.h>
#include <windef.h>
#include <windows.h>
#include <winerror.h>
#include <winnt.h>

auto BFWMSetWindowPosEx(HWND hwnd, HWND insertAfter, const RECT *rect,
                        DWORD extraFlags) -> BOOL {
   return SetWindowPos(hwnd, insertAfter, rect->left, rect->top,
                       rect->right - rect->left, rect->bottom - rect->top,
                       SWP_NOACTIVATE | extraFlags);
}

auto BFWMApplyLayoutPosition(HWND hwnd, const RECT *rect) -> BOOL {
   return BFWMSetWindowPosEx(hwnd, nullptr, rect,
                             SWP_FRAMECHANGED | SWP_NOSENDCHANGING |
                                 SWP_ASYNCWINDOWPOS);
}

auto BFWMSetWindowPos(HWND hwnd, const RECT *rect) -> BOOL {
   return BFWMSetWindowPosEx(hwnd, HWND_TOP, rect,
                             SWP_NOCOPYBITS | SWP_FRAMECHANGED);
}

void IssueRectWithDpiConversion(struct BFWMContext *ctx, HWND hwnd,
                                HMONITOR hmon, RECT rect,
                                BOOL (*issue)(HWND, const RECT *)) {
   DPI_AWARENESS_CONTEXT target_ctx = GetWindowDpiAwarenessContext(hwnd);
   BOOL const unaware = static_cast<BOOL>(
       (AreDpiAwarenessContextsEqual(target_ctx,
                                     DPI_AWARENESS_CONTEXT_UNAWARE) == TRUE) ||
       (AreDpiAwarenessContextsEqual(
            target_ctx, DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED) == TRUE));
   DPI_AWARENESS_CONTEXT prev_ctx = nullptr;
   if ((unaware == TRUE) && (ctx != nullptr) && (ctx->dpi != nullptr)) {
      UINT const dpi = ctx->dpi->GetDpi(hmon);
      float const scale = (float)dpi / (float)DpiSystem::BaseDpi();
      if ((dpi != 0) && (scale > 1.0F)) {
         prev_ctx = SetThreadDpiAwarenessContext(target_ctx);
         if (prev_ctx != nullptr) {
            rect.left = DpiSystem::Unscale(rect.left, dpi);
            rect.top = DpiSystem::Unscale(rect.top, dpi);
            rect.right = DpiSystem::Unscale(rect.right, dpi);
            rect.bottom = DpiSystem::Unscale(rect.bottom, dpi);
         }
      }
   }
   issue(hwnd, &rect);
   if (prev_ctx != nullptr)
      SetThreadDpiAwarenessContext(prev_ctx);
}

auto BFWMSetWindowZ(HWND hwnd, BFWMZOrder z) -> BOOL {
   HWND insertAfter;
   switch (z) {
   case BFWM_Z_BOTTOM:
      insertAfter = HWND_BOTTOM;
      break;
   case BFWM_Z_TOP:
      insertAfter = HWND_TOP;
      break;
   case BFWM_Z_TOPMOST:
      insertAfter = HWND_TOPMOST;
      break;
   case BFWM_Z_NOTOPMOST:
      insertAfter = HWND_NOTOPMOST;
      break;
   default:
      return FALSE;
   }
   return SetWindowPos(hwnd, insertAfter, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

auto BFWMSetFullscreenPosition(HWND hwnd, const RECT *rect) -> BOOL {
   return SetWindowPos(hwnd, HWND_TOPMOST, rect->left, rect->top,
                       rect->right - rect->left, rect->bottom - rect->top,
                       SWP_FRAMECHANGED | SWP_NOACTIVATE);
}

auto BFWMRedrawContent(HWND hwnd) -> BOOL {
   return RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
}

auto BFWMShowWindow(HWND hwnd) -> BOOL { return ShowWindow(hwnd, SW_SHOW); }

auto BFWMHideWindow(HWND hwnd) -> BOOL { return ShowWindow(hwnd, SW_HIDE); }

auto BFWMMinimizeWindow(HWND hwnd) -> BOOL {
   return ShowWindow(hwnd, SW_MINIMIZE);
}

void BFWMSaveWindowStyle(BFWMWindowStyle *style, HWND hwnd) {
   style->style = GetWindowLong(hwnd, GWL_STYLE);
   style->exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
}

void BFWMRestoreWindowStyle(HWND hwnd, const BFWMWindowStyle *style) {
   SetWindowLong(hwnd, GWL_STYLE, (LONG)style->style);
   SetWindowLong(hwnd, GWL_EXSTYLE, (LONG)style->exStyle);
}

void BFWMApplyFullscreenStyle(HWND hwnd) {
   SetWindowLong(hwnd, GWL_STYLE,
                 WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
   SetWindowLong(hwnd, GWL_EXSTYLE, WS_EX_TOPMOST);
}

void BFWMSetWindowData(HWND hwnd, void *data) {
   SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)data);
}

auto BFWMGetWindowData(HWND hwnd) -> void * {
   return (void *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

auto BFWMEnableNCRendering(HWND hwnd) -> BOOL {
   DWORD nc_policy = DWMNCRP_ENABLED;
   return SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY,
                                          &nc_policy, sizeof(nc_policy)));
}
