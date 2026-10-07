/**
 * @file geometry.h
 * @brief Window geometry convenience functions.
 *
 * Provides simple inline helpers for querying window geometry.
 */

#ifndef BFWM_WINDOW_GEOMETRY_H
#define BFWM_WINDOW_GEOMETRY_H

#include <windows.h>

/**
 * @brief Get the centre point of a window.
 *
 * @param hwnd Window handle
 * @param out  Receives the centre coordinates
 * @return TRUE on success, FALSE if GetWindowRect fails
 */
inline auto GetWindowCentre(HWND hwnd, POINT *out) -> BOOL {
   RECT r;
   if (GetWindowRect(hwnd, &r) == 0)
      return FALSE;
   out->x = (r.left + r.right) / 2;
   out->y = (r.top + r.bottom) / 2;
   return TRUE;
}

#endif
