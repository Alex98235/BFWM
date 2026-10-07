/**
 * @file hooks.h
 * @brief Win32 keyboard and mouse hook management.
 *
 * Installs and tears down low-level keyboard (WH_KEYBOARD_LL) and
 * mouse (WH_MOUSE_LL) hooks to capture input for the window manager.
 */

#ifndef BFWM_INPUT_HOOKS_H
#define BFWM_INPUT_HOOKS_H

#include "../core/bfwm_context.h"
#include <windows.h>

/**
 * @brief Install the low-level keyboard hook.
 *
 * Registers LowLevelKeyboardProc as a global WH_KEYBOARD_LL hook.
 * Must be called from a thread with a message pump.
 *
 * @param context The BFWM context (stored for use in the hook proc)
 * @return TRUE if the hook was installed successfully
 */
auto SetupKeyboardHooks(BFWMContext *context) -> BOOL;

/**
 * @brief Remove all installed input hooks.
 */
void TeardownKeyboardHooks();

/**
 * @brief Low-level keyboard hook procedure.
 *
 * Captures key events, enqueues them into the keystroke queue for
 * processing by the window manager.
 *
 * @param nCode    Hook code
 * @param wParam   Message type (WM_KEYDOWN, WM_SYSKEYDOWN, etc.)
 * @param lParam   Pointer to a KBDLLHOOKSTRUCT
 * @return The return value of CallNextHookEx
 */
auto CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
    -> LRESULT;

/**
 * @brief Low-level mouse hook procedure.
 *
 * Tracks mouse activity for focus-follows-mouse and click-to-focus
 * behaviour.
 *
 * @param nCode    Hook code
 * @param wParam   Mouse message identifier
 * @param lParam   Pointer to a MSLLHOOKSTRUCT
 * @return The return value of CallNextHookEx
 */
auto CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) -> LRESULT;

#endif
