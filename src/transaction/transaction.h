/**
 * @file transaction.h
 * @brief Transaction-based state batching for window operations.
 *
 * Instead of calling Win32 APIs (SetWindowPos, SwitchToThisWindow,
 * OverlaySetColor) immediately as each handler runs, operations are
 * queued during a transaction and flushed atomically in priority order:
 *
 *   1. Window positions             (OP_MOVE_WINDOW)
 *   2. Workspace relayouts          (OP_RELAYOUT_WORKSPACE)
 *   3. Border colors                (OP_BORDER_COLOR)
 *   4. Window visibility / cloaking (OP_CLOAK_WINDOW)
 *   5. Focus                        (OP_FOCUS_WINDOW)
 *
 * This (hopefully) eliminates the taskbar flashing and interleaved redraws that
 * occur when move, border, and focus calls are made independently.
 *
 * Usage:
 * @code
 *    ctx->transaction.Begin();
 *    DispatchKeybind(ctx, &queued);     // handlers queue ops
 *    BFWMTransactionCommit(ctx);      // flush to Win32 in priority order
 * @endcode
 */

#ifndef BFWM_TRANSACTION_H
#define BFWM_TRANSACTION_H

#include "operation.h"
#include <vector>
#include <windows.h>

struct BFWMContext;

/**
 * @brief Dynamic array of pending operations for a single transaction.
 */
class TransactionState {
 public:
   /// Reserve the same initial capacity as the old raw-array growth (32).
   TransactionState() { ops_.reserve(32); }

   // Lifecycle
   void Begin(); // nested begin is a no-op
   void Abort(); // Debug log + clear ops + end transaction

   // Queue operations. Each is a no-op when NOT in a transaction
   /// Queue a window-move operation.
   void QueueMoveWindow(HWND hwnd, int x, int y, int width, int height);
   /// Queue a workspace re-layout operation.
   /// At commit time, ApplyLayout is called on this workspace.
   void QueueRelayout(Workspace *workspace);
   /// Queue a border-color change.
   void QueueBorderColor(HWND hwnd, COLORREF color);
   /// Queue a focus operation.
   void QueueFocus(HWND hwnd);
   /// Queue a window cloaking operation (Pass 1, before relayouts/moves).
   void QueueCloak(HWND hwnd, BOOL cloak);
   /// Queue a z-order change.
   void QueueZOrder(HWND hwnd, HWND insert_after);
   /// Queue a show-window operation.
   void QueueShowWindow(HWND hwnd);
   /// Queue a hide-window operation.
   void QueueHideWindow(HWND hwnd);
   /// Queue a window style change (GWL_STYLE + GWL_EXSTYLE).
   void QueueSetWindowStyle(HWND hwnd, DWORD style, DWORD exstyle);
   /// Queue a border redraw.
   void QueueRedrawBorder(HWND hwnd);
   /// Queue a content area redraw.
   void QueueRedrawContent(HWND hwnd);
   /// Queue a minimize operation.
   void QueueMinimizeWindow(HWND hwnd);
   /// Queue a bar-visibility operation.
   void QueueBarVisibility(HWND hwnd, BOOL enabled);

   // Access for the free BFWMTransactionCommit orchestration (keep minimal)
   [[nodiscard]] auto IsInTransaction() const -> BOOL {
      return in_transaction_;
   }
   void SetInTransaction(BOOL active) { in_transaction_ = active; }
   [[nodiscard]] auto OpCount() const -> size_t { return ops_.size(); }
   [[nodiscard]] auto IsEmpty() const -> BOOL {
      return static_cast<BOOL>(ops_.empty());
   }
   [[nodiscard]] auto Capacity() const -> size_t { return ops_.capacity(); }
   auto OpAt(size_t index) -> PendingOperation & { return ops_[index]; }
   void ClearOps() { ops_.clear(); }

 private:
   std::vector<PendingOperation> ops_;
   BOOL in_transaction_ = FALSE;
};

/**
 * @brief Commit the current transaction.
 *
 * Flushes all queued operations to Win32 in priority order:
 *   moves → relayouts → borders → focus
 *
 * After commit the pending queue is cleared, the transaction is ended,
 * and all window changes happen in a single batch.
 *
 * @param ctx The BFWM context
 */
void BFWMTransactionCommit(struct BFWMContext *ctx);

/**
 * @brief Drain the SPSC event queue and process all pending events.
 *
 * Pops EventRecords from ctx->event_queue. For each event:
 *   - Applies internal-state tracking (no Win32 calls)
 *   - Begins a transaction, queues the appropriate operations, commits
 *
 * @param ctx The BFWM context
 */
void DrainEventQueue(struct BFWMContext *ctx);

#endif /* BFWM_TRANSACTION_H */
