#include "cbelt.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
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
