/**
 * @file border_manager.c
 * @brief Implementation of the border manager queue-routing service.
 *
 * See border_manager.h for the full description.  This module performs no
 * immediate Win32 work — every call queues a pending operation that is
 * applied at transaction commit time.
 */

#include "border_manager.h"

#include "../../core/main_context.h"

#include "../../core/shared_state.h"
#include "../../transaction/transaction.h"

void BorderManagerSetColor(struct SharedState * /*shared*/,
                           struct MainContext *main, HWND hwnd,
                           COLORREF color) {
   main->transaction.QueueBorderColor(hwnd, color);
}

void BorderManagerSetActive(struct SharedState *shared,
                            struct MainContext *main, HWND hwnd) {
   BorderManagerSetColor(shared, main, hwnd, shared->config.border_color);
}

void BorderManagerSetInactive(struct SharedState *shared,
                              struct MainContext *main, HWND hwnd) {
   BorderManagerSetColor(shared, main, hwnd, shared->config.inactive_border);
}

void BorderManagerRedraw(struct SharedState * /*shared*/,
                         struct MainContext *main, HWND hwnd) {
   main->transaction.QueueRedrawBorder(hwnd);
}
