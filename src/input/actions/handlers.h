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
#include "../../core/main_context.h"
#include "../../core/shared_state.h"

auto HandleSpawn(SharedState *shared, MainContext *main, BFWMAction *action)
    -> int;
auto HandleExec(SharedState *shared, MainContext *main, BFWMAction *action)
    -> int;
auto HandleKillActive(SharedState *shared, MainContext *main,
                      BFWMAction *action) -> int;
auto HandleMinimize(SharedState *shared, MainContext *main, BFWMAction *action)
    -> int;
auto HandleFullscreen(SharedState *shared, MainContext *main,
                      BFWMAction *action) -> int;
auto HandleToggleFloat(SharedState *shared, MainContext *main,
                       BFWMAction *action) -> int;
auto HandleMoveWindow(SharedState *shared, MainContext *main,
                      BFWMAction *action) -> int;
auto HandleFocus(SharedState *shared, MainContext *main, BFWMAction *action)
    -> int;
auto HandleWorkspace(SharedState *shared, MainContext *main, BFWMAction *action)
    -> int;
auto HandleMoveToWorkspace(SharedState *shared, MainContext *main,
                           BFWMAction *action) -> int;
auto HandleResizeWindow(SharedState *shared, MainContext *main,
                        BFWMAction *action) -> int;
auto HandleSplit(SharedState *shared, MainContext *main, BFWMAction *action)
    -> int;
auto HandleSwapSplit(SharedState *shared, MainContext *main, BFWMAction *action)
    -> int;
auto HandleLayout(SharedState *shared, MainContext *main, BFWMAction *action)
    -> int;
auto HandleCycleLayout(SharedState *shared, MainContext *main,
                       BFWMAction *action) -> int;
auto HandleCustom(SharedState *shared, MainContext *main, BFWMAction *action)
    -> int;
auto HandleReloadConfig(SharedState *shared, MainContext *main,
                        BFWMAction *action) -> int;
auto HandleToggleGaps(SharedState *shared, MainContext *main,
                      BFWMAction *action) -> int;
auto HandleMoveWorkspaceToMonitor(SharedState *shared, MainContext *main,
                                  BFWMAction *action) -> int;

/**
 * @brief Toggle fullscreen for an arbitrary HWND (not just the focused one).
 */
void ToggleFullscreenForHwnd(HWND hwnd, SharedState *shared, MainContext *main);

/**
 * @brief Restore all fullscreened windows to their saved rects and show bars.
 *
 * Called during program exit so windows aren't left with oversized
 * off-screen-borders positioning.
 */
void RestoreFullscreenWindows(SharedState *shared, MainContext *main);

void CheckPendingKills(SharedState *shared, MainContext *main);

#endif /* BFWM_HANDLERS_H */
