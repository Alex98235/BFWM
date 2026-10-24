#include "cbelt.h"
#include <windows.h>

#include "../../src/core/action_dispatcher.h"

CBELT_GROUP("action_dispatcher")

/* Tracks which handler was called and what it received */
namespace {

int g_last_handler_called = -1;
BFWMAction *g_last_handler_action = nullptr;
SharedState *g_last_handler_shared = nullptr;
int g_mock_return_value = 0;

inline void reset_trace() {
   g_last_handler_called = -1;
   g_last_handler_action = nullptr;
   g_last_handler_shared = nullptr;
   g_mock_return_value = 0;
}

inline auto mock_handler_kill(SharedState *shared, MainContext * /*main*/,
                              BFWMAction *action) -> int {
   g_last_handler_called = ActionKillActive;
   g_last_handler_action = action;
   g_last_handler_shared = shared;
   return g_mock_return_value;
}

inline auto mock_handler_spawn(SharedState *shared, MainContext * /*main*/,
                               BFWMAction *action) -> int {
   g_last_handler_called = ActionSpawn;
   g_last_handler_action = action;
   g_last_handler_shared = shared;
   return g_mock_return_value;
}

inline auto mock_handler_custom(SharedState *shared, MainContext * /*main*/,
                                BFWMAction *action) -> int {
   g_last_handler_called = ActionCustom;
   g_last_handler_action = action;
   g_last_handler_shared = shared;
   return g_mock_return_value;
}
} // namespace

CBELT_TEST(register_and_dispatch_kill_active) {
   reset_trace();

   ActionDispatcherRegister(ActionKillActive, mock_handler_kill);

   BFWMAction *act = BFWMActionCreateKillActive();
   BFWMAction *saved_act = act;

   int dummy_ctx = 42;
   auto *fake_ctx = reinterpret_cast<SharedState *>(&dummy_ctx);
   int result = ActionDispatcherDispatch(
       fake_ctx, reinterpret_cast<MainContext *>(&dummy_ctx), act);

   BFWMActionDestroy(act);
   ActionDispatcherRegister(ActionKillActive, nullptr);

   cbelt_assert_true(0 == result);
   cbelt_assert_true(ActionKillActive == g_last_handler_called);
   cbelt_assert_true(saved_act == g_last_handler_action);
   cbelt_assert_true(fake_ctx == g_last_handler_shared);

   return TEST_SUCCESS;
}

CBELT_TEST(register_and_dispatch_spawn) {
   reset_trace();

   ActionDispatcherRegister(ActionSpawn, mock_handler_spawn);

   BFWMAction *act = BFWMActionCreateSpawn("notepad.exe");
   BFWMAction *saved_act = act;

   int dummy_ctx = 42;
   auto *fake_ctx = reinterpret_cast<SharedState *>(&dummy_ctx);
   int result = ActionDispatcherDispatch(
       fake_ctx, reinterpret_cast<MainContext *>(&dummy_ctx), act);

   BFWMActionDestroy(act);
   ActionDispatcherRegister(ActionSpawn, nullptr);

   cbelt_assert_true(0 == result);
   cbelt_assert_true(ActionSpawn == g_last_handler_called);
   cbelt_assert_true(saved_act == g_last_handler_action);
   cbelt_assert_true(fake_ctx == g_last_handler_shared);

   return TEST_SUCCESS;
}

CBELT_TEST(handler_receives_context) {
   reset_trace();

   int dummy_ctx = 42;
   auto *fake_ctx = reinterpret_cast<SharedState *>(&dummy_ctx);

   ActionDispatcherRegister(ActionKillActive, mock_handler_kill);

   BFWMAction *act = BFWMActionCreateKillActive();
   ActionDispatcherDispatch(fake_ctx,
                            reinterpret_cast<MainContext *>(&dummy_ctx), act);

   BFWMActionDestroy(act);
   ActionDispatcherRegister(ActionKillActive, nullptr);

   cbelt_assert_true(fake_ctx == g_last_handler_shared);

   return TEST_SUCCESS;
}

CBELT_TEST(handler_return_value_propagated) {
   reset_trace();

   ActionDispatcherRegister(ActionKillActive, mock_handler_kill);

   BFWMAction *act = BFWMActionCreateKillActive();

   int dummy_ctx = 42;
   auto *fake_ctx = reinterpret_cast<SharedState *>(&dummy_ctx);
   g_mock_return_value = 42;
   int result = ActionDispatcherDispatch(
       fake_ctx, reinterpret_cast<MainContext *>(&dummy_ctx), act);
   cbelt_assert_true(42 == result);

   g_mock_return_value = -7;
   result = ActionDispatcherDispatch(
       fake_ctx, reinterpret_cast<MainContext *>(&dummy_ctx), act);
   cbelt_assert_true(-7 == result);

   BFWMActionDestroy(act);
   ActionDispatcherRegister(ActionKillActive, nullptr);

   return TEST_SUCCESS;
}

CBELT_TEST(dispatch_with_no_handler_returns_minus_one) {
   BFWMAction *act = BFWMActionCreateSplit();

   int result = ActionDispatcherDispatch(nullptr, nullptr, act);

   BFWMActionDestroy(act);

   cbelt_assert_true(-1 == result);

   return TEST_SUCCESS;
}

CBELT_TEST(dispatch_after_unregister_returns_minus_one) {
   reset_trace();
   ActionDispatcherRegister(ActionFullscreen, mock_handler_kill);

   BFWMAction *act = BFWMActionCreateFullscreen();

   int result = ActionDispatcherDispatch(nullptr, nullptr, act);
   cbelt_assert_true(0 == result);

   ActionDispatcherRegister(ActionFullscreen, nullptr);

   result = ActionDispatcherDispatch(nullptr, nullptr, act);
   BFWMActionDestroy(act);

   cbelt_assert_true(-1 == result);

   return TEST_SUCCESS;
}

CBELT_TEST(re_register_overwrites_previous_handler) {
   reset_trace();

   ActionDispatcherRegister(ActionSpawn, mock_handler_kill);

   BFWMAction *act = BFWMActionCreateSpawn("test.exe");

   int dummy_ctx = 42;
   auto *fake_ctx = reinterpret_cast<SharedState *>(&dummy_ctx);
   ActionDispatcherDispatch(fake_ctx,
                            reinterpret_cast<MainContext *>(&dummy_ctx), act);
   cbelt_assert_true(ActionKillActive == g_last_handler_called);

   ActionDispatcherRegister(ActionSpawn, mock_handler_spawn);

   ActionDispatcherDispatch(fake_ctx,
                            reinterpret_cast<MainContext *>(&dummy_ctx), act);
   BFWMActionDestroy(act);
   ActionDispatcherRegister(ActionSpawn, nullptr);

   cbelt_assert_true(ActionSpawn == g_last_handler_called);

   return TEST_SUCCESS;
}

CBELT_TEST(multiple_handlers_independent) {
   reset_trace();

   ActionDispatcherRegister(ActionKillActive, mock_handler_kill);
   ActionDispatcherRegister(ActionCustom, mock_handler_custom);

   BFWMAction *act_kill = BFWMActionCreateKillActive();
   BFWMAction *act_custom = BFWMActionCreateCustom("fn");

   int dummy_ctx = 42;
   auto *fake_ctx = reinterpret_cast<SharedState *>(&dummy_ctx);
   ActionDispatcherDispatch(
       fake_ctx, reinterpret_cast<MainContext *>(&dummy_ctx), act_kill);
   cbelt_assert_true(ActionKillActive == g_last_handler_called);

   ActionDispatcherDispatch(
       fake_ctx, reinterpret_cast<MainContext *>(&dummy_ctx), act_custom);
   cbelt_assert_true(ActionCustom == g_last_handler_called);

   BFWMActionDestroy(act_kill);
   BFWMActionDestroy(act_custom);
   ActionDispatcherRegister(ActionKillActive, nullptr);
   ActionDispatcherRegister(ActionCustom, nullptr);

   return TEST_SUCCESS;
}

CBELT_TEST(dispatch_out_of_range_type_returns_minus_one) {
   BFWMAction bad_action;
   // Deliberately tests an out-of-range value.
   // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
   bad_action.type = static_cast<BFWMActionType>(99);
   bad_action.args = std::monostate{};

   int result = ActionDispatcherDispatch(nullptr, nullptr, &bad_action);
   cbelt_assert_true(-1 == result);

   return TEST_SUCCESS;
}
