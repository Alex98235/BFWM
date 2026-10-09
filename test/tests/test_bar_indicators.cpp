#include "cbelt.h"
#include <cstring>
#include <string>
#include <windows.h>

#include "../../src/bar/bar.h"

CBELT_GROUP("bar_indicators")

namespace {

auto num_value(const char *name, double value, int precision) -> IndicatorValue {
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

   std::string const out = IndicatorComposeFormat(cfg, rt);
   cbelt_assert(strcmp(out.c_str(), "3.14/15.9") == 0);
   return TEST_SUCCESS;
}

CBELT_TEST(format_uses_value_default_precision) {
   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_MEMORY;
   cfg.format = "{value}"; // no .Nf -> default precision from the bag

   IndicatorRuntime rt;
   rt.values.push_back(num_value("value", 3.14159, 1));

   std::string const out = IndicatorComposeFormat(cfg, rt);
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

   std::string const out = IndicatorComposeFormat(cfg, rt);
   cbelt_assert(strcmp(out.c_str(), "M|muted") == 0);
   return TEST_SUCCESS;
}

CBELT_TEST(format_unknown_specifier_is_literal) {
   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_CPU;
   cfg.format = "x{nope}y";

   IndicatorRuntime rt;
   rt.values.push_back(num_value("value", 1.0, 0));

   std::string const out = IndicatorComposeFormat(cfg, rt);
   cbelt_assert(strcmp(out.c_str(), "x{nope}y") == 0);
   return TEST_SUCCESS;
}

CBELT_TEST(format_provider_default_when_empty) {
   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_WORKSPACES; // provider default "{id}"
   cfg.format = "";

   IndicatorRuntime rt;
   rt.values.push_back(str_value("id", "3"));

   std::string const out = IndicatorComposeFormat(cfg, rt);
   cbelt_assert(strcmp(out.c_str(), "3") == 0);
   return TEST_SUCCESS;
}

CBELT_TEST(format_title_raw_when_empty) {
   BarIndicatorConfig cfg = {};
   cfg.type = BAR_INDICATOR_TITLE;
   cfg.format = "";

   IndicatorRuntime rt;
   rt.text = "raw window title";

   std::string const out = IndicatorComposeFormat(cfg, rt);
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

   std::string const ta = IndicatorComposeFormat(cfg, a);
   std::string const tb = IndicatorComposeFormat(cfg, b);
   cbelt_assert(strcmp(ta.c_str(), "10") == 0);
   cbelt_assert(strcmp(tb.c_str(), "90") == 0);

   // Composing b must not have mutated a's value bag or text.
   std::string const ta2 = IndicatorComposeFormat(cfg, a);
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
         Clay_ElementId const expected =
             Clay_GetElementIdWithIndex(ws, index);
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
   std::string const composed = IndicatorComposeFormat(cfg, rt);
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
   std::string composed = IndicatorComposeFormat(cfg, rt);
   cbelt_assert(composed.size() == 3);
   cbelt_assert(composed == rt.text);

   /* Multi-byte UTF-8 (é = C3 A9) survives mapping + compose byte-for-byte. */
   out.text.assign("caf\xC3\xA9", 5);
   ApplyIndicatorOutput(out, rt);
   cbelt_assert(rt.text.size() == 5);
   cbelt_assert(static_cast<unsigned char>(rt.text[3]) == 0xC3);
   cbelt_assert(static_cast<unsigned char>(rt.text[4]) == 0xA9);
   composed = IndicatorComposeFormat(cfg, rt);
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



