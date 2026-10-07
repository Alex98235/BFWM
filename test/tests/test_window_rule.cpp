#include "cbelt.h"
#include <array>
#include <memory>
#include <windows.h>

#include "../../src/core/bfwm_context.h"
#include "../../src/window/window.h"
#include "../../src/window/rules/rules.h"

CBELT_GROUP("window_rule")

namespace {

inline auto make_window(void) -> std::unique_ptr<Window> {
   return std::make_unique<Window>(reinterpret_cast<HWND>(1), L"test");
}

inline auto make_match_all_entry(void) -> WindowMatchEntry {
   // No criteria set — matches every window.
   return WindowMatchEntry{};
}
} // namespace

/* =========================================================================
 * RULE_FORCE_FLOATING
 * ========================================================================= */

CBELT_TEST(force_floating_sets_flags) {
   auto win = make_window();
   WindowRule rule = {};
   rule.entries.push_back(make_match_all_entry());
   rule.action.type = RULE_FORCE_FLOATING;
   std::array<WindowRule *, 1> rules = {&rule};
   BFWMContext ctx = {};
   ctx.window_rules.assign(rules.begin(), rules.end());
   ctx.window_rule_count = 1;

   WindowRulesApply(&ctx, win.get(), "test.exe", "TestClass", "test");

   cbelt_assert(win->ForceFloating() == TRUE);
   cbelt_assert(win->ForceTiled() == FALSE);
   cbelt_assert(win->IsFloating() == TRUE);
   cbelt_assert(win->IsFullscreen() == FALSE);
   cbelt_assert(win->IsUnmanagedFullscreen() == FALSE);
   cbelt_assert(win->IsMaximized() == FALSE);

   return TEST_SUCCESS;
}

CBELT_TEST(force_floating_clears_conflicting) {
   auto win = make_window();
   win->SetFullscreen(TRUE);
   win->SetUnmanagedFullscreen(TRUE);
   win->SetMaximized(TRUE);
   win->SetForceTiled(TRUE);

   WindowRule rule = {};
   rule.entries.push_back(make_match_all_entry());
   rule.action.type = RULE_FORCE_FLOATING;
   std::array<WindowRule *, 1> rules = {&rule};
   BFWMContext ctx = {};
   ctx.window_rules.assign(rules.begin(), rules.end());
   ctx.window_rule_count = 1;

   WindowRulesApply(&ctx, win.get(), "test.exe", "TestClass", "test");

   cbelt_assert(win->ForceFloating() == TRUE);
   cbelt_assert(win->ForceTiled() == FALSE);
   cbelt_assert(win->IsFloating() == TRUE);
   cbelt_assert(win->IsFullscreen() == FALSE);
   cbelt_assert(win->IsUnmanagedFullscreen() == FALSE);
   cbelt_assert(win->IsMaximized() == FALSE);

   return TEST_SUCCESS;
}

/* =========================================================================
 * RULE_FORCE_TILED
 * ========================================================================= */

CBELT_TEST(force_tiled_sets_flags) {
   auto win = make_window();
   WindowRule rule = {};
   rule.entries.push_back(make_match_all_entry());
   rule.action.type = RULE_FORCE_TILED;
   std::array<WindowRule *, 1> rules = {&rule};
   BFWMContext ctx = {};
   ctx.window_rules.assign(rules.begin(), rules.end());
   ctx.window_rule_count = 1;

   WindowRulesApply(&ctx, win.get(), "test.exe", "TestClass", "test");

   cbelt_assert(win->ForceTiled() == TRUE);
   cbelt_assert(win->ForceFloating() == FALSE);
   cbelt_assert(win->IsFloating() == FALSE);
   cbelt_assert(win->IsFullscreen() == FALSE);
   cbelt_assert(win->IsUnmanagedFullscreen() == FALSE);
   cbelt_assert(win->IsMaximized() == FALSE);

   return TEST_SUCCESS;
}

CBELT_TEST(force_tiled_clears_conflicting) {
   auto win = make_window();
   win->SetFloating(TRUE);
   win->SetFullscreen(TRUE);
   win->SetUnmanagedFullscreen(TRUE);
   win->SetMaximized(TRUE);
   win->SetForceFloating(TRUE);

   WindowRule rule = {};
   rule.entries.push_back(make_match_all_entry());
   rule.action.type = RULE_FORCE_TILED;
   std::array<WindowRule *, 1> rules = {&rule};
   BFWMContext ctx = {};
   ctx.window_rules.assign(rules.begin(), rules.end());
   ctx.window_rule_count = 1;

   WindowRulesApply(&ctx, win.get(), "test.exe", "TestClass", "test");

   cbelt_assert(win->ForceTiled() == TRUE);
   cbelt_assert(win->ForceFloating() == FALSE);
   cbelt_assert(win->IsFloating() == FALSE);
   cbelt_assert(win->IsFullscreen() == FALSE);
   cbelt_assert(win->IsUnmanagedFullscreen() == FALSE);
   cbelt_assert(win->IsMaximized() == FALSE);

   return TEST_SUCCESS;
}

/* =========================================================================
 * RULE_SET_FLOATING — regression: should NOT touch force flags
 * ========================================================================= */

CBELT_TEST(set_floating_does_not_force) {
   auto win = make_window();
   win->SetFullscreen(TRUE);
   win->SetUnmanagedFullscreen(TRUE);

   WindowRule rule = {};
   rule.entries.push_back(make_match_all_entry());
   rule.action.type = RULE_SET_FLOATING;
   std::array<WindowRule *, 1> rules = {&rule};
   BFWMContext ctx = {};
   ctx.window_rules.assign(rules.begin(), rules.end());
   ctx.window_rule_count = 1;

   WindowRulesApply(&ctx, win.get(), "test.exe", "TestClass", "test");

   cbelt_assert(win->ForceFloating() == FALSE);
   cbelt_assert(win->ForceTiled() == FALSE);
   cbelt_assert(win->IsFloating() == TRUE);

   return TEST_SUCCESS;
}

/* =========================================================================
 * No match — rule does not apply
 * ========================================================================= */

CBELT_TEST(no_match_skips_rule) {
   auto win = make_window();

   WindowRule rule = {};
   WindowMatchEntry entry = {};
   WindowMatchCriterion c = {};
   c.op = MATCH_EQUALS;
   c.pattern = "noway";
   entry.process_match = c;
   rule.entries.push_back(entry);
   rule.action.type = RULE_FORCE_FLOATING;
   std::array<WindowRule *, 1> rules = {&rule};
   BFWMContext ctx = {};
   ctx.window_rules.assign(rules.begin(), rules.end());
   ctx.window_rule_count = 1;

   WindowRulesApply(&ctx, win.get(), "test.exe", "TestClass", "test");

   cbelt_assert(win->ForceFloating() == FALSE);
   cbelt_assert(win->IsFloating() == FALSE);

   return TEST_SUCCESS;
}
