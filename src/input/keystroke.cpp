#include "keystroke.h"
#include "../logging/logger.h"
#include <minwindef.h>
#include <string>
#include <windows.h>

enum {
   KEY_PRINTABLE_MIN = 0x20,
   KEY_PRINTABLE_MAX = 0x7E,
};

auto GetKeyNameW(DWORD vkCode) -> const wchar_t * {
   switch (vkCode) {
   case VK_SPACE:
      return L"Space";
   case VK_RETURN:
      return L"Enter";
   case VK_TAB:
      return L"Tab";
   case VK_ESCAPE:
      return L"Escape";
   case VK_BACK:
      return L"Backspace";
   case VK_LEFT:
      return L"Left";
   case VK_RIGHT:
      return L"Right";
   case VK_UP:
      return L"Up";
   case VK_DOWN:
      return L"Down";
   case VK_CONTROL:
      return L"Ctrl";
   case VK_SHIFT:
      return L"Shift";
   case VK_MENU:
      return L"Alt";
   case VK_LWIN:
      return L"Win";
   default:
      if (vkCode >= KEY_PRINTABLE_MIN && vkCode <= KEY_PRINTABLE_MAX) {
         static thread_local std::wstring printable;
         printable.assign(1, (wchar_t)vkCode);
         return printable.c_str();
      }
      return L"?";
   }
}

void DebugKey(const KBDLLHOOKSTRUCT *key, const ModifierState *mods) {
   DebugW(L"Key: %-12ls (0x%02X) | Ctrl:%-2d Shift:%-2d Alt:%-2d",
          GetKeyNameW(key->vkCode), key->vkCode, mods->ctrl, mods->shift,
          mods->alt);
}
