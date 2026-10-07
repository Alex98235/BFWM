/**
 * @file action_dispatcher.c
 * @brief Implementation of the pluggable action dispatcher.
 *
 * Maintains a fixed-size table indexed by BFWMActionType value.
 * Handlers default to NULL (unregistered) and are populated at startup.
 */

#include "action_dispatcher.h"

#include "../config/action.h"
#include "../logging/logger.h"
#include "bfwm_context.h"
#include <cstddef>

/**
 * @brief Maximum number of action types the dispatcher can handle.
 *
 * Derives from the last entry in BFWMActionType. Update this if new
 * action types are added after ActionReloadConfig.
 */
#define ACTION_TYPE_COUNT ((size_t)ActionType_Count)

namespace {
/** @brief Static table of registered handlers. */
std::array<ActionHandler, ACTION_TYPE_COUNT> handlers;
} // namespace

void ActionDispatcherRegister(BFWMActionType type, ActionHandler handler) {
   auto idx = (size_t)type;
   if (idx >= ACTION_TYPE_COUNT) {
      Error("ActionDispatcherRegister: type %d out of range (max %zu)", type,
            ACTION_TYPE_COUNT - 1);
      return;
   }
   handlers[idx] = handler;
}

auto ActionDispatcherDispatch(BFWMContext *ctx, BFWMAction *action) -> int {
   auto idx = (size_t)action->type;

   if (idx >= ACTION_TYPE_COUNT) {
      Error("ActionDispatcherDispatch: unknown action type %d", action->type);
      return -1;
   }

   ActionHandler handler = handlers[idx];
   if (handler == nullptr) {
      Warn("ActionDispatcherDispatch: no handler registered for type %d",
           action->type);
      return -1;
   }

   return handler(ctx, action);
}
