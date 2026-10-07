#include "cbelt.h"
#include <cstring>
#include <string>
#include <variant>
#include <windows.h>

#include "../../src/config/action.h"

CBELT_GROUP("BFWM_action")

/* =========================================================================
 * BFWMActionCreate / Destroy — null safety
 * ========================================================================= */

CBELT_TEST(destroy_null_is_safe) {
   BFWMActionDestroy(nullptr);
   return TEST_SUCCESS;
}

/* =========================================================================
 * Action creation with no arguments
 * ========================================================================= */

CBELT_TEST(create_kill_active) {
   BFWMAction *a = BFWMActionCreateKillActive();
   cbelt_assert(a != nullptr);
   cbelt_assert(a->type == ActionKillActive);
   cbelt_assert(std::holds_alternative<std::monostate>(a->args));
   BFWMActionDestroy(a);
   return TEST_SUCCESS;
}

CBELT_TEST(create_toggle_float) {
   BFWMAction *a = BFWMActionCreateToggleFloat();
   cbelt_assert(a != nullptr);
   cbelt_assert(a->type == ActionToggleFloat);
   cbelt_assert(std::holds_alternative<std::monostate>(a->args));
   BFWMActionDestroy(a);
   return TEST_SUCCESS;
}

CBELT_TEST(create_fullscreen) {
   BFWMAction *a = BFWMActionCreateFullscreen();
   cbelt_assert(a != nullptr);
   cbelt_assert(a->type == ActionFullscreen);
   cbelt_assert(std::holds_alternative<std::monostate>(a->args));
   BFWMActionDestroy(a);
   return TEST_SUCCESS;
}

CBELT_TEST(create_split) {
   BFWMAction *a = BFWMActionCreateSplit();
   cbelt_assert(a != nullptr);
   cbelt_assert(a->type == ActionSplit);
   cbelt_assert(std::holds_alternative<std::monostate>(a->args));
   BFWMActionDestroy(a);
   return TEST_SUCCESS;
}

/* =========================================================================
 * Action creation with enum arguments
 * ========================================================================= */

CBELT_TEST(create_move_window) {
   BFWMAction *a = BFWMActionCreateMoveWindow(DirLeft, 10);
   cbelt_assert(a != nullptr);
   cbelt_assert(a->type == ActionMoveWindow);
   cbelt_assert(!std::holds_alternative<std::monostate>(a->args));
   cbelt_assert(std::get<ActionArgsMoveWindow>(a->args).direction == DirLeft);
   cbelt_assert(std::get<ActionArgsMoveWindow>(a->args).pixels == 10);
   BFWMActionDestroy(a);

   /* Test another direction and default pixels */
   a = BFWMActionCreateMoveWindow(DirDown, 0);
   cbelt_assert(std::get<ActionArgsMoveWindow>(a->args).direction == DirDown);
   cbelt_assert(std::get<ActionArgsMoveWindow>(a->args).pixels == 0);
   BFWMActionDestroy(a);
   return TEST_SUCCESS;
}

CBELT_TEST(create_focus) {
   BFWMAction *a = BFWMActionCreateFocus(DirRight);
   cbelt_assert(a != nullptr);
   cbelt_assert(a->type == ActionFocus);
   cbelt_assert(std::get<ActionArgsFocus>(a->args).direction == DirRight);
   BFWMActionDestroy(a);

   a = BFWMActionCreateFocus(DirUp);
   cbelt_assert(std::get<ActionArgsFocus>(a->args).direction == DirUp);
   BFWMActionDestroy(a);
   return TEST_SUCCESS;
}

CBELT_TEST(create_workspace_absolute) {
   BFWMAction *a = BFWMActionCreateWorkspace(3, DirNext);
   cbelt_assert(a != nullptr);
   cbelt_assert(a->type == ActionWorkspace);
   cbelt_assert(std::get<ActionArgsWorkspace>(a->args).index == 3);
   cbelt_assert(std::get<ActionArgsWorkspace>(a->args).direction == DirNext);
   BFWMActionDestroy(a);
   return TEST_SUCCESS;
}

CBELT_TEST(create_workspace_relative) {
   BFWMAction *a = BFWMActionCreateWorkspace(-1, DirPrev);
   cbelt_assert(a != nullptr);
   cbelt_assert(std::get<ActionArgsWorkspace>(a->args).index == -1);
   cbelt_assert(std::get<ActionArgsWorkspace>(a->args).direction == DirPrev);
   BFWMActionDestroy(a);
   return TEST_SUCCESS;
}

CBELT_TEST(create_move_to_workspace) {
   BFWMAction *a = BFWMActionCreateMoveToWorkspace(5);
   cbelt_assert(a != nullptr);
   cbelt_assert(a->type == ActionMoveToWorkspace);
   cbelt_assert(std::get<ActionArgsMoveToWorkspace>(a->args).index == 5);
   BFWMActionDestroy(a);

   /* Edge case with index 0 */
   a = BFWMActionCreateMoveToWorkspace(0);
   cbelt_assert(std::get<ActionArgsMoveToWorkspace>(a->args).index == 0);
   BFWMActionDestroy(a);
   return TEST_SUCCESS;
}

/* =========================================================================
 * Action creation with string arguments
 * ========================================================================= */

CBELT_TEST(create_spawn) {
   std::string cmd = "notepad.exe";
   BFWMAction *a = BFWMActionCreateSpawn(cmd.data());
   cbelt_assert(a != nullptr);
   cbelt_assert(a->type == ActionSpawn);
   auto &args = std::get<ActionArgsSpawn>(a->args);
   cbelt_assert(!args.command.empty());
   cbelt_assert(args.command == cmd);
   /* String should be a deep copy, not pointing to the original */
   cmd[0] = 'X';
   cbelt_assert(args.command == "notepad.exe");
   BFWMActionDestroy(a);
   return TEST_SUCCESS;
}

CBELT_TEST(create_spawn_empty_command) {
   BFWMAction *a = BFWMActionCreateSpawn("");
   cbelt_assert(a != nullptr);
   auto &args = std::get<ActionArgsSpawn>(a->args);
   cbelt_assert(args.command.empty());
   BFWMActionDestroy(a);
   return TEST_SUCCESS;
}

CBELT_TEST(create_layout) {
   BFWMAction *a = BFWMActionCreateLayout("grid");
   cbelt_assert(a != nullptr);
   cbelt_assert(a->type == ActionLayout);
   cbelt_assert(std::get<ActionArgsLayout>(a->args).layout_name == "grid");
   BFWMActionDestroy(a);
   return TEST_SUCCESS;
}

CBELT_TEST(create_custom) {
   BFWMAction *a = BFWMActionCreateCustom("my_callback");
   cbelt_assert(a != nullptr);
   cbelt_assert(a->type == ActionCustom);
   cbelt_assert(std::get<ActionArgsCustom>(a->args).lua_callback ==
                "my_callback");
   BFWMActionDestroy(a);
   return TEST_SUCCESS;
}

/* =========================================================================
 * BFWMActionClone — deep copy verification
 * ========================================================================= */

CBELT_TEST(clone_kill_active) {
   BFWMAction *orig = BFWMActionCreateKillActive();
   BFWMAction *clone = BFWMActionClone(orig);
   cbelt_assert(clone != nullptr);
   cbelt_assert(clone != orig);
   cbelt_assert(clone->type == ActionKillActive);
   cbelt_assert(std::holds_alternative<std::monostate>(clone->args));
   BFWMActionDestroy(orig);
   BFWMActionDestroy(clone);
   return TEST_SUCCESS;
}

CBELT_TEST(clone_move_window) {
   BFWMAction *orig = BFWMActionCreateMoveWindow(DirLeft, 42);
   BFWMAction *clone = BFWMActionClone(orig);
   cbelt_assert(clone != nullptr);
   cbelt_assert(clone != orig);
   cbelt_assert(clone->type == ActionMoveWindow);
   /* Clone carries its own args storage with the same values */
   cbelt_assert(&clone->args != &orig->args);
   cbelt_assert(std::get<ActionArgsMoveWindow>(clone->args).direction ==
                std::get<ActionArgsMoveWindow>(orig->args).direction);
   auto &cargs = std::get<ActionArgsMoveWindow>(clone->args);
   cbelt_assert(cargs.direction == DirLeft);
   cbelt_assert(cargs.pixels == 42);
   /* Modify original — clone should be unaffected */
   std::get<ActionArgsMoveWindow>(orig->args).pixels = 99;
   cbelt_assert(cargs.pixels == 42);
   BFWMActionDestroy(orig);
   BFWMActionDestroy(clone);
   return TEST_SUCCESS;
}

CBELT_TEST(clone_spawn) {
   BFWMAction *orig = BFWMActionCreateSpawn("cmd.exe");
   BFWMAction *clone = BFWMActionClone(orig);
   cbelt_assert(clone != nullptr);
   cbelt_assert(clone->type == ActionSpawn);

   auto &cargs = std::get<ActionArgsSpawn>(clone->args);
   cbelt_assert(cargs.command == "cmd.exe");

   /* String should be a deep copy — mutating the original's copy must not
    * affect the clone */
   auto &oargs = std::get<ActionArgsSpawn>(orig->args);
   oargs.command = "mutated.exe";
   cbelt_assert(cargs.command == "cmd.exe");

   BFWMActionDestroy(orig);
   BFWMActionDestroy(clone);
   return TEST_SUCCESS;
}

CBELT_TEST(clone_layout) {
   BFWMAction *orig = BFWMActionCreateLayout("master-stack");
   BFWMAction *clone = BFWMActionClone(orig);
   cbelt_assert(clone != nullptr);
   cbelt_assert(clone->type == ActionLayout);

   auto &cargs = std::get<ActionArgsLayout>(clone->args);
   cbelt_assert(cargs.layout_name == "master-stack");

   /* String should be a deep copy */
   auto &oargs = std::get<ActionArgsLayout>(orig->args);
   oargs.layout_name = "mutated";
   cbelt_assert(cargs.layout_name == "master-stack");

   BFWMActionDestroy(orig);
   BFWMActionDestroy(clone);
   return TEST_SUCCESS;
}

CBELT_TEST(clone_custom) {
   BFWMAction *orig = BFWMActionCreateCustom("my_callback");
   BFWMAction *clone = BFWMActionClone(orig);
   cbelt_assert(clone != nullptr);
   cbelt_assert(clone->type == ActionCustom);

   auto &cargs = std::get<ActionArgsCustom>(clone->args);
   cbelt_assert(cargs.lua_callback == "my_callback");

   /* String should be a deep copy */
   auto &oargs = std::get<ActionArgsCustom>(orig->args);
   oargs.lua_callback = "mutated";
   cbelt_assert(cargs.lua_callback == "my_callback");

   BFWMActionDestroy(orig);
   BFWMActionDestroy(clone);
   return TEST_SUCCESS;
}

CBELT_TEST(clone_focus) {
   BFWMAction *orig = BFWMActionCreateFocus(DirDown);
   BFWMAction *clone = BFWMActionClone(orig);
   cbelt_assert(clone != nullptr);
   cbelt_assert(clone->type == ActionFocus);
   auto &cargs = std::get<ActionArgsFocus>(clone->args);
   cbelt_assert(cargs.direction == DirDown);
   BFWMActionDestroy(orig);
   BFWMActionDestroy(clone);
   return TEST_SUCCESS;
}

CBELT_TEST(clone_workspace) {
   BFWMAction *orig = BFWMActionCreateWorkspace(2, DirNext);
   BFWMAction *clone = BFWMActionClone(orig);
   cbelt_assert(clone != nullptr);
   cbelt_assert(clone->type == ActionWorkspace);
   auto &cargs = std::get<ActionArgsWorkspace>(clone->args);
   cbelt_assert(cargs.index == 2);
   cbelt_assert(cargs.direction == DirNext);
   BFWMActionDestroy(orig);
   BFWMActionDestroy(clone);
   return TEST_SUCCESS;
}

CBELT_TEST(clone_move_to_workspace) {
   BFWMAction *orig = BFWMActionCreateMoveToWorkspace(7);
   BFWMAction *clone = BFWMActionClone(orig);
   cbelt_assert(clone != nullptr);
   cbelt_assert(clone->type == ActionMoveToWorkspace);
   auto &cargs = std::get<ActionArgsMoveToWorkspace>(clone->args);
   cbelt_assert(cargs.index == 7);

   /* Modify original — clone should be unaffected */
   std::get<ActionArgsMoveToWorkspace>(orig->args).index = 99;
   cbelt_assert(cargs.index == 7);

   BFWMActionDestroy(orig);
   BFWMActionDestroy(clone);
   return TEST_SUCCESS;
}

/* =========================================================================
 * BFWMActionClone — null/empty src args edge cases
 * ========================================================================= */

CBELT_TEST(clone_with_null_args_is_safe) {
   /* ActionToggleFloat has no args; clone must handle it */
   BFWMAction *orig = BFWMActionCreateToggleFloat();
   BFWMAction *clone = BFWMActionClone(orig);
   cbelt_assert(clone != nullptr);
   cbelt_assert(clone->type == ActionToggleFloat);
   cbelt_assert(std::holds_alternative<std::monostate>(clone->args));
   BFWMActionDestroy(orig);
   BFWMActionDestroy(clone);
   return TEST_SUCCESS;
}
