#include "hooks.h"
#include "../config/keybinds.h"
#include "../core/bfwm_def.h"
#include "../core/main_context.h"
#include "../core/shared_state.h"
#include "../core/worker_context.h"
#include "../window/events/events.h"
#include "../window/window.h"
#include "keystroke.h"
#include "queue.h"
#include <libloaderapi.h>
#include <minwindef.h>
#include <windef.h>
#include <windows.h>

enum {
   KEY_STATE_SIZE = 256,
   KEY_INDEX_MASK = 0xFF,
   HIGH_BIT_MASK = 0x8000,
};

namespace {
WorkerContext *g_worker = nullptr;
HHOOK g_kb_hook = nullptr;
HHOOK g_mouse_hook = nullptr;

std::array<BOOL, KEY_STATE_SIZE> key_down = {};

/* Per-side modifier tracking. Windows delivers AltGr as a synthetic
   Left Ctrl + Right Alt chord; remembering the physical side lets the hook
   tell AltGr apart from a genuine Ctrl+Alt keybind chord. */
BOOL g_left_ctrl_down = FALSE;
BOOL g_right_alt_down = FALSE;

inline void UpdateModifierState(WorkerContext *worker, DWORD vkCode,
                                BOOL isPressed) {
   switch (vkCode) {
   default:
      break;
   case VK_CONTROL:
   case VK_LCONTROL:
      worker->modifiers.ctrl = isPressed;
      g_left_ctrl_down = isPressed;
      break;
   case VK_RCONTROL:
      worker->modifiers.ctrl = isPressed;
      break;
   case VK_SHIFT:
   case VK_LSHIFT:
   case VK_RSHIFT:
      worker->modifiers.shift = isPressed;
      break;
   case VK_MENU:
   case VK_LMENU:
      worker->modifiers.alt = isPressed;
      break;
   case VK_RMENU:
      worker->modifiers.alt = isPressed;
      g_right_alt_down = isPressed;
      break;
   case VK_LWIN:
   case VK_RWIN:
      worker->modifiers.super = isPressed;
      break;
   }
}

inline auto IsModifierKey(DWORD vkCode) -> BOOL {
   return static_cast<BOOL>(
       vkCode == VK_CONTROL || vkCode == VK_LCONTROL || vkCode == VK_RCONTROL ||
       vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT ||
       vkCode == VK_MENU || vkCode == VK_LMENU || vkCode == VK_RMENU ||
       vkCode == VK_LWIN || vkCode == VK_RWIN);
}

/* AltGr is delivered by Windows as Left Ctrl + Right Alt. Those chords are
   text input (third-level characters such as '@'), not window-manager
   hotkeys, so they must never match a Ctrl+Alt keybind. */
inline auto IsAltGrChord(const ModifierState &mods) -> BOOL {
   return static_cast<BOOL>((mods.ctrl != 0) && (mods.alt != 0) &&
                            (g_left_ctrl_down != 0) && (g_right_alt_down != 0));
}

/* Pack a POINT into an LPARAM: low 32 bits = x, high 32 bits = y. */
inline auto PackPoint(POINT point) -> LPARAM {
   return ((LPARAM)(DWORD)point.y << 32) | (DWORD)point.x;
}

} // namespace

auto SetupKeyboardHooks(WorkerContext *worker) -> BOOL {
   g_worker = worker;
   if (g_worker == nullptr)
      return FALSE;

   g_kb_hook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                GetModuleHandle(nullptr), 0);
   if (g_kb_hook == nullptr) {
      g_worker = nullptr;
      return FALSE;
   }

   g_mouse_hook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc,
                                   GetModuleHandle(nullptr), 0);
   if (g_mouse_hook == nullptr) {
      UnhookWindowsHookEx(g_kb_hook);
      g_kb_hook = nullptr;
      g_worker = nullptr;
      return FALSE;
   }

   return TRUE;
}

void TeardownKeyboardHooks() {
   if (g_kb_hook != nullptr) {
      UnhookWindowsHookEx(g_kb_hook);
      g_kb_hook = nullptr;
   }
   if (g_mouse_hook != nullptr) {
      UnhookWindowsHookEx(g_mouse_hook);
      g_mouse_hook = nullptr;
   }
   g_worker = nullptr;
}

auto CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
    -> LRESULT {
   if (nCode == HC_ACTION) {
      auto *pKey = (KBDLLHOOKSTRUCT *)lParam;
      BOOL const isPressed =
          static_cast<BOOL>(wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
      BOOL const isReleased =
          static_cast<BOOL>(wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

      if (g_worker == nullptr)
         return CallNextHookEx(nullptr, nCode, wParam, lParam);

      UpdateModifierState(g_worker, pKey->vkCode, isPressed);

      if (isReleased != 0) {
         key_down[pKey->vkCode & KEY_INDEX_MASK] = FALSE;
      }

      if ((isPressed != 0) && (IsModifierKey(pKey->vkCode) == 0)) {
         if (g_worker->main->focused_hwnd != nullptr) {
            Window *focused = g_worker->main->windows->FindByHwnd(
                g_worker->main->focused_hwnd);
            if ((focused != nullptr) && (focused->IsUnmanagedFullscreen() != 0))
               return CallNextHookEx(nullptr, nCode, wParam, lParam);
         }

         Keystroke queued;
         queued.key = *pKey;
         queued.modifiers = g_worker->modifiers;
         queued.repeat = key_down[pKey->vkCode & KEY_INDEX_MASK];
         key_down[pKey->vkCode & KEY_INDEX_MASK] = TRUE;

         auto *actions = KBDFind(g_worker->shared->keybinds, queued);
         if ((actions != nullptr) && (IsAltGrChord(queued.modifiers) == 0)) {
            g_worker->keystroke_queue.Enqueue(queued);
            return 1;
         }
      }
   }
   return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

auto CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
    -> LRESULT {
   if (nCode >= 0) {
      if (wParam == WM_LBUTTONDOWN && (g_worker != nullptr)) {
         POINT point;
         GetCursorPos(&point);
         // Desktop-click handling lives on the main thread (workspace.c) —
         // mirror the WM_FORCE_FOCUS post precedent.
         PostThreadMessage(g_worker->shared->main_thread_id,
                           WM_APP_DESKTOP_CLICK, 0, PackPoint(point));
         return CallNextHookEx(nullptr, nCode, wParam, lParam);
      }
      if (wParam == WM_MOUSEMOVE && (g_worker != nullptr) &&
          (g_worker->shared->config.focus_follows_mouse != 0)) {
         if (((GetAsyncKeyState(VK_LBUTTON) & HIGH_BIT_MASK) != 0) ||
             ((GetAsyncKeyState(VK_RBUTTON) & HIGH_BIT_MASK) != 0))
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
         POINT point;
         GetCursorPos(&point);
         HWND hover = WindowFromPoint(point);
         if (hover != nullptr)
            hover = GetAncestor(hover, GA_ROOT);

         // Cheap pre-filter (no workspace.h needed); the main thread resolves
         // the workspace-active check via WorkspaceHandleMouseHover.
         if ((hover != nullptr) && hover != g_worker->main->focused_hwnd &&
             (g_worker->main->windows->FindByHwnd(hover) != nullptr)) {
            PostThreadMessage(g_worker->shared->main_thread_id,
                              WM_APP_MOUSE_HOVER, 0, PackPoint(point));
         }
      }
   }
   return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
