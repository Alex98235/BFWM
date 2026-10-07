
#include "focus.h"
#include "../logging/logger.h"
#include <array>
#include <minwindef.h>
#include <processthreadsapi.h>
#include <windef.h>
#include <windows.h>

auto FocusWindowReliable(HWND hWnd) -> BOOL {
   if (IsWindow(hWnd) == FALSE) {
      Debug("FocusWindowReliable: Invalid window handle %p", hWnd);
      return FALSE;
   }

   if (GetForegroundWindow() == hWnd)
      return TRUE;

   BOOL result = FALSE;

   // 1. AttachThreadInput + SetForegroundWindow — standard workaround.
   //    Attach to the foreground window's input queue so the OS sees us
   //    as part of that thread's input chain.  This prevents the taskbar
   //    flash that occurs when a background process calls
   //    SetForegroundWindow directly.
   {
      HWND hForeground = GetForegroundWindow();
      DWORD const dwForeThread = GetWindowThreadProcessId(hForeground, nullptr);
      DWORD const dwCurrThread = GetCurrentThreadId();

      if (dwForeThread != dwCurrThread && dwForeThread != 0) {
         if (AttachThreadInput(dwCurrThread, dwForeThread, TRUE) == TRUE) {
            result = SetForegroundWindow(hWnd);
            AttachThreadInput(dwCurrThread, dwForeThread, FALSE);
            if ((result == TRUE) && GetForegroundWindow() == hWnd)
               return TRUE;
         }
      }
   }

   // 2. SwitchToThisWindow — less restricted fallback.  MS docs discourage
   //    it but it works on modern Windows when AttachThreadInput fails.
   SwitchToThisWindow(hWnd, TRUE);
   if (GetForegroundWindow() == hWnd)
      return TRUE;

   // 3. Desktop thread attach — most invasive.  Attach to the desktop
   //    thread which owns the input queue for the entire session.
   {
      HWND hDesktop = GetDesktopWindow();
      DWORD const dwDesktopThread = GetWindowThreadProcessId(hDesktop, nullptr);
      DWORD const dwCurrThread = GetCurrentThreadId();

      if (dwDesktopThread != dwCurrThread && dwDesktopThread != 0) {
         if (AttachThreadInput(dwCurrThread, dwDesktopThread, TRUE) == TRUE) {
            result = SetForegroundWindow(hWnd);
            AttachThreadInput(dwCurrThread, dwDesktopThread, FALSE);
            if (result == TRUE && GetForegroundWindow() == hWnd)
               return TRUE;
         }
      }
   }

   // 4. Alt+Tab SendInput simulation — last resort.  Simulates Alt+Tab
   //    keystrokes to switch focus, then tries SetForegroundWindow.
   {
      std::array<INPUT, 4> inputs = {};
      inputs[0].type = INPUT_KEYBOARD;
      inputs[0].ki.wVk = VK_MENU;
      inputs[1].type = INPUT_KEYBOARD;
      inputs[1].ki.wVk = VK_TAB;
      inputs[2].type = INPUT_KEYBOARD;
      inputs[2].ki.wVk = VK_TAB;
      inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
      inputs[3].type = INPUT_KEYBOARD;
      inputs[3].ki.wVk = VK_MENU;
      inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
      SendInput(4, inputs.data(), sizeof(INPUT));

      result = SetForegroundWindow(hWnd);
   }

   return static_cast<BOOL>((result == TRUE) && GetForegroundWindow() == hWnd);
}
