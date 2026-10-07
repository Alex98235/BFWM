/**
 * @file parser.h
 * @brief Lua configuration file parser.
 *
 * Handles loading, parsing, and reloading of config.lua. Populates
 * the BFWM context with runtime settings, keybindings, workspace
 * configurations, and bar layout definitions.
 */

#ifndef BFWM_LUA_CONFIG_H
#define BFWM_LUA_CONFIG_H

#include "../../workspace/layouts/layout_api.h"
#include <array>
#ifdef __cplusplus
extern "C" {
#endif
#include <lua.h>
#ifdef __cplusplus
}
#endif

#include <cstddef>
#include <string>

enum {
   BAR_MAX_INDICATORS = 8,
   BAR_DEFAULT_HEIGHT = 30,
};

/// Number of per-indicator icon strings (volume states) in BarIndicator.
enum { BAR_MAX_ICONS = 5 };

using BarIndicatorType = enum {
   BAR_INDICATOR_WORKSPACES,
   BAR_INDICATOR_TITLE,
   BAR_INDICATOR_CLOCK,
   BAR_INDICATOR_VOLUME,
   BAR_INDICATOR_NETWORK,
   BAR_INDICATOR_CPU,
   BAR_INDICATOR_MEMORY,
   BAR_INDICATOR_COUNT,
};

using BarIndicatorAlign = enum {
   BAR_ALIGN_LEFT,
   BAR_ALIGN_CENTER,
   BAR_ALIGN_RIGHT,
};

using BarIndicatorConfig = struct BarIndicatorConfig {
   BarIndicatorType type;
   BarIndicatorAlign align;
   int max_width;
   int font_size;
   std::string format;
   bool show_position_bar;
   std::array<std::string, BAR_MAX_ICONS> icons;
   int icon_count;
   std::string icon_muted;
   std::string icon_disconnected;
   std::string icon_ethernet;
   std::string format_ethernet;
   std::string format_wifi;
   int poll_rate_ms;            /* 0 = bar default (500ms) */
   COLORREF color;              /* 0 = use bar default text */
   COLORREF color_disconnected; /* network: disconnected state */
   COLORREF color_muted;        /* volume: muted state */
};

using BarConfig = struct BarConfig {
   int height;
   bool enabled;
   int corner_radius;
   struct {
      int top;
      int right;
      int bottom;
      int left;
   } margin;
   struct {
      int top;
      int right;
      int bottom;
      int left;
   } padding;
   struct {
      int width;
      COLORREF color;
   } border;
   struct {
      int size;
      int weight;
      std::string name;
   } font;
   struct {
      COLORREF background;
      COLORREF text;
      COLORREF active_workspace;
      COLORREF inactive_workspace;
      COLORREF tab_border;
   } colors;
   std::array<BarIndicatorConfig, BAR_MAX_INDICATORS> indicators = {};
   int indicator_count;
};

/**
 * @brief Per-workspace configuration from BFWM.workspaces.
 */
using WorkspaceConfig = struct WorkspaceConfig {
   /// Workspace ID (e.g. 1, 2, 3)
   size_t id;
   /// User-facing label (e.g. acodinga)
   std::string label;
   /// 0 = unassigned, 1+ = display_number to pin to
   int assigned_monitor;
   /// LAYOUT_NONE means use default
   LayoutType layout;
   /// Whether layout was explicitly set
   BOOL has_layout;
};

using LuaConfig = struct LuaConfig {
   lua_State *L;
   std::string config_path;
};

class KeybindDictionary;
struct BFWMContext;

/**
 * @brief Initialize the Lua state, load the config file, parse settings
 *        and keybinds.
 *
 * Opens a Lua VM, loads config.lua from the given path, reads runtime
 * settings into ctx->config, and populates the keybind dictionary.
 *
 * @param config      The Lua config state
 * @param ctx         The BFWM context (settings + keybinds are written here)
 * @param config_path Path to config.lua (e.g. aconfig.luaa)
 * @return true on success, false on failure
 */
auto LuaConfigLoad(LuaConfig *config, struct BFWMContext *ctx,
                   const char *config_path) -> bool;

/**
 * @brief Reload the configuration (re-parse config.lua).
 *
 * @param config The Lua config state
 * @param ctx    The BFWM context (settings + keybinds are updated here)
 * @return true on success
 */
auto LuaConfigReload(LuaConfig *config, struct BFWMContext *ctx) -> bool;

/**
 * @brief Free the Lua state. Call on shutdown.
 * @param config The Lua config state
 */
void LuaConfigFree(LuaConfig *config);

/**
 * @brief Call a named Lua function from the config.
 *
 * Used by ActionCustom to invoke user-defined callbacks.
 *
 * @param config    The Lua config state
 * @param ctx       The BFWM context (for snackbar notifications)
 * @param func_name Name of the Lua function to call
 * @return true if the function was found and called successfully
 */
auto LuaConfigCall(LuaConfig *config, struct BFWMContext *ctx,
                   const char *func_name) -> bool;

#endif
