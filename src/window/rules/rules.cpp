#include "rules.h"
#include "../../core/bfwm_context.h"
#include "../../logging/logger.h"
#include "../window.h"

#include <algorithm>
#include <cstring>
#include <handleapi.h>
#include <minwindef.h>
#include <processthreadsapi.h>
#include <psapi.h>
#include <re.h>
#include <windef.h>
#include <windows.h>
#include <winnt.h>

namespace {
inline auto CriterionMatches(const std::optional<WindowMatchCriterion> &c,
                             const char *text) -> bool {
   if (!c.has_value())
      return true;

   if (text == nullptr)
      text = "";

   switch (c->op) {
   case MATCH_EQUALS:
      return c->pattern == text;
   case MATCH_INCLUDES:
      return strstr(text, c->pattern.c_str()) != nullptr;
   case MATCH_REGEX: {
      int len = 0;
      return re_match(c->pattern.c_str(), text, &len) >= 0;
   }
   case MATCH_NOT_EQUALS:
      return c->pattern != text;
   case MATCH_NOT_REGEX: {
      int len = 0;
      return re_match(c->pattern.c_str(), text, &len) < 0;
   }
   default:
      return false;
   }
}

} // namespace

void WindowRuleDestroy(WindowRule *rule) { delete rule; }

auto WindowRuleMatch(const WindowRule *rule, const char *process,
                     const char *class_name, const char *title) -> bool {

   return std::ranges::any_of(
       rule->entries, [&](const WindowMatchEntry &entry) -> bool {
          return CriterionMatches(entry.process_match, process) &&
                 CriterionMatches(entry.class_match, class_name) &&
                 CriterionMatches(entry.title_match, title);
       });
}

auto GetWindowProcessName(HWND hwnd, char *buf, size_t buf_size) -> const
    char * {
   DWORD pid;
   if (GetWindowThreadProcessId(hwnd, &pid) == 0U)
      return nullptr;

   HANDLE hProc = OpenProcess(
       PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
   if (hProc == nullptr)
      return nullptr;

   DWORD const size = GetModuleBaseNameA(hProc, nullptr, buf, (DWORD)buf_size);
   CloseHandle(hProc);

   if (size == 0)
      return nullptr;

   // Strip a trailing ".exe" (case-insensitive) so rules match on the bare
   // process name, mirroring the old strrchr('.') + _stricmp logic.
   std::string name(buf);
   size_t const dot = name.rfind('.');
   if ((dot != std::string::npos) &&
       (_stricmp(name.c_str() + dot, ".exe") == 0))
      name.resize(dot);
   memcpy(buf, name.c_str(), name.size() + 1);

   return buf;
}

auto WindowRulesApply(struct BFWMContext *ctx, Window *window,
                      const char *class_name, const char *title,
                      const char *process) -> WindowRuleResult {
   if (ctx->window_rule_count == 0)
      return RULE_RESULT_CONTINUE;

   for (size_t i = 0; i < ctx->window_rule_count; i++) {
      WindowRule *rule = ctx->window_rules[i];
      if (rule->action.type == RULE_IGNORE)
         continue;
      if (!WindowRuleMatch(rule, process, class_name, title))
         continue;

      Info("Window rule matched for hwnd=%p (process=%s, class=%s, "
           "title=%ls) action=%d",
           (void *)window->GetHwnd(), (process != nullptr) ? process : "(null)",
           (class_name != nullptr) ? class_name : "(null)",
           window->GetTitle().c_str(), rule->action.type);

      switch (rule->action.type) {
      case RULE_SET_FLOATING:
         window->SetFloating(TRUE);
         GetWindowRect(window->GetHwnd(), window->SavedRectPtr());
         return RULE_RESULT_APPLIED;
      case RULE_FORCE_FLOATING:
         window->SetForceFloating(TRUE);
         window->SetForceTiled(FALSE);
         window->SetFloating(TRUE);
         window->SetFullscreen(FALSE);
         window->SetUnmanagedFullscreen(FALSE);
         window->SetMaximized(FALSE);
         GetWindowRect(window->GetHwnd(), window->SavedRectPtr());
         return RULE_RESULT_APPLIED;
      case RULE_FORCE_TILED:
         window->SetForceTiled(TRUE);
         window->SetForceFloating(FALSE);
         window->SetFloating(FALSE);
         window->SetFullscreen(FALSE);
         window->SetUnmanagedFullscreen(FALSE);
         window->SetMaximized(FALSE);
         return RULE_RESULT_APPLIED;
      case RULE_MOVE_TO_WORKSPACE:
         window->SetRuleTargetWorkspace(rule->action.workspace_index);
         return RULE_RESULT_APPLIED;
      default:
         break;
      }
   }

   return RULE_RESULT_CONTINUE;
}
