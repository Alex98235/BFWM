#ifndef BFWM_TRAY_H
#define BFWM_TRAY_H

#include <windows.h>

/**
 * @brief Add the system-tray icon (hidden window + Shell_NotifyIcon).
 *
 * Non-fatal if it fails — BFWM still runs headless. Call once on the main
 * thread, before MainLoop.
 *
 * @param shared Shared state.
 * @param worker Worker context (its keystroke queue wakes the main loop).
 * @param main   Main context (its `running` flag signals shutdown).
 * @return TRUE if the icon was added, FALSE on any failure
 */
auto TrayInit(struct SharedState *shared, struct WorkerContext *worker,
              struct MainContext *main) -> BOOL;

/**
 * @brief Remove the tray icon and free its resources.
 *
 * Call once after MainLoop returns, before ShutdownBFWM.
 */
void TrayShutdown();

#endif /* BFWM_TRAY_H */
