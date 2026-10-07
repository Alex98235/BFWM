#include "action.h"
#include "../core/bfwm_def.h"

// --- Constructor helpers ---

auto BFWMActionCreateMoveWindow(BFWMDirection direction, int pixels)
    -> BFWMAction * {
   auto *action = new BFWMAction;
   action->type = ActionMoveWindow;
   action->args =
       ActionArgsMoveWindow{.direction = direction, .pixels = pixels};
   action->repeatable = 0;
   return action;
}

auto BFWMActionCreateSpawn(const char *command) -> BFWMAction * {
   auto *action = new BFWMAction;
   action->type = ActionSpawn;
   action->args = ActionArgsSpawn{command};
   action->repeatable = 0;
   return action;
}

auto BFWMActionCreateExec(const char *command) -> BFWMAction * {
   auto *action = new BFWMAction;
   action->type = ActionExec;
   action->args = ActionArgsExec{command};
   action->repeatable = 0;
   return action;
}

auto BFWMActionCreateKillActive() -> BFWMAction * {
   auto *action = new BFWMAction;
   action->type = ActionKillActive;
   action->args = std::monostate{};
   action->repeatable = 0;
   return action;
}

auto BFWMActionCreateMinimize() -> BFWMAction * {
   auto *action = new BFWMAction;
   action->type = ActionMinimize;
   action->args = std::monostate{};
   action->repeatable = 0;
   return action;
}

auto BFWMActionCreateToggleFloat() -> BFWMAction * {
   auto *action = new BFWMAction;
   action->type = ActionToggleFloat;
   action->args = std::monostate{};
   action->repeatable = 0;
   return action;
}

auto BFWMActionCreateFocus(BFWMDirection direction) -> BFWMAction * {
   auto *action = new BFWMAction;
   action->type = ActionFocus;
   action->args = ActionArgsFocus{direction};
   action->repeatable = 0;
   return action;
}

auto BFWMActionCreateWorkspace(int index, BFWMDirection direction)
    -> BFWMAction * {
   auto *action = new BFWMAction;
   action->type = ActionWorkspace;
   action->args = ActionArgsWorkspace{.index = index, .direction = direction};
   action->repeatable = 0;
   return action;
}

auto BFWMActionCreateMoveToWorkspace(int index) -> BFWMAction * {
   auto *action = new BFWMAction;
   action->type = ActionMoveToWorkspace;
   action->args = ActionArgsMoveToWorkspace{index};
   action->repeatable = 0;
   return action;
}

auto BFWMActionCreateFullscreen() -> BFWMAction * {
   auto *action = new BFWMAction;
   action->type = ActionFullscreen;
   action->args = std::monostate{};
   action->repeatable = 0;
   return action;
}

auto BFWMActionCreateSplit() -> BFWMAction * {
   auto *action = new BFWMAction;
   action->type = ActionSplit;
   action->args = std::monostate{};
   action->repeatable = 0;
   return action;
}

auto BFWMActionCreateResizeWindow(BFWMDirection direction, int pixels)
    -> BFWMAction * {
   auto *action = new BFWMAction;
   action->type = ActionResizeWindow;
   action->args =
       ActionArgsMoveWindow{.direction = direction, .pixels = pixels};
   action->repeatable = 0;
   return action;
}

auto BFWMActionCreateLayout(const char *layout_name) -> BFWMAction * {
   auto *action = new BFWMAction;
   action->type = ActionLayout;
   action->args = ActionArgsLayout{layout_name};
   action->repeatable = 0;
   return action;
}

auto BFWMActionCreateSwapSplit() -> BFWMAction * {
   auto *action = new BFWMAction;
   action->type = ActionSwapSplit;
   action->args = std::monostate{};
   action->repeatable = 0;
   return action;
}

auto BFWMActionCreateCustom(const char *lua_callback) -> BFWMAction * {
   auto *action = new BFWMAction;
   action->type = ActionCustom;
   action->args = ActionArgsCustom{lua_callback};
   action->repeatable = 0;
   return action;
}

auto BFWMActionCreateReloadConfig() -> BFWMAction * {
   auto *action = new BFWMAction;
   action->type = ActionReloadConfig;
   action->args = std::monostate{};
   action->repeatable = 0;
   return action;
}

auto BFWMActionCreateToggleGaps() -> BFWMAction * {
   auto *action = new BFWMAction;
   action->type = ActionToggleGaps;
   action->args = std::monostate{};
   action->repeatable = 0;
   return action;
}

auto BFWMActionCreateCycleLayout(BFWMDirection direction)
    -> BFWMAction * {
   auto *action = new BFWMAction;
   action->type = ActionCycleLayout;
   action->args = ActionArgsCycleLayout{direction};
   action->repeatable = 0;
   return action;
}

auto BFWMActionCreateMoveWorkspaceToMonitor(int index,
                                              BFWMDirection direction)
    -> BFWMAction * {
   auto *action = new BFWMAction;
   action->type = ActionMoveWorkspaceToMonitor;
   action->args =
       ActionArgsMoveWorkspaceToMonitor{.index = index, .direction = direction};
   action->repeatable = 0;
   return action;
}

auto BFWMActionClone(const BFWMAction *src) -> BFWMAction * {
   return new BFWMAction(*src);
}

void BFWMActionDestroy(BFWMAction *action) { delete action; }