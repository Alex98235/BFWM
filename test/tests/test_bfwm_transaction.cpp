#include "cbelt.h"
#include <windows.h>

#include "../../src/core/bfwm_context.h"
#include "../../src/transaction/transaction.h"

CBELT_GROUP("BFWM_transaction")

namespace {

inline auto create_minimal_ctx(void) -> struct BFWMContext * {
   return BFWMContextInit();
}
} // namespace

CBELT_TEST(begin_sets_in_transaction) {
   struct BFWMContext *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   cbelt_assert(ctx->transaction.IsInTransaction() == FALSE);

   ctx->transaction.Begin();
   cbelt_assert(ctx->transaction.IsInTransaction() == TRUE);
   cbelt_assert(ctx->transaction.OpCount() == 0);

   BFWMContextFree(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(nested_begin_is_noop) {
   struct BFWMContext *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->transaction.Begin();
   size_t cap_before = ctx->transaction.Capacity();

   ctx->transaction.Begin();
   cbelt_assert(ctx->transaction.IsInTransaction() == TRUE);
   cbelt_assert(ctx->transaction.OpCount() == 0);
   cbelt_assert(ctx->transaction.Capacity() == cap_before);

   BFWMContextFree(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(commit_empty_transaction) {
   struct BFWMContext *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->transaction.Begin();
   BFWMTransactionCommit(ctx);
   cbelt_assert(ctx->transaction.IsInTransaction() == FALSE);
   cbelt_assert(ctx->transaction.OpCount() == 0);

   BFWMContextFree(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(commit_without_begin_is_noop) {
   struct BFWMContext *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   BFWMTransactionCommit(ctx);
   cbelt_assert(ctx->transaction.IsInTransaction() == FALSE);
   cbelt_assert(ctx->transaction.OpCount() == 0);

   BFWMContextFree(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(abort_clears_queue_and_flag) {
   struct BFWMContext *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->transaction.Begin();

   ctx->transaction.QueueMoveWindow(reinterpret_cast<HWND>(1), 10, 20, 100, 200);
   ctx->transaction.QueueFocus(reinterpret_cast<HWND>(2));
   cbelt_assert(ctx->transaction.OpCount() == 2);

   ctx->transaction.Abort();
   cbelt_assert(ctx->transaction.IsInTransaction() == FALSE);
   cbelt_assert(ctx->transaction.OpCount() == 0);

   BFWMContextFree(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(abort_without_begin_is_safe) {
   struct BFWMContext *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->transaction.Abort();
   cbelt_assert(ctx->transaction.IsInTransaction() == FALSE);

   BFWMContextFree(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(queue_move_window) {
   struct BFWMContext *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->transaction.Begin();

   ctx->transaction.QueueMoveWindow(reinterpret_cast<HWND>(42), 10, 20, 800, 600);
   cbelt_assert(ctx->transaction.OpCount() == 1);

   PendingOperation *op = &ctx->transaction.OpAt(0);
   cbelt_assert(op->type == OP_MOVE_WINDOW);
   cbelt_assert(op->hwnd == reinterpret_cast<HWND>(42));
   cbelt_assert(op->op_types.move_rect.x == 10);
   cbelt_assert(op->op_types.move_rect.y == 20);
   cbelt_assert(op->op_types.move_rect.width == 800);
   cbelt_assert(op->op_types.move_rect.height == 600);

   BFWMContextFree(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(queue_relayout) {
   struct BFWMContext *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->transaction.Begin();

   Workspace fake_ws;

   ctx->transaction.QueueRelayout(&fake_ws);
   cbelt_assert(ctx->transaction.OpCount() == 1);

   PendingOperation *op = &ctx->transaction.OpAt(0);
   cbelt_assert(op->type == OP_RELAYOUT_WORKSPACE);
   cbelt_assert(op->hwnd == nullptr);
   cbelt_assert(op->op_types.workspace == &fake_ws);

   BFWMContextFree(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(queue_border_color) {
   struct BFWMContext *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->transaction.Begin();

   ctx->transaction.QueueBorderColor(reinterpret_cast<HWND>(7), 0x0000FF);
   cbelt_assert(ctx->transaction.OpCount() == 1);

   PendingOperation *op = &ctx->transaction.OpAt(0);
   cbelt_assert(op->type == OP_BORDER_COLOR);
   cbelt_assert(op->hwnd == reinterpret_cast<HWND>(7));
   cbelt_assert(op->op_types.border.color == 0x0000FF);

   BFWMContextFree(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(queue_focus) {
   struct BFWMContext *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->transaction.Begin();

   ctx->transaction.QueueFocus(reinterpret_cast<HWND>(99));
   cbelt_assert(ctx->transaction.OpCount() == 1);

   PendingOperation *op = &ctx->transaction.OpAt(0);
   cbelt_assert(op->type == OP_FOCUS_WINDOW);
   cbelt_assert(op->hwnd == reinterpret_cast<HWND>(99));

   BFWMContextFree(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(queue_outside_transaction_is_noop) {
   struct BFWMContext *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->transaction.QueueMoveWindow(reinterpret_cast<HWND>(1), 0, 0, 100, 100);
   ctx->transaction.QueueFocus(reinterpret_cast<HWND>(2));
   ctx->transaction.QueueBorderColor(reinterpret_cast<HWND>(3), 0xFFFFFF);
   ctx->transaction.QueueRelayout(nullptr);

   cbelt_assert(ctx->transaction.OpCount() == 0);

   BFWMContextFree(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(multiple_ops_maintain_order) {
   struct BFWMContext *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->transaction.Begin();

   ctx->transaction.QueueMoveWindow(reinterpret_cast<HWND>(1), 0, 0, 100, 100);
   ctx->transaction.QueueBorderColor(reinterpret_cast<HWND>(2), 0xFF0000);
   ctx->transaction.QueueFocus(reinterpret_cast<HWND>(3));
   ctx->transaction.QueueRelayout(reinterpret_cast<Workspace *>(static_cast<intptr_t>(0x1234)));

   cbelt_assert(ctx->transaction.OpCount() == 4);
   cbelt_assert(ctx->transaction.OpAt(0).type == OP_MOVE_WINDOW);
   cbelt_assert(ctx->transaction.OpAt(1).type == OP_BORDER_COLOR);
   cbelt_assert(ctx->transaction.OpAt(2).type == OP_FOCUS_WINDOW);
   cbelt_assert(ctx->transaction.OpAt(3).type == OP_RELAYOUT_WORKSPACE);

   BFWMContextFree(ctx);
   return TEST_SUCCESS;
}

CBELT_TEST(queue_capacity_expands_beyond_initial) {
   struct BFWMContext *ctx = create_minimal_ctx();
   cbelt_assert_not_null(ctx);

   ctx->transaction.Begin();

   for (int i = 0; i < 33; i++) {
      ctx->transaction.QueueMoveWindow(reinterpret_cast<HWND>(static_cast<intptr_t>(i + 1)), i, 0, 100,
                                       100);
   }

   cbelt_assert(ctx->transaction.OpCount() == 33);
   cbelt_assert(ctx->transaction.Capacity() >= 33);

   for (int i = 0; i < 33; i++) {
      cbelt_assert(ctx->transaction.OpAt(i).type == OP_MOVE_WINDOW);
      cbelt_assert(ctx->transaction.OpAt(i).hwnd == reinterpret_cast<HWND>(static_cast<intptr_t>(i + 1)));
   }

   BFWMContextFree(ctx);
   return TEST_SUCCESS;
}
