/**
 * @file win_utils.h
 * @brief Win32 window manipulation utilities.
 *
 * Thin wrappers around common Win32 window operations (positioning,
 * z-order, visibility, styles, and window data) used throughout the
 * window manager.
 */

#ifndef BFWM_WIN_UTILS_H
#define BFWM_WIN_UTILS_H

#include <windows.h>

/* Forward: the DPI registry is owned by the BFWM context. */
struct BFWMContext;

/**
 * @brief Captures the current window style and extended style.
 */
using BFWMWindowStyle = struct {
   /// Window style (GWL_STYLE)
   DWORD style;
   /// Extended window style (GWL_EXSTYLE)
   DWORD exStyle;
};

/**
 * @brief Base window positioning function.
 *
 * Always applies SWP_NOACTIVATE.  Callers supply any extra flags.
 *
 * @param hwnd       Window handle
 * @param insertAfter HWND to insert after (for z-order), or NULL
 * @param rect       New position and size
 * @param extraFlags Additional SWP_* flags to OR in
 * @return TRUE on success
 */
auto BFWMSetWindowPosEx(HWND hwnd, HWND insertAfter, const RECT *rect,
                        DWORD extraFlags) -> BOOL;

/**
 * @brief Tiled layout positioning.  Does not change z-order.
 *
 * Uses SWP_NOCOPYBITS and SWP_FRAMECHANGED on top of the base flags.
 *
 * @param hwnd Window handle
 * @param rect New position and size
 * @return TRUE on success
 */
auto BFWMApplyLayoutPosition(HWND hwnd, const RECT *rect) -> BOOL;

/**
 * @brief Floating window positioning.  Brings window to top of z-order.
 *
 * Same flags as BFWMApplyLayoutPosition but with HWND_TOP.
 *
 * @param hwnd Window handle
 * @param rect New position and size
 * @return TRUE on success
 */
auto BFWMSetWindowPos(HWND hwnd, const RECT *rect) -> BOOL;

/**
 * @brief Issue a physical rect to a window, converting to the target's
 *        logical coordinate space when it is DPI-unaware on a scaled monitor.
 *
 * Mirrors the IssueMove conversion: for an unaware target on a scaled monitor
 * the thread is switched into the target's awareness context and the rect is
 * unscaled so the DWM composes it back to the exact physical rect. Aware
 * targets and 100%-scale destinations are issued unchanged with no context
 * switch (zero behaviour change for PM-aware windows).
 *
 * @param ctx   The BFWM context (DPI registry)
 * @param hwnd  The target window
 * @param hmon  The monitor the rect is in (for its DPI)
 * @param rect  The physical rect to issue
 * @param issue The Win32 call that applies the rect
 */
void IssueRectWithDpiConversion(struct BFWMContext *ctx, HWND hwnd,
                                HMONITOR hmon, RECT rect,
                                BOOL (*issue)(HWND, const RECT *));

/**
 * @brief Z-order positions for BFWMSetWindowZ.
 *
 * Maps directly to Win32 hWndInsertAfter sentinel values.
 */
using BFWMZOrder = enum {
   BFWM_Z_BOTTOM = 1,     ///< HWND_BOTTOM    — place at very bottom
   BFWM_Z_TOP = 0,        ///< HWND_TOP       — place at top of normal tier
   BFWM_Z_TOPMOST = -1,   ///< HWND_TOPMOST   — place in topmost tier
   BFWM_Z_NOTOPMOST = -2, ///< HWND_NOTOPMOST — move from topmost to normal
};

/**
 * @brief Move a window to a z-order position.
 * @param hwnd Window handle
 * @param z    Target z-order position
 * @return TRUE on success
 */
auto BFWMSetWindowZ(HWND hwnd, BFWMZOrder z) -> BOOL;

/**
 * @brief Position a window for fullscreen (fills monitor work area).
 * @param hwnd Window handle
 * @param rect Fullscreen rectangle
 * @return TRUE on success
 */
auto BFWMSetFullscreenPosition(HWND hwnd, const RECT *rect) -> BOOL;

/**
 * @brief Force redraw of the window's client area.
 * @param hwnd Window handle
 * @return TRUE on success
 */
auto BFWMRedrawContent(HWND hwnd) -> BOOL;

/**
 * @brief Show the window (SW_SHOW).
 * @param hwnd Window handle
 * @return TRUE on success
 */
auto BFWMShowWindow(HWND hwnd) -> BOOL;

/**
 * @brief Hide the window (SW_HIDE).
 * @param hwnd Window handle
 * @return TRUE on success
 */
auto BFWMHideWindow(HWND hwnd) -> BOOL;

/**
 * @brief Minimise the window (SW_MINIMIZE).
 * @param hwnd Window handle
 * @return TRUE on success
 */
auto BFWMMinimizeWindow(HWND hwnd) -> BOOL;

/**
 * @brief Save the current window style for later restoration.
 * @param style Receives the saved style
 * @param hwnd  Window handle
 */
void BFWMSaveWindowStyle(BFWMWindowStyle *style, HWND hwnd);

/**
 * @brief Restore a previously saved window style.
 * @param hwnd  Window handle
 * @param style Saved style to restore
 */
void BFWMRestoreWindowStyle(HWND hwnd, const BFWMWindowStyle *style);

/**
 * @brief Apply fullscreen-compatible window styles.
 *
 * Removes borders and title bar for an immersive fullscreen experience.
 * @param hwnd Window handle
 */
void BFWMApplyFullscreenStyle(HWND hwnd);

/**
 * @brief Attach user data to a window via SetProp/WindowLongPtr.
 * @param hwnd Window handle
 * @param data Pointer to associate with this window
 */
void BFWMSetWindowData(HWND hwnd, void *data);

/**
 * @brief Retrieve user data attached to a window.
 * @param hwnd Window handle
 * @return The associated pointer, or NULL
 */
auto BFWMGetWindowData(HWND hwnd) -> void *;

/**
 * @brief Enable non-client area rendering (DWM) for the window.
 * @param hwnd Window handle
 * @return TRUE on success
 */
auto BFWMEnableNCRendering(HWND hwnd) -> BOOL;

#endif
