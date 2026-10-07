/**
 * @file border_manager.c
 * @brief Implementation of the border manager queue-routing service.
 *
 * See border_manager.h for the full description.  This module performs no
 * immediate Win32 work — every call queues a pending operation that is
 * applied at transaction commit time.
 */

#include "border_manager.h"

#include "../../core/bfwm_context.h"
#include "../../transaction/transaction.h"

void BorderManagerSetColor(struct BFWMContext *ctx, HWND hwnd, COLORREF color) {
   ctx->transaction.QueueBorderColor(hwnd, color);
}

void BorderManagerSetActive(struct BFWMContext *ctx, HWND hwnd) {
   BorderManagerSetColor(ctx, hwnd, ctx->config.border_color);
}

void BorderManagerSetInactive(struct BFWMContext *ctx, HWND hwnd) {
   BorderManagerSetColor(ctx, hwnd, ctx->config.inactive_border);
}

void BorderManagerRedraw(struct BFWMContext *ctx, HWND hwnd) {
   ctx->transaction.QueueRedrawBorder(hwnd);
}
