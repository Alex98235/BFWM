/**
 * @file filter.h
 * @brief Window filtering and classification utilities.
 *
 * Provides functions to determine whether a window should be managed
 * by the window manager, including class name blacklisting and
 * manageability checks.
 */

#ifndef BFWM_WINDOW_FILTER_H
#define BFWM_WINDOW_FILTER_H

#include <windef.h>

struct BFWMContext;

auto IsExcludedByRules(struct BFWMContext *ctx, const char *class_name,
                       const char *title, const char *process) -> bool;

/**
 * @brief Check whether the given window is a manageable window.
 *
 * A manageable window is, in short, any visible window created by the user,
 * not the system. Checks include visibility, absence of certain styles,
 * and exclusion from the class blacklist.
 *
 * @param hwnd             A window handle
 * @param skip_cloak_check If true, skip the DWMWA_CLOAKED test. Only the
 *                         startup enumeration sets this, so minimized
 *                         windows (DWM-cloaked but visible) still register.
 * @return true if the window is manageable, else false
 */
auto IsManageableWindow(struct BFWMContext *ctx, HWND hwnd,
                        BOOL skip_cloak_check) -> bool;

/**
 * @brief Check whether a window is a shell surface (desktop or taskbar).
 *
 * Shell surfaces are top-level windows owned by the shell that are not
 * user windows: the desktop (Progman and its WorkerW/icon-list
 * descendants, resolved via GetShellWindow) and the taskbar. They must
 * never be tiled, and clicks on them are desktop clicks.
 *
 * @param hwnd A window handle
 * @return true if the window is a shell surface, false otherwise
 */
auto IsShellSurface(HWND hwnd) -> bool;

/**
 * @brief Check whether a window belongs to a system process.
 *
 * Returns true if the window's executable resides under System32 or
 * SysWOW64. This provides a general catch-all for installer,
 * uninstaller, and other OS-tool windows that should not be tiled.
 *
 * @param hwnd A window handle
 * @return true if the process is a system process, false otherwise
 */
auto IsSystemProcess(HWND hwnd) -> bool;

/**
 * @brief Check whether a window's process runs at a higher integrity
 *        level than BFWMWM.
 *
 * Returns true if the process that owns @p hwnd runs at a higher Windows
 * integrity level (e.g. elevated/administrator processes). UIPI makes
 * SetWindowPos fail silently for such windows, so they can never be moved,
 * tiled, or ringed — they must be rejected at registration.
 *
 * @param hwnd A window handle
 * @return true if the process is elevated above BFWMWM, false otherwise
 */
auto IsElevatedWindow(HWND hwnd) -> bool;
#endif
