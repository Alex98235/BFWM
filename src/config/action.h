/**
 * @file action.h
 * @brief Defines all window manager action types and their argument structures.
 *
 * This file declares the core action system used to represent user-invoked
 * operations such as moving windows, spawning processes, changing workspaces,
 * and more. Each action type has a corresponding argument struct.
 */

#ifndef BFWM_ACTION_H
#define BFWM_ACTION_H

#include "../core/bfwm_def.h"
#include <string>
#include <variant>

/**
 * @brief Enumeration of all possible window manager action types.
 */
using BFWMActionType = enum BFWMActionType {
   /// Move/resize focused window
   ActionMoveWindow,
   /// Launch a process
   ActionSpawn,
   /// Close focused window
   ActionKillActive,
   /// Toggle floating for focused window
   ActionToggleFloat,
   /// Focus next/prev/urgent window on workspace/direction
   ActionFocus,
   /// Switch workspace
   ActionWorkspace,
   /// Move window to a different workspace
   ActionMoveToWorkspace,
   /// Toggle fullscreen
   ActionFullscreen,
   /// Toggle split direction
   ActionSplit,
   /// Switch layout (master-stack, rows, grid, etc.)
   ActionLayout,
   /// Resize focused window (keyboard or mouse)
   ActionResizeWindow,
   /// Swap the two children of the parent container
   ActionSwapSplit,
   /// User-defined Lua callback
   ActionCustom,
   /// Execute a shell command (no window)
   ActionExec,
   /// Toggle gaps on/off
   ActionToggleGaps,
   /// Cycle layout (Next/Prev through all registered layouts)
   ActionCycleLayout,
   /// Reload config.lua (re-parses settings and keybinds)
   ActionReloadConfig,
   /// Move active workspace to adjacent monitor
   ActionMoveWorkspaceToMonitor,
   /// Minimize focused window
   ActionMinimize,
   /// Sentinel — add new action types above this line
   ActionType_Count,
};

/**
 * @brief Arguments for moving or resizing a window.
 */
struct ActionArgsMoveWindow {
   /// Direction to move the window
   BFWMDirection direction;
   /// Distance in pixels (0 = default snap distance)
   int pixels;
};

/**
 * @brief Arguments for spawning a GUI process.
 */
struct ActionArgsSpawn {
   /// Command string (e.g. "code", "C:\\Programs\\emacs.exe")
   std::string command;
};

/**
 * @brief Arguments for executing a shell command (no window).
 */
struct ActionArgsExec {
   /// Command string (e.g. "terminal -e cmd.exe", "my_script.bat")
   std::string command;
};

/**
 * @brief Arguments for focusing a window.
 */
struct ActionArgsFocus {
   /// Direction to focus
   BFWMDirection direction;
};

/**
 * @brief Arguments for switching workspaces.
 */
struct ActionArgsWorkspace {
   /// -1 = relative (use direction), else absolute index
   int index;
   /// DirNext/DirPrev for relative, ignored for absolute
   BFWMDirection direction;
};

/**
 * @brief Arguments for moving a window to a different workspace.
 */
struct ActionArgsMoveToWorkspace {
   /// Target workspace index
   int index;
};

/**
 * @brief Arguments for changing the layout.
 */
struct ActionArgsLayout {
   /// Layout name (e.g. "master-stack", "rows", "grid")
   std::string layout_name;
};

/**
 * @brief Arguments for cycling the layout.
 */
struct ActionArgsCycleLayout {
   /// DirNext or DirPrev
   BFWMDirection direction;
};

/**
 * @brief Arguments for moving a workspace to a different monitor.
 */
struct ActionArgsMoveWorkspaceToMonitor {
   /// Target monitor display number (-1 = use direction)
   int index;
   /// Direction when index < 0
   BFWMDirection direction;
};

/**
 * @brief Arguments for invoking a custom Lua callback.
 */
struct ActionArgsCustom {
   /// Name of the Lua function to call
   std::string lua_callback;
};

/**
 * @brief Type-safe union of all action argument structs.
 *
 * std::monostate represents actions that take no arguments.
 */
using BFWMActionArgs =
    std::variant<std::monostate, ActionArgsMoveWindow, ActionArgsSpawn,
                 ActionArgsExec, ActionArgsFocus, ActionArgsWorkspace,
                 ActionArgsMoveToWorkspace, ActionArgsLayout,
                 ActionArgsCycleLayout, ActionArgsMoveWorkspaceToMonitor,
                 ActionArgsCustom>;

/**
 * @brief Represents a single actionable command with its arguments.
 *
 * The args field holds one of the ActionArgsXxx structs defined above,
 * depending on the value of the type field.
 */
using BFWMAction = struct BFWMAction {
   /// The type of action to perform
   BFWMActionType type;
   /// Arguments for the action (variant of ActionArgsXxx)
   BFWMActionArgs args;
   /// Non-zero if this action is allowed to fire on keyboard auto-repeat
   int repeatable;
};

/**
 * @brief Create a MoveWindow action.
 * @param direction Direction to move
 * @param pixels    Distance in pixels (0 = default)
 * @return BFWMAction* Allocated action (heap), must be freed with
 * BFWMActionDestroy()
 */
auto BFWMActionCreateMoveWindow(BFWMDirection direction, int pixels)
    -> BFWMAction *;
/**
 * @brief Create a Spawn action.
 * @param command Command string to execute
 * @return BFWMAction* Allocated action (heap), must be freed with
 * BFWMActionDestroy()
 */
auto BFWMActionCreateSpawn(const char *command) -> BFWMAction *;

/**
 * @brief Create an Exec action (shell command, no window).
 * @param command Command string to execute
 * @return BFWMAction* Allocated action (heap), must be freed with
 * BFWMActionDestroy()
 */
auto BFWMActionCreateExec(const char *command) -> BFWMAction *;

/**
 * @brief Create a KillActive action.
 * @return BFWMAction* Allocated action (heap), must be freed with
 * BFWMActionDestroy()
 */
auto BFWMActionCreateKillActive() -> BFWMAction *;

/**
 * @brief Create a Minimize action.
 * @return BFWMAction* Allocated action (heap), must be freed with
 * BFWMActionDestroy()
 */
auto BFWMActionCreateMinimize() -> BFWMAction *;

/**
 * @brief Create a ToggleFloat action.
 * @return BFWMAction* Allocated action (heap), must be freed with
 * BFWMActionDestroy()
 */
auto BFWMActionCreateToggleFloat() -> BFWMAction *;

/**
 * @brief Create a Focus action.
 * @param direction Direction to focus
 * @return BFWMAction* Allocated action (heap), must be freed with
 * BFWMActionDestroy()
 */
auto BFWMActionCreateFocus(BFWMDirection direction) -> BFWMAction *;

/**
 * @brief Create a Workspace action.
 * @param index     Workspace index (-1 for relative)
 * @param direction Direction for relative switching
 * @return BFWMAction* Allocated action (heap), must be freed with
 * BFWMActionDestroy()
 */
auto BFWMActionCreateWorkspace(int index, BFWMDirection direction)
    -> BFWMAction *;

/**
 * @brief Create a MoveToWorkspace action.
 * @param index Target workspace index
 * @return BFWMAction* Allocated action (heap), must be freed with
 * BFWMActionDestroy()
 */
auto BFWMActionCreateMoveToWorkspace(int index) -> BFWMAction *;

/**
 * @brief Create a Fullscreen action.
 * @return BFWMAction* Allocated action (heap), must be freed with
 * BFWMActionDestroy()
 */
auto BFWMActionCreateFullscreen() -> BFWMAction *;

/**
 * @brief Create a Split action.
 * @return BFWMAction* Allocated action (heap), must be freed with
 * BFWMActionDestroy()
 */
auto BFWMActionCreateSplit() -> BFWMAction *;

/**
 * @brief Create a ResizeWindow action.
 * @param direction Direction to resize
 * @param pixels    Distance in pixels
 * @return BFWMAction* Allocated action (heap), must be freed with
 * BFWMActionDestroy()
 */
auto BFWMActionCreateResizeWindow(BFWMDirection direction, int pixels)
    -> BFWMAction *;

/**
 * @brief Create a Layout action.
 * @param layout_name Layout name string
 * @return BFWMAction* Allocated action (heap), must be freed with
 * BFWMActionDestroy()
 */
auto BFWMActionCreateLayout(const char *layout_name) -> BFWMAction *;

/**
 * @brief Create a SwapSplit action.
 * @return BFWMAction* Allocated action (heap), must be freed with
 * BFWMActionDestroy()
 */
auto BFWMActionCreateSwapSplit() -> BFWMAction *;

/**
 * @brief Create a Custom action.
 * @param lua_callback Name of the Lua function to call
 * @return BFWMAction* Allocated action (heap), must be freed with
 * BFWMActionDestroy()
 */
auto BFWMActionCreateCustom(const char *lua_callback) -> BFWMAction *;

/**
 * @brief Create a ReloadConfig action (re-load config.lua).
 * @return BFWMAction* Allocated action (heap), must be freed with
 * BFWMActionDestroy()
 */
auto BFWMActionCreateReloadConfig() -> BFWMAction *;
auto BFWMActionCreateToggleGaps() -> BFWMAction *;
auto BFWMActionCreateCycleLayout(BFWMDirection direction) -> BFWMAction *;
auto BFWMActionCreateMoveWorkspaceToMonitor(int index,
                                              BFWMDirection direction)
    -> BFWMAction *;

/**
 * @brief Deep-copy an action (for storing in the keybind dictionary).
 * @param src Source action to clone
 * @return BFWMAction* Newly allocated copy of the action
 */
auto BFWMActionClone(const BFWMAction *src) -> BFWMAction *;

/**
 * @brief Free an action and its associated arguments.
 * @param action The action to destroy
 */
void BFWMActionDestroy(BFWMAction *action);

#endif