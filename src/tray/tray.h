#ifndef BFWM_TRAY_H
#define BFWM_TRAY_H

#include <windows.h>

struct BFWMContext;

/**
 * @brief Add the system-tray icon (hidden window + Shell_NotifyIcon).
 *
 * Non-fatal if it fails — BFWMWM still runs headless. Call once on the main
 * thread, before MainLoop.
 *
 * @param ctx The BFWM context (used to signal shutdown from the tray menu)
 * @return TRUE if the icon was added, FALSE on any failure
 */
auto TrayInit(struct BFWMContext *ctx) -> BOOL;

/**
 * @brief Remove the tray icon and free its resources.
 *
 * Call once after MainLoop returns, before ShutdownBFWM.
 */
void TrayShutdown();

#endif /* BFWM_TRAY_H */
