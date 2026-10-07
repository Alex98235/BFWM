/**
 * @file detect.h
 * @brief Fullscreen window detection.
 *
 * Determines whether a given window is in a fullscreen state and
 * classifies the type of fullscreen.
 */

#ifndef BFWM_WINDOW_FULLSCREEN_DETECT_H
#define BFWM_WINDOW_FULLSCREEN_DETECT_H

#include <windows.h>

/**
 * @brief Classification of a window's fullscreen state.
 */
using FullscreenType = enum {
   /// Unable to determine state
   FS_UNKNOWN = 0,
   /// DirectX/Vulkan exclusive fullscreen
   FS_EXCLUSIVE_FULLSCREEN = 1 << 1,
   /// Regular maximised window
   FS_MAXIMIZED_WINDOW = 1 << 2,
   /// Borderless window covering the entire monitor
   FS_BORDERLESS_WINDOW = 1 << 3,
   /// Definitely not fullscreen
   FS_NOT_FULLSCREEN = 1 << 4
};

/**
 * @brief Detect the fullscreen state of a window.
 *
 * Checks the window's placement, style, and monitor coverage to
 * classify its fullscreen type.
 *
 * @param hwnd Window to inspect
 * @return The detected FullscreenType
 */
auto DetectFullscreenWindow(HWND hwnd) -> FullscreenType;

#endif
