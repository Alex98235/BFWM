#include "cbelt.h"
#include <windows.h>

#include "../../src/core/main_context.h"

#include "../../src/core/shared_state.h"

#include "../../src/core/worker_context.h"
#include "../../src/transaction/transaction.h"

CBELT_GROUP("BFWM_transaction")

namespace {

struct TestCtx {
   SharedState shared;
   WorkerContext worker;
   MainContext main;
};

inline auto create_minimal_ctx(void) -> TestCtx * {
   auto *c = new TestCtx();
   BFWMInit(&c->shared, &c->main);
   c->worker.shared = &c->shared;
   c->worker.main = &c->main;
   return c;
}

inline void destroy_minimal_ctx(TestCtx *ctx) {
   BFWMFree(&ctx->shared, &ctx->main);
   delete ctx;
}
} // namespace

CBELT_TEST(begin_sets_in_transaction) {
   TestCtx *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   cbelt_assert(ctx->main.transaction.IsInTransaction() == FALSE);

   ctx->main.transaction.Begin();
   cbelt_assert(ctx->main.transaction.IsInTransaction() == TRUE);
   cbelt_assert(ctx->main.transaction.OpCount() == 0);

   destroy_minimal_ctx(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(nested_begin_is_noop) {
   TestCtx *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->main.transaction.Begin();
   size_t cap_before = ctx->main.transaction.Capacity();

   ctx->main.transaction.Begin();
   cbelt_assert(ctx->main.transaction.IsInTransaction() == TRUE);
   cbelt_assert(ctx->main.transaction.OpCount() == 0);
   cbelt_assert(ctx->main.transaction.Capacity() == cap_before);

   destroy_minimal_ctx(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(commit_empty_transaction) {
   TestCtx *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->main.transaction.Begin();
   BFWMTransactionCommit(&ctx->shared, &ctx->main);
   cbelt_assert(ctx->main.transaction.IsInTransaction() == FALSE);
   cbelt_assert(ctx->main.transaction.OpCount() == 0);

   destroy_minimal_ctx(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(commit_without_begin_is_noop) {
   TestCtx *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   BFWMTransactionCommit(&ctx->shared, &ctx->main);
   cbelt_assert(ctx->main.transaction.IsInTransaction() == FALSE);
   cbelt_assert(ctx->main.transaction.OpCount() == 0);

   destroy_minimal_ctx(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(abort_clears_queue_and_flag) {
   TestCtx *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->main.transaction.Begin();

   ctx->main.transaction.QueueMoveWindow(reinterpret_cast<HWND>(1), 10, 20, 100,
                                         200);
   ctx->main.transaction.QueueFocus(reinterpret_cast<HWND>(2));
   cbelt_assert(ctx->main.transaction.OpCount() == 2);

   ctx->main.transaction.Abort();
   cbelt_assert(ctx->main.transaction.IsInTransaction() == FALSE);
   cbelt_assert(ctx->main.transaction.OpCount() == 0);

   destroy_minimal_ctx(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(abort_without_begin_is_safe) {
   TestCtx *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->main.transaction.Abort();
   cbelt_assert(ctx->main.transaction.IsInTransaction() == FALSE);

   destroy_minimal_ctx(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(queue_move_window) {
   TestCtx *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->main.transaction.Begin();

   ctx->main.transaction.QueueMoveWindow(reinterpret_cast<HWND>(42), 10, 20,
                                         800, 600);
   cbelt_assert(ctx->main.transaction.OpCount() == 1);

   PendingOperation *op = &ctx->main.transaction.OpAt(0);
   cbelt_assert(op->type == OP_MOVE_WINDOW);
   cbelt_assert(op->hwnd == reinterpret_cast<HWND>(42));
   cbelt_assert(op->op_types.move_rect.x == 10);
   cbelt_assert(op->op_types.move_rect.y == 20);
   cbelt_assert(op->op_types.move_rect.width == 800);
   cbelt_assert(op->op_types.move_rect.height == 600);

   destroy_minimal_ctx(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(queue_relayout) {
   TestCtx *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->main.transaction.Begin();

   Workspace fake_ws;

   ctx->main.transaction.QueueRelayout(&fake_ws);
   cbelt_assert(ctx->main.transaction.OpCount() == 1);

   PendingOperation *op = &ctx->main.transaction.OpAt(0);
   cbelt_assert(op->type == OP_RELAYOUT_WORKSPACE);
   cbelt_assert(op->hwnd == nullptr);
   cbelt_assert(op->op_types.workspace == &fake_ws);

   destroy_minimal_ctx(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(queue_border_color) {
   TestCtx *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->main.transaction.Begin();

   ctx->main.transaction.QueueBorderColor(reinterpret_cast<HWND>(7), 0x0000FF);
   cbelt_assert(ctx->main.transaction.OpCount() == 1);

   PendingOperation *op = &ctx->main.transaction.OpAt(0);
   cbelt_assert(op->type == OP_BORDER_COLOR);
   cbelt_assert(op->hwnd == reinterpret_cast<HWND>(7));
   cbelt_assert(op->op_types.border.color == 0x0000FF);

   destroy_minimal_ctx(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(queue_focus) {
   TestCtx *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->main.transaction.Begin();

   ctx->main.transaction.QueueFocus(reinterpret_cast<HWND>(99));
   cbelt_assert(ctx->main.transaction.OpCount() == 1);

   PendingOperation *op = &ctx->main.transaction.OpAt(0);
   cbelt_assert(op->type == OP_FOCUS_WINDOW);
   cbelt_assert(op->hwnd == reinterpret_cast<HWND>(99));

   destroy_minimal_ctx(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(queue_outside_transaction_is_noop) {
   TestCtx *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->main.transaction.QueueMoveWindow(reinterpret_cast<HWND>(1), 0, 0, 100,
                                         100);
   ctx->main.transaction.QueueFocus(reinterpret_cast<HWND>(2));
   ctx->main.transaction.QueueBorderColor(reinterpret_cast<HWND>(3), 0xFFFFFF);
   ctx->main.transaction.QueueRelayout(nullptr);

   cbelt_assert(ctx->main.transaction.OpCount() == 0);

   destroy_minimal_ctx(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(multiple_ops_maintain_order) {
   TestCtx *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->main.transaction.Begin();

   ctx->main.transaction.QueueMoveWindow(reinterpret_cast<HWND>(1), 0, 0, 100,
                                         100);
   ctx->main.transaction.QueueBorderColor(reinterpret_cast<HWND>(2), 0xFF0000);
   ctx->main.transaction.QueueFocus(reinterpret_cast<HWND>(3));
   ctx->main.transaction.QueueRelayout(
       reinterpret_cast<Workspace *>(static_cast<intptr_t>(0x1234)));

   cbelt_assert(ctx->main.transaction.OpCount() == 4);
   cbelt_assert(ctx->main.transaction.OpAt(0).type == OP_MOVE_WINDOW);
   cbelt_assert(ctx->main.transaction.OpAt(1).type == OP_BORDER_COLOR);
   cbelt_assert(ctx->main.transaction.OpAt(2).type == OP_FOCUS_WINDOW);
   cbelt_assert(ctx->main.transaction.OpAt(3).type == OP_RELAYOUT_WORKSPACE);

   destroy_minimal_ctx(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(queue_capacity_expands_beyond_initial) {
   TestCtx *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->main.transaction.Begin();

   for (int i = 0; i < 33; i++) {
      ctx->main.transaction.QueueMoveWindow(
          reinterpret_cast<HWND>(static_cast<intptr_t>(i + 1)), i, 0, 100, 100);
   }

   cbelt_assert(ctx->main.transaction.OpCount() == 33);
   cbelt_assert(ctx->main.transaction.Capacity() >= 33);

   for (int i = 0; i < 33; i++) {
      cbelt_assert(ctx->main.transaction.OpAt(i).type == OP_MOVE_WINDOW);
      cbelt_assert(ctx->main.transaction.OpAt(i).hwnd ==
                   reinterpret_cast<HWND>(static_cast<intptr_t>(i + 1)));
   }

   destroy_minimal_ctx(ctx);
   return TEST_SUCCESS;
}
