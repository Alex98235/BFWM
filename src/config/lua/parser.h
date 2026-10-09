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
#include <vector>

enum {
   BAR_MAX_INDICATORS = 8,
   BAR_MAX_STATES = 8,
   BAR_DEFAULT_HEIGHT = 30,
};

using BarIndicatorType = enum {
   BAR_INDICATOR_WORKSPACES,
   BAR_INDICATOR_TITLE,
   BAR_INDICATOR_CLOCK,
   BAR_INDICATOR_VOLUME,
   BAR_INDICATOR_NETWORK,
   BAR_INDICATOR_CPU,
   BAR_INDICATOR_MEMORY,
   BAR_INDICATOR_CUSTOM,
   BAR_INDICATOR_COUNT,
};

using BarIndicatorAlign = enum {
   BAR_ALIGN_LEFT,
   BAR_ALIGN_CENTER,
   BAR_ALIGN_RIGHT,
};

/**
 * @brief One conditional rule within a Bar.indicators entry.
 *
 * A rule carries exactly one condition and its effects. When `state` is
 * non-empty the rule matches a runtime's discrete state by name; otherwise
 * it is a numeric threshold on the indicator's primary value ("when the
 * primary value >= at").
 */
struct IndicatorStateRule {
   /// Discrete state name to match; empty => numeric rule.
   std::string state;
   /// Whether this numeric rule carries a threshold.
   bool has_at = false;
   /// Numeric threshold: matches when primary >= at.
   double at = 0.0;
   /// Glyph override.
   std::string icon;
   /// Format override for this state/range.
   std::string format;
   /// Text colour override.
   COLORREF color = 0;
   /// Whether `color` was actually supplied (0 is a valid colour: #000000).
   bool has_color = false;
};

using BarIndicatorConfig = struct BarIndicatorConfig {
   BarIndicatorType type;
   BarIndicatorAlign align;
   /// Stable per-indicator id (Lua callbacks / addressing).
   std::string id;
   /// Maximum rendered width in pixels (0 = unlimited).
   int max_width = 0;
   /// Font size override (0 = bar default).
   int font_size = 0;
   /// Poll interval in ms (0 = provider default).
   int poll_rate_ms = 0;
   /// Format string (dialect: `{name}`, `{name:.Nf}`).
   std::string format;
   /// Lua callback name invoked on click.
   std::string on_click;
   /// Lua callback name invoked on scroll.
   std::string on_scroll;
   /// Named output target (reserved; phase 2).
   std::string output;
   /// Text colour override.
   COLORREF color = 0;
   /// Whether `color` was actually supplied (0 is a valid colour: #000000).
   bool color_set = false;
   /// Per-state / per-range rules.
   std::array<IndicatorStateRule, BAR_MAX_STATES> states = {};
   /// Number of valid entries in `states`.
   int state_count = 0;
   /// Workspaces: show the scrollbar-style position indicator.
   bool position_bar = false;
};

/// Canonical config name for an indicator type (e.g. "volume"). Used for
/// defaulting `BarIndicatorConfig::id`.
auto BarIndicatorTypeName(BarIndicatorType type) -> const char *;

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

/**
 * @brief One named value returned by a custom indicator's `output`.
 *
 * Neutral mirror of the bar's value-bag entry (no bar.h dependency), so the
 * Lua bridge can be built and tested in isolation.
 */
struct LuaIndicatorValue {
   std::string name;
   bool is_num = false;
   double num = 0.0;
   std::string str;
   int precision = 0;
};

/**
 * @brief Result of calling a custom indicator's `output(id)` global.
 *
 * A bare string return sets `valid`/`has_text`; a table return fills the
 * optional fields below. `valid` means "the function returned a value".
 */
struct LuaIndicatorOutput {
   bool valid = false; ///< output existed and returned a value
   bool has_text = false;
   std::string text;
   bool has_state = false;
   std::string state;
   bool has_icon = false;
   std::string icon;
   bool has_value = false;
   double value = 0.0;
   bool has_color = false;
   COLORREF color = 0;
   std::vector<LuaIndicatorValue> values;
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

/**
 * @brief Invoke a named global Lua indicator click callback.
 *
 * Signature: `on_click(id, button, mods)` where `button` is
 * "left"|"right"|"middle" and `mods` is `{ctrl=, shift=, alt=}`. The function
 * is looked up as a global only (no inline-source fallback). Missing,
 * non-function, or erroring callbacks return false and are logged once (at
 * Debug) per function name.
 *
 * @param config The Lua config state
 * @param ctx    The BFWM context (unused today; reserved)
 * @param func   Global function name (the indicator's `on_click`)
 * @param id     The indicator's `id`
 * @param button Mouse button name
 * @param ctrl   Ctrl modifier state
 * @param shift  Shift modifier state
 * @param alt    Alt modifier state
 * @return true if the function was found and callable and completed
 */
auto LuaConfigCallClick(LuaConfig *config, struct BFWMContext *ctx,
                        const char *func, const char *id, const char *button,
                        bool ctrl, bool shift, bool alt) -> bool;

/**
 * @brief Invoke a named global Lua indicator scroll callback.
 *
 * Signature: `on_scroll(id, dir)` where `dir` is "up"|"down". Missing,
 * non-function, or erroring callbacks return false and are logged once (at
 * Debug) per function name.
 *
 * @param config The Lua config state
 * @param ctx    The BFWM context (unused today; reserved)
 * @param func   Global function name (the indicator's `on_scroll`)
 * @param id     The indicator's `id`
 * @param dir    Scroll direction ("up"|"down")
 * @return true if the function was found and callable and completed
 */
auto LuaConfigCallScroll(LuaConfig *config, struct BFWMContext *ctx,
                         const char *func, const char *id, const char *dir)
    -> bool;

/**
 * @brief Call a custom indicator's `output(id)` global.
 *
 * Global-only lookup (no inline-source fallback), called as `output(id)` and
 * expecting one return value:
 *   - a bare string -> `valid=true, has_text=true` (raw UTF-8 bytes preserved);
 *   - a table      -> optional `text`, `state`, `icon`, `value` (number),
 *                     `color` (`"#rrggbb"`), and `values` (a nested
 *                     `name -> number|string` table).
 * `nil`/`false`/a missing function/any error -> `valid=false`, returns false.
 * Quiet: failures are logged once (at Debug) per function name; the Lua stack
 * is balanced on every path.
 *
 * @param config The Lua config state
 * @param ctx    The BFWM context (unused today; reserved)
 * @param func   Global function name (the indicator's `output`)
 * @param id     The indicator's `id` (passed as the sole argument)
 * @param out    Filled on success; left `valid=false` otherwise
 * @return true when the function ran and returned a string or table
 */
auto LuaConfigCallOutput(LuaConfig *config, struct BFWMContext *ctx,
                         const char *func, const char *id,
                         LuaIndicatorOutput *out) -> bool;

#endif
