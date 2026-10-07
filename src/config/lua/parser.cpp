#include "parser.h"
#include "../../core/bfwm_context.h"
#include "../../core/portable.h"
#include "../../input/keystroke.h"
#include "../../logging/logger.h"
#include "../../monitor/monitor.h"
#include "../../notification/snackbar.h"
#include "../../window/rules/rules.h"
#include "../../workspace/layouts/layout_api.h"
#include "../../workspace/workspace.h"
#include "../action.h"
#include "../defaults.h"
#include "../keybinds.h"
#include "builder.h"
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#ifdef __cplusplus
extern "C" {
#endif
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#ifdef __cplusplus
}
#endif
#include <minwindef.h>

#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <stringapiset.h>
#include <utility>
#include <windef.h>
#include <windows.h>
#include <wingdi.h>
#include <winnls.h>

enum {
   KEY_NAME_BUF_SIZE = 64,
   STRTOUL_BASE_HEX = 16,
   BAR_INDICATOR_ICON_MAX = 5,
   MAX_DISABLED_MONITORS = 16,
   WIDE_TEXT_BUF_SIZE = 1024,
   OPACITY_MAX = 255,
   MAX_SNACKBAR_QUEUE = 16,
};

// ---------------------------------------------------------------------------
// Lua -> Window rules parser
// ---------------------------------------------------------------------------
namespace {

inline auto parse_criterion(struct BFWMContext *ctx, lua_State *lua_state,
                            int idx) -> std::optional<WindowMatchCriterion> {
   int const abs_idx = lua_absindex(lua_state, idx);
   lua_pushnil(lua_state);
   if (lua_next(lua_state, abs_idx) == 0) {
      lua_pop(lua_state, 1);
      return std::nullopt;
   }

   const char *op_name = lua_tostring(lua_state, -2);
   const char *pattern = lua_tostring(lua_state, -1);

   if ((op_name == nullptr) || (pattern == nullptr)) {
      lua_pop(lua_state, 2);
      return std::nullopt;
   }

   WindowMatchCriterion criterion = {};
   criterion.pattern = pattern;

   if (strcmp(op_name, "equals") == 0) {
      criterion.op = MATCH_EQUALS;
   } else if (strcmp(op_name, "includes") == 0) {
      criterion.op = MATCH_INCLUDES;
   } else if (strcmp(op_name, "regex") == 0) {
      criterion.op = MATCH_REGEX;
   } else if (strcmp(op_name, "not_equals") == 0) {
      criterion.op = MATCH_NOT_EQUALS;
   } else if (strcmp(op_name, "not_regex") == 0) {
      criterion.op = MATCH_NOT_REGEX;
   } else {
      Snackbar::Warn(ctx, L"Unknown match operator '%hs' in window rule",
                     (op_name != nullptr) ? op_name : "(null)");
      lua_pop(lua_state, 2);
      return std::nullopt;
   }

   lua_pop(lua_state, 2);
   return criterion;
}

inline void parse_action(struct BFWMContext *ctx, lua_State *lua_state, int idx,
                         WindowRule *rule) {
   if (lua_isstring(lua_state, idx) != 0) {
      const char *s = lua_tostring(lua_state, idx);
      if (strcmp(s, "ignore") == 0) {
         rule->action.type = RULE_IGNORE;
      } else if (strcmp(s, "set-floating") == 0) {
         rule->action.type = RULE_SET_FLOATING;
      } else if (strcmp(s, "force-floating") == 0) {
         rule->action.type = RULE_FORCE_FLOATING;
      } else if (strcmp(s, "force-tiled") == 0) {
         rule->action.type = RULE_FORCE_TILED;
      } else {
         Snackbar::Warn(ctx, L"Unknown window rule action string '%hs'",
                        (s != nullptr) ? s : "(null)");
      }
   } else if (lua_istable(lua_state, idx)) {
      int const abs_idx = lua_absindex(lua_state, idx);
      lua_pushnil(lua_state);
      if (lua_next(lua_state, abs_idx) != 0) {
         const char *key = lua_tostring(lua_state, -2);
         if ((key != nullptr) && strcmp(key, "move_to_workspace") == 0) {
            rule->action.type = RULE_MOVE_TO_WORKSPACE;
            if (lua_isinteger(lua_state, -1) != 0)
               rule->action.workspace_index = (int)lua_tointeger(lua_state, -1);
         } else {
            Snackbar::Warn(ctx, L"Unknown window rule action key '%hs'",
                           (key != nullptr) ? key : "(null)");
         }
         lua_pop(lua_state, 2);
      }
   }
}

inline auto parse_match_entry(lua_State *lua_state, int entry_idx,
                              struct BFWMContext *ctx, size_t i, size_t j)
    -> std::optional<WindowMatchEntry> {
   WindowMatchEntry entry = {};

   lua_getfield(lua_state, entry_idx, "process");
   if (lua_istable(lua_state, -1)) {
      entry.process_match = parse_criterion(ctx, lua_state, -1);
   }
   lua_pop(lua_state, 1);

   lua_getfield(lua_state, entry_idx, "class");
   if (lua_istable(lua_state, -1))
      entry.class_match = parse_criterion(ctx, lua_state, -1);
   lua_pop(lua_state, 1);

   lua_getfield(lua_state, entry_idx, "title");
   if (lua_istable(lua_state, -1))
      entry.title_match = parse_criterion(ctx, lua_state, -1);
   lua_pop(lua_state, 1);

   if (!entry.process_match.has_value() && !entry.class_match.has_value() &&
       !entry.title_match.has_value()) {
      Snackbar::Warn(
          ctx, L"Window rule %zu, match entry %zu: no valid criteria", i, j);
      return std::nullopt;
   }

   return entry;
}

inline auto parse_single_rule(lua_State *lua_state, struct BFWMContext *ctx,
                              size_t i) -> WindowRule * {
   if (!lua_istable(lua_state, -1))
      return nullptr;

   int const rule_idx = lua_gettop(lua_state);
   auto *rule = new WindowRule{};
   if (rule == nullptr)
      return nullptr;

   lua_getfield(lua_state, rule_idx, "match");
   if (lua_istable(lua_state, -1)) {
      int const match_idx = lua_gettop(lua_state);
      auto match_count = (size_t)lua_rawlen(lua_state, match_idx);

      if (match_count > 0) {
         rule->entries.reserve(match_count);

         for (size_t j = 1; j <= match_count; j++) {
            lua_rawgeti(lua_state, match_idx, (lua_Integer)j);
            if (!lua_istable(lua_state, -1)) {
               lua_pop(lua_state, 1);
               continue;
            }

            std::optional<WindowMatchEntry> entry =
                parse_match_entry(lua_state, lua_gettop(lua_state), ctx, i, j);

            if (entry.has_value())
               rule->entries.push_back(std::move(*entry));

            lua_pop(lua_state, 1);
         }
      }
   }
   lua_pop(lua_state, 1);

   lua_getfield(lua_state, rule_idx, "action");
   parse_action(ctx, lua_state, -1, rule);
   lua_pop(lua_state, 1);

   lua_getfield(lua_state, rule_idx, "run_once");
   if (lua_isboolean(lua_state, -1)) {
      rule->run_once = (lua_toboolean(lua_state, -1) != 0) ? TRUE : FALSE;
   } else {
      rule->run_once = TRUE;
   }
   lua_pop(lua_state, 1);

   if (!rule->entries.empty())
      return rule;

   Snackbar::Warn(ctx, L"Window rule %zu has no match entries, skipping", i);
   delete rule;
   return nullptr;
}

inline auto parse_window_rules(LuaConfig *config, struct BFWMContext *ctx)
    -> int {
   lua_State *lua_state = config->L;
   int count = 0;

   lua_getglobal(lua_state, "BFWM");
   if (!lua_istable(lua_state, -1)) {
      lua_pop(lua_state, 1);
      return 0;
   }

   lua_getfield(lua_state, -1, "window_rules");
   if (!lua_istable(lua_state, -1)) {
      lua_pop(lua_state, 2);
      return 0;
   }

   int const rules_idx = lua_gettop(lua_state);
   auto rule_count = (size_t)lua_rawlen(lua_state, rules_idx);

   if (rule_count == 0) {
      lua_pop(lua_state, 2);
      return 0;
   }

   for (size_t i = 1; i <= rule_count; i++) {
      lua_rawgeti(lua_state, rules_idx, (lua_Integer)i);
      WindowRule *rule = parse_single_rule(lua_state, ctx, i);
      if (rule != nullptr) {
         ctx->window_rules.push_back(rule);
         count++;
      }
      lua_pop(lua_state, 1);
   }

   ctx->window_rule_count = (size_t)count;
   lua_pop(lua_state, 2);

   Info("Loaded %d window rules from config.lua", count);
   return count;
}

// Forward declarations
static auto parse_layout_name(const char *name) -> LayoutType;

// ---------------------------------------------------------------------------
// Workspace config parser (BFWM.workspaces)
// ---------------------------------------------------------------------------

inline void parse_workspace_options(WorkspaceConfig *workspace_cfg,
                                    lua_State *lua_state, int opts_idx,
                                    const char *id_str) {
   lua_getfield(lua_state, opts_idx, "label");
   if (lua_isstring(lua_state, -1) != 0) {
      const char *s = lua_tostring(lua_state, -1);
      if (s != nullptr)
         workspace_cfg->label = s;
   }
   lua_pop(lua_state, 1);

   lua_getfield(lua_state, opts_idx, "monitor");
   if (lua_isinteger(lua_state, -1) != 0) {
      workspace_cfg->assigned_monitor = (int)lua_tointeger(lua_state, -1);
   }
   lua_pop(lua_state, 1);

   lua_getfield(lua_state, opts_idx, "layout");
   if (lua_isstring(lua_state, -1) != 0) {
      const char *layout_name = lua_tostring(lua_state, -1);
      LayoutType const layout_type = parse_layout_name(layout_name);
      if (layout_type != LAYOUT_NONE) {
         workspace_cfg->layout = layout_type;
         workspace_cfg->has_layout = TRUE;
      } else {
         Warn("Workspace '%s': unknown layout '%s'", id_str, layout_name);
      }
   }
   lua_pop(lua_state, 1);
}

inline void validate_workspace_monitor(struct BFWMContext *ctx,
                                       const WorkspaceConfig *workspace_cfg) {
   if (workspace_cfg->assigned_monitor > 0 && !ctx->monitors->Empty()) {
      BOOL found = FALSE;
      for (const auto &monitor : ctx->monitors->Monitors()) {
         if (std::cmp_equal(monitor->GetDisplayNumber(),
                            workspace_cfg->assigned_monitor)) {
            found = TRUE;
            break;
         }
      }
      if (found == 0) {
         Snackbar::Warn(
             ctx, L"Workspace %zu assigned to monitor %d, which does not exist",
             workspace_cfg->id, workspace_cfg->assigned_monitor);
      }
   }
}

inline auto parse_workspace_config(LuaConfig *config, struct BFWMContext *ctx)
    -> int {
   lua_State *lua_state = config->L;
   ctx->config.workspace_config_count = 0;

   lua_getglobal(lua_state, "BFWM");
   if (!lua_istable(lua_state, -1)) {
      lua_pop(lua_state, 1);
      return 0;
   }

   lua_getfield(lua_state, -1, "workspaces");
   if (!lua_istable(lua_state, -1)) {
      lua_pop(lua_state, 2);
      return 0;
   }

   int const ws_table_idx = lua_gettop(lua_state);
   auto count = (size_t)lua_rawlen(lua_state, ws_table_idx);

   for (size_t i = 1; i <= count; i++) {
      lua_rawgeti(lua_state, ws_table_idx, (lua_Integer)i);
      if (!lua_istable(lua_state, -1)) {
         Snackbar::Warn(ctx, L"Skipping non-table entry in workspaces array");
         lua_pop(lua_state, 1);
         continue;
      }

      int const entry_idx = lua_gettop(lua_state);
      auto entry_len = (size_t)lua_rawlen(lua_state, entry_idx);
      if (entry_len < 1) {
         lua_pop(lua_state, 1);
         continue;
      }

      lua_rawgeti(lua_state, entry_idx, 1);
      const char *id_str = lua_tostring(lua_state, -1);
      if (id_str == nullptr) {
         lua_pop(lua_state, 2);
         continue;
      }

      WorkspaceConfig workspace_cfg = {};
      workspace_cfg.id = (size_t)atol(id_str);
      workspace_cfg.assigned_monitor = 0;
      workspace_cfg.layout = LAYOUT_NONE;
      workspace_cfg.has_layout = FALSE;

      if (entry_len >= 2) {
         lua_rawgeti(lua_state, entry_idx, 2);
         if (lua_istable(lua_state, -1)) {
            parse_workspace_options(&workspace_cfg, lua_state,
                                    lua_gettop(lua_state), id_str);
         }
         lua_pop(lua_state, 1);
      }

      lua_pop(lua_state, 1);

      validate_workspace_monitor(ctx, &workspace_cfg);

      bool duplicate = false;
      for (int j = 0; j < ctx->config.workspace_config_count; j++) {
         if (ctx->config.workspace_configs[j].id == workspace_cfg.id) {
            Snackbar::Warn(ctx,
                           L"Duplicate workspace config for ID %zu, "
                           L"keeping first definition",
                           workspace_cfg.id);
            duplicate = true;
            break;
         }
      }
      if (duplicate)
         continue;

      ctx->config.workspace_configs[ctx->config.workspace_config_count++] =
          workspace_cfg;
   }

   lua_pop(lua_state, 2);

   Info("Loaded %d workspace configs", ctx->config.workspace_config_count);
   return ctx->config.workspace_config_count;
}

/* Parse the BFWM.keybinds table and insert into the dictionary */
inline auto parse_keybinds(LuaConfig *config, struct BFWMContext *ctx,
                           KeybindDictionary *dict) -> int {
   lua_State *lua_state = config->L;

   lua_getglobal(lua_state, "BFWM");
   if (!lua_istable(lua_state, -1)) {
      Debug("No 'BFWM' table — keeping default keybinds");
      lua_pop(lua_state, 1);
      return 0;
   }

   lua_getfield(lua_state, -1, "keybinds");
   if (!lua_istable(lua_state, -1)) {
      Debug("No 'BFWM.keybinds' table — keeping default keybinds");
      lua_pop(lua_state, 2);
      return 0;
   }

   size_t const entry_count = lua_rawlen(lua_state, -1);
   if (entry_count == 0) {
      Debug("Empty 'BFWM.keybinds' — clearing all keybinds");
      KBDictClear(*dict);
      lua_pop(lua_state, 2);
      return 0;
   }

   /* Config file defines keybinds — replace defaults */
   KBDictClear(*dict);

   int const keybinds_idx = lua_gettop(lua_state);
   int count = 0;

   /* Iterate over the keybinds array */
   lua_pushnil(lua_state);
   while (lua_next(lua_state, keybinds_idx) != 0) {
      /* Stack: ..., key, value  (value is a bind entry { key_str, action_str,
       * opts_table }) */
      if (!lua_istable(lua_state, -1)) {
         Snackbar::Warn(ctx, L"Skipping non-table entry in keybinds array");
         lua_pop(lua_state, 1); /* pop value */
         continue;
      }

      int const entry_idx = lua_gettop(lua_state);

      /* Entry[1] = key string (e.g. "Super Shift H") */
      lua_rawgeti(lua_state, entry_idx, 1);
      const char *key_str = lua_tostring(lua_state, -1);

      /* Entry[2] = action name string (e.g. "KillActive") */
      lua_rawgeti(lua_state, entry_idx, 2);
      const char *action_str = lua_tostring(lua_state, -1);

      /* Entry[3] = options table (may be empty) */
      lua_rawgeti(lua_state, entry_idx, 3);

      if ((key_str == nullptr) || (action_str == nullptr)) {
         Snackbar::Warn(
             ctx,
             L"Skipping malformed keybind entry (need at least 2 elements)");
         lua_pop(lua_state, 4);
         continue;
      }

      /* Parse key string -> ModifierState + key name */
      std::string key_name;
      ModifierState mods;
      {
         std::array<char, KEY_NAME_BUF_SIZE> key_name_buf = {};
         parse_key_string(key_str, &mods, key_name_buf.data(),
                          key_name_buf.size());
         key_name.assign(key_name_buf.data());
      }

      if (key_name.empty()) {
         Snackbar::Warn(ctx, L"Could not parse key name from '%hs'", key_str);
         lua_pop(lua_state, 4);
         continue;
      }

      DWORD const virtual_key = key_name_to_vk(key_name.c_str());
      if (virtual_key == 0) {
         Snackbar::Warn(ctx, L"Unknown key '%hs' in bind '%hs'",
                        key_name.c_str(), key_str);
         lua_pop(lua_state, 4);
         continue;
      }

      /* Build the Keystroke */
      Keystroke keystroke;
      memset(&keystroke, 0, sizeof(keystroke));
      keystroke.key.vkCode = virtual_key;
      keystroke.modifiers = mods;

      /* Build the action */
      int const opts_idx = lua_gettop(lua_state); /* options table index */
      BFWMAction *action = build_action(config, ctx, action_str, opts_idx);

      if (action != nullptr) {
         KBDictInsert(*dict, keystroke, std::unique_ptr<BFWMAction>(action));
         count++;
      }

      lua_pop(lua_state, 4); /* pop value + 3 rawget results */
   }

   lua_pop(lua_state, 2); /* pop keybinds table, BFWM table */

   Info("Loaded %d keybinds from config.lua", count);
   return count;
}

// ---------------------------------------------------------------------------
// Settings parser (reads BFWM.{gap_between, gap_edge, border_color, ...})
// ---------------------------------------------------------------------------

inline auto parse_layout_name(const char *name) -> LayoutType {
   if (strcmpi_portable(name, "dwindle") == 0)
      return DWINDLE;
   if (strcmpi_portable(name, "monocle") == 0)
      return MONOCLE;
   if (strcmpi_portable(name, "master") == 0)
      return MASTER;
   return LAYOUT_NONE;
}

const std::array<std::string, 16> known_BFWM_keys = {"gap_between",
                                                     "gap_edge",
                                                     "border_color",
                                                     "inactive_border",
                                                     "border_width",
                                                     "border_radius",
                                                     "focus_follows_mouse",
                                                     "mouse_follows_focus",
                                                     "unlock_window_resize",
                                                     "unlock_window_move",
                                                     "keybinds",
                                                     "window_rules",
                                                     "layout",
                                                     "workspaces",
                                                     "disabled_monitors",
                                                     "notify"};

inline auto BFWM_newindex(lua_State *lua_state) -> int {
   auto *ctx =
       (struct BFWMContext *)lua_touserdata(lua_state, lua_upvalueindex(1));
   if (lua_type(lua_state, -2) != LUA_TSTRING) {
      Snackbar::Warn(ctx, L"BFWM keys must be strings");
      return luaL_error(lua_state, "BFWM keys must be strings");
   }
   const char *key = lua_tostring(lua_state, -2);
   for (const auto &known_BFWM_key : known_BFWM_keys) {
      if (known_BFWM_key == key) {
         lua_rawset(lua_state, -3);
         return 0;
      }
   }
   Snackbar::Warn(ctx, L"Unknown config option 'BFWM.%hs'",
                  (key != nullptr) ? key : "(null)");
   return luaL_error(lua_state, "Unknown config option 'BFWM.%s'", key);
}

// ---------------------------------------------------------------------------
// Bar table metatable — catches typos in config.lua's Bar.* options
// ---------------------------------------------------------------------------

const std::array<std::string, 9> known_bar_keys = {
    "height", "font",    "colors", "indicators",   "enabled",
    "margin", "padding", "border", "corner_radius"};
const std::array<std::string, 5> known_colors_keys = {
    "background", "text", "active_workspace", "inactive_workspace",
    "tab_border"};
const std::array<std::string, 3> known_font_keys = {"size", "name", "weight"};
const std::array<std::string, 16> known_indicator_keys = {"type",
                                                          "align",
                                                          "max_width",
                                                          "format",
                                                          "show_position_bar",
                                                          "icons",
                                                          "icon_muted",
                                                          "icon_disconnected",
                                                          "icon_ethernet",
                                                          "format_ethernet",
                                                          "format_wifi",
                                                          "size",
                                                          "poll_rate",
                                                          "color",
                                                          "color_disconnected",
                                                          "color_muted"};

const std::array<std::string, 4> known_margin_padding_keys = {"top", "right",
                                                              "bottom", "left"};
const std::array<std::string, 2> known_border_keys = {"width", "color"};

inline auto colors_newindex(lua_State *lua_state) -> int {
   auto *ctx =
       (struct BFWMContext *)lua_touserdata(lua_state, lua_upvalueindex(1));
   if (lua_type(lua_state, -2) != LUA_TSTRING) {
      Snackbar::Warn(ctx, L"Bar.colors keys must be strings");
      return luaL_error(lua_state, "Bar.colors keys must be strings");
   }
   const char *key = lua_tostring(lua_state, -2);
   for (const auto &known_color_key : known_colors_keys) {
      if (known_color_key == key) {
         lua_rawset(lua_state, -3);
         return 0;
      }
   }
   Snackbar::Warn(ctx, L"Unknown Bar.colors option '%hs'",
                  (key != nullptr) ? key : "(null)");
   return luaL_error(lua_state, "Unknown Bar.colors option '%s'", key);
}

inline auto font_newindex(lua_State *lua_state) -> int {
   auto *ctx =
       (struct BFWMContext *)lua_touserdata(lua_state, lua_upvalueindex(1));
   if (lua_type(lua_state, -2) != LUA_TSTRING) {
      Snackbar::Warn(ctx, L"Bar.font keys must be strings");
      return luaL_error(lua_state, "Bar.font keys must be strings");
   }
   const char *key = lua_tostring(lua_state, -2);
   for (const auto &known_font_key : known_font_keys) {
      if (known_font_key == key) {
         lua_rawset(lua_state, -3);
         return 0;
      }
   }
   Snackbar::Warn(ctx, L"Unknown Bar.font option '%hs'",
                  (key != nullptr) ? key : "(null)");
   return luaL_error(lua_state, "Unknown Bar.font option '%s'", key);
}

inline auto margin_padding_newindex(lua_State *lua_state) -> int {
   auto *ctx =
       (struct BFWMContext *)lua_touserdata(lua_state, lua_upvalueindex(1));
   const char *table_name =
       (const char *)lua_touserdata(lua_state, lua_upvalueindex(2));
   if (lua_type(lua_state, -2) != LUA_TSTRING) {
      Snackbar::Warn(ctx, L"Bar.%hs keys must be strings", table_name);
      return luaL_error(lua_state, "Bar.%s keys must be strings", table_name);
   }
   const char *key = lua_tostring(lua_state, -2);
   for (const auto &known_margin_padding_key : known_margin_padding_keys) {
      if (known_margin_padding_key == key) {
         lua_rawset(lua_state, -3);
         return 0;
      }
   }
   Snackbar::Warn(ctx, L"Unknown Bar.%hs option '%hs'", table_name,
                  (key != nullptr) ? key : "(null)");
   return luaL_error(lua_state, "Unknown Bar.%s option '%s'", table_name, key);
}

inline auto border_newindex(lua_State *lua_state) -> int {
   auto *ctx =
       (struct BFWMContext *)lua_touserdata(lua_state, lua_upvalueindex(1));
   const char *table_name =
       (const char *)lua_touserdata(lua_state, lua_upvalueindex(2));
   if (lua_type(lua_state, -2) != LUA_TSTRING) {
      Snackbar::Warn(ctx, L"Bar.%hs keys must be strings", table_name);
      return luaL_error(lua_state, "Bar.%s keys must be strings", table_name);
   }
   const char *key = lua_tostring(lua_state, -2);
   for (const auto &known_border_key : known_border_keys) {
      if (known_border_key == key) {
         lua_rawset(lua_state, -3);
         return 0;
      }
   }
   Snackbar::Warn(ctx, L"Unknown Bar.%hs option '%hs'", table_name,
                  (key != nullptr) ? key : "(null)");
   return luaL_error(lua_state, "Unknown Bar.%s option '%s'", table_name, key);
}

inline auto handle_bar_colors_newindex(lua_State *lua_state,
                                       struct BFWMContext *ctx) -> int {
   lua_pushnil(lua_state);
   while (lua_next(lua_state, -2) != 0) {
      const char *field = lua_tostring(lua_state, -2);
      if (field != nullptr) {
         bool found = false;
         for (const auto &known_colors_key : known_colors_keys) {
            if (known_colors_key == field) {
               found = true;
               break;
            }
         }
         if (!found) {
            Snackbar::Warn(ctx, L"Unknown Bar.colors option '%hs'", field);
            return luaL_error(lua_state, "Unknown Bar.colors option '%s'",
                              field);
         }
      }
      lua_pop(lua_state, 1);
   }
   lua_newtable(lua_state);
   lua_pushlightuserdata(lua_state, ctx);
   lua_pushcclosure(lua_state, colors_newindex, 1);
   lua_setfield(lua_state, -2, "__newindex");
   lua_setmetatable(lua_state, -2);
   lua_rawset(lua_state, -3);
   return 0;
}

inline auto handle_bar_font_newindex(lua_State *lua_state,
                                     struct BFWMContext *ctx) -> int {
   lua_pushnil(lua_state);
   while (lua_next(lua_state, -2) != 0) {
      const char *field = lua_tostring(lua_state, -2);
      if (field != nullptr) {
         bool found = false;
         for (const auto &known_font_key : known_font_keys) {
            if (known_font_key == field) {
               found = true;
               break;
            }
         }
         if (!found) {
            Snackbar::Warn(ctx, L"Unknown Bar.font option '%hs'", field);
            return luaL_error(lua_state, "Unknown Bar.font option '%s'", field);
         }
      }
      lua_pop(lua_state, 1);
   }
   lua_newtable(lua_state);
   lua_pushlightuserdata(lua_state, ctx);
   lua_pushcclosure(lua_state, font_newindex, 1);
   lua_setfield(lua_state, -2, "__newindex");
   lua_setmetatable(lua_state, -2);
   lua_rawset(lua_state, -3);
   return 0;
}

inline auto handle_bar_margin_newindex(lua_State *lua_state,
                                       struct BFWMContext *ctx,
                                       const char *table_name) -> int {
   lua_newtable(lua_state);
   lua_pushlightuserdata(lua_state, ctx);
   lua_pushlightuserdata(lua_state, (void *)table_name);
   lua_pushcclosure(lua_state, margin_padding_newindex, 2);
   lua_setfield(lua_state, -2, "__newindex");
   lua_setmetatable(lua_state, -2);
   lua_rawset(lua_state, -3);
   return 0;
}

inline auto handle_bar_padding_newindex(lua_State *lua_state,
                                        struct BFWMContext *ctx,
                                        const char *table_name) -> int {
   lua_newtable(lua_state);
   lua_pushlightuserdata(lua_state, ctx);
   lua_pushlightuserdata(lua_state, (void *)table_name);
   lua_pushcclosure(lua_state, margin_padding_newindex, 2);
   lua_setfield(lua_state, -2, "__newindex");
   lua_setmetatable(lua_state, -2);
   lua_rawset(lua_state, -3);
   return 0;
}

inline auto handle_bar_border_newindex(lua_State *lua_state,
                                       struct BFWMContext *ctx,
                                       const char *table_name) -> int {
   lua_newtable(lua_state);
   lua_pushlightuserdata(lua_state, ctx);
   lua_pushlightuserdata(lua_state, (void *)table_name);
   lua_pushcclosure(lua_state, border_newindex, 2);
   lua_setfield(lua_state, -2, "__newindex");
   lua_setmetatable(lua_state, -2);
   lua_rawset(lua_state, -3);
   return 0;
}

inline auto bar_newindex(lua_State *lua_state) -> int {
   auto *ctx =
       (struct BFWMContext *)lua_touserdata(lua_state, lua_upvalueindex(1));
   if (lua_type(lua_state, -2) != LUA_TSTRING) {
      Snackbar::Warn(ctx, L"Bar keys must be strings");
      return luaL_error(lua_state, "Bar keys must be strings");
   }
   const char *key = lua_tostring(lua_state, -2);

   if (strcmp(key, "colors") == 0 && lua_istable(lua_state, -1))
      return handle_bar_colors_newindex(lua_state, ctx);

   if (strcmp(key, "font") == 0 && lua_istable(lua_state, -1))
      return handle_bar_font_newindex(lua_state, ctx);

   if (strcmp(key, "margin") == 0 && lua_istable(lua_state, -1))
      return handle_bar_margin_newindex(lua_state, ctx, "margin");

   if (strcmp(key, "padding") == 0 && lua_istable(lua_state, -1))
      return handle_bar_padding_newindex(lua_state, ctx, "padding");

   if (strcmp(key, "border") == 0 && lua_istable(lua_state, -1))
      return handle_bar_border_newindex(lua_state, ctx, "border");

   for (const auto &known_bar_key : known_bar_keys) {
      if (known_bar_key == key) {
         lua_rawset(lua_state, -3);
         return 0;
      }
   }
   Snackbar::Warn(ctx, L"Unknown Bar option '%hs'", key);
   return luaL_error(lua_state, "Unknown Bar option '%s'", key);
}

// ---------------------------------------------------------------------------
// Snackbar table metatable — catches typos in config.lua's Snackbar.* options
// ---------------------------------------------------------------------------

const std::array<std::string, 28> known_snackbar_keys = {
    "enabled",        "log_level",      "position",
    "margin_left",    "margin_right",   "margin_top",
    "margin_bottom",  "min_width",      "max_width",
    "padding_left",   "padding_right",  "padding_top",
    "padding_bottom", "background",     "text",
    "divider",        "divider_height", "font_size",
    "font_name",      "corner_radius",  "opacity",
    "close_on_click", "pause_on_hover", "click_to_expand",
    "max_queue",      "monitor",        "display_duration_ms",
    "colors"};

inline auto snackbar_newindex(lua_State *lua_state) -> int {
   auto *ctx =
       (struct BFWMContext *)lua_touserdata(lua_state, lua_upvalueindex(1));
   if (lua_type(lua_state, -2) != LUA_TSTRING) {
      Snackbar::Warn(ctx, L"Snackbar keys must be strings");
      return luaL_error(lua_state, "Snackbar keys must be strings");
   }
   const char *key = lua_tostring(lua_state, -2);
   for (const auto &known_snackbar_key : known_snackbar_keys) {
      if (known_snackbar_key == key) {
         lua_rawset(lua_state, -3);
         return 0;
      }
   }
   Snackbar::Warn(ctx, L"Unknown Snackbar option '%hs'",
                  (key != nullptr) ? key : "(null)");
   return luaL_error(lua_state, "Unknown Snackbar option '%s'", key);
}

inline void parse_int_field(lua_State *lua_state, struct BFWMContext *ctx,
                            const char *key, int *out) {
   lua_getfield(lua_state, -1, key);
   if (lua_isinteger(lua_state, -1) != 0) {
      *out = (int)lua_tointeger(lua_state, -1);
   } else if (lua_type(lua_state, -1) != LUA_TNIL) {
      Snackbar::Warn(ctx, L"Expected '%hs' to be an integer", key);
   }
   lua_pop(lua_state, 1);
}

inline void parse_bool_field(lua_State *lua_state, struct BFWMContext *ctx,
                             const char *key, BOOL *out) {
   lua_getfield(lua_state, -1, key);
   if (lua_isboolean(lua_state, -1)) {
      *out = (lua_toboolean(lua_state, -1) != 0) ? TRUE : FALSE;
   } else if (lua_type(lua_state, -1) != LUA_TNIL) {
      Snackbar::Warn(ctx, L"Expected '%hs' to be a boolean", key);
   }
   lua_pop(lua_state, 1);
}

inline void parse_string_field(lua_State *lua_state, struct BFWMContext *ctx,
                               const char *key, std::string &out) {
   lua_getfield(lua_state, -1, key);
   if (lua_isstring(lua_state, -1) != 0) {
      const char *s = lua_tostring(lua_state, -1);
      if (s != nullptr)
         out = s;
   } else if (lua_type(lua_state, -1) != LUA_TNIL) {
      Snackbar::Warn(ctx, L"Expected '%hs' to be a string", key);
   }
   lua_pop(lua_state, 1);
}

inline void parse_color_field(lua_State *lua_state, struct BFWMContext *ctx,
                              const char *key, COLORREF *out) {
   lua_getfield(lua_state, -1, key);
   if (lua_isstring(lua_state, -1) != 0) {
      const char *s = lua_tostring(lua_state, -1);
      if ((s != nullptr) && s[0] == '#') {
         unsigned long const hex = strtoul(s + 1, nullptr, STRTOUL_BASE_HEX);
         *out = RGB((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
      } else {
         Snackbar::Warn(ctx, L"Color value for '%hs' should start with '#'",
                        key);
      }
   } else if (lua_type(lua_state, -1) != LUA_TNIL) {
      Snackbar::Warn(ctx, L"Expected '%hs' to be a color string", key);
   }
   lua_pop(lua_state, 1);
}

inline void validate_indicator_keys(lua_State *lua_state, int i) {
   lua_pushnil(lua_state);
   while (lua_next(lua_state, -2) != 0) {
      const char *field = lua_tostring(lua_state, -2);
      if (field != nullptr) {
         bool found = false;
         for (const auto &known_indicator_key : known_indicator_keys) {
            if (known_indicator_key == field) {
               found = true;
               break;
            }
         }
         if (!found)
            Warn("Unknown key '%s' in Bar.indicators[%d]", field, i);
      }
      lua_pop(lua_state, 1);
   }
}

inline void parse_indicator_type(lua_State *lua_state,
                                 BarIndicatorConfig *indicator_cfg, int i) {
   lua_getfield(lua_state, -1, "type");
   const char *type = lua_tostring(lua_state, -1);
   if (type != nullptr) {
      if (strcmp(type, "workspaces") == 0) {
         indicator_cfg->type = BAR_INDICATOR_WORKSPACES;
      } else if (strcmp(type, "title") == 0) {
         indicator_cfg->type = BAR_INDICATOR_TITLE;
      } else if (strcmp(type, "clock") == 0) {
         indicator_cfg->type = BAR_INDICATOR_CLOCK;
      } else if (strcmp(type, "volume") == 0) {
         indicator_cfg->type = BAR_INDICATOR_VOLUME;
      } else if (strcmp(type, "network") == 0) {
         indicator_cfg->type = BAR_INDICATOR_NETWORK;
      } else if (strcmp(type, "cpu") == 0) {
         indicator_cfg->type = BAR_INDICATOR_CPU;
      } else if (strcmp(type, "memory") == 0) {
         indicator_cfg->type = BAR_INDICATOR_MEMORY;
      } else {
         Warn("Unknown indicator type '%s' in Bar.indicators[%d]", type, i);
      }
   }
   lua_pop(lua_state, 1);
}

inline void parse_indicator_align(lua_State *lua_state,
                                  BarIndicatorConfig *indicator_cfg) {
   lua_getfield(lua_state, -1, "align");
   const char *align = lua_tostring(lua_state, -1);
   if (align != nullptr) {
      if (strcmp(align, "left") == 0) {
         indicator_cfg->align = BAR_ALIGN_LEFT;
      } else if (strcmp(align, "right") == 0) {
         indicator_cfg->align = BAR_ALIGN_RIGHT;
      } else if (strcmp(align, "center") == 0) {
         indicator_cfg->align = BAR_ALIGN_CENTER;
      } else {
         Warn("Unknown indicator align '%s', defaulting to LEFT", align);
         indicator_cfg->align = BAR_ALIGN_LEFT;
      }
   }
   lua_pop(lua_state, 1);
}

inline void parse_indicator_icons(lua_State *lua_state,
                                  BarIndicatorConfig *indicator_cfg) {
   lua_getfield(lua_state, -1, "icons");
   if (lua_istable(lua_state, -1)) {
      int n = (int)lua_rawlen(lua_state, -1);
      n = std::min<int>(n, BAR_INDICATOR_ICON_MAX);
      for (int j = 0; j < n; j++) {
         lua_rawgeti(lua_state, -1, j + 1);
         if (lua_isstring(lua_state, -1) != 0) {
            const char *s = lua_tostring(lua_state, -1);
            if (s != nullptr)
               indicator_cfg->icons[j] = s;
            indicator_cfg->icon_count++;
         }
         lua_pop(lua_state, 1);
      }
   }
   lua_pop(lua_state, 1);
}

inline auto parse_single_indicator(lua_State *lua_state,
                                   struct BFWMContext *ctx,
                                   BarIndicatorConfig *indicator_cfg, int i)
    -> BOOL {
   *indicator_cfg = BarIndicatorConfig{};

   validate_indicator_keys(lua_state, i);
   parse_indicator_type(lua_state, indicator_cfg, i);
   parse_indicator_align(lua_state, indicator_cfg);
   parse_int_field(lua_state, ctx, "max_width", &indicator_cfg->max_width);
   parse_string_field(lua_state, ctx, "format", indicator_cfg->format);
   lua_getfield(lua_state, -1, "show_position_bar");
   if (lua_isboolean(lua_state, -1)) {
      indicator_cfg->show_position_bar =
          (static_cast<int>(lua_toboolean(lua_state, -1) != 0) != 0);
   } else if (lua_type(lua_state, -1) != LUA_TNIL) {
      Snackbar::Warn(ctx, L"Expected 'show_position_bar' to be a boolean");
   }
   lua_pop(lua_state, 1);
   parse_int_field(lua_state, ctx, "size", &indicator_cfg->font_size);
   parse_int_field(lua_state, ctx, "poll_rate", &indicator_cfg->poll_rate_ms);
   parse_indicator_icons(lua_state, indicator_cfg);
   parse_string_field(lua_state, ctx, "icon_muted", indicator_cfg->icon_muted);
   parse_string_field(lua_state, ctx, "icon_disconnected",
                      indicator_cfg->icon_disconnected);
   parse_string_field(lua_state, ctx, "icon_ethernet",
                      indicator_cfg->icon_ethernet);
   parse_string_field(lua_state, ctx, "format_ethernet",
                      indicator_cfg->format_ethernet);
   parse_string_field(lua_state, ctx, "format_wifi",
                      indicator_cfg->format_wifi);
   parse_color_field(lua_state, ctx, "color", &indicator_cfg->color);
   parse_color_field(lua_state, ctx, "color_disconnected",
                     &indicator_cfg->color_disconnected);
   parse_color_field(lua_state, ctx, "color_muted",
                     &indicator_cfg->color_muted);
   return TRUE;
}

inline void parse_bar_font_config(lua_State *lua_state, BarConfig *cfg) {
   lua_getfield(lua_state, -1, "size");
   if (lua_isinteger(lua_state, -1) != 0) {
      cfg->font.size = (int)lua_tointeger(lua_state, -1);
   } else if (lua_type(lua_state, -1) != LUA_TNIL) {
      Warn("Bar.font.size should be an integer, using default (%d)",
           cfg->font.size);
   }
   lua_pop(lua_state, 1);
   lua_getfield(lua_state, -1, "name");
   if (lua_isstring(lua_state, -1) != 0) {
      const char *s = lua_tostring(lua_state, -1);
      if (s != nullptr)
         cfg->font.name = s;
   } else if (lua_type(lua_state, -1) != LUA_TNIL) {
      Warn("Bar.font.name should be a string, using default (%s)",
           cfg->font.name.c_str());
   }
   lua_pop(lua_state, 1);
   lua_getfield(lua_state, -1, "weight");
   if (lua_isinteger(lua_state, -1) != 0) {
      cfg->font.weight = (int)lua_tointeger(lua_state, -1);
   } else if (lua_type(lua_state, -1) != LUA_TNIL) {
      Warn("Bar.font.weight should be an integer, using default (%d)",
           cfg->font.weight);
   }
   lua_pop(lua_state, 1);
}

inline void parse_bar_colors_config(lua_State *lua_state,
                                    struct BFWMContext *ctx, BarConfig *cfg) {
   parse_color_field(lua_state, ctx, "background", &cfg->colors.background);
   parse_color_field(lua_state, ctx, "text", &cfg->colors.text);
   parse_color_field(lua_state, ctx, "active_workspace",
                     &cfg->colors.active_workspace);
   parse_color_field(lua_state, ctx, "inactive_workspace",
                     &cfg->colors.inactive_workspace);
   parse_color_field(lua_state, ctx, "tab_border", &cfg->colors.tab_border);
}

inline void parse_bar_config(lua_State *lua_state, struct BFWMContext *ctx) {
   BarConfig *cfg = &ctx->config.bar_cfg;

   lua_getglobal(lua_state, "Bar");
   if (!lua_istable(lua_state, -1)) {
      lua_pop(lua_state, 1);
      return;
   }

   lua_getfield(lua_state, -1, "enabled");
   if (lua_isboolean(lua_state, -1)) {
      cfg->enabled =
          (((lua_toboolean(lua_state, -1) != 0) ? TRUE : FALSE) != 0);
   } else if (lua_type(lua_state, -1) != LUA_TNIL) {
      Snackbar::Warn(ctx,
                     L"Bar.enabled should be a boolean, using default (%s)",
                     cfg->enabled ? L"true" : L"false");
   }
   lua_pop(lua_state, 1);

   lua_getfield(lua_state, -1, "height");
   if (lua_isinteger(lua_state, -1) != 0) {
      cfg->height = (int)lua_tointeger(lua_state, -1);
   } else if (lua_type(lua_state, -1) != LUA_TNIL) {
      Snackbar::Warn(ctx,
                     L"Bar.height should be an integer, using default (%d)",
                     cfg->height);
   }
   lua_pop(lua_state, 1);

   lua_getfield(lua_state, -1, "font");
   if (lua_istable(lua_state, -1))
      parse_bar_font_config(lua_state, cfg);
   lua_pop(lua_state, 1);

   lua_getfield(lua_state, -1, "colors");
   if (lua_istable(lua_state, -1))
      parse_bar_colors_config(lua_state, ctx, cfg);
   lua_pop(lua_state, 1);

   lua_getfield(lua_state, -1, "corner_radius");
   if (lua_isinteger(lua_state, -1) != 0) {
      cfg->corner_radius = (int)lua_tointeger(lua_state, -1);
   } else if (lua_type(lua_state, -1) != LUA_TNIL) {
      Snackbar::Warn(
          ctx, L"Bar.corner_radius should be an integer, using default (%d)",
          cfg->corner_radius);
   }
   lua_pop(lua_state, 1);

   lua_getfield(lua_state, -1, "margin");
   if (lua_istable(lua_state, -1)) {
      parse_int_field(lua_state, ctx, "top", &cfg->margin.top);
      parse_int_field(lua_state, ctx, "right", &cfg->margin.right);
      parse_int_field(lua_state, ctx, "bottom", &cfg->margin.bottom);
      parse_int_field(lua_state, ctx, "left", &cfg->margin.left);
   }
   lua_pop(lua_state, 1);

   lua_getfield(lua_state, -1, "padding");
   if (lua_istable(lua_state, -1)) {
      parse_int_field(lua_state, ctx, "top", &cfg->padding.top);
      parse_int_field(lua_state, ctx, "right", &cfg->padding.right);
      parse_int_field(lua_state, ctx, "bottom", &cfg->padding.bottom);
      parse_int_field(lua_state, ctx, "left", &cfg->padding.left);
   }
   lua_pop(lua_state, 1);

   lua_getfield(lua_state, -1, "border");
   if (lua_istable(lua_state, -1)) {
      parse_int_field(lua_state, ctx, "width", &cfg->border.width);
      parse_color_field(lua_state, ctx, "color", &cfg->border.color);
   }
   lua_pop(lua_state, 1);

   cfg->indicator_count = 0;
   lua_getfield(lua_state, -1, "indicators");
   if (lua_istable(lua_state, -1)) {
      int const n = (int)lua_rawlen(lua_state, -1);
      for (int i = 1; i <= n && cfg->indicator_count < BAR_MAX_INDICATORS;
           i++) {
         lua_rawgeti(lua_state, -1, i);
         if (!lua_istable(lua_state, -1)) {
            lua_pop(lua_state, 1);
            continue;
         }

         if (parse_single_indicator(lua_state, ctx,
                                    &cfg->indicators[cfg->indicator_count],
                                    i) != 0) {
            cfg->indicator_count++;
         }
         lua_pop(lua_state, 1);
      }
   }
   lua_pop(lua_state, 1);

   lua_pop(lua_state, 1);
}

inline auto parse_modifier_string(const char *s) -> DWORD {
   if (strcmpi_portable(s, "LALT") == 0)
      return VK_LMENU;
   if (strcmpi_portable(s, "RALT") == 0)
      return VK_RMENU;
   if (strcmpi_portable(s, "ALT") == 0)
      return VK_MENU;
   if (strcmpi_portable(s, "LCTRL") == 0)
      return VK_LCONTROL;
   if (strcmpi_portable(s, "RCTRL") == 0)
      return VK_RCONTROL;
   if (strcmpi_portable(s, "CTRL") == 0 || strcmpi_portable(s, "CONTROL") == 0)
      return VK_CONTROL;
   if (strcmpi_portable(s, "LSHIFT") == 0)
      return VK_LSHIFT;
   if (strcmpi_portable(s, "RSHIFT") == 0)
      return VK_RSHIFT;
   if (strcmpi_portable(s, "SHIFT") == 0)
      return VK_SHIFT;
   Warn("Unknown modifier '%s', defaulting to unlocked", s);
   return 0;
}

inline void parse_settings(lua_State *lua_state, struct BFWMContext *ctx) {
   lua_getglobal(lua_state, "BFWM");
   if (!lua_istable(lua_state, -1)) {
      lua_pop(lua_state, 1);
      return;
   }

   parse_int_field(lua_state, ctx, "gap_between", &ctx->config.gap_between);
   parse_int_field(lua_state, ctx, "gap_edge", &ctx->config.gap_edge);
   parse_color_field(lua_state, ctx, "border_color", &ctx->config.border_color);
   parse_color_field(lua_state, ctx, "inactive_border",
                     &ctx->config.inactive_border);
   parse_int_field(lua_state, ctx, "border_width", &ctx->config.border_width);
   parse_int_field(lua_state, ctx, "border_radius", &ctx->config.border_radius);
   parse_bool_field(lua_state, ctx, "focus_follows_mouse",
                    &ctx->config.focus_follows_mouse);
   parse_bool_field(lua_state, ctx, "mouse_follows_focus",
                    &ctx->config.mouse_follows_focus);

   lua_getfield(lua_state, -1, "unlock_window_resize");
   if (lua_isstring(lua_state, -1) != 0) {
      const char *s = lua_tostring(lua_state, -1);
      if (s[0] != '\0')
         ctx->config.unlock_modifier = parse_modifier_string(s);
   } else if (!lua_isnil(lua_state, -1)) {
      Warn("'BFWM.unlock_window_resize' must be a string, got %s",
           lua_typename(lua_state, lua_type(lua_state, -1)));
   }
   lua_pop(lua_state, 1);

   lua_getfield(lua_state, -1, "unlock_window_move");
   if (lua_isstring(lua_state, -1) != 0) {
      const char *s = lua_tostring(lua_state, -1);
      if (s[0] != '\0')
         ctx->config.unlock_move_modifier = parse_modifier_string(s);
   } else if (!lua_isnil(lua_state, -1)) {
      Warn("'BFWM.unlock_window_move' must be a string, got %s",
           lua_typename(lua_state, lua_type(lua_state, -1)));
   }
   lua_pop(lua_state, 1);

   lua_getfield(lua_state, -1, "layout");
   if (lua_isstring(lua_state, -1) != 0) {
      const char *s = lua_tostring(lua_state, -1);
      LayoutType const layout_type = parse_layout_name(s);
      if (layout_type != LAYOUT_NONE) {
         ctx->config.default_layout = layout_type;
      } else {
         Warn("'BFWM.layout' references an unknown layout type: \"%s\"", s);
      }
   } else if (!lua_isnil(lua_state, -1)) {
      Warn("'BFWM.layout' must be a string, got %s",
           lua_typename(lua_state, lua_type(lua_state, -1)));
   }
   lua_pop(lua_state, 1);

   /* Parse disabled_monitors array */
   ctx->config.disabled_monitor_count = 0;
   lua_getfield(lua_state, -1, "disabled_monitors");
   if (lua_istable(lua_state, -1)) {
      auto n = (size_t)lua_rawlen(lua_state, -1);
      n = std::min<size_t>(n, MAX_DISABLED_MONITORS);
      for (size_t di = 0; di < n; di++) {
         lua_rawgeti(lua_state, -1, (lua_Integer)(di + 1));
         if (lua_isinteger(lua_state, -1) != 0) {
            int const val = (int)lua_tointeger(lua_state, -1);
            ctx->config
                .disabled_monitors[ctx->config.disabled_monitor_count++] = val;
         } else {
            Snackbar::Warn(ctx,
                           L"disabled_monitors entry should be an integer");
         }
         lua_pop(lua_state, 1);
      }
   }
   lua_pop(lua_state, 1);

   lua_pop(lua_state, 1); /* pop BFWM table */

   Debug("parse_settings: unlock_modifier=0x%lx unlock_move=0x%lx",
         ctx->config.unlock_modifier, ctx->config.unlock_move_modifier);
}

// ---------------------------------------------------------------------------
// Lua-callable BFWM.notify(text)
// ---------------------------------------------------------------------------

inline auto lua_BFWM_notify(lua_State *lua_state) -> int {
   struct BFWMContext *ctx = nullptr;
   /* Retrieve the context from the Lua registry or upvalue.
      Since we register this function after the BFWM table exists,
      we need the context pointer.  Use a lightuserdata upvalue. */
   ctx = (struct BFWMContext *)lua_touserdata(lua_state, lua_upvalueindex(1));
   if (ctx == nullptr)
      return 0;

   const char *text = luaL_optstring(lua_state, 1, NULL);
   if (text == nullptr)
      return 0;

   // Proper two-call conversion: query the required length first, then
   // resize the string to the true converted size.
   int const wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
   std::wstring wtext(wlen > 0 ? (size_t)wlen : 0, L'\0');
   if (wlen > 0)
      MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext.data(), wlen);
   Snackbar::Show(ctx, LogLevel::Info, &wtext, TRUE);
   return 0;
}

// ---------------------------------------------------------------------------
// Snackbar config parser
// ---------------------------------------------------------------------------

inline void parse_snackbar_config(lua_State *lua_state,
                                  struct BFWMContext *ctx) {
   SnackbarConfig *cfg = &ctx->config.snackbar;

   lua_getglobal(lua_state, "Snackbar");
   if (!lua_istable(lua_state, -1)) {
      lua_pop(lua_state, 1);
      return;
   }

   parse_bool_field(lua_state, ctx, "enabled", &cfg->enabled);

   lua_getfield(lua_state, -1, "log_level");
   if (lua_isstring(lua_state, -1) != 0) {
      const char *s = lua_tostring(lua_state, -1);
      if (strcmp(s, "none") == 0) {
         cfg->log_level = SNACKBAR_LOG_NONE;
      } else if (strcmp(s, "error") == 0) {
         cfg->log_level = SNACKBAR_LOG_ERROR;
      } else if (strcmp(s, "warn") == 0) {
         cfg->log_level = SNACKBAR_LOG_WARN;
      } else if (strcmp(s, "info") == 0) {
         cfg->log_level = SNACKBAR_LOG_INFO;
      } else if (strcmp(s, "debug") == 0) {
         cfg->log_level = SNACKBAR_LOG_DEBUG;
      } else {
         Snackbar::Warn(ctx, L"Unknown Snackbar.log_level '%hs'",
                        (s != nullptr) ? s : "(null)");
      }
   }
   lua_pop(lua_state, 1);

   lua_getfield(lua_state, -1, "position");
   if (lua_isstring(lua_state, -1) != 0) {
      const char *s = lua_tostring(lua_state, -1);
      if (strcmp(s, "bottom-right") == 0) {
         cfg->position = SNACKBAR_POSITION_BOTTOM_RIGHT;
      } else if (strcmp(s, "bottom-left") == 0) {
         cfg->position = SNACKBAR_POSITION_BOTTOM_LEFT;
      } else if (strcmp(s, "top-right") == 0) {
         cfg->position = SNACKBAR_POSITION_TOP_RIGHT;
      } else if (strcmp(s, "top-left") == 0) {
         cfg->position = SNACKBAR_POSITION_TOP_LEFT;
      } else {
         Snackbar::Warn(ctx, L"Unknown Snackbar.position '%hs'",
                        (s != nullptr) ? s : "(null)");
      }
   }
   lua_pop(lua_state, 1);

   parse_int_field(lua_state, ctx, "margin_left", &cfg->margin_left);
   parse_int_field(lua_state, ctx, "margin_right", &cfg->margin_right);
   parse_int_field(lua_state, ctx, "margin_top", &cfg->margin_top);
   parse_int_field(lua_state, ctx, "margin_bottom", &cfg->margin_bottom);
   parse_int_field(lua_state, ctx, "min_width", &cfg->min_width);
   parse_int_field(lua_state, ctx, "max_width", &cfg->max_width);
   parse_int_field(lua_state, ctx, "padding_left", &cfg->padding_left);
   parse_int_field(lua_state, ctx, "padding_right", &cfg->padding_right);
   parse_int_field(lua_state, ctx, "padding_top", &cfg->padding_top);
   parse_int_field(lua_state, ctx, "padding_bottom", &cfg->padding_bottom);
   parse_color_field(lua_state, ctx, "background", &cfg->background);
   parse_color_field(lua_state, ctx, "text", &cfg->text);
   parse_color_field(lua_state, ctx, "divider", &cfg->divider);
   parse_int_field(lua_state, ctx, "divider_height", &cfg->divider_height);
   parse_int_field(lua_state, ctx, "font_size", &cfg->font_size);
   parse_string_field(lua_state, ctx, "font_name", cfg->font_name);
   parse_int_field(lua_state, ctx, "corner_radius", &cfg->corner_radius);
   parse_int_field(lua_state, ctx, "opacity", &cfg->opacity);
   parse_bool_field(lua_state, ctx, "close_on_click", &cfg->close_on_click);
   parse_bool_field(lua_state, ctx, "pause_on_hover", &cfg->pause_on_hover);
   parse_bool_field(lua_state, ctx, "click_to_expand", &cfg->click_to_expand);
   parse_int_field(lua_state, ctx, "max_queue", &cfg->max_queue);
   parse_int_field(lua_state, ctx, "monitor", &cfg->monitor);
   parse_int_field(lua_state, ctx, "display_duration_ms",
                   &cfg->display_duration_ms);

   /* Parse colors sub-table */
   lua_getfield(lua_state, -1, "colors");
   if (lua_istable(lua_state, -1)) {
      parse_color_field(lua_state, ctx, "error", &cfg->colors.error);
      parse_color_field(lua_state, ctx, "warn", &cfg->colors.warn);
      parse_color_field(lua_state, ctx, "info", &cfg->colors.info);
      parse_color_field(lua_state, ctx, "debug", &cfg->colors.debug);
      parse_color_field(lua_state, ctx, "normal", &cfg->colors.normal);
   }
   lua_pop(lua_state, 1);

   cfg->opacity = std::max(cfg->opacity, 0);
   cfg->opacity = std::min<int>(cfg->opacity, OPACITY_MAX);
   cfg->max_queue = std::max(cfg->max_queue, 1);
   cfg->max_queue = std::min<int>(cfg->max_queue, MAX_SNACKBAR_QUEUE);

   if (cfg->min_width > 0 && cfg->max_width >= 1 &&
       cfg->min_width > cfg->max_width) {
      Snackbar::Warn(ctx,
                     L"Snackbar.min_width (%d) > Snackbar.max_width (%d); "
                     L"capping min_width",
                     cfg->min_width, cfg->max_width);
      cfg->min_width = cfg->max_width;
   }

   lua_pop(lua_state, 1); /* pop Snackbar table */
}

inline void
update_workspace_label(Workspace *workspace,
                       const struct WorkspaceConfig *workspace_cfg) {
   if ((workspace_cfg != nullptr) && !workspace_cfg->label.empty()) {
      workspace->SetLabel(workspace_cfg->label);
   } else {
      workspace->SetLabel("");
   }
}

inline void activate_another_workspace(struct BFWMContext *ctx, Monitor *mon,
                                       Workspace *workspace) {
   for (size_t j = 0; j < mon->Workspaces().size(); j++) {
      if (mon->Workspaces()[j] != workspace) {
         WorkspaceActivateSimple(ctx, mon, workspace, mon->Workspaces()[j],
                                 true);
         break;
      }
   }
}

inline auto find_workspace_index(Monitor *mon, Workspace *workspace) -> int {
   for (size_t j = 0; j < mon->Workspaces().size(); j++) {
      if (mon->Workspaces()[j] == workspace)
         return (int)j;
   }
   return -1;
}

inline void remove_workspace_from_source(Monitor *mon, int ws_idx) {
   mon->RemoveWorkspaceAt((size_t)ws_idx);
}

inline void add_workspace_to_destination(Monitor *dst_mon, Workspace *workspace,
                                         BOOL was_active,
                                         struct BFWMContext *ctx) {
   dst_mon->TrackWorkspace(workspace);
   workspace->RecalculateRect(dst_mon, ctx);
   if (dst_mon->GetActiveWorkspace() == nullptr)
      dst_mon->SetActiveWorkspace(workspace);
   if (was_active != 0)
      WorkspaceActivate(ctx, dst_mon, workspace->GetIdentifier());
}

inline void create_workspace_for_empty_monitor(Monitor *mon,
                                               struct BFWMContext *ctx) {
   if (!mon->Workspaces().empty())
      return;

   size_t const new_id = FindNextWorkspaceId(ctx);
   std::wstring const name = L"ws_" + std::to_wstring(new_id);
   auto *new_ws = CreateWorkspaceForMonitor(ctx, new_id, &name, mon);
   if (new_ws == nullptr)
      return;

   new_ws->ApplyConfig(ctx, new_id);
   mon->TrackWorkspace(new_ws);
   mon->SetActiveWorkspace(new_ws);
}

inline void
move_workspace_to_monitor(struct BFWMContext *ctx, Monitor *mon,
                          Workspace *workspace,
                          const struct WorkspaceConfig *workspace_cfg) {
   UINT const target_display = (UINT)workspace_cfg->assigned_monitor;
   if (mon->GetDisplayNumber() == target_display)
      return;

   Monitor *dst_mon = FindMonitorByDisplayNumber(ctx, target_display);
   if (dst_mon == nullptr) {
      Warn("Workspace %zu reassigned to monitor %d on reload, "
           "but that monitor does not exist — keeping on current "
           "monitor",
           workspace->GetIdentifier(), workspace_cfg->assigned_monitor);
      return;
   }

   InfoW(
       L"Moving workspace %zu from monitor %u to monitor %u per config reload",
       workspace->GetIdentifier(), mon->GetDisplayNumber(),
       dst_mon->GetDisplayNumber());

   BOOL const was_active =
       static_cast<BOOL>(mon->GetActiveWorkspace() == workspace);

   if (was_active != 0)
      activate_another_workspace(ctx, mon, workspace);

   int const ws_idx = find_workspace_index(mon, workspace);
   if (ws_idx < 0)
      return;

   remove_workspace_from_source(mon, ws_idx);
   add_workspace_to_destination(dst_mon, workspace, was_active, ctx);
   create_workspace_for_empty_monitor(mon, ctx);
}

inline void update_workspace_configs_on_reload(struct BFWMContext *ctx) {
   for (size_t m = 0; m < ctx->monitors->Size(); m++) {
      Monitor *mon = ctx->monitors->At(m);
      for (size_t w = 0; w < mon->Workspaces().size(); w++) {
         Workspace *workspace = mon->Workspaces()[w];
         const struct WorkspaceConfig *workspace_cfg =
             FindWorkspaceConfig(ctx, workspace->GetIdentifier());

         update_workspace_label(workspace, workspace_cfg);

         if ((workspace_cfg != nullptr) && workspace_cfg->assigned_monitor > 0)
            move_workspace_to_monitor(ctx, mon, workspace, workspace_cfg);
      }
   }
}
} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

auto LuaConfigLoad(LuaConfig *config, struct BFWMContext *ctx,
                   const char *config_path) -> bool {
   if (config->L != nullptr) {
      Debug("Lua state already exists, reusing");
   } else {
      config->L = luaL_newstate();
      if (config->L == nullptr) {
         Error("Could not create Lua state");
         return false;
      }
      luaL_openlibs(config->L);
   }

   if (config_path != nullptr) {
      config->config_path = config_path;
   }

   /* Create a metatable-backed BFWM global table.
       __newindex errors on unknown keys, so typos in config.lua
       are caught at load time instead of silently ignored. */
   lua_newtable(config->L);
   lua_newtable(config->L);
   lua_pushlightuserdata(config->L, ctx);
   lua_pushcclosure(config->L, BFWM_newindex, 1);
   lua_setfield(config->L, -2, "__newindex");
   lua_setmetatable(config->L, -2);
   lua_setglobal(config->L, "BFWM");

   lua_newtable(config->L);
   lua_newtable(config->L);
   lua_pushlightuserdata(config->L, ctx);
   lua_pushcclosure(config->L, bar_newindex, 1);
   lua_setfield(config->L, -2, "__newindex");
   lua_setmetatable(config->L, -2);

   lua_newtable(config->L);
   lua_newtable(config->L);
   lua_pushlightuserdata(config->L, ctx);
   lua_pushcclosure(config->L, colors_newindex, 1);
   lua_setfield(config->L, -2, "__newindex");
   lua_setmetatable(config->L, -2);
   lua_pushstring(config->L, "colors");
   lua_rawset(config->L, -3);

   lua_newtable(config->L);
   lua_newtable(config->L);
   lua_pushlightuserdata(config->L, ctx);
   lua_pushcclosure(config->L, font_newindex, 1);
   lua_setfield(config->L, -2, "__newindex");
   lua_setmetatable(config->L, -2);
   lua_pushstring(config->L, "font");
   lua_rawset(config->L, -3);

   lua_setglobal(config->L, "Bar");

   lua_newtable(config->L);
   lua_newtable(config->L);
   lua_pushlightuserdata(config->L, ctx);
   lua_pushcclosure(config->L, snackbar_newindex, 1);
   lua_setfield(config->L, -2, "__newindex");
   lua_setmetatable(config->L, -2);
   lua_setglobal(config->L, "Snackbar");

   /* Register BFWM.notify() before loading config so user code
      can call it from config.lua. */
   lua_getglobal(config->L, "BFWM");
   lua_pushlightuserdata(config->L, ctx);
   lua_pushcclosure(config->L, lua_BFWM_notify, 1);
   lua_setfield(config->L, -2, "notify");
   lua_pop(config->L, 1);

   /* Reset config to defaults before (re-)parsing.  Settings removed
      from config.lua on reload must revert to defaults instead of
      retaining stale values.  On first load this also ensures sane
      defaults even if the config file is missing or fails. */
   ConfigLoadDefaults(ctx);

   /* No config file found (empty path): the C++ defaults loaded above are
      the whole configuration.  Do not attempt to dofile an empty path,
      which would raise a spurious "Error loading config" snackbar, and do
      not run the parse_* pass (parse_bar_config resets indicator_count and
      rebuilds from the Lua state, which would wipe the default indicators). */
   if (config->config_path.empty()) {
      return true;
   }

   /* Load/run the config file */
   if (luaL_dofile(config->L, config->config_path.c_str()) != LUA_OK) {
      Snackbar::Error(ctx, L"Error loading config: %hs",
                      lua_tostring(config->L, -1));
      lua_pop(config->L, 1);
      return false;
   }

   /* Parse runtime settings (gap_between, gap_edge, colors, etc.) */
   parse_settings(config->L, ctx);

   /* Parse bar config (Bar.height, Bar.font, Bar.colors, etc.) */
   parse_bar_config(config->L, ctx);

   /* Parse snackbar config */
   parse_snackbar_config(config->L, ctx);

   /* Parse keybinds from the loaded config and insert into the dictionary.
      If the keybinds table is missing, ConfigLoadDefaults at line 1156
      already populated the dictionary with C defaults, so they survive. */
   parse_keybinds(config, ctx, &ctx->keybinds);

   /* Free existing window rules before (re-)parsing */
   for (WindowRule *rule : ctx->window_rules)
      WindowRuleDestroy(rule);
   ctx->window_rules.clear();
   ctx->window_rule_count = 0;

   /* Parse window rules */
   parse_window_rules(config, ctx);

   /* Parse workspace config (must be after monitors are set up) */
   parse_workspace_config(config, ctx);

   /* Update existing workspaces with new config (harmless on initial load) */
   update_workspace_configs_on_reload(ctx);

   return true;
}

auto LuaConfigReload(LuaConfig *config, struct BFWMContext *ctx) -> bool {
   return LuaConfigLoad(config, ctx, nullptr);
}

void LuaConfigFree(LuaConfig *config) {
   if (config->L != nullptr) {
      lua_close(config->L);
      config->L = nullptr;
   }
}

auto LuaConfigCall(LuaConfig *config, struct BFWMContext *ctx,
                   const char *func_name) -> bool {
   if (config->L == nullptr) {
      Snackbar::Error(ctx, L"Lua state not initialized, cannot call '%hs'",
                      (func_name != nullptr) ? func_name : "(null)");
      return false;
   }

   /* First try: look up a named global function */
   lua_getglobal(config->L, func_name);
   if (lua_isfunction(config->L, -1)) {
      if (lua_pcall(config->L, 0, 0, 0) != LUA_OK) {
         Snackbar::Error(ctx, L"Error calling Lua function '%hs': %hs",
                         (func_name != nullptr) ? func_name : "(null)",
                         lua_tostring(config->L, -1));
         lua_pop(config->L, 1);
         return false;
      }
      return true;
   }
   lua_pop(config->L, 1);

   if (luaL_loadstring(config->L, func_name) != LUA_OK) {
      Snackbar::Warn(
          ctx,
          L"Lua function '%hs' not found and failed to parse as inline code",
          (func_name != nullptr) ? func_name : "(null)");
      lua_pop(config->L, 1);
      return false;
   }

   if (lua_pcall(config->L, 0, 0, 0) != LUA_OK) {
      Snackbar::Error(ctx, L"Error running inline Lua '%hs': %hs",
                      (func_name != nullptr) ? func_name : "(null)",
                      lua_tostring(config->L, -1));
      lua_pop(config->L, 1);
      return false;
   }
   return true;
}
