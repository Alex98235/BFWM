/**
 * @file wm_worker.h
 * @brief Main worker thread for the window manager.
 *
 * Provides the entry point for the primary WM worker thread that
 * processes keystroke events and coordinates window management
 * operations.
 */

#ifndef BFWM_WM_WORKER_H
#define BFWM_WM_WORKER_H

#include "../core/bfwm_context.h"

/**
 * @brief Start the combined worker thread (keyboard + window-event hooks).
 *
 * Installs low-level keyboard/mouse hooks and a WinEvent hook in a single
 * thread, then runs a message-pump loop until ctx->running is FALSE.
 *
 * @param ctx The BFWM context to pass to the worker thread
 * @return true on success, false on failure
 */
auto WMWorkerRun(BFWMContext *ctx) -> bool;

/**
 * @brief Signal the worker thread to stop gracefully.
 *
 * Waits for the worker thread to finish its current processing
 * cycle and exit. The thread handle is read from @c ctx->worker_thread
 * and cleared after the handle is closed.
 *
 * @param ctx The BFWM context whose worker_thread to tear down
 * @return true on success or if ctx/thread_handle is NULL, false on failure
 */
auto WMWorkerStop(BFWMContext *ctx) -> bool;

#endif
