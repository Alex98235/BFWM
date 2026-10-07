/**
 * @file cleanup.h
 * @brief Declarations for window registry cleanup after termination.
 */

#ifndef BFWM_WINDOW_CLEANUP_H
#define BFWM_WINDOW_CLEANUP_H

#include <windows.h>

struct BFWMContext;
class Window;

/**
 * @brief Unregister a window from both the window registry and workspace,
 *        free its memory, and re-apply layout.
 *
 * This must be called after a window is forcefully destroyed (e.g. via
 * TerminateProcess) so that all internal registries remain consistent.
 *
 * @param ctx   The BFWM context
 * @param hWnd  HWND of the terminated window
 */
void CleanupTerminatedWindow(struct BFWMContext *ctx, HWND hWnd);

#endif /* BFWM_WINDOW_CLEANUP_H */
