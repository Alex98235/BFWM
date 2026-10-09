/**
 * @file bfwm_def.h
 * @brief Core type definitions shared across BFWM modules.
 */

#ifndef BFWM_DEF_H
#define BFWM_DEF_H

#include <windows.h>

#define WM_APP_DISPLAYCHANGE (WM_APP + 2)
#define WM_APP_SUSPEND (WM_APP + 3)
#define WM_APP_RESUME (WM_APP + 4)
#define WM_FORCE_FOCUS (WM_APP + 5)
#define WM_APP_DESKTOP_CLICK (WM_APP + 6)
#define WM_APP_MOUSE_HOVER (WM_APP + 7)
#define WM_APP_DPI_CHANGED (WM_APP + 8)
#define WM_APP_DPI_WATCHER_RECREATE (WM_APP + 9)
/// Tray-icon callback message posted by the shell to the hidden window.
#define WM_TRAY_CALLBACK (WM_APP + 10)

/**
 * @brief Direction/position arguments reused across several actions.
 */
using BFWMDirection = enum BFWMDirection {
   DirLeft,
   DirRight,
   DirUp,
   DirDown,
   /// Next (e.g., next workspace or window)
   DirNext,
   /// Previous (e.g., previous workspace or window)
   DirPrev,
};

/** @brief Default log file location for BFWM */
#define BFWM_LOG_PATH "%APPDATA%\\BFWM\\bfwm.log"

#endif
