#include "builder.h"
#include "../../core/bfwm_def.h"
#include "../../core/portable.h"
#include "../../input/keystroke.h"
#include "../../logging/logger.h"
#include "../../memory/safe.h"
#include "../../notification/snackbar.h"
#include "../action.h"
#include "parser.h"
#ifdef __cplusplus
extern "C" {
#endif
#include <lua.h>
#ifdef __cplusplus
}
#endif
#include <cstring>
#include <minwindef.h>
#include <string>
#include <windows.h>

enum {
   FKEY_BASE = 10,
   FKEY_MAX = 24,
   KEY_STR_BUF_SIZE = 256,
   DEFAULT_RESIZE_PIXELS = 50,
};

auto str_to_direction(const char *s) -> BFWMDirection {
   if (strcmpi_portable(s, "left") == 0)
      return DirLeft;
   if (strcmpi_portable(s, "right") == 0)
      return DirRight;
   if (strcmpi_portable(s, "up") == 0)
      return DirUp;
   if (strcmpi_portable(s, "down") == 0)
      return DirDown;
   if (strcmpi_portable(s, "next") == 0)
      return DirNext;
   if (strcmpi_portable(s, "prev") == 0)
      return DirPrev;
   Warn("Unknown direction '%s', defaulting to DirNext", s);
   return DirNext;
}

auto key_name_to_vk(const char *name) -> DWORD {
   if (strcmpi_portable(name, "Space") == 0)
      return VK_SPACE;
   if (strcmpi_portable(name, "Return") == 0 ||
       strcmpi_portable(name, "Enter") == 0)
      return VK_RETURN;
   if (strcmpi_portable(name, "Tab") == 0)
      return VK_TAB;
   if (strcmpi_portable(name, "Escape") == 0 ||
       strcmpi_portable(name, "Esc") == 0)
      return VK_ESCAPE;
   if (strcmpi_portable(name, "Backspace") == 0)
      return VK_BACK;
   if (strcmpi_portable(name, "Left") == 0)
      return VK_LEFT;
   if (strcmpi_portable(name, "Right") == 0)
      return VK_RIGHT;
   if (strcmpi_portable(name, "Up") == 0)
      return VK_UP;
   if (strcmpi_portable(name, "Down") == 0)
      return VK_DOWN;

   if (name[0] >= 'A' && name[0] <= 'Z' && name[1] == '\0')
      return (DWORD)name[0];
   if (name[0] >= '0' && name[0] <= '9' && name[1] == '\0')
      return (DWORD)name[0];

   if (name[0] == 'F' && name[1] >= '1' && name[1] <= '9') {
      int n = name[1] - '0';
      if (name[2] == '\0')
         return VK_F1 + (n - 1);
      if (name[2] >= '0' && name[2] <= '9' && name[3] == '\0') {
         n = (n * FKEY_BASE) + (name[2] - '0');
         if (n >= FKEY_BASE && n <= FKEY_MAX)
            return VK_F1 + (n - 1);
      }
   }

   Warn("Unknown key name '%s'", name);
   return 0;
}

void parse_key_string(const char *key_str, ModifierState *mods,
                      char *out_key_name, size_t out_key_name_sz) {
   memset(mods, 0, sizeof(ModifierState));
   out_key_name[0] = '\0';

   std::string buf;
   buf.resize(KEY_STR_BUF_SIZE);
   safe_strcpy(buf.data(), buf.size(), key_str);

   const char *delim = " \t";
   char *ctx = nullptr;
   char *token = ltokenize(buf.data(), delim, &ctx);

   while (token != nullptr) {
      if (strcmpi_portable(token, "Super") == 0 ||
          strcmpi_portable(token, "Win") == 0) {
         mods->super = TRUE;
      } else if (strcmpi_portable(token, "Ctrl") == 0 ||
                 strcmpi_portable(token, "Control") == 0) {
         mods->ctrl = TRUE;
      } else if (strcmpi_portable(token, "Shift") == 0) {
         mods->shift = TRUE;
      } else if (strcmpi_portable(token, "Alt") == 0) {
         mods->alt = TRUE;
      } else {
         safe_strcpy(out_key_name, out_key_name_sz, token);
      }
      token = ltokenize(nullptr, delim, &ctx);
   }
}

namespace {

inline auto table_get_string_field(lua_State *lua_state, int table_idx,
                                   const char *field) -> const char * {
   lua_getfield(lua_state, table_idx, field);
   const char *val = nullptr;
   if (lua_isstring(lua_state, -1) != 0)
      val = lua_tostring(lua_state, -1);
   lua_pop(lua_state, 1);
   return val;
}
} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto build_action(LuaConfig *config, struct BFWMContext *ctx,
                  const char *action_name, int opts_table_idx) -> BFWMAction * {
   lua_State *lua_state = config->L;
   BFWMAction *action = nullptr;

   do {
      if (strcmp(action_name, "MoveWindow") == 0) {
         const char *dir =
             table_get_string_field(lua_state, opts_table_idx, "direction");
         BFWMDirection const d =
             (dir != nullptr) ? str_to_direction(dir) : DirLeft;
         action = BFWMActionCreateMoveWindow(d, 0);
         break;
      }

      if (strcmp(action_name, "Spawn") == 0) {
         const char *cmd =
             table_get_string_field(lua_state, opts_table_idx, "command");
         if (cmd == nullptr) {
            Snackbar::Error(ctx, L"Spawn action requires a 'command' option");
            return nullptr;
         }
         action = BFWMActionCreateSpawn(cmd);
         break;
      }

      if (strcmp(action_name, "KillActive") == 0) {
         action = BFWMActionCreateKillActive();
         break;
      }

      if (strcmp(action_name, "Minimize") == 0) {
         action = BFWMActionCreateMinimize();
         break;
      }

      if (strcmp(action_name, "ToggleFloat") == 0) {
         action = BFWMActionCreateToggleFloat();
         break;
      }

      if (strcmp(action_name, "FocusWindow") == 0) {
         const char *dir =
             table_get_string_field(lua_state, opts_table_idx, "direction");
         BFWMDirection const d =
             (dir != nullptr) ? str_to_direction(dir) : DirNext;
         action = BFWMActionCreateFocus(d);
         break;
      }

      if (strcmp(action_name, "Workspace") == 0) {
         lua_getfield(lua_state, opts_table_idx, "index");
         int index = 1;
         if (lua_isinteger(lua_state, -1) != 0)
            index = (int)lua_tointeger(lua_state, -1);
         lua_pop(lua_state, 1);
         action = BFWMActionCreateWorkspace(index, DirNext);
         break;
      }

      if (strcmp(action_name, "MoveToWorkspace") == 0) {
         lua_getfield(lua_state, opts_table_idx, "index");
         int index = 1;
         if (lua_isinteger(lua_state, -1) != 0)
            index = (int)lua_tointeger(lua_state, -1);
         lua_pop(lua_state, 1);
         action = BFWMActionCreateMoveToWorkspace(index);
         break;
      }

      if (strcmp(action_name, "Fullscreen") == 0) {
         action = BFWMActionCreateFullscreen();
         break;
      }

      if (strcmp(action_name, "ToggleSplit") == 0) {
         action = BFWMActionCreateSplit();
         break;
      }

      if (strcmp(action_name, "SwapSplit") == 0) {
         action = BFWMActionCreateSwapSplit();
         break;
      }

      if (strcmp(action_name, "SetLayout") == 0) {
         const char *layout =
             table_get_string_field(lua_state, opts_table_idx, "layout");
         if (layout == nullptr) {
            Snackbar::Error(ctx,
                            L"SetLayout action requires a 'layout' option");
            return nullptr;
         }
         action = BFWMActionCreateLayout(layout);
         break;
      }

      if (strcmp(action_name, "ResizeWindow") == 0) {
         const char *dir =
             table_get_string_field(lua_state, opts_table_idx, "direction");
         BFWMDirection const d =
             (dir != nullptr) ? str_to_direction(dir) : DirLeft;
         lua_getfield(lua_state, opts_table_idx, "pixels");
         int pixels = DEFAULT_RESIZE_PIXELS;
         if (lua_isinteger(lua_state, -1) != 0)
            pixels = (int)lua_tointeger(lua_state, -1);
         lua_pop(lua_state, 1);
         action = BFWMActionCreateResizeWindow(d, pixels);
         break;
      }

      if (strcmp(action_name, "Call") == 0) {
         const char *lua_fn =
             table_get_string_field(lua_state, opts_table_idx, "lua");
         if (lua_fn == nullptr) {
            Snackbar::Error(ctx, L"Call action requires a 'lua' option "
                                 L"specifying a function name");
            return nullptr;
         }
         action = BFWMActionCreateCustom(lua_fn);
         break;
      }

      if (strcmp(action_name, "Exec") == 0) {
         const char *cmd =
             table_get_string_field(lua_state, opts_table_idx, "command");
         if (cmd == nullptr) {
            Snackbar::Error(ctx, L"Exec action requires a 'command' option");
            return nullptr;
         }
         action = BFWMActionCreateExec(cmd);
         break;
      }

      if (strcmp(action_name, "ToggleGaps") == 0) {
         action = BFWMActionCreateToggleGaps();
         break;
      }

      if (strcmp(action_name, "CycleLayout") == 0) {
         const char *dir =
             table_get_string_field(lua_state, opts_table_idx, "direction");
         BFWMDirection const d =
             (dir != nullptr) ? str_to_direction(dir) : DirNext;
         action = BFWMActionCreateCycleLayout(d);
         break;
      }

      if (strcmp(action_name, "ReloadConfig") == 0) {
         action = BFWMActionCreateReloadConfig();
         break;
      }

      if (strcmp(action_name, "MoveWorkspaceToMonitor") == 0) {
         lua_getfield(lua_state, opts_table_idx, "index");
         int index = -1;
         if (lua_isinteger(lua_state, -1) != 0)
            index = (int)lua_tointeger(lua_state, -1);
         lua_pop(lua_state, 1);
         const char *dir =
             table_get_string_field(lua_state, opts_table_idx, "direction");
         BFWMDirection const d =
             (dir != nullptr) ? str_to_direction(dir) : DirNext;
         action = BFWMActionCreateMoveWorkspaceToMonitor(index, d);
         break;
      }

      Snackbar::Error(ctx, L"Unknown action type '%hs'",
                      (action_name != nullptr) ? action_name : "(null)");
   } while (false);

   if ((action != nullptr) && lua_istable(lua_state, opts_table_idx)) {
      lua_getfield(lua_state, opts_table_idx, "repeatable");
      if (lua_isboolean(lua_state, -1))
         action->repeatable = lua_toboolean(lua_state, -1);
      lua_pop(lua_state, 1);
   }
   return action;
}
