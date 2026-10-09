#include "cbelt.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>
#include <windows.h>

#include "../../src/config/action.h"
#include "../../src/config/defaults.h"
#include "../../src/config/lua/parser.h"
#include "../../src/core/bfwm_context.h"
#include "../../src/memory/safe.h"

CBELT_GROUP("lua_config")

/* =========================================================================
 * Helpers
 * ========================================================================= */

namespace {

inline auto write_temp_config(const char *content) -> std::string {
   std::array<char, MAX_PATH> tmp_path;
   std::array<char, MAX_PATH> tmp_file;
   GetTempPathA(MAX_PATH, tmp_path.data());
   GetTempFileNameA(tmp_path.data(), "mct", 0, tmp_file.data());
   FILE *f = fopen(tmp_file.data(), "w");
   if (!f)
      return {};
   fputs(content, f);
   fclose(f);
   return tmp_file.data();
}

inline auto create_test_ctx(void) -> BFWMContext * {
   /* Use the production initializer: it placement-news every non-trivial
    * member (keybinds, window_rules, lua.config_path, keystroke_queue,
    * bar_cfg, ...) that raw calloc storage would leave unconstructed. */
   return BFWMContextInit();
}

inline void destroy_test_ctx(BFWMContext *ctx) {
   BFWMContextFree(ctx);
}
} // namespace

/* =========================================================================
 * BarConfigDefaults
 * ========================================================================= */

CBELT_TEST(bar_config_defaults) {
   BarConfig cfg;
   BarConfigDefaults(&cfg);

   cbelt_assert(cfg.height == 32);
   cbelt_assert(cfg.enabled == TRUE);
   cbelt_assert(cfg.font.size == 18);
   cbelt_assert(cfg.font.weight == FW_BOLD);
   cbelt_assert(strcmp(cfg.font.name.data(), "Segoe UI") == 0);
   cbelt_assert(cfg.colors.background == RGB(0x30, 0x34, 0x46));
   cbelt_assert(cfg.colors.text == RGB(0xc6, 0xd0, 0xf5));
   cbelt_assert(cfg.colors.active_workspace == RGB(0xef, 0x9f, 0x76));
   cbelt_assert(cfg.colors.inactive_workspace == RGB(0x62, 0x68, 0x80));
   cbelt_assert(cfg.colors.tab_border == RGB(0x23, 0x26, 0x34));
    cbelt_assert(cfg.indicator_count == 3);
   return TEST_SUCCESS;
}

/* =========================================================================
 * LuaConfigLoad — valid config (staged: each group adds fields)
 * ========================================================================= */

CBELT_TEST(load_bare_minimum) {
   const char *lua = "BFWM.gap_between = 8\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   bool ok = LuaConfigLoad(&config, ctx, path.c_str());
   cbelt_assert(ok == TRUE);
   cbelt_assert(ctx->config.gap_between == 8);
   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(load_all_settings) {
   const char *lua =
       "BFWM.gap_between = 8\n"
       "BFWM.gap_edge = 4\n"
       "BFWM.border_color = \"#00ff00\"\n"
       "BFWM.inactive_border = \"#333333\"\n"
       "BFWM.border_width = 4\n"
       "BFWM.border_radius = 12\n"
       "BFWM.focus_follows_mouse = true\n"
       "BFWM.mouse_follows_focus = true\n"
       "BFWM.unlock_window_resize = \"LALT\"\n"
       "BFWM.unlock_window_move = \"LCTRL\"\n"
       "BFWM.layout = \"monocle\"\n"
       "Bar.height = 28\n"
       "Bar.enabled = false\n"
       "Bar.font = { size = 12, name = \"Arial\", weight = 700 }\n"
       "Bar.colors = { background = \"#111111\", text = \"#ffffff\",\n"
       "              active_workspace = \"#ffaa00\",\n"
       "              inactive_workspace = \"#555555\",\n"
       "              tab_border = \"#000000\" }\n";

   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   bool ok = LuaConfigLoad(&config, ctx, path.c_str());
   cbelt_assert(ok == TRUE);

   cbelt_assert(ctx->config.gap_between == 8);
   cbelt_assert(ctx->config.gap_edge == 4);
   cbelt_assert(ctx->config.border_color == RGB(0x00, 0xff, 0x00));
   cbelt_assert(ctx->config.inactive_border == RGB(0x33, 0x33, 0x33));
   cbelt_assert(ctx->config.border_width == 4);
   cbelt_assert(ctx->config.border_radius == 12);
   cbelt_assert(ctx->config.focus_follows_mouse == TRUE);
   cbelt_assert(ctx->config.mouse_follows_focus == TRUE);
   cbelt_assert(ctx->config.unlock_modifier == VK_LMENU);
   cbelt_assert(ctx->config.unlock_move_modifier == VK_LCONTROL);
   cbelt_assert(ctx->config.default_layout == MONOCLE);
   cbelt_assert(ctx->config.bar_cfg.height == 28);
   cbelt_assert(ctx->config.bar_cfg.enabled == FALSE);
   cbelt_assert(ctx->config.bar_cfg.font.size == 12);
   cbelt_assert(strcmp(ctx->config.bar_cfg.font.name.data(), "Arial") == 0);
   cbelt_assert(ctx->config.bar_cfg.font.weight == 700);
   cbelt_assert(ctx->config.bar_cfg.colors.background == RGB(0x11, 0x11, 0x11));
   cbelt_assert(ctx->config.bar_cfg.colors.text == RGB(0xff, 0xff, 0xff));
   cbelt_assert(ctx->config.bar_cfg.colors.active_workspace ==
                RGB(0xff, 0xaa, 0x00));
   cbelt_assert(ctx->config.bar_cfg.colors.inactive_workspace ==
                RGB(0x55, 0x55, 0x55));
   cbelt_assert(ctx->config.bar_cfg.colors.tab_border == RGB(0x00, 0x00, 0x00));

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(load_with_keybinds) {
   const char *lua =
       "BFWM.keybinds = {\n"
       "    { \"Super Return\", \"Spawn\", { command = \"cmd.exe\" } },\n"
       "    { \"Super Q\", \"KillActive\" },\n"
       "}\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   bool ok = LuaConfigLoad(&config, ctx, path.c_str());
   cbelt_assert(ok == TRUE);
   cbelt_assert(!ctx->keybinds.entries.empty());
   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(load_with_indicators) {
   const char *lua =
       "Bar.indicators = {\n"
       "    { type = \"workspaces\", align = \"left\" },\n"
       "    { type = \"clock\", align = \"right\", format = \"%H:%M\" },\n"
       "}\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   bool ok = LuaConfigLoad(&config, ctx, path.c_str());
   cbelt_assert(ok == TRUE);
   cbelt_assert(ctx->config.bar_cfg.indicator_count == 2);
   cbelt_assert(ctx->config.bar_cfg.indicators[0].type ==
                BAR_INDICATOR_WORKSPACES);
   cbelt_assert(ctx->config.bar_cfg.indicators[0].align == BAR_ALIGN_LEFT);
   cbelt_assert(ctx->config.bar_cfg.indicators[1].type == BAR_INDICATOR_CLOCK);
   cbelt_assert(ctx->config.bar_cfg.indicators[1].align == BAR_ALIGN_RIGHT);
   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(load_with_indicator_states) {
   const char *lua =
       "Bar.indicators = {\n"
       "    { type = \"volume\", align = \"right\", id = \"vol\",\n"
       "      on_click = \"click_fn\", on_scroll = \"scroll_fn\",\n"
       "      output = \"out\", format = \"{icon} {value:.0f}\",\n"
       "      states = {\n"
       "          { state = \"muted\", icon = \"M\", color = \"#ff0000\" },\n"
       "          { at = 75, icon = \"H\", format = \"{value:.1f}\" },\n"
       "          { at = 25, icon = \"L\" },\n"
       "      }\n"
       "    },\n"
       "}\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   bool ok = LuaConfigLoad(&config, ctx, path.c_str());
   cbelt_assert(ok == TRUE);
   cbelt_assert(ctx->config.bar_cfg.indicator_count == 1);

   const BarIndicatorConfig *ind = &ctx->config.bar_cfg.indicators[0];
   cbelt_assert(ind->type == BAR_INDICATOR_VOLUME);
   cbelt_assert(ind->align == BAR_ALIGN_RIGHT);
   cbelt_assert(strcmp(ind->id.c_str(), "vol") == 0);
   cbelt_assert(strcmp(ind->on_click.c_str(), "click_fn") == 0);
   cbelt_assert(strcmp(ind->on_scroll.c_str(), "scroll_fn") == 0);
   cbelt_assert(strcmp(ind->output.c_str(), "out") == 0);
   cbelt_assert(strcmp(ind->format.c_str(), "{icon} {value:.0f}") == 0);

   cbelt_assert(ind->state_count == 3);
   /* Discrete rule */
   cbelt_assert(strcmp(ind->states[0].state.c_str(), "muted") == 0);
   cbelt_assert(strcmp(ind->states[0].icon.c_str(), "M") == 0);
   cbelt_assert(ind->states[0].color == RGB(0xff, 0x00, 0x00));
   cbelt_assert(ind->states[0].has_color == true);
   cbelt_assert(ind->states[0].has_at == false);
   /* Numeric rules */
   cbelt_assert(ind->states[1].state.empty());
   cbelt_assert(ind->states[1].has_at == true);
   cbelt_assert(ind->states[1].at == 75.0);
   cbelt_assert(strcmp(ind->states[1].format.c_str(), "{value:.1f}") == 0);
   cbelt_assert(ind->states[2].has_at == true);
   cbelt_assert(ind->states[2].at == 25.0);
   /* id explicitly supplied */
   cbelt_assert(strcmp(ind->id.c_str(), "vol") == 0);

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(load_black_color_is_expressible) {
   const char *lua =
       "Bar.indicators = {\n"
       "    { type = \"volume\", align = \"right\", color = \"#000000\",\n"
       "      states = { { at = 50, color = \"#000000\" } } },\n"
       "}\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   bool ok = LuaConfigLoad(&config, ctx, path.c_str());
   cbelt_assert(ok == TRUE);

   const BarIndicatorConfig *ind = &ctx->config.bar_cfg.indicators[0];
   cbelt_assert(ind->color_set == true);
   cbelt_assert(ind->color == RGB(0x00, 0x00, 0x00));
   cbelt_assert(ind->state_count == 1);
   cbelt_assert(ind->states[0].has_color == true);
   cbelt_assert(ind->states[0].color == RGB(0x00, 0x00, 0x00));

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(load_defaults_id_and_tolerates_bad_rules) {
   const char *lua =
       "Bar.indicators = {\n"
       "    { type = \"memory\", align = \"right\",\n"
       "      states = {\n"
       "          { doesnothave = \"state\" },\n"       // unknown key + no state/at
       "          { at = \"not-a-number\" },\n"        // malformed at
       "      } },\n"
       "}\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   bool ok = LuaConfigLoad(&config, ctx, path.c_str());
   cbelt_assert(ok == TRUE);

   const BarIndicatorConfig *ind = &ctx->config.bar_cfg.indicators[0];
   cbelt_assert(strcmp(ind->id.c_str(), "memory") == 0);
   cbelt_assert(ind->color_set == false);
   /* Rules are retained even when unusable; resolution just ignores them. */
   cbelt_assert(ind->state_count == 2);
   cbelt_assert(ind->states[0].has_at == false);
   cbelt_assert(ind->states[0].state.empty());
   cbelt_assert(ind->states[1].has_at == false);

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(load_with_position_bar) {
   const char *lua = "Bar.indicators = {\n"
                     "    { type = \"workspaces\", align = \"left\", "
                     "position_bar = true },\n"
                     "}\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   bool ok = LuaConfigLoad(&config, ctx, path.c_str());
   cbelt_assert(ok == TRUE);
   cbelt_assert(ctx->config.bar_cfg.indicators[0].position_bar == true);
   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(load_dedupes_indicator_ids) {
   const char *lua =
       "Bar.indicators = {\n"
       "    { type = \"volume\", align = \"right\" },\n"
       "    { type = \"volume\", align = \"right\" },\n"
       "    { type = \"volume\", align = \"right\", id = \"volume_2\" },\n"
       "    { type = \"volume\", align = \"right\", id = \"custom\" },\n"
       "    { type = \"volume\", align = \"right\", id = \"custom\" },\n"
       "}\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   cbelt_assert(LuaConfigLoad(&config, ctx, path.c_str()) == TRUE);

   const BarConfig *bar = &ctx->config.bar_cfg;
   cbelt_assert(bar->indicator_count == 5);
   cbelt_assert(strcmp(bar->indicators[0].id.c_str(), "volume") == 0);
   cbelt_assert(strcmp(bar->indicators[1].id.c_str(), "volume_2") == 0);
   cbelt_assert(strcmp(bar->indicators[2].id.c_str(), "volume_2_2") == 0);
   cbelt_assert(strcmp(bar->indicators[3].id.c_str(), "custom") == 0);
   cbelt_assert(strcmp(bar->indicators[4].id.c_str(), "custom_2") == 0);

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(load_drops_unknown_indicator_type) {
   const char *lua =
       "Bar.indicators = {\n"
       "    { type = \"clock\", align = \"right\" },\n"
       "    { type = \"not_a_type\", align = \"left\" },\n"
       "    { align = \"left\" },\n"
       "    { type = \"cpu\", align = \"right\" },\n"
       "}\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   cbelt_assert(LuaConfigLoad(&config, ctx, path.c_str()) == TRUE);

   const BarConfig *bar = &ctx->config.bar_cfg;
   cbelt_assert(bar->indicator_count == 2);
   cbelt_assert(bar->indicators[0].type == BAR_INDICATOR_CLOCK);
   cbelt_assert(bar->indicators[1].type == BAR_INDICATOR_CPU);

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(load_truncates_extra_indicators) {
   const char *lua =
       "Bar.indicators = {\n"
       "    { type = \"workspaces\", align = \"left\" },\n"
       "    { type = \"title\", align = \"center\" },\n"
       "    { type = \"clock\", align = \"right\" },\n"
       "    { type = \"cpu\", align = \"right\" },\n"
       "    { type = \"memory\", align = \"right\" },\n"
       "    { type = \"volume\", align = \"right\" },\n"
       "    { type = \"network\", align = \"right\" },\n"
       "    { type = \"cpu\", align = \"right\" },\n"
       "    { type = \"memory\", align = \"right\" },\n"
       "}\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   bool ok = LuaConfigLoad(&config, ctx, path.c_str());
   cbelt_assert(ok == TRUE);
   cbelt_assert(ctx->config.bar_cfg.indicator_count == BAR_MAX_INDICATORS);
   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(load_with_window_rules) {
   const char *lua = "BFWM.window_rules = {\n"
                     "    {\n"
                     "        match = {\n"
                     "            { class = { equals = \"Calc\" } },\n"
                     "        },\n"
                     "        action = \"set-floating\",\n"
                     "    },\n"
                     "}\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   bool ok = LuaConfigLoad(&config, ctx, path.c_str());
   cbelt_assert(ok == TRUE);
   cbelt_assert(ctx->window_rule_count == 1);
   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

/* =========================================================================
 * LuaConfigLoad — error paths
 * ========================================================================= */

CBELT_TEST(load_unknown_BFWM_key) {
   const char *lua = "BFWM.wrong_key = 1\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());

   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   bool ok = LuaConfigLoad(&config, ctx, path.c_str());
   cbelt_assert(ok == FALSE);

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(load_unknown_bar_key) {
   const char *lua = "Bar.wrong_key = \"x\"\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());

   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   bool ok = LuaConfigLoad(&config, ctx, path.c_str());
   cbelt_assert(ok == FALSE);

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(load_syntax_error) {
   const char *lua = "BFWM.gap_between =\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());

   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   bool ok = LuaConfigLoad(&config, ctx, path.c_str());
   cbelt_assert(ok == FALSE);

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(load_missing_BFWM_table) {
   const char *lua = "-- no BFWM table, just comments\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());

   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   bool ok = LuaConfigLoad(&config, ctx, path.c_str());
   cbelt_assert(ok == TRUE);

   /* Defaults should be applied after reset */
   cbelt_assert(ctx->config.gap_between == 6);
   cbelt_assert(ctx->config.border_width == 4);
   cbelt_assert(ctx->config.border_radius == 8);
   cbelt_assert(ctx->config.default_layout == DWINDLE);
   cbelt_assert(!ctx->keybinds.entries.empty()); /* default keybinds loaded */

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(load_empty_path_uses_defaults) {
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};

   /* Empty path = no config file found: must succeed using C++ defaults,
      without raising a spurious "Error loading config" snackbar. */
   bool ok = LuaConfigLoad(&config, ctx, "");
   cbelt_assert(ok == TRUE);

   cbelt_assert(ctx->config.gap_between == 6);
   cbelt_assert(ctx->config.border_width == 4);
   cbelt_assert(ctx->config.border_radius == 8);
   cbelt_assert(ctx->config.default_layout == DWINDLE);
   cbelt_assert(ctx->config.bar_cfg.indicator_count == 3); /* defaults kept */
   cbelt_assert(!ctx->keybinds.entries.empty()); /* default keybinds loaded */

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   return TEST_SUCCESS;
}

/* =========================================================================
 * LuaConfigCall
 * ========================================================================= */

CBELT_TEST(call_existing_function) {
   const char *lua = "function test_fn() end\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());

   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   bool ok = LuaConfigLoad(&config, ctx, path.c_str());
   cbelt_assert(ok == TRUE);

   bool result = LuaConfigCall(&config, ctx, "test_fn");
   cbelt_assert(result == TRUE);

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(call_missing_function) {
   const char *lua = "-- no function defined\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());

   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   bool ok = LuaConfigLoad(&config, ctx, path.c_str());
   cbelt_assert(ok == TRUE);

   bool result = LuaConfigCall(&config, ctx, "does_not_exist");
   cbelt_assert(result == FALSE);

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(call_with_null_state) {
   LuaConfig config = {};
   bool result = LuaConfigCall(&config, nullptr, "anything");
   cbelt_assert(result == FALSE);
   return TEST_SUCCESS;
}

/* =========================================================================
 * Indicator click / scroll callbacks
 * ========================================================================= */

CBELT_TEST(call_click_receives_args) {
   const char *lua = "seen = {}\n"
                     "function probe_click(id, button, mods)\n"
                     "  seen.id = id\n"
                     "  seen.button = button\n"
                     "  seen.ctrl = mods.ctrl\n"
                     "  seen.shift = mods.shift\n"
                     "  seen.alt = mods.alt\n"
                     "end\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());

   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   cbelt_assert(LuaConfigLoad(&config, ctx, path.c_str()) == TRUE);

   int const before = lua_gettop(config.L);
   bool const ok = LuaConfigCallClick(&config, ctx, "probe_click", "vol",
                                      "left", true, false, true);
   cbelt_assert(ok == TRUE);
   cbelt_assert(lua_gettop(config.L) == before); /* success keeps stack balanced */

   lua_State *L = config.L;
   lua_getglobal(L, "seen");
   cbelt_assert(lua_istable(L, -1));
   lua_getfield(L, -1, "id");
   cbelt_assert(strcmp(lua_tostring(L, -1), "vol") == 0);
   lua_pop(L, 1);
   lua_getfield(L, -1, "button");
   cbelt_assert(strcmp(lua_tostring(L, -1), "left") == 0);
   lua_pop(L, 1);
   lua_getfield(L, -1, "ctrl");
   cbelt_assert(lua_toboolean(L, -1) == 1);
   lua_pop(L, 1);
   lua_getfield(L, -1, "shift");
   cbelt_assert(lua_toboolean(L, -1) == 0);
   lua_pop(L, 1);
   lua_getfield(L, -1, "alt");
   cbelt_assert(lua_toboolean(L, -1) == 1);
   lua_pop(L, 1);
   lua_pop(L, 1); /* seen */

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(call_scroll_receives_args) {
   const char *lua = "seen_scroll = {}\n"
                     "function probe_scroll(id, dir)\n"
                     "  seen_scroll.id = id\n"
                     "  seen_scroll.dir = dir\n"
                     "end\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());

   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   cbelt_assert(LuaConfigLoad(&config, ctx, path.c_str()) == TRUE);

   int const before = lua_gettop(config.L);
   bool const ok =
       LuaConfigCallScroll(&config, ctx, "probe_scroll", "cpu", "down");
   cbelt_assert(ok == TRUE);
   cbelt_assert(lua_gettop(config.L) == before); /* success keeps stack balanced */

   lua_State *L = config.L;
   lua_getglobal(L, "seen_scroll");
   cbelt_assert(lua_istable(L, -1));
   lua_getfield(L, -1, "id");
   cbelt_assert(strcmp(lua_tostring(L, -1), "cpu") == 0);
   lua_pop(L, 1);
   lua_getfield(L, -1, "dir");
   cbelt_assert(strcmp(lua_tostring(L, -1), "down") == 0);
   lua_pop(L, 1);
   lua_pop(L, 1); /* seen_scroll */

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(call_missing_callback_returns_false_without_throwing) {
   const char *lua = "-- no callbacks defined\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());

   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   cbelt_assert(LuaConfigLoad(&config, ctx, path.c_str()) == TRUE);

   int const before = lua_gettop(config.L);
   cbelt_assert_false(LuaConfigCallClick(&config, ctx, "no_such_click", "id",
                                         "left", false, false, false));
   cbelt_assert_false(LuaConfigCallScroll(&config, ctx, "no_such_scroll", "id",
                                          "up"));
   cbelt_assert(lua_gettop(config.L) == before); /* stack balanced */

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(call_erroring_callback_returns_false_and_balances) {
   const char *lua = "function boom(id, button, mods)\n"
                     "  error('kaboom')\n"
                     "end\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());

   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   cbelt_assert(LuaConfigLoad(&config, ctx, path.c_str()) == TRUE);

   int const before = lua_gettop(config.L);
   cbelt_assert_false(LuaConfigCallClick(&config, ctx, "boom", "id", "left",
                                         false, false, false));
   cbelt_assert(lua_gettop(config.L) == before); /* stack balanced */

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(call_non_function_global_returns_false_and_balances) {
   const char *lua = "cb_func = 42\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());

   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   cbelt_assert(LuaConfigLoad(&config, ctx, path.c_str()) == TRUE);

   int const before = lua_gettop(config.L);
   cbelt_assert_false(LuaConfigCallClick(&config, ctx, "cb_func", "id", "left",
                                         false, false, false));
   cbelt_assert_false(
       LuaConfigCallScroll(&config, ctx, "cb_func", "id", "up"));
   cbelt_assert(lua_gettop(config.L) == before); /* stack balanced */

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(call_empty_or_null_func_returns_false) {
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   cbelt_assert(LuaConfigLoad(&config, ctx, "") == TRUE);
   cbelt_assert_not_null(config.L);

   int const before = lua_gettop(config.L);
   cbelt_assert_false(
       LuaConfigCallClick(&config, ctx, "", "id", "left", false, false, false));
   cbelt_assert_false(LuaConfigCallClick(&config, ctx, nullptr, "id", "left",
                                         false, false, false));
   cbelt_assert_false(LuaConfigCallScroll(&config, ctx, "", "id", "up"));
   cbelt_assert_false(LuaConfigCallScroll(&config, ctx, nullptr, "id", "up"));
   cbelt_assert(lua_gettop(config.L) == before);

   /* Also safe with no Lua state at all. */
   LuaConfig no_state = {};
   cbelt_assert_false(LuaConfigCallClick(&no_state, ctx, "fn", "id", "left",
                                         false, false, false));
   cbelt_assert_false(LuaConfigCallScroll(&no_state, ctx, "fn", "id", "up"));

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   return TEST_SUCCESS;
}

/* =========================================================================
 * Custom indicator parsing + output bridge
 * ========================================================================= */

CBELT_TEST(load_custom_indicator) {
   const char *lua =
       "Bar.indicators = {\n"
       "    { type = \"custom\", align = \"right\", id = \"my\",\n"
       "      output = \"my_output\", on_click = \"my_click\" },\n"
       "}\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   cbelt_assert(LuaConfigLoad(&config, ctx, path.c_str()) == TRUE);

   const BarConfig *bar = &ctx->config.bar_cfg;
   cbelt_assert(bar->indicator_count == 1);
   cbelt_assert(bar->indicators[0].type == BAR_INDICATOR_CUSTOM);
   cbelt_assert(strcmp(bar->indicators[0].id.c_str(), "my") == 0);
   cbelt_assert(strcmp(bar->indicators[0].output.c_str(), "my_output") == 0);
   cbelt_assert(strcmp(bar->indicators[0].on_click.c_str(), "my_click") == 0);

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(load_drops_custom_without_output) {
   const char *lua =
       "Bar.indicators = {\n"
       "    { type = \"custom\", align = \"left\", id = \"bad\" },\n"
       "    { type = \"cpu\", align = \"right\" },\n"
       "}\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   cbelt_assert(LuaConfigLoad(&config, ctx, path.c_str()) == TRUE);

   const BarConfig *bar = &ctx->config.bar_cfg;
   cbelt_assert(bar->indicator_count == 1);
   cbelt_assert(bar->indicators[0].type == BAR_INDICATOR_CPU);

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(call_output_string_return) {
   const char *lua = "function out(id) return \"hi \" .. id end\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   cbelt_assert(LuaConfigLoad(&config, ctx, path.c_str()) == TRUE);

   int const before = lua_gettop(config.L);
   LuaIndicatorOutput out;
   cbelt_assert(LuaConfigCallOutput(&config, ctx, "out", "s1", &out) == TRUE);
   cbelt_assert(out.valid == true);
   cbelt_assert(out.has_text == true);
   cbelt_assert(strcmp(out.text.c_str(), "hi s1") == 0);
   cbelt_assert(lua_gettop(config.L) == before); /* success balances */

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(call_output_table_return) {
   const char *lua =
       "function out(id)\n"
       "  return { text = \"t\", state = \"st\", icon = \"ic\", value = 42,\n"
       "           color = \"#0a0b0c\", values = { n = 3, x = \"y\" } }\n"
       "end\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   cbelt_assert(LuaConfigLoad(&config, ctx, path.c_str()) == TRUE);

   int const before = lua_gettop(config.L);
   LuaIndicatorOutput out;
   cbelt_assert(LuaConfigCallOutput(&config, ctx, "out", "s", &out) == TRUE);
   cbelt_assert(out.valid == true);
   cbelt_assert(out.has_text && strcmp(out.text.c_str(), "t") == 0);
   cbelt_assert(out.has_state && strcmp(out.state.c_str(), "st") == 0);
   cbelt_assert(out.has_icon && strcmp(out.icon.c_str(), "ic") == 0);
   cbelt_assert(out.has_value && out.value == 42.0);
   cbelt_assert(out.has_color == true);
   cbelt_assert(out.color == RGB(0x0a, 0x0b, 0x0c));
   cbelt_assert(out.values.size() == 2);
   bool found_num = false;
   bool found_str = false;
   for (const LuaIndicatorValue &v : out.values) {
      if (v.name == "n") {
         found_num = v.is_num && v.num == 3.0;
      } else if (v.name == "x") {
         found_str = (!v.is_num) && v.str == "y";
      }
   }
   cbelt_assert(found_num && found_str);
   cbelt_assert(lua_gettop(config.L) == before);

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(call_output_nil_or_missing_returns_false) {
   const char *lua = "function out(id) return nil end\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   cbelt_assert(LuaConfigLoad(&config, ctx, path.c_str()) == TRUE);

   int const before = lua_gettop(config.L);
   LuaIndicatorOutput out;
   out.valid = true; /* must be reset by the call */
   cbelt_assert_false(LuaConfigCallOutput(&config, ctx, "out", "s", &out));
   cbelt_assert(out.valid == false);
   cbelt_assert_false(
       LuaConfigCallOutput(&config, ctx, "no_such_fn", "s", &out));
   cbelt_assert(lua_gettop(config.L) == before);

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(call_output_error_returns_false_and_balances) {
   const char *lua = "function out(id) error('boom') end\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   cbelt_assert(LuaConfigLoad(&config, ctx, path.c_str()) == TRUE);

   int const before = lua_gettop(config.L);
   LuaIndicatorOutput out;
   cbelt_assert_false(LuaConfigCallOutput(&config, ctx, "out", "s", &out));
   cbelt_assert(out.valid == false);
   cbelt_assert(lua_gettop(config.L) == before);

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(call_output_non_function_returns_false) {
   const char *lua = "out = 5\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   cbelt_assert(LuaConfigLoad(&config, ctx, path.c_str()) == TRUE);

   int const before = lua_gettop(config.L);
   LuaIndicatorOutput out;
   cbelt_assert_false(LuaConfigCallOutput(&config, ctx, "out", "s", &out));
   cbelt_assert(lua_gettop(config.L) == before);

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(call_output_preserves_raw_bytes) {
   /* Embedded NUL ("a\0b") and multi-byte UTF-8 ("caf" + C3 A9). The Lua
    * decimal escapes parse to raw bytes; the bridge must preserve length. */
   const char *lua = "function nul(id) return \"a\\0b\" end\n"
                     "function utf8(id) return \"caf\\195\\169\" end\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   cbelt_assert(LuaConfigLoad(&config, ctx, path.c_str()) == TRUE);

   LuaIndicatorOutput out;
   cbelt_assert(LuaConfigCallOutput(&config, ctx, "nul", "s", &out) == TRUE);
   cbelt_assert(out.has_text == true);
   cbelt_assert(out.text.size() == 3);
   cbelt_assert(out.text[0] == 'a' && out.text[1] == '\0' && out.text[2] == 'b');

   cbelt_assert(LuaConfigCallOutput(&config, ctx, "utf8", "s", &out) == TRUE);
   cbelt_assert(out.text.size() == 5);
   cbelt_assert(static_cast<unsigned char>(out.text[3]) == 0xC3);
   cbelt_assert(static_cast<unsigned char>(out.text[4]) == 0xA9);

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

CBELT_TEST(call_output_rejects_malformed_color) {
   const char *lua = "function c1(id) return { color = \"#zz\" } end\n"
                     "function c2(id) return { color = \"not-a-color\" } end\n"
                     "function c3(id) return { color = \"#fff\" } end\n"
                     "function c4(id) return { color = \"#00ff00\" } end\n";
   std::string path = write_temp_config(lua);
   cbelt_assert_false(path.empty());
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   cbelt_assert(LuaConfigLoad(&config, ctx, path.c_str()) == TRUE);

   LuaIndicatorOutput out;
   cbelt_assert(LuaConfigCallOutput(&config, ctx, "c1", "s", &out) == TRUE);
   cbelt_assert(out.has_color == false);
   cbelt_assert(LuaConfigCallOutput(&config, ctx, "c2", "s", &out) == TRUE);
   cbelt_assert(out.has_color == false);
   cbelt_assert(LuaConfigCallOutput(&config, ctx, "c3", "s", &out) == TRUE);
   cbelt_assert(out.has_color == false);
   cbelt_assert(LuaConfigCallOutput(&config, ctx, "c4", "s", &out) == TRUE);
   cbelt_assert(out.has_color == true);
   cbelt_assert(out.color == RGB(0x00, 0xff, 0x00));

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path.c_str());
   return TEST_SUCCESS;
}

/* =========================================================================
 * BFWM.exec_cache / BFWM.spawn argument parsing
 * ========================================================================= */

CBELT_TEST(exec_cache_invalid_args_return_nil) {
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   cbelt_assert(LuaConfigLoad(&config, ctx, "") == TRUE);
   cbelt_assert_not_null(ctx->exec_cache);

   const char *cases[] = {
       "r = BFWM.exec_cache(42, 'k')",               /* argv not a string/table */
       "r = BFWM.exec_cache({}, 'k')",               /* empty argv */
       "r = BFWM.exec_cache('', 'k')",               /* empty argv[0] */
       "r = BFWM.exec_cache('cmd.exe')",             /* missing key */
       "r = BFWM.exec_cache({ 'cmd.exe', 5 }, 'k')", /* non-string element */
       "BFWM.spawn(42)",                             /* invalid spawn argv */
       "BFWM.spawn({})",                             /* empty spawn argv */
   };
   for (const char *code : cases)
      cbelt_assert(LuaConfigCall(&config, ctx, code) == TRUE);

   lua_getglobal(config.L, "r");
   cbelt_assert(lua_isnil(config.L, -1));
   lua_pop(config.L, 1);

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(exec_cache_valid_argv_roundtrip) {
   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};
   cbelt_assert(LuaConfigLoad(&config, ctx, "") == TRUE);

   /* First call starts a child and returns nil (nothing cached yet). */
   cbelt_assert(LuaConfigCall(
                    &config, ctx,
                    "r = BFWM.exec_cache("
                    "{ 'cmd.exe', '/c', 'echo', 'bfwm_lua' }, 'lk', 0)") ==
                TRUE);
   lua_getglobal(config.L, "r");
   cbelt_assert(lua_isnil(config.L, -1));
   lua_pop(config.L, 1);

   /* Poll the cache (non-blocking) until the child is reaped. */
   std::vector<std::string> const argv = {"cmd.exe", "/c", "echo", "bfwm_lua"};
   bool found = false;
   for (int i = 0; i < 300; i++) {
      const std::string *value = ExecCacheGet(ctx->exec_cache, argv, "lk", 0);
      if ((value != nullptr) &&
          (value->find("bfwm_lua") != std::string::npos)) {
         found = true;
         break;
      }
      Sleep(10);
   }
   cbelt_assert(found == true);

   /* Fire-and-forget spawn must not crash; harvest it before teardown. */
   cbelt_assert(LuaConfigCall(&config, ctx,
                              "BFWM.spawn({ 'cmd.exe', '/c', 'echo', 'sp' })") ==
                TRUE);
   Sleep(50);
   (void)ExecCacheGet(ctx->exec_cache, argv, "lk", 0);

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   return TEST_SUCCESS;
}

/* =========================================================================
 * LuaConfigFree
 * ========================================================================= */

CBELT_TEST(free_null_state_is_safe) {
   LuaConfig config = {};
   LuaConfigFree(&config);
   return TEST_SUCCESS;
}

/* =========================================================================
 * LuaConfigReload
 * ========================================================================= */

CBELT_TEST(reload_updates_config) {
   const char *lua1 = "BFWM.gap_between = 4\n";
   const char *lua2 = "BFWM.gap_between = 12\n";

   std::string path1 = write_temp_config(lua1);
   cbelt_assert_false(path1.empty());

   BFWMContext *ctx = create_test_ctx();
   LuaConfig config = {};

   /* First load */
   bool ok = LuaConfigLoad(&config, ctx, path1.c_str());
   cbelt_assert(ok == TRUE);
   cbelt_assert(ctx->config.gap_between == 4);

   /* Rewrite the config file */
   FILE *f = fopen(path1.c_str(), "w");
   cbelt_assert_not_null(f);
   fputs(lua2, f);
   fclose(f);

   /* Reload */
   ok = LuaConfigReload(&config, ctx);
   cbelt_assert(ok == TRUE);
   cbelt_assert(ctx->config.gap_between == 12);

   LuaConfigFree(&config);
   destroy_test_ctx(ctx);
   remove(path1.c_str());
   return TEST_SUCCESS;
}
