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

#include "../core/main_context.h"
#include "../core/worker_context.h"

/**
 * @brief Start the combined worker thread (keyboard + window-event hooks).
 *
 * Installs low-level keyboard/mouse hooks and a WinEvent hook in a single
 * thread, then runs a message-pump loop until main->running is FALSE.
 *
 * @param worker The worker context to pass to the worker thread
 * @param main   The main context whose worker_thread handle is populated
 * @return true on success, false on failure
 */
auto WMWorkerRun(WorkerContext *worker, MainContext *main) -> bool;

/**
 * @brief Signal the worker thread to stop gracefully.
 *
 * Waits for the worker thread to finish its current processing
 * cycle and exit. The thread handle is read from @c main->worker_thread
 * and cleared after the handle is closed.
 *
 * @param main The main context whose worker_thread to tear down
 * @return true on success or if main/thread_handle is NULL, false on failure
 */
auto WMWorkerStop(MainContext *main) -> bool;

#endif
