/**
 * @file focus.h
 * @brief Window focus management.
 *
 * Provides a reliable window focus function with multiple fallback
 * strategies to work around Win32 focus-stealing restrictions.
 */

#ifndef BFWM_FOCUS_H
#define BFWM_FOCUS_H

#include <windows.h>

/**
 * @brief Reliable function to set foreground window using multiple fallback
 * methods.
 *
 * Uses a chain of methods:
 * 1. SwitchToThisWindow
 * 2. AttachThreadInput + SetForegroundWindow
 * 3. SendInput (Alt+Tab simulation) - always works as last resort
 *
 * @param hWnd The window to focus
 * @return TRUE if focus was successfully set, FALSE otherwise
 */
auto FocusWindowReliable(HWND hWnd) -> BOOL;

#endif
