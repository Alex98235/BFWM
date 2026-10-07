/**
 * @file keystroke.h
 * @brief Defines the Keystroke data structure and modifier state tracking.
 *
 * Provides types for representing keyboard input events along with their
 * associated modifier key states (Ctrl, Shift, Alt, Super/Windows).
 */

#ifndef BFWM_KEYSTROKE_H
#define BFWM_KEYSTROKE_H

#include <windows.h>

/**
 * @brief Tracks the state of keyboard modifiers.
 */
using ModifierState = struct {
   /// Ctrl key state
   BOOL ctrl;
   /// Shift key state
   BOOL shift;
   /// Alt key state
   BOOL alt;
   /// Windows/Logo key state
   BOOL super;
};

/**
 * @brief Represents a complete keyboard input event.
 *
 * Combines the low-level keyboard hook data with modifier state
 * for identifying configured keybindings.
 */
using Keystroke = struct {
   /// Low-level keyboard hook data
   KBDLLHOOKSTRUCT key;
   /// Modifier key states at the time of the event
   ModifierState modifiers;
   /// Non-zero if this is an auto-repeat keystroke
   BOOL repeat;
};

/**
 * @brief Get the human-readable name for a virtual key code.
 *
 * @param vkCode Virtual-key code
 * @return const wchar_t* Pointer to a string describing the key
 */
auto GetKeyNameW(DWORD vkCode) -> const wchar_t *;

/**
 * @brief Print debug information about a keyboard event.
 *
 * @param key  The low-level keyboard hook structure
 * @param mods The modifier state at the time of the event
 */
void DebugKey(const KBDLLHOOKSTRUCT *key, const ModifierState *mods);

#endif
