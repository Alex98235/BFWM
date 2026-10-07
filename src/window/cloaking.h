/**
 * @file cloaking.h
 * @brief Window cloaking (DWMWA_CLOAK) support.
 *
 * Allows showing or hiding a window via the DWM cloaking API without
 * destroying or unmanaging the window.
 */

#ifndef BFWM_WINDOW_CLOAK_H
#define BFWM_WINDOW_CLOAK_H

#include <windows.h>

/**
 * @brief Set the DWM cloaking state of a window.
 *
 * Cloaking hides a window from the taskbar and alt-tab switcher while
 * keeping it in the DWM composition tree. Useful for workspace switching.
 *
 * @param hwnd    Window handle
 * @param cloaked TRUE to cloak, FALSE to uncloak
 * @return S_OK on success, otherwise an HRESULT error code
 */
auto SetWindowCloakState(HWND hwnd, BOOL cloaked) -> HRESULT;

#endif
