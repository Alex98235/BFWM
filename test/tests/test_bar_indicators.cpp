#include "cbelt.h"
#include <cstring>
#include <string>
#include <windows.h>

#include "../../src/bar/bar.h"
#include "../../src/bar/system_indicator_mapping.h"

CBELT_GROUP("bar_indicators")

namespace {

auto num_value(const char *name, double value, int precision)
    -> IndicatorValue {
   IndicatorValue v;
   v.name = name;
   v.is_num = true;
   v.num = value;
   v.precision = precision;
   return v;
}

auto str_value(const char *name, const char *text) -> IndicatorValue {
   IndicatorValue v;
   v.name = name;
   v.str = text;
   return v;
}

auto numeric_rule(double at, const char *icon) -> IndicatorStateRule {
   IndicatorStateRule rule;
   rule.has_at = true;
   rule.at = at;
   rule.icon = icon;
   return rule;
}

auto discrete_rule(const char *state, const char *icon) -> IndicatorStateRule {
   IndicatorStateRule rule;
   rule.state = state;
   rule.icon = icon;
   return rule;
}

/// Minimal provider for testing IndicatorComposeFormat (default_format = "")
struct TestIndicatorProvider : IndicatorProvider {
   TestIndicatorProvider() {
      name = "test";
      default_format = "";
      default_poll_ms = 0;
      per_frame = true;
      is_collection = false;
   }
   void Refresh(const BarIndicatorConfig &, IndicatorRuntime &, SharedState *,
                MainContext *) const override {}
};

/// Workspaces provider for testing (default_format = "{id}")
struct WorkspacesTestProvider : IndicatorProvider {
   WorkspacesTestProvider() {
      name = "workspaces";
      default_format = "{id}";
      default_poll_ms = 0;
      per_frame = true;
      is_collection = true;
   }
   void Refresh(const BarIndicatorConfig &, IndicatorRuntime &, SharedState *,
                MainContext *) const override {}
};

TestIndicatorProvider g_test_provider;
WorkspacesTestProvider g_workspaces_test_provider;

} // namespace

/* =========================================================================
 * Unified format dialect
 * ========================================================================= */

CBELT_TEST(format_value_alias_and_explicit_precision) {
   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_MEMORY;
   cfg.format = "{value:.2f}/{total_gb:.1f}";

   IndicatorRuntime rt;
   rt.values.push_back(num_value("value", 3.14159, 1));
   rt.values.push_back(num_value("total_gb", 15.9, 1));

   std::string const out = IndicatorComposeFormat(cfg, rt, &g_test_provider);
   cbelt_assert(strcmp(out.c_str(), "3.14/15.9") == 0);
   return TEST_SUCCESS;
}

CBELT_TEST(format_uses_value_default_precision) {
   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_MEMORY;
   cfg.format = "{value}"; // no .Nf -> default precision from the bag

   IndicatorRuntime rt;
   rt.values.push_back(num_value("value", 3.14159, 1));

   std::string const out = IndicatorComposeFormat(cfg, rt, &g_test_provider);
   cbelt_assert(strcmp(out.c_str(), "3.1") == 0);
   return TEST_SUCCESS;
}

CBELT_TEST(format_icon_and_state_tokens) {
   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_VOLUME;
   cfg.format = "{icon}|{state}";

   IndicatorRuntime rt;
   rt.icon = "M";
   rt.state = "muted";

   std::string const out = IndicatorComposeFormat(cfg, rt, &g_test_provider);
   cbelt_assert(strcmp(out.c_str(), "M|muted") == 0);
   return TEST_SUCCESS;
}

CBELT_TEST(format_missing_specifier_renders_empty) {
   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_CPU;
   cfg.format = "x{nope}y";

   IndicatorRuntime rt;
   rt.values.push_back(num_value("value", 1.0, 0));

   /* An absent specifier renders nothing (surrounding literals survive). */
   std::string const out = IndicatorComposeFormat(cfg, rt, &g_test_provider);
   cbelt_assert(strcmp(out.c_str(), "xy") == 0);
   return TEST_SUCCESS;
}

CBELT_TEST(format_missing_specifier_with_others_fills_present) {
   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_CPU;

   IndicatorRuntime rt;
   rt.values.push_back(num_value("value", 42.0, 0));

   /* No icon/text present: the absent tokens contribute nothing at all. */
   cfg.format = "{icon}{text}";
   std::string const empty = IndicatorComposeFormat(cfg, rt, &g_test_provider);
   cbelt_assert(empty.empty());

   /* A present value still fills in while an absent one stays empty. */
   cfg.format = "{value}{text}";
   std::string const mixed = IndicatorComposeFormat(cfg, rt, &g_test_provider);
   cbelt_assert(strcmp(mixed.c_str(), "42") == 0);
   return TEST_SUCCESS;
}

CBELT_TEST(format_provider_default_when_empty) {
   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_WORKSPACES; // provider default "{id}"
   cfg.format = "";

   IndicatorRuntime rt;
   rt.values.push_back(str_value("id", "3"));

   std::string const out =
       IndicatorComposeFormat(cfg, rt, &g_workspaces_test_provider);
   cbelt_assert(strcmp(out.c_str(), "3") == 0);
   return TEST_SUCCESS;
}

CBELT_TEST(format_title_raw_when_empty) {
   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_TITLE;
   cfg.format = "";

   IndicatorRuntime rt;
   rt.text = "raw window title";

   std::string const out = IndicatorComposeFormat(cfg, rt, &g_test_provider);
   cbelt_assert(strcmp(out.c_str(), "raw window title") == 0);
   return TEST_SUCCESS;
}

/* =========================================================================
 * State rule resolution
 * ========================================================================= */

CBELT_TEST(state_discrete_wins_over_numeric) {
   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_VOLUME;
   cfg.state_count = 2;
   cfg.states[0] = numeric_rule(50.0, "N50");
   cfg.states[1] = discrete_rule("muted", "MUTED");

   IndicatorRuntime rt;
   rt.values.push_back(num_value("value", 90.0, 0));
   rt.state = "muted";

   const IndicatorStateRule *rule = IndicatorFindStateRule(cfg, rt);
   cbelt_assert_not_null(rule);
   cbelt_assert(strcmp(rule->icon.c_str(), "MUTED") == 0);
   return TEST_SUCCESS;
}

CBELT_TEST(state_highest_at_not_exceeding_primary) {
   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_CPU;
   cfg.state_count = 3;
   cfg.states[0] = numeric_rule(25.0, "A");
   cfg.states[1] = numeric_rule(75.0, "B");
   cfg.states[2] = numeric_rule(50.0, "C");

   IndicatorRuntime rt;
   rt.values.push_back(num_value("value", 80.0, 0));
   const IndicatorStateRule *rule = IndicatorFindStateRule(cfg, rt);
   cbelt_assert_not_null(rule);
   cbelt_assert(strcmp(rule->icon.c_str(), "B") == 0);

   rt.values[0].num = 60.0;
   rule = IndicatorFindStateRule(cfg, rt);
   cbelt_assert_not_null(rule);
   cbelt_assert(strcmp(rule->icon.c_str(), "C") == 0);
   return TEST_SUCCESS;
}

CBELT_TEST(state_fallback_is_lowest_at) {
   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_CPU;
   cfg.state_count = 3;
   cfg.states[0] = numeric_rule(25.0, "A");
   cfg.states[1] = numeric_rule(75.0, "B");
   cfg.states[2] = numeric_rule(50.0, "C");

   IndicatorRuntime rt;
   rt.values.push_back(num_value("value", 5.0, 0));

   const IndicatorStateRule *rule = IndicatorFindStateRule(cfg, rt);
   cbelt_assert_not_null(rule);
   cbelt_assert(strcmp(rule->icon.c_str(), "A") == 0);
   return TEST_SUCCESS;
}

CBELT_TEST(state_none_when_no_rules) {
   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_CPU;
   cfg.state_count = 0;

   IndicatorRuntime rt;
   rt.values.push_back(num_value("value", 42.0, 0));

   cbelt_assert_null(IndicatorFindStateRule(cfg, rt));
   return TEST_SUCCESS;
}

/* =========================================================================
 * Regression: same-type instances own independent runtimes (no shared buffer)
 * ========================================================================= */

CBELT_TEST(same_type_instances_are_independent) {
   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_CPU;
   cfg.format = "{value}";

   IndicatorRuntime a;
   a.values.push_back(num_value("value", 10.0, 0));
   IndicatorRuntime b;
   b.values.push_back(num_value("value", 90.0, 0));

   std::string const ta = IndicatorComposeFormat(cfg, a, &g_test_provider);
   std::string const tb = IndicatorComposeFormat(cfg, b, &g_test_provider);
   cbelt_assert(strcmp(ta.c_str(), "10") == 0);
   cbelt_assert(strcmp(tb.c_str(), "90") == 0);

   // Composing b must not have mutated a's value bag or text.
   std::string const ta2 = IndicatorComposeFormat(cfg, a, &g_test_provider);
   cbelt_assert(strcmp(ta2.c_str(), "10") == 0);
   cbelt_assert(a.values.size() == 1);
   cbelt_assert(a.values[0].num == 10.0);
   return TEST_SUCCESS;
}

/* =========================================================================
 * Poll gate (M1a): keyed on `polled`, never on drawability
 * ========================================================================= */

CBELT_TEST(should_poll_gates_on_polled_not_drawability) {
   IndicatorRuntime rt;
   rt.polled = true;
   rt.last_poll_ms = 1000;
   rt.text.clear(); // not drawable

   // Polled, non-per-frame, within the rate window -> skip even if empty.
   cbelt_assert_false(IndicatorShouldPoll(false, 500, rt, 1200));
   // After the rate window -> poll.
   cbelt_assert_true(IndicatorShouldPoll(false, 500, rt, 1600));
   // Per-frame always polls.
   cbelt_assert_true(IndicatorShouldPoll(true, 500, rt, 1200));
   // Never polled -> poll immediately.
   rt.polled = false;
   cbelt_assert_true(IndicatorShouldPoll(false, 500, rt, 1200));
   return TEST_SUCCESS;
}

/* =========================================================================
 * Effect resolution (M3): rule > indicator > bar default
 * ========================================================================= */

CBELT_TEST(resolve_effects_precedence) {
   Clay_Color const bar_default = {
       .r = 1.0F, .g = 2.0F, .b = 3.0F, .a = 255.0F};

   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_VOLUME;
   cfg.state_count = 1;
   cfg.states[0] = discrete_rule("muted", "MUTED");

   IndicatorRuntime rt;
   rt.state = "muted";
   std::string icon;
   Clay_Color color;

   // Matched rule color wins.
   cfg.color = RGB(0x00, 0x00, 0xff);
   cfg.color_set = true;
   cfg.states[0].color = RGB(0xff, 0x00, 0x00);
   cfg.states[0].has_color = true;
   IndicatorResolveEffects(cfg, rt, bar_default, icon, color);
   cbelt_assert(strcmp(icon.c_str(), "MUTED") == 0);
   cbelt_assert(color.r == 255.0F && color.g == 0.0F && color.b == 0.0F);

   // No rule color -> indicator color.
   cfg.states[0].has_color = false;
   IndicatorResolveEffects(cfg, rt, bar_default, icon, color);
   cbelt_assert(color.r == 0.0F && color.g == 0.0F && color.b == 255.0F);

   // Neither -> bar default.
   cfg.color_set = false;
   IndicatorResolveEffects(cfg, rt, bar_default, icon, color);
   cbelt_assert(color.r == 1.0F && color.g == 2.0F && color.b == 3.0F);
   return TEST_SUCCESS;
}

CBELT_TEST(resolve_effects_no_rule_keeps_indicator_icon_empty) {
   Clay_Color const bar_default = {
       .r = 1.0F, .g = 2.0F, .b = 3.0F, .a = 255.0F};
   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_CPU;
   cfg.state_count = 0;

   IndicatorRuntime rt;
   std::string icon = "provider-default";
   Clay_Color color;
   IndicatorResolveEffects(cfg, rt, bar_default, icon, color);
   cbelt_assert(icon.empty());
   cbelt_assert(color.r == 1.0F && color.g == 2.0F && color.b == 3.0F);
   return TEST_SUCCESS;
}

/* =========================================================================
 * Shared element-id arithmetic (render == hit)
 * ========================================================================= */

CBELT_TEST(indicator_element_id_single_uses_slot) {
   Clay_String const ind = {
       .isStaticallyAllocated = true, .length = 3, .chars = "ind"};
   for (int slot = 0; slot < BAR_MAX_INDICATORS; slot++) {
      Clay_ElementId const got = IndicatorElementId(false, slot, -1);
      Clay_ElementId const expected =
          Clay_GetElementIdWithIndex(ind, (uint32_t)slot);
      cbelt_assert(got.id == expected.id);
      cbelt_assert(got.offset == expected.offset);
      if (slot + 1 < BAR_MAX_INDICATORS) {
         Clay_ElementId const other = IndicatorElementId(false, slot + 1, -1);
         cbelt_assert(other.id != got.id);
      }
   }
   return TEST_SUCCESS;
}

CBELT_TEST(indicator_element_id_workspace_reads_back) {
   Clay_String const ws = {
       .isStaticallyAllocated = true, .length = 2, .chars = "ws"};
   for (int slot = 0; slot < BAR_MAX_INDICATORS; slot++) {
      for (int item = 0; item < BAR_MAX_WS_LABELS; item++) {
         uint32_t const index = IndicatorWorkspaceIndex(slot, item);
         /* Decode back to (slot, item) without loss. */
         cbelt_assert((int)(index % BAR_MAX_WS_LABELS) == item);
         cbelt_assert((int)(index / BAR_MAX_WS_LABELS) == slot);

         /* The id the renderer/hit-test build from the shared helper matches
          * the explicit Clay computation for that index. */
         Clay_ElementId const got = IndicatorElementId(true, slot, item);
         Clay_ElementId const expected = Clay_GetElementIdWithIndex(ws, index);
         cbelt_assert(got.id == expected.id);
         cbelt_assert(got.offset == expected.offset);
      }
   }

   /* Ids are distinct across (slot, item) pairs that the renderer emits. */
   cbelt_assert(IndicatorElementId(true, 0, 0).id !=
                IndicatorElementId(true, 0, 1).id);
   cbelt_assert(IndicatorElementId(true, 0, 0).id !=
                IndicatorElementId(true, 1, 0).id);
   return TEST_SUCCESS;
}

/* =========================================================================
 * Custom indicator output -> runtime mapping
 * ========================================================================= */

namespace {

auto find_runtime_value(const IndicatorRuntime &rt, const char *name)
    -> const IndicatorValue * {
   for (const IndicatorValue &v : rt.values) {
      if (v.name == name)
         return &v;
   }
   return nullptr;
}

} // namespace

CBELT_TEST(apply_custom_output_maps_fields) {
   LuaIndicatorOutput out;
   out.valid = true;
   out.has_text = true;
   out.text = "hello";
   out.has_state = true;
   out.state = "st";
   out.has_icon = true;
   out.icon = "IC";
   out.has_value = true;
   out.value = 7.5;
   out.has_color = true;
   out.color = RGB(0x11, 0x22, 0x33);
   LuaIndicatorValue num;
   num.name = "num";
   num.is_num = true;
   num.num = 2.5;
   num.precision = 1;
   LuaIndicatorValue str;
   str.name = "str";
   str.str = "abc";
   out.values.push_back(num);
   out.values.push_back(str);

   IndicatorRuntime rt;
   rt.text = "stale";
   rt.state = "old";
   rt.icon = "old";
   rt.color_set = true;
   ApplyIndicatorOutput(out, rt);

   cbelt_assert(strcmp(rt.text.c_str(), "hello") == 0);
   cbelt_assert(strcmp(rt.state.c_str(), "st") == 0);
   cbelt_assert(strcmp(rt.icon.c_str(), "IC") == 0);
   cbelt_assert(rt.color_set == true);
   cbelt_assert(rt.color.r == 17.0F && rt.color.g == 34.0F &&
                rt.color.b == 51.0F);
   cbelt_assert(rt.valid == true);

   const IndicatorValue *val = find_runtime_value(rt, "value");
   cbelt_assert_not_null(val);
   cbelt_assert(val->is_num && val->num == 7.5);
   val = find_runtime_value(rt, "num");
   cbelt_assert_not_null(val);
   cbelt_assert(val->is_num && val->num == 2.5 && val->precision == 1);
   val = find_runtime_value(rt, "str");
   cbelt_assert_not_null(val);
   cbelt_assert(!val->is_num && val->str == "abc");
   return TEST_SUCCESS;
}

CBELT_TEST(apply_custom_output_clears_previous_bag) {
   LuaIndicatorOutput out;
   out.valid = true;
   out.has_value = true;
   out.value = 1.0;

   IndicatorRuntime rt;
   rt.values.push_back(num_value("old", 9.0, 0));
   rt.state = "oldstate";
   rt.icon = "oldicon";
   rt.color_set = true;
   rt.text = "oldtext";

   ApplyIndicatorOutput(out, rt);

   cbelt_assert(rt.values.size() == 1);
   cbelt_assert(find_runtime_value(rt, "old") == nullptr);
   cbelt_assert(rt.state.empty());
   cbelt_assert(rt.icon.empty());
   cbelt_assert(rt.color_set == false);
   cbelt_assert(rt.text.empty());
   cbelt_assert(rt.valid == true); /* value present */
   return TEST_SUCCESS;
}

CBELT_TEST(apply_custom_output_partial_is_valid) {
   LuaIndicatorOutput out;
   out.valid = true;
   out.has_state = true;
   out.state = "onlystate";

   IndicatorRuntime rt;
   ApplyIndicatorOutput(out, rt);
   cbelt_assert(rt.valid == true);
   cbelt_assert(rt.text.empty());
   cbelt_assert(strcmp(rt.state.c_str(), "onlystate") == 0);
   return TEST_SUCCESS;
}

CBELT_TEST(apply_custom_output_empty_is_invalid) {
   LuaIndicatorOutput out;
   out.valid = true; /* a table that carried no fields */
   IndicatorRuntime rt;
   rt.text = "stale";
   ApplyIndicatorOutput(out, rt);
   cbelt_assert(rt.valid == false);
   cbelt_assert(rt.text.empty());
   cbelt_assert(rt.color_set == false);
   return TEST_SUCCESS;
}

CBELT_TEST(apply_custom_output_clear_drops_runtime) {
   LuaIndicatorOutput out;
   out.valid = true; /* a `{ clear = true }` table carries no drawable fields */
   out.clear = true;

   IndicatorRuntime rt;
   rt.text = "stale";
   rt.state = "old";
   rt.icon = "old";
   rt.color_set = true;
   rt.values.push_back(num_value("old", 9.0, 0));
   rt.valid = true;

   ApplyIndicatorOutput(out, rt);

   cbelt_assert(rt.valid == false);
   cbelt_assert(rt.text.empty());
   cbelt_assert(rt.state.empty());
   cbelt_assert(rt.icon.empty());
   cbelt_assert(rt.color_set == false);
   cbelt_assert(rt.values.empty());
   cbelt_assert(rt.clear_requested == true);

   /* A non-clear output resets the flag so it never persists across polls. */
   LuaIndicatorOutput plain;
   plain.valid = true;
   plain.has_text = true;
   plain.text = "again";
   ApplyIndicatorOutput(plain, rt);
   cbelt_assert(rt.clear_requested == false);
   cbelt_assert(rt.valid == true);
   return TEST_SUCCESS;
}

CBELT_TEST(clear_requested_drops_before_format_compose) {
   /* `{ clear = true }` maps to an invalid, empty runtime with the flag set. */
   LuaIndicatorOutput out;
   out.valid = true;
   out.clear = true;
   IndicatorRuntime rt;
   rt.text = "old";
   ApplyIndicatorOutput(out, rt);
   cbelt_assert(rt.clear_requested == true);
   cbelt_assert(rt.valid == false);
   cbelt_assert(rt.text.empty());

   /* With a non-empty format the absent specifiers no longer leak, but the
    * literal separator between them would remain, so the runtime would still
    * be non-empty (and thus drawable) without the short-circuit. */
   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_CUSTOM;
   cfg.format = "{icon} {text}";
   std::string const residue =
       IndicatorComposeFormat(cfg, rt, &g_test_provider);
   cbelt_assert(residue.find('{') == std::string::npos);
   cbelt_assert(!residue.empty());

   /* The compose short-circuit drops the cleared output instead: not drawn. */
   cbelt_assert(IndicatorDropClearedOutput(rt) == true);
   cbelt_assert(rt.valid == false);
   cbelt_assert(rt.text.empty());

   /* A subsequent normal output clears the flag and composes again. */
   LuaIndicatorOutput again;
   again.valid = true;
   again.has_text = true;
   again.text = "back";
   ApplyIndicatorOutput(again, rt);
   cbelt_assert(rt.clear_requested == false);
   cbelt_assert(IndicatorDropClearedOutput(rt) == false);
   std::string const composed =
       IndicatorComposeFormat(cfg, rt, &g_test_provider);
   cbelt_assert(strcmp(composed.c_str(), " back") == 0);
   cbelt_assert(rt.valid == true);
   return TEST_SUCCESS;
}

CBELT_TEST(indicator_resolve_effects_output_color_precedence) {
   Clay_Color const bar_default = {
       .r = 9.0F, .g = 9.0F, .b = 9.0F, .a = 255.0F};
   IndicatorRuntime rt;
   rt.color = {.r = 1.0F, .g = 2.0F, .b = 3.0F, .a = 255.0F};
   rt.color_set = true;

   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_CUSTOM;
   cfg.color = RGB(0x00, 0x00, 0xff);
   cfg.color_set = true;
   std::string icon;
   Clay_Color color;

   /* Output color wins over cfg color and the bar default. */
   IndicatorResolveEffects(cfg, rt, bar_default, icon, color);
   cbelt_assert(color.r == 1.0F && color.g == 2.0F && color.b == 3.0F);

   /* A matching rule color overrides the output color. */
   cfg.state_count = 1;
   cfg.states[0] = discrete_rule("muted", "M");
   cfg.states[0].color = RGB(0xff, 0x00, 0x00);
   cfg.states[0].has_color = true;
   rt.state = "muted";
   IndicatorResolveEffects(cfg, rt, bar_default, icon, color);
   cbelt_assert(color.r == 255.0F && color.g == 0.0F && color.b == 0.0F);

   /* No output color -> cfg color. */
   rt.color_set = false;
   cfg.state_count = 0;
   rt.state.clear();
   IndicatorResolveEffects(cfg, rt, bar_default, icon, color);
   cbelt_assert(color.r == 0.0F && color.g == 0.0F && color.b == 255.0F);
   return TEST_SUCCESS;
}

CBELT_TEST(custom_value_bag_composes) {
   LuaIndicatorOutput out;
   out.valid = true;
   out.has_value = true;
   out.value = 3.14159;
   LuaIndicatorValue ratio;
   ratio.name = "ratio";
   ratio.is_num = true;
   ratio.num = 2.5;
   ratio.precision = 0;
   out.values.push_back(ratio);

   IndicatorRuntime rt;
   ApplyIndicatorOutput(out, rt);

   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_CUSTOM;
   cfg.format = "{value:.2f} {ratio:.1f}";
   std::string const composed =
       IndicatorComposeFormat(cfg, rt, &g_test_provider);
   cbelt_assert(strcmp(composed.c_str(), "3.14 2.5") == 0);
   return TEST_SUCCESS;
}

CBELT_TEST(custom_output_bytes_survive_mapping_and_compose) {
   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_CUSTOM; /* format empty -> text kept verbatim */

   LuaIndicatorOutput out;
   out.valid = true;
   out.has_text = true;
   out.text.assign("a\0b", 3);

   IndicatorRuntime rt;
   ApplyIndicatorOutput(out, rt);
   cbelt_assert(rt.text.size() == 3);
   cbelt_assert(rt.text[0] == 'a' && rt.text[1] == '\0' && rt.text[2] == 'b');
   std::string composed = IndicatorComposeFormat(cfg, rt, &g_test_provider);
   cbelt_assert(composed.size() == 3);
   cbelt_assert(composed == rt.text);

   /* Multi-byte UTF-8 (é = C3 A9) survives mapping + compose byte-for-byte. */
   out.text.assign("caf\xC3\xA9", 5);
   ApplyIndicatorOutput(out, rt);
   cbelt_assert(rt.text.size() == 5);
   cbelt_assert(static_cast<unsigned char>(rt.text[3]) == 0xC3);
   cbelt_assert(static_cast<unsigned char>(rt.text[4]) == 0xA9);
   composed = IndicatorComposeFormat(cfg, rt, &g_test_provider);
   cbelt_assert(composed == rt.text);
   return TEST_SUCCESS;
}

CBELT_TEST(keep_previous_restores_whole_runtime_on_polled_failure) {
   /* The last good runtime: text + bag + state + icon + color. */
   IndicatorRuntime previous;
   previous.text = "GOOD";
   previous.state = "busy";
   previous.icon = "I";
   previous.color = {.r = 1.0F, .g = 2.0F, .b = 3.0F, .a = 255.0F};
   previous.color_set = true;
   previous.values.push_back(num_value("value", 42.0, 0));
   previous.valid = true;

   /* A failed poll clears the bag and reports !valid. */
   IndicatorRuntime rt;
   rt.valid = false;

   bool const restored = IndicatorKeepPrevious(false, previous, rt);
   cbelt_assert(restored == true);
   cbelt_assert(strcmp(rt.text.c_str(), "GOOD") == 0);
   cbelt_assert(strcmp(rt.state.c_str(), "busy") == 0);
   cbelt_assert(strcmp(rt.icon.c_str(), "I") == 0);
   cbelt_assert(rt.color_set == true);
   cbelt_assert(rt.color.r == 1.0F && rt.color.g == 2.0F && rt.color.b == 3.0F);
   cbelt_assert(rt.values.size() == 1);

   /* Per-frame providers never restore: a failure renders nothing. */
   IndicatorRuntime per_frame;
   per_frame.valid = false;
   cbelt_assert(IndicatorKeepPrevious(true, previous, per_frame) == false);
   cbelt_assert(per_frame.text.empty());
   cbelt_assert(per_frame.values.empty());

   /* First-ever failure (default previous) stays hidden. */
   IndicatorRuntime fresh;
   fresh.valid = false;
   IndicatorRuntime no_previous;
   cbelt_assert(IndicatorKeepPrevious(false, no_previous, fresh) == true);
   cbelt_assert(fresh.valid == false);
   cbelt_assert(fresh.text.empty());
   return TEST_SUCCESS;
}

CBELT_TEST(keep_previous_ignored_when_clear_requested) {
   /* A last-good runtime that must NOT be revived after an explicit clear. */
   IndicatorRuntime previous;
   previous.text = "GOOD";
   previous.valid = true;
   previous.values.push_back(num_value("value", 42.0, 0));

   /* Poll returned `{ clear = true }`: ApplyIndicatorOutput made it invalid but
    * flagged clear_requested, which suppresses the keep-previous restore. */
   IndicatorRuntime rt;
   rt.clear_requested = true;
   rt.valid = false;
   bool const restored = IndicatorKeepPrevious(false, previous, rt);
   cbelt_assert(restored == false);
   cbelt_assert(rt.valid == false);
   cbelt_assert(rt.text.empty());
   cbelt_assert(rt.values.empty());

   /* A plain nil/invalid poll still restores the last good snapshot. */
   IndicatorRuntime plain;
   plain.valid = false;
   plain.clear_requested = false;
   cbelt_assert(IndicatorKeepPrevious(false, previous, plain) == true);
   cbelt_assert(strcmp(plain.text.c_str(), "GOOD") == 0);
   cbelt_assert(plain.valid == true);
   cbelt_assert(plain.values.size() == 1);
   return TEST_SUCCESS;
}

/* =========================================================================
 * Registry totality (regression: Workspaces/Title must resolve everywhere)
 * ========================================================================= */

namespace {

/// Find a numeric value by name in a runtime's bag.
auto bag_num(const IndicatorRuntime &rt, const char *name, double *out)
    -> bool {
   for (const auto &v : rt.values) {
      if (v.name == name) {
         if (!v.is_num)
            return false;
         *out = v.num;
         return true;
      }
   }
   return false;
}

/// Find a string value by name in a runtime's bag.
auto bag_str(const IndicatorRuntime &rt, const char *name, std::string *out)
    -> bool {
   for (const auto &v : rt.values) {
      if (v.name == name) {
         if (v.is_num)
            return false;
         *out = v.str;
         return true;
      }
   }
   return false;
}

} // namespace

CBELT_TEST(registry_resolves_every_indicator_type) {
   /* The registry must be total: every type resolves to a provider. A missing
      provider silently breaks hit-testing, click/scroll dispatch and width
      measurement. */
   Bar bar(nullptr, nullptr, nullptr, nullptr);
   auto registry = IndicatorRegistry::CreateDefault(&bar);

   for (int t = 0; t < static_cast<int>(BAR_INDICATOR_COUNT); t++) {
      const IndicatorProvider *p =
          registry->Get(static_cast<BarIndicatorType>(t));
      cbelt_assert(p != nullptr);
   }

   const IndicatorProvider *ws = registry->Get(BAR_INDICATOR_WORKSPACES);
   cbelt_assert_not_null(ws);
   cbelt_assert(ws->is_collection == true);
   cbelt_assert_str_equal(ws->default_format, "{id}");

   const IndicatorProvider *title = registry->Get(BAR_INDICATOR_TITLE);
   cbelt_assert_not_null(title);
   cbelt_assert(title->is_collection == false);

   /* Defaults must match the historical provider table (byte-identical UI). */
   const IndicatorProvider *clock = registry->Get(BAR_INDICATOR_CLOCK);
   cbelt_assert_str_equal(clock->default_format, "%H:%M");
   cbelt_assert(clock->per_frame == true);
   cbelt_assert(clock->default_poll_ms == 0);

   const IndicatorProvider *mem = registry->Get(BAR_INDICATOR_MEMORY);
   cbelt_assert_str_equal(mem->default_format, "{icon}{value:.1f}G");
   cbelt_assert(mem->per_frame == false);
   cbelt_assert(mem->default_poll_ms == 500);

   return TEST_SUCCESS;
}

/* =========================================================================
 * Status -> runtime mapping (byte-identical value bag / state semantics)
 * ========================================================================= */

CBELT_TEST(volume_mapping_value_and_muted_state) {
   IndicatorRuntime rt;
   VolumeStatus s = {};
   s.available = TRUE;
   s.level = 0.5F;
   s.muted = FALSE;
   bar_indicator_mapping::ApplyVolumeStatus(s, rt);
   cbelt_assert(rt.valid == true);
   double v = -1;
   cbelt_assert(bag_num(rt, "value", &v) == true);
   cbelt_assert(v == 50.0);
   cbelt_assert(rt.state.empty());

   IndicatorRuntime muted;
   s.muted = TRUE;
   bar_indicator_mapping::ApplyVolumeStatus(s, muted);
   cbelt_assert_str_equal(muted.state.c_str(), "muted");

   IndicatorRuntime unavailable;
   s.available = FALSE;
   bar_indicator_mapping::ApplyVolumeStatus(s, unavailable);
   cbelt_assert(unavailable.valid == false);
   return TEST_SUCCESS;
}

CBELT_TEST(network_mapping_state_and_value) {
   NetworkStatus s = {};
   s.available = TRUE;
   s.signal_percent = 77;
   s.ssid = "ignored";

   IndicatorRuntime disc;
   s.type = NET_DISCONNECTED;
   bar_indicator_mapping::ApplyNetworkStatus(s, disc);
   cbelt_assert(disc.valid == true);
   cbelt_assert_str_equal(disc.state.c_str(), "disconnected");
   double v = -1;
   cbelt_assert(bag_num(disc, "value", &v) == true);
   cbelt_assert(v == 0.0);

   IndicatorRuntime eth;
   s.type = NET_ETHERNET;
   bar_indicator_mapping::ApplyNetworkStatus(s, eth);
   cbelt_assert_str_equal(eth.state.c_str(), "ethernet");
   v = -1;
   cbelt_assert(bag_num(eth, "value", &v) == true);
   cbelt_assert(v == 0.0);

   IndicatorRuntime wifi;
   s.type = NET_WIFI;
   bar_indicator_mapping::ApplyNetworkStatus(s, wifi);
   cbelt_assert(wifi.state.empty());
   v = -1;
   cbelt_assert(bag_num(wifi, "value", &v) == true);
   cbelt_assert(v == 77.0);
   std::string ssid;
   cbelt_assert(bag_str(wifi, "ssid", &ssid) == true);
   cbelt_assert_str_equal(ssid.c_str(), "ignored");
   return TEST_SUCCESS;
}

CBELT_TEST(memory_mapping_gigabytes) {
   IndicatorRuntime rt;
   MemoryStatus s = {};
   s.available = TRUE;
   s.total_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
   s.available_bytes = 4ULL * 1024ULL * 1024ULL * 1024ULL;
   bar_indicator_mapping::ApplyMemoryStatus(s, rt);
   cbelt_assert(rt.valid == true);

   double v = -1;
   cbelt_assert(bag_num(rt, "value", &v) == true);
   cbelt_assert(v == 12.0);
   double total = -1;
   cbelt_assert(bag_num(rt, "total_gb", &total) == true);
   cbelt_assert(total == 16.0);
   double avail = -1;
   cbelt_assert(bag_num(rt, "avail_gb", &avail) == true);
   cbelt_assert(avail == 4.0);

   /* The status bag exposes no "load" value. */
   double load = -1;
   cbelt_assert(bag_num(rt, "load", &load) == false);
   return TEST_SUCCESS;
}

CBELT_TEST(clock_format_and_default) {
   SYSTEMTIME t = {};
   t.wYear = 2026;
   t.wMonth = 10;
   t.wDay = 10;
   t.wHour = 13;
   t.wMinute = 5;
   t.wSecond = 7;
   t.wDayOfWeek = 6;

   IndicatorRuntime def;
   bar_indicator_mapping::FormatClock(t, "", def);
   cbelt_assert(def.valid == true);
   cbelt_assert_str_equal(def.text.c_str(), "13:05");

   IndicatorRuntime fmt;
   bar_indicator_mapping::FormatClock(t, "%H:%M:%S", fmt);
   cbelt_assert_str_equal(fmt.text.c_str(), "13:05:07");

   IndicatorRuntime year;
   bar_indicator_mapping::FormatClock(t, "%Y", year);
   cbelt_assert_str_equal(year.text.c_str(), "2026");
   return TEST_SUCCESS;
}
