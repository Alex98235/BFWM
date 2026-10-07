/**
 * @file builder.h
 * @brief Lua config action builder.
 *
 * Converts parsed Lua configuration data into BFWMAction structs
 * and translates string representations of keys and directions into
 * their internal enum/DWORD values.
 */

#ifndef BFWM_ACTION_BUILDER_H
#define BFWM_ACTION_BUILDER_H

#include "../../core/bfwm_def.h"
#include "../../input/keystroke.h"
#include <windows.h>

struct LuaConfig;
struct BFWMContext;
struct BFWMAction;

/**
 * @brief Convert a direction string to a BFWMDirection enum.
 *
 * Accepts "left", "right", "up", "down", "next", "prev" (case-insensitive).
 *
 * @param s The direction string
 * @return The corresponding BFWMDirection value
 */
auto str_to_direction(const char *s) -> BFWMDirection;

/**
 * @brief Convert a key name string to a virtual-key code.
 *
 * Accepts names like "a", "space", "lcontrol", "f1", etc.
 *
 * @param name The key name string
 * @return The corresponding virtual-key code, or 0 if unknown
 */
auto key_name_to_vk(const char *name) -> DWORD;

/**
 * @brief Parse a combined key string into modifier state and key name.
 *
 * Splits a string like "Mod4+Shift+a" into the modifier bitmask and
 * the base key name.
 *
 * @param key_str         The combined key string
 * @param mods            Receives the parsed modifier flags
 * @param out_key_name    Buffer for the extracted key name
 * @param out_key_name_sz Size of out_key_name buffer
 */
void parse_key_string(const char *key_str, ModifierState *mods,
                      char *out_key_name, size_t out_key_name_sz);

/**
 * @brief Build a BFWMAction from a Lua action definition.
 *
 * Creates the appropriate action type based on action_name, parsing
 * additional options from the Lua table at opts_table_idx.
 *
 * @param config          The Lua config state
 * @param ctx             The BFWM context
 * @param action_name     The action type name (e.g. "spawn", "focus")
 * @param opts_table_idx  Lua stack index for the options table
 * @return A newly allocated BFWMAction, or NULL on failure
 */
auto build_action(struct LuaConfig *config, struct BFWMContext *ctx,
                  const char *action_name, int opts_table_idx)
    -> struct BFWMAction *;

#endif
