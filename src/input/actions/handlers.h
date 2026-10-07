/**
 * @file handlers.h
 * @brief Declares handler functions for every BFWMActionType.
 *
 * Each handler implements ActionHandler and is registered with the
 * ActionDispatcher during program initialization.
 */

#ifndef BFWM_HANDLERS_H
#define BFWM_HANDLERS_H

#include "../../config/action.h"
#include "../../core/bfwm_context.h"

auto HandleSpawn(BFWMContext *ctx, BFWMAction *action) -> int;
auto HandleExec(BFWMContext *ctx, BFWMAction *action) -> int;
auto HandleKillActive(BFWMContext *ctx, BFWMAction *action) -> int;
auto HandleMinimize(BFWMContext *ctx, BFWMAction *action) -> int;
auto HandleFullscreen(BFWMContext *ctx, BFWMAction *action) -> int;
auto HandleToggleFloat(BFWMContext *ctx, BFWMAction *action) -> int;
auto HandleMoveWindow(BFWMContext *ctx, BFWMAction *action) -> int;
auto HandleFocus(BFWMContext *ctx, BFWMAction *action) -> int;
auto HandleWorkspace(BFWMContext *ctx, BFWMAction *action) -> int;
auto HandleMoveToWorkspace(BFWMContext *ctx, BFWMAction *action) -> int;
auto HandleResizeWindow(BFWMContext *ctx, BFWMAction *action) -> int;
auto HandleSplit(BFWMContext *ctx, BFWMAction *action) -> int;
auto HandleSwapSplit(BFWMContext *ctx, BFWMAction *action) -> int;
auto HandleLayout(BFWMContext *ctx, BFWMAction *action) -> int;
auto HandleCycleLayout(BFWMContext *ctx, BFWMAction *action) -> int;
auto HandleCustom(BFWMContext *ctx, BFWMAction *action) -> int;
auto HandleReloadConfig(BFWMContext *ctx, BFWMAction *action) -> int;
auto HandleToggleGaps(BFWMContext *ctx, BFWMAction *action) -> int;
auto HandleMoveWorkspaceToMonitor(BFWMContext *ctx, BFWMAction *action)
    -> int;

/**
 * @brief Toggle fullscreen for an arbitrary HWND (not just the focused one).
 *
 * Extracted so that both HandleFullscreen (keybind) and the maximize-button
 * intercept in window_events.c can share the same logic.
 */
void ToggleFullscreenForHwnd(HWND hwnd, BFWMContext *ctx);

/**
 * @brief Restore all fullscreened windows to their saved rects and show bars.
 *
 * Called during program exit so windows aren't left with oversized
 * off-screen-borders positioning.
 */
void RestoreFullscreenWindows(BFWMContext *ctx);

void CheckPendingKills(BFWMContext *ctx);

#endif /* BFWM_HANDLERS_H */
