/**
 * @file events.h
 * @brief Window event hooking and enumeration.
 *
 * Sets up Windows event hooks to track window creation, destruction,
 * movement, and other events.
 */

#ifndef BFWM_WINDOW_EVENTS_H
#define BFWM_WINDOW_EVENTS_H

#include "../../core/main_context.h"
#include "../../core/shared_state.h"
#include "../../core/worker_context.h"
#include <windows.h>

/**
 * @brief Install the WinEvent hook (EVENT_OBJECT_DESTROY to
 *        EVENT_OBJECT_LOCATIONCHANGE).
 * @param worker Worker context whose event queue receives events.
 * @return TRUE on success, FALSE on failure.
 */
auto SetupWindowEventHook(WorkerContext *worker) -> BOOL;

/**
 * @brief Unhook the WinEvent hook installed by SetupWindowEventHook.
 * @param context Context whose event_hook is cleared.
 */
void TeardownWindowEventHook();

/**
 * @brief EnumWindows callback for discovering existing top-level windows.
 *
 * @param hwnd   Window handle
 * @param lParam Application-defined data (WindowEnumData*)
 * @return TRUE to continue enumeration, FALSE to stop
 */
struct WindowEnumData {
   struct SharedState *shared;
   struct MainContext *main;
};

auto CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) -> BOOL;

/**
 * @brief WinEvent hook callback procedure.
 *
 * Processes window events such as creation, destruction, and movement.
 */
void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
                           LONG idObject, LONG idChild, DWORD dwEventThread,
                           DWORD dwmsEventTime);

enum WindowTriggerReturnType {
   NO_CONTEXT,
   NO_CLASS,
   REGISTER_SKIPPED,
   WINDOW_ALREADY_REGISTERED,
   REGISTER_SUCCESSFUL
};

/**
 * @brief Process a queued MOVESIZESTART event (main thread, inside
 * transaction).
 *
 * Called from DrainEventQueue. Updates resize_hwnd tracking only.
 */
void ProcessMoveSizeStart(SharedState *shared, MainContext *main, HWND hwnd);

/**
 * @brief Process a queued MOVESIZEEND event (main thread, inside transaction).
 *
 * Called from DrainEventQueue. Queues transaction ops for relayout
 * and floating-window handling.
 */
void ProcessMoveSizeEnd(SharedState *shared, MainContext *main, HWND hwnd);

/**
 * @brief Process a queued MINIMIZESTART event (main thread, in transaction).
 *
 * Removes window from workspace, queues relayout + redraw ops,
 * and sets up auto-focus guard.
 */
void ProcessMinimizeStart(SharedState *shared, MainContext *main, HWND hwnd);

/**
 * @brief Process a queued MINIMIZEEND event (main thread, in transaction).
 *
 * Re-adds the window to its workspace and queues a relayout.
 */
void ProcessMinimizeEnd(SharedState *shared, MainContext *main, HWND hwnd);

/**
 * @brief Process a queued DESTROY event (main thread, in transaction).
 *
 * Removes the window from its workspace and registry, queues relayout
 * and bar-visibility ops, and sets up auto-focus guard.
 */
void ProcessObjectDestroy(SharedState *shared, MainContext *main, HWND hwnd);

/**
 * @brief Process a queued HIDE event (main thread, in transaction).
 *
 * Removes window from its workspace, queues relayout, and sets up
 * auto-focus guard if the hidden window was focused.
 */
void ProcessObjectHide(SharedState *shared, MainContext *main, HWND hwnd);

/**
 * @brief Process a queued SHOW event (main thread, in transaction).
 *
 * Registers the window if needed, adds it to its workspace, queues
 * relayout and floating-window clamping ops.
 */
void ProcessObjectShow(SharedState *shared, MainContext *main, HWND hwnd);

/**
 * @brief Process a queued REORDER event (main thread, in transaction).
 *
 * Re-asserts the border overlay ring's relative z-slot so it stays directly
 * above its target window after something is raised above it.
 */
void ProcessObjectReorder(SharedState *shared, MainContext *main, HWND hwnd);

/**
 * @brief Process a queued FOREGROUND or CREATE event (main thread, in
 * transaction).
 *
 * Handles the auto-focus guard (defeating Windows auto-focus after
 * destroy/minimize), then calls HandleWindowCreate.
 */
void ProcessCreateOrForeground(SharedState *shared, MainContext *main,
                               DWORD event, HWND hwnd);

/**
 * @brief Refresh fullscreen/maximize state for all registered windows.
 *
 * Runs the window-transition check for every window so transitions that
 * fire no window event are still picked up — most importantly a game
 * switching to exclusive fullscreen, which only fires WM_DISPLAYCHANGE
 * (no CREATE/FOREGROUND/MOVESIZEEND accompanies the display-mode change).
 *
 * Handles both directions: entering fullscreen (window is removed from the
 * layout engine so later relayouts skip it) and exiting fullscreen (window
 * is re-inserted). Called from ProcessReconcile before ReconcileMonitors,
 * inside a transaction and holding main->lock.
 *
 * @param shared Shared state (config, keybinds)
 * @param main   Main-thread context
 */
void RefreshFullscreenStates(SharedState *shared, MainContext *main);

#endif
