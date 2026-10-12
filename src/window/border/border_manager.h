/**
 * @file border_manager.h
 * @brief Pure queue-routing layer for window border operations.
 *
 * Every border color change and redraw is routed through this service and
 * queued on the current transaction — never applied immediately here; the
 * actual border rendering happens at the OP_BORDER_COLOR apply site.
 */

#ifndef BFWM_BORDER_MANAGER_H
#define BFWM_BORDER_MANAGER_H

#include <windows.h>

/**
 * @brief Queue a border color change for the given window.
 *
 * Deferred to commit time via BFWMTransactionQueueBorderColor (standard
 * early-return guard when no transaction is active).
 *
 * @param shared   The BFWM shared state
 * @param main     The main-thread context
 * @param hwnd  Target window handle
 * @param color The desired COLORREF border color
 */
void BorderManagerSetColor(struct SharedState *shared, struct MainContext *main,
                           HWND hwnd, COLORREF color);

/**
 * @brief Queue the active border color for the given window.
 *
 * @param shared   The BFWM shared state
 * @param main     The main-thread context
 * @param hwnd Target window handle
 */
void BorderManagerSetActive(struct SharedState *shared,
                            struct MainContext *main, HWND hwnd);

/**
 * @brief Queue the inactive border color for the given window.
 *
 * @param shared   The BFWM shared state
 * @param main     The main-thread context
 * @param hwnd Target window handle
 */
void BorderManagerSetInactive(struct SharedState *shared,
                              struct MainContext *main, HWND hwnd);

/**
 * @brief Queue a border redraw for the given window.
 *
 * @param shared   The BFWM shared state
 * @param main     The main-thread context
 * @param hwnd Target window handle
 */
void BorderManagerRedraw(struct SharedState *shared, struct MainContext *main,
                         HWND hwnd);

#endif /* BFWM_BORDER_MANAGER_H */
