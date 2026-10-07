#include "filter.h"
#include "../core/bfwm_context.h"
#include "../window/rules/rules.h"
#include <array>
#include <cstring>
#include <dwmapi.h>
#include <minwindef.h>
#include <windef.h>
#include <windows.h>
#include <winnt.h>

/**
 * @brief Check if a process image path originates from a system directory.
 */
enum { SYSTEM_PATH_BUF = 1024, MIN_SYSTEM_PATH_LEN = 12, CLASS_NAME_BUF = 256 };

namespace {

inline auto IsSystemImagePath(const wchar_t *path) -> bool {
   size_t const len = wcslen(path);
   if (len < MIN_SYSTEM_PATH_LEN || len >= SYSTEM_PATH_BUF)
      return false;

   std::wstring lower;
   lower.reserve(len + 1);
   for (size_t i = 0; i <= len; i++)
      lower.push_back((wchar_t)towlower(path[i]));

   const std::wstring sys32 = L"c:\\windows\\system32\\";
   const std::wstring syswow = L"c:\\windows\\syswow64\\";

   return (lower.starts_with(sys32)) || (lower.starts_with(syswow));
}

const std::array<std::string, 4> default_blacklist = {
    "Windows.UI.Core.CoreWindow", // Windows UI system (cloak is
                                  // state-dependent)
    "WindowsInputBox",            // Input overlays
    "MultitaskingViewFrame",      // Task View
    "D3DProxyWindow"};            // Unity/DirectX device-init helper
                                  // (never user-facing; created briefly
                                  // during graphics-device init)

// Integrity level RID of the process that owns hwnd, or 0 if it cannot
// be determined.  S-1-16-*: 4096=LOW, 8192=MEDIUM, 12288=HIGH, 16384=SYSTEM.
inline auto GetWindowIntegrityLevel(HWND hwnd) -> DWORD {
   DWORD pid = 0;
   if ((GetWindowThreadProcessId(hwnd, &pid) == 0U) || (pid == 0U))
      return 0U;

   HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
   if (hProc == nullptr)
      return 0U;

   DWORD level = 0U;
   HANDLE hToken = nullptr;
   if (OpenProcessToken(hProc, TOKEN_QUERY, &hToken) == TRUE) {
      // NOLINTNEXTLINE
      BYTE buffer[64];
      DWORD size = 0U;
      if (GetTokenInformation(hToken, TokenIntegrityLevel, buffer,
                              sizeof(buffer), &size) == TRUE) {
         auto *label = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(buffer);
         PSID const sid = label->Label.Sid;
         DWORD const count = *GetSidSubAuthorityCount(sid);
         if (count > 0U)
            level = *GetSidSubAuthority(sid, count - 1U);
      }
      CloseHandle(hToken);
   }
   CloseHandle(hProc);
   return level;
}

// Integrity level of the current process (cached once; the WM is
// single-threaded, so a function-local static is safe).
inline auto GetOwnIntegrityLevel() -> DWORD {
   DWORD level = 0U;
   HANDLE hToken = nullptr;
   if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken) == TRUE) {
      // NOLINTNEXTLINE
      BYTE buffer[64];
      DWORD size = 0U;
      if (GetTokenInformation(hToken, TokenIntegrityLevel, buffer,
                              sizeof(buffer), &size) == TRUE) {
         auto *label = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(buffer);
         PSID const sid = label->Label.Sid;
         DWORD const count = *GetSidSubAuthorityCount(sid);
         if (count > 0U)
            level = *GetSidSubAuthority(sid, count - 1U);
      }
      CloseHandle(hToken);
   }
   return level;
}
} // namespace

auto IsShellSurface(HWND hwnd) -> bool {
   if (hwnd == nullptr)
      return false;

   // The desktop: Progman and every descendant (WorkerW layers, the icon
   // list) resolve to GetShellWindow via GA_ROOT — no class matching.
   HWND root = GetAncestor(hwnd, GA_ROOT);
   if (root == GetShellWindow())
      return true;

   // The taskbar has no handle API; its two class names are the only
   // shell surfaces that must be matched by class.
   std::wstring class_name;
   class_name.resize(CLASS_NAME_BUF);
   int const len = GetClassNameW(root, class_name.data(), CLASS_NAME_BUF);
   if (len <= 0)
      return false;
   class_name.resize((size_t)len);
   return (class_name == L"Shell_TrayWnd") ||
          (class_name == L"Shell_SecondaryTrayWnd");
}

auto IsSystemProcess(HWND hwnd) -> bool {
   DWORD pid;
   if (GetWindowThreadProcessId(hwnd, &pid) == 0U)
      return false;

   HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
   if (hProc == nullptr)
      return false;

   std::wstring path;
   DWORD size = SYSTEM_PATH_BUF;
   path.resize(size);
   BOOL const succeeded =
       QueryFullProcessImageNameW(hProc, 0, path.data(), &size);
   CloseHandle(hProc);

   if (succeeded != TRUE)
      return false;
   path.resize(size);
   return IsSystemImagePath(path.data());
}

// True when the window's process runs at a higher integrity level than
// BFWMWM (e.g. elevated installers/terminals). UIPI makes SetWindowPos
// fail silently for such windows, they cannot be moved, tiled, or ringed,
// so they are rejected at registration.
auto IsElevatedWindow(HWND hwnd) -> bool {
   static DWORD const own_level = GetOwnIntegrityLevel();
   DWORD const win_level = GetWindowIntegrityLevel(hwnd);
   return (own_level != 0U) && (win_level > own_level);
}

auto IsExcludedByRules(struct BFWMContext *ctx, const char *class_name,
                       const char *title, const char *process) -> bool {
   for (const auto &blacklist_entry : default_blacklist) {
      if (strcmp(class_name, blacklist_entry.data()) == 0) {
         return true;
      }
   }
   for (size_t i = 0; i < ctx->window_rule_count; i++) {
      WindowRule *rule = ctx->window_rules[i];
      if (rule->action.type != RULE_IGNORE)
         continue;
      if (WindowRuleMatch(rule, process, class_name, title))
         return true;
   }
   return false;
}

auto IsManageableWindow(struct BFWMContext *ctx, HWND hwnd,
                        BOOL skip_cloak_check) -> bool {
   (void)ctx;
   // Hidden windows are never registered: they register on
   // EVENT_OBJECT_SHOW / EVENT_SYSTEM_MINIMIZEEND once they actually
   // become visible.  Minimized windows pass IsWindowVisible (minimize
   // does not clear WS_VISIBLE), so this check does not exclude them.
   if (IsWindowVisible(hwnd) == FALSE) {
      return false;
   }

   if (GetParent(hwnd) != nullptr) {
      return false;
   }

   // Reject shell surfaces (desktop, taskbar) — not real user windows.
   if (IsShellSurface(hwnd)) {
      return false;
   }

   LONG const exstyle = GetWindowLongA(hwnd, GWL_EXSTYLE);
   if ((exstyle & WS_EX_TOOLWINDOW) != 0) {
      return false;
   }

   LONG const style = GetWindowLongA(hwnd, GWL_STYLE);
   if ((style & (WS_OVERLAPPEDWINDOW | WS_POPUP | WS_CHILD)) == 0U) {
      return false;
   }

   // Reject windows from system directories (installers, uninstallers,
   // OS tools).
   if (IsSystemProcess(hwnd)) {
      return false;
   }

   // Reject windows running at a higher integrity level than us (elevated
   // processes).  UIPI silently fails SetWindowPos for those windows, so
   // they can never be moved or ringed — leave them untouched.
   if (IsElevatedWindow(hwnd)) {
      return false;
   }

   // During startup (EnumWindows) the cloak check is skipped: minimized
   // windows are DWM-cloaked yet still manageable, and must register so
   // they can be tiled on first restore.
   if (skip_cloak_check == FALSE) {
      BOOL is_cloaked = FALSE;
      DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &is_cloaked,
                            sizeof(is_cloaked));
      if (is_cloaked == TRUE)
         return false;
   }

   return true;
}
