/**
 * @file action_dispatcher.h
 * @brief Pluggable dispatch mechanism for BFWMAction types.
 *
 * Instead of maintaining a central switch-statement in main.c, each action
 * type registers its own handler. New action types can be added without
 * touching the dispatch core, and handlers can be unit-tested independently.
 *
 * Usage:
 * @code
 *    // Register during startup
 *    ActionDispatcherRegister(ActionSpawn, HandleSpawn);
 *    ActionDispatcherRegister(ActionKillActive, HandleKillActive);
 *    // ...
 *
 *    // Dispatch when a keystroke is received
 *    for (size_t i = 0; actions[i] != NULL; i++) {
 *        ActionDispatcherDispatch(ctx, actions[i]);
 *    }
 * @endcode
 */

#ifndef BFWM_ACTION_DISPATCHER_H
#define BFWM_ACTION_DISPATCHER_H

#include "../config/action.h"
#include "../core/bfwm_context.h"

/**
 * @brief Handler function pointer.
 *
 * Implementations receive the application context (for access to Lua, window
 * manager, workspace, etc.) and the action to execute.
 *
 * @param ctx   Application context (never NULL).
 * @param action The action to handle (never NULL).
 * @return 0 on success, non-zero on failure.
 */
using ActionHandler = int (*)(BFWMContext *ctx, BFWMAction *action);

/**
 * @brief Register a handler for a specific action type.
 *
 * Later registrations of the same type overwrite the previous handler.
 * Passing NULL as handler unregisters the type.
 *
 * @param type    The BFWMActionType to register for.
 * @param handler The handler function, or NULL to unregister.
 */
void ActionDispatcherRegister(BFWMActionType type, ActionHandler handler);

/**
 * @brief Dispatch a single action to its registered handler.
 *
 * @param ctx    Application context.
 * @param action The action to execute.
 * @return 0 on success, non-zero if no handler was registered or the handler
 *         returned an error.
 */
auto ActionDispatcherDispatch(BFWMContext *ctx, BFWMAction *action) -> int;

#endif /* BFWM_ACTION_DISPATCHER_H */
