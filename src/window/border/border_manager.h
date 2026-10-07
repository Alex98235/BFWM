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

struct BFWMContext;

/**
 * @brief Queue a border color change for the given window.
 *
 * Deferred to commit time via BFWMTransactionQueueBorderColor (standard
 * early-return guard when no transaction is active).
 *
 * @param ctx   The BFWM context
 * @param hwnd  Target window handle
 * @param color The desired COLORREF border color
 */
void BorderManagerSetColor(struct BFWMContext *ctx, HWND hwnd,
                           COLORREF color);

/**
 * @brief Queue the active border color for the given window.
 *
 * @param ctx  The BFWM context
 * @param hwnd Target window handle
 */
void BorderManagerSetActive(struct BFWMContext *ctx, HWND hwnd);

/**
 * @brief Queue the inactive border color for the given window.
 *
 * @param ctx  The BFWM context
 * @param hwnd Target window handle
 */
void BorderManagerSetInactive(struct BFWMContext *ctx, HWND hwnd);

/**
 * @brief Queue a border redraw for the given window.
 *
 * @param ctx  The BFWM context
 * @param hwnd Target window handle
 */
void BorderManagerRedraw(struct BFWMContext *ctx, HWND hwnd);

#endif /* BFWM_BORDER_MANAGER_H */
