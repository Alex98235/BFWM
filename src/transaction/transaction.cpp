/**
 * @file BFWM_transaction.c
 * @brief Implementation of the transaction-based state batching system.
 *
 * During a transaction, window operations (move, border, focus, etc.) are
 * queued as PendingOperation entries and flushed to Win32 in priority order
 * during BFWMTransactionCommit().
 */

#include "transaction.h"

#include "../bar/bar.h"
#include "../core/bfwm_context.h"
#include "../core/sync.h"
#include "../dpi/dpi.h"
#include "../logging/logger.h"
#include "../window/border/overlay.h"
#include "../window/cloaking.h"
#include "../window/events/events.h"
#include "../window/window.h"
#include "../workspace/placement.h"
#include "../workspace/workspace.h"
#include "operation.h"
#include "spsc_queue.h"
#include <cstddef>
#include <cstdlib>
#include <minwindef.h>
#include <windef.h>
#include <windows.h>
#include <winerror.h>
#include <winnt.h>

enum { WM_CLOSE_TIMEOUT_MS = 2000 };

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

namespace {

/**
 * @brief Apply a single pending operation to the Win32/WinRT APIs.
 *
 * This is called from the commit loop in priority order.
 */
/**
 * @brief Apply a single OP_BAR_VISIBILITY operation.
 *
 * Shows or hides the workspace bar on the monitor containing the window.
 */
inline void ApplyBarVisibility(struct BFWMContext *ctx,
                               PendingOperation *operation) {
   if (ctx == nullptr)
      return;
   Workspace *visibility_ws = FindWorkspaceByHwnd(ctx, operation->hwnd);
   if (visibility_ws == nullptr)
      return;
   Monitor *mon = FindMonitorByWorkspace(ctx, visibility_ws);
   if ((mon != nullptr) && (mon->GetBar() != nullptr)) {
      if (operation->op_types.bar_visibility.enabled != 0) {
         mon->GetBar()->Show();
      } else {
         mon->GetBar()->Hide();
      }
   }
}

/**
 * @brief Mark a window's border overlay ring dirty.
 *
 * The ring is converged once per commit by FlushOverlayRings (one wait + sync
 * + paint). No per-op sync or paint happens here. No-op when the window has no
 * overlay.
 */
inline void MarkWindowOverlayDirty(struct BFWMContext *ctx, HWND hwnd) {
   Window *win = ctx->windows->FindByHwnd(hwnd);
   if (win != nullptr)
      win->MarkOverlayDirty();
}

/* Commit-end overlay flush: converge every dirty ring (color + position) with
 * one wait + sync + paint, then present everything with ONE DirectComposition
 * commit. Runs after the passes so the last color wins and the async layout
 * moves have been issued. */
inline void FlushOverlayRings(struct BFWMContext *ctx) {
   for (const auto &window : ctx->windows->Windows()) {
      Window *win = window.get();
      if (win != nullptr) {
         /* Per-commit reconcile: the guaranteed backstop for z-drift and
          * visibility drift, immune to dropped queue events and to BFWM's
          * own changes never generating events (own-process events skipped).
          * Pure/cheap — re-asserts z or marks dirty so the flush below
          * converges. Runs BEFORE FlushOverlay. */
         win->ReconcileOverlay(
             static_cast<BOOL>((win->IsFullscreen() != 0) ||
                               (win->IsUnmanagedFullscreen() != 0)),
             FALSE);
         win->FlushOverlay();
      }
   }
}

inline void ApplyOperation(struct BFWMContext *ctx,
                           PendingOperation *operation) {
   switch (operation->type) {

   case OP_MOVE_WINDOW: {
      RECT const r = {.left = operation->op_types.move_rect.x,
                      .top = operation->op_types.move_rect.y,
                      .right = operation->op_types.move_rect.x +
                               operation->op_types.move_rect.width,
                      .bottom = operation->op_types.move_rect.y +
                                operation->op_types.move_rect.height};
      // OP_OVERLAY_MOVE: geometry converges at the commit-end flush, which
      // waits for the async move to land at this rect. Ungated single-shot
      // issue (no backpressure, no moves_in_flight) — see placement.h.
      Window *win = ctx->windows->FindByHwnd(operation->hwnd);
      if (win != nullptr)
         PlacementIssueMove(win, &r, ctx);
      break;
   }

   case OP_RELAYOUT_WORKSPACE:
      if (operation->op_types.workspace != nullptr)
         operation->op_types.workspace->ApplyLayout(ctx);

      break;

   case OP_BORDER_COLOR:
      if (IsWindowVisible(operation->hwnd) != 0) {
         // OP_OVERLAY_PAINT: the ring replaces the DWM border color path.
         Window *win = ctx->windows->FindByHwnd(operation->hwnd);
         if (win != nullptr) {
            win->SetOverlayColor(operation->op_types.border.color);
         }
      }
      break;

   case OP_CLOAK_WINDOW: {
      BOOL const desired = operation->op_types.cloak.cloaked;
      Window *win = ctx->windows->FindByHwnd(operation->hwnd);
      BOOL const prev = (win != nullptr) ? win->IsCloaked() : FALSE;
      HRESULT const result = SetWindowCloakState(operation->hwnd, desired);
      if (SUCCEEDED(result)) {
         if (win != nullptr)
            win->SetCloaked(desired);
      } else {
         // Revert the flag to its prior state so cloak bookkeeping stays
         // consistent with reality. A TYPE_E_ELEMENTNOTFOUND means the shell
         // has no view for this HWND (reuse / post-wake reset): mark it stale
         // so ReclaimStaleWindows can purge it instead of retrying forever.
         if (win != nullptr) {
            win->SetCloaked(prev);
            if (result == TYPE_E_ELEMENTNOTFOUND)
               win->SetStale(TRUE);
         }
         Warn("OP_CLOAK_WINDOW: SetWindowCloakState(%p, %d) failed "
              "(result=0x%08lx)",
              operation->hwnd, desired, (unsigned long)result);
      }
      // Visibility is keyed on cloak state inside the module; the flush
      // hides/shows the ring with the cloaking.
      MarkWindowOverlayDirty(ctx, operation->hwnd);
      break;
   }

   case OP_FOCUS_WINDOW: {
      // FocusWindowImmediate returns FALSE when it declined to steal the
      // foreground from a fullscreen window; keep focused_hwnd unchanged in
      // that case so BFWM still considers the fullscreen window focused.
      if (FocusWindowImmediate(operation->hwnd, ctx) == TRUE)
         ctx->focused_hwnd = operation->hwnd;

      break;
   }

   case OP_CLOSE_WINDOW: {
      DWORD_PTR result;
      SendMessageTimeout(operation->hwnd, WM_CLOSE, 0, 0,
                         SMTO_ABORTIFHUNG | SMTO_BLOCK, WM_CLOSE_TIMEOUT_MS,
                         &result);
      break;
   }

   case OP_ZORDER: {
      SetWindowPos(
          operation->hwnd, operation->op_types.z_order.insert_after, 0, 0, 0, 0,
          SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
      break;
   }

   case OP_SHOW_WINDOW:
      if (operation->op_types.show_hide.visible != 0) {
         ShowWindow(operation->hwnd, SW_SHOW);
      } else {
         ShowWindow(operation->hwnd, SW_HIDE);
      }
      break;

   case OP_HIDE_WINDOW:
      ShowWindow(operation->hwnd, SW_HIDE);
      break;

   case OP_SET_WINDOW_STYLE:
      SetWindowLong(operation->hwnd, GWL_STYLE,
                    (LONG)operation->op_types.window_style.style);
      SetWindowLong(operation->hwnd, GWL_EXSTYLE,
                    (LONG)operation->op_types.window_style.exstyle);
      SetWindowPos(operation->hwnd, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                       SWP_FRAMECHANGED);
      break;

   case OP_REDRAW_BORDER:
      RedrawWindow(operation->hwnd, nullptr, nullptr,
                   RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
      break;

   case OP_REDRAW_CONTENT:
      RedrawWindow(operation->hwnd, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_UPDATENOW);
      break;

   case OP_MINIMIZE_WINDOW:
      ShowWindow(operation->hwnd, SW_MINIMIZE);
      break;

   case OP_UPDATE_FOCUS_TRACKING:
      // Internal state only — handled inline in DrainEventQueue.
      // This op type exists so the commit pass ordering can sequence
      // focus-tracking state relative to other operations.
      break;

   case OP_BAR_VISIBILITY:
      ApplyBarVisibility(ctx, operation);
      break;
   }
}

auto MakeMoveWindowOperation(HWND hwnd, int x, int y, int width, int height)
    -> PendingOperation {
   PendingOperation operation = {};
   operation.type = OP_MOVE_WINDOW;
   operation.hwnd = hwnd;
   operation.op_types.move_rect = {
       .x = x, .y = y, .width = width, .height = height};
   return operation;
}

auto MakeRelayoutOperation(Workspace *workspace) -> PendingOperation {
   PendingOperation operation = {};
   operation.type = OP_RELAYOUT_WORKSPACE;
   operation.hwnd = nullptr;
   operation.op_types.workspace = workspace;
   return operation;
}

auto MakeBorderColorOperation(HWND hwnd, COLORREF color) -> PendingOperation {
   PendingOperation operation = {};
   operation.type = OP_BORDER_COLOR;
   operation.hwnd = hwnd;
   operation.op_types.border = {color};
   return operation;
}

auto MakeFocusOperation(HWND hwnd) -> PendingOperation {
   PendingOperation operation = {};
   operation.type = OP_FOCUS_WINDOW;
   operation.hwnd = hwnd;
   return operation;
}

auto MakeCloakOperation(HWND hwnd, BOOL cloak) -> PendingOperation {
   PendingOperation operation = {};
   operation.type = OP_CLOAK_WINDOW;
   operation.hwnd = hwnd;
   operation.op_types.cloak = {cloak};
   return operation;
}

auto MakeZOrderOperation(HWND hwnd, HWND insert_after) -> PendingOperation {
   PendingOperation operation = {};
   operation.type = OP_ZORDER;
   operation.hwnd = hwnd;
   operation.op_types.z_order = {insert_after};
   return operation;
}

auto MakeShowOperation(HWND hwnd, BOOL visible) -> PendingOperation {
   PendingOperation operation = {};
   operation.type = OP_SHOW_WINDOW;
   operation.hwnd = hwnd;
   operation.op_types.show_hide = {visible};
   return operation;
}

auto MakeSetWindowStyleOperation(HWND hwnd, DWORD style, DWORD exstyle)
    -> PendingOperation {
   PendingOperation operation = {};
   operation.type = OP_SET_WINDOW_STYLE;
   operation.hwnd = hwnd;
   operation.op_types.window_style = {.style = style, .exstyle = exstyle};
   return operation;
}

auto MakeRedrawBorderOperation(HWND hwnd) -> PendingOperation {
   PendingOperation operation = {};
   operation.type = OP_REDRAW_BORDER;
   operation.hwnd = hwnd;
   return operation;
}

auto MakeRedrawContentOperation(HWND hwnd) -> PendingOperation {
   PendingOperation operation = {};
   operation.type = OP_REDRAW_CONTENT;
   operation.hwnd = hwnd;
   return operation;
}

auto MakeMinimizeWindowOperation(HWND hwnd) -> PendingOperation {
   PendingOperation operation = {};
   operation.type = OP_MINIMIZE_WINDOW;
   operation.hwnd = hwnd;
   return operation;
}

auto MakeBarVisibilityOperation(HWND hwnd, BOOL enabled) -> PendingOperation {
   PendingOperation operation = {};
   operation.type = OP_BAR_VISIBILITY;
   operation.hwnd = hwnd;
   operation.op_types.bar_visibility = {enabled};
   return operation;
}

} // namespace

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void TransactionState::Begin() {
   if (in_transaction_ == TRUE)
      return; /* nested begin is a no-operation */
   in_transaction_ = TRUE;
}

void TransactionState::QueueMoveWindow(HWND hwnd, int x, int y, int width,
                                       int height) {
   if (in_transaction_ == FALSE)
      return;
   ops_.push_back(MakeMoveWindowOperation(hwnd, x, y, width, height));
}

void TransactionState::QueueRelayout(Workspace *workspace) {
   if (in_transaction_ == FALSE)
      return;
   ops_.push_back(MakeRelayoutOperation(workspace));
}

void TransactionState::QueueBorderColor(HWND hwnd, COLORREF color) {
   if (in_transaction_ == FALSE)
      return;
   ops_.push_back(MakeBorderColorOperation(hwnd, color));
}

void TransactionState::QueueFocus(HWND hwnd) {
   if (in_transaction_ == FALSE)
      return;
   ops_.push_back(MakeFocusOperation(hwnd));
}

void TransactionState::QueueCloak(HWND hwnd, BOOL cloak) {
   if (in_transaction_ == FALSE)
      return;
   ops_.push_back(MakeCloakOperation(hwnd, cloak));
}

void TransactionState::QueueZOrder(HWND hwnd, HWND insert_after) {
   if (in_transaction_ == FALSE)
      return;
   ops_.push_back(MakeZOrderOperation(hwnd, insert_after));
}

void TransactionState::QueueShowWindow(HWND hwnd) {
   if (in_transaction_ == FALSE)
      return;
   ops_.push_back(MakeShowOperation(hwnd, TRUE));
}

void TransactionState::QueueHideWindow(HWND hwnd) {
   if (in_transaction_ == FALSE)
      return;
   ops_.push_back(MakeShowOperation(hwnd, FALSE));
}

void TransactionState::QueueSetWindowStyle(HWND hwnd, DWORD style,
                                           DWORD exstyle) {
   if (in_transaction_ == FALSE)
      return;
   ops_.push_back(MakeSetWindowStyleOperation(hwnd, style, exstyle));
}

void TransactionState::QueueRedrawBorder(HWND hwnd) {
   if (in_transaction_ == FALSE)
      return;
   ops_.push_back(MakeRedrawBorderOperation(hwnd));
}

void TransactionState::QueueRedrawContent(HWND hwnd) {
   if (in_transaction_ == FALSE)
      return;
   ops_.push_back(MakeRedrawContentOperation(hwnd));
}

void TransactionState::QueueMinimizeWindow(HWND hwnd) {
   if (in_transaction_ == FALSE)
      return;
   ops_.push_back(MakeMinimizeWindowOperation(hwnd));
}

void TransactionState::QueueBarVisibility(HWND hwnd, BOOL enabled) {
   if (in_transaction_ == FALSE)
      return;
   ops_.push_back(MakeBarVisibilityOperation(hwnd, enabled));
}

void TransactionState::Abort() {
   Debug("Transaction: ABORT (%zu ops discarded)", ops_.size());
   ops_.clear();
   in_transaction_ = FALSE;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void BFWMTransactionCommit(struct BFWMContext *ctx) {
   if (ctx->transaction.IsInTransaction() == FALSE)
      return;
   if (ctx->transaction.IsEmpty() == TRUE) {
      ctx->transaction.SetInTransaction(FALSE);
      // Handlers can mark rings dirty without queuing ops (floating moves);
      // converge them even on an otherwise-empty commit.
      FlushOverlayRings(ctx);
      return;
   }

   // Ops apply in priority order; queuing passes (relayout, focus)
   // expand into ops picked up by later passes.  Pass 10 applies the
   // focus-queued border/redraw ops last — focus wins by queue position.
   //
   //   Pass  1: OP_CLOAK_WINDOW           — DWM visibility first
   //   Pass  2: OP_SET_WINDOW_STYLE       — style changes before relayout
   //   Pass  3: OP_ZORDER                 — z-order before repositioning
   //   Pass  4: OP_RELAYOUT_WORKSPACE     — may queue moves + borders
   //   Pass  5: OP_MOVE_WINDOW            — all SetWindowPos calls
   //   Pass  6: OP_BORDER_COLOR, OP_CLOSE_WINDOW
   //   Pass  7: OP_REDRAW_BORDER, OP_REDRAW_CONTENT
   //   Pass  8: OP_SHOW_WINDOW, OP_HIDE_WINDOW, OP_BAR_VISIBILITY
   //   Pass  9: OP_FOCUS_WINDOW           — queues border/redraw ops for
   //                                        Pass 10
   //   Pass 10: OP_BORDER_COLOR, OP_REDRAW_BORDER, OP_UPDATE_FOCUS_TRACKING,
   //            OP_MINIMIZE_WINDOW
   //

   ctx->transaction.SetInTransaction(
       TRUE); /* ensure ApplyLayout continues queuing */

   // --- Pass 1: Apply cloaking FIRST ---
   // DWM must see the correct cloaked/uncloaked state before any
   // SetWindowPos calls to prevent stale visuals.
   for (size_t index = 0; index < ctx->transaction.OpCount(); index++) {
      if (ctx->transaction.OpAt(index).type == OP_CLOAK_WINDOW) {
         ApplyOperation(ctx, &ctx->transaction.OpAt(index));
      }
   }

   // --- Pass 2: Apply window style changes ---
   // SetWindowLong for window styles before relayouts so the layout
   // engine sees correct style state (e.g. borders, captions).
   for (size_t index = 0; index < ctx->transaction.OpCount(); index++) {
      if (ctx->transaction.OpAt(index).type == OP_SET_WINDOW_STYLE) {
         ApplyOperation(ctx, &ctx->transaction.OpAt(index));
      }
   }

   // --- Pass 3: Apply Z-order changes ---
   // SetWindowPos for z-order before repositioning so the window
   // is in its correct Z position before move/size takes effect.
   for (size_t index = 0; index < ctx->transaction.OpCount(); index++) {
      if (ctx->transaction.OpAt(index).type == OP_ZORDER) {
         ApplyOperation(ctx, &ctx->transaction.OpAt(index));
      }
   }

   // --- Pass 4: Process workspace re-layouts ---
   // These call ApplyLayout which will queue additional
   // OP_MOVE_WINDOW and OP_BORDER_COLOR ops during the transaction.
   for (size_t index = 0; index < ctx->transaction.OpCount(); index++) {
      if (ctx->transaction.OpAt(index).type == OP_RELAYOUT_WORKSPACE) {
         ApplyOperation(ctx, &ctx->transaction.OpAt(index));
      }
   }

   // --- Pass 5: Apply all window moves ---
   // Use individual SetWindowPos calls.  DeferWindowPos can suppress
   // synchronous side-effects (WM_WINDOWPOSCHANGED, WM_NCCALCSIZE) that
   // certain windows need to correctly process position changes.
   for (size_t index = 0; index < ctx->transaction.OpCount(); index++) {
      if (ctx->transaction.OpAt(index).type == OP_MOVE_WINDOW) {
         ApplyOperation(ctx, &ctx->transaction.OpAt(index));
      }
   }

   // --- Pass 6: Apply borders and close ---
   // Exclude focus ops, they must run LAST to win over any stale
   // border ops queued by ApplyLayout (which uses the old
   // ctx->focused_hwnd value during Pass 4).
   for (size_t index = 0; index < ctx->transaction.OpCount(); index++) {
      OperationType const t = ctx->transaction.OpAt(index).type;
      if (t == OP_BORDER_COLOR || t == OP_CLOSE_WINDOW) {
         ApplyOperation(ctx, &ctx->transaction.OpAt(index));
      }
   }

   // --- Pass 7: Redraw window borders and content ---
   // Redraw after all positioning and border color changes so the
   // visual update reflects the final state.
   for (size_t index = 0; index < ctx->transaction.OpCount(); index++) {
      OperationType const t = ctx->transaction.OpAt(index).type;
      if (t == OP_REDRAW_BORDER || t == OP_REDRAW_CONTENT) {
         ApplyOperation(ctx, &ctx->transaction.OpAt(index));
      }
   }

   // --- Pass 8: Apply visibility changes ---
   // Show/hide windows after all positioning and redraw so the
   // window becomes visible in its final position.
   for (size_t index = 0; index < ctx->transaction.OpCount(); index++) {
      OperationType const t = ctx->transaction.OpAt(index).type;
      if (t == OP_SHOW_WINDOW || t == OP_HIDE_WINDOW ||
          t == OP_BAR_VISIBILITY) {
         ApplyOperation(ctx, &ctx->transaction.OpAt(index));
      }
   }

   // --- Pass 9: Apply focus ---
   // Focus queues border/redraw ops (FocusWindowImmediate); Pass 10
   // applies them so focus wins by queue position.
   for (size_t index = 0; index < ctx->transaction.OpCount(); index++) {
      if (ctx->transaction.OpAt(index).type == OP_FOCUS_WINDOW) {
         ApplyOperation(ctx, &ctx->transaction.OpAt(index));
      }
   }

   // --- Pass 10: Apply focus-queued borders, redraws, focus-tracking,
   // and minimize LAST ---
   // Picks up ops queued during Pass 9 (relayout inactives already
   // applied in Pass 6; queue order preserves borders-then-redraws).
   // Focus-tracking/minimize run last.
   for (size_t index = 0; index < ctx->transaction.OpCount(); index++) {
      OperationType const t = ctx->transaction.OpAt(index).type;
      if (t == OP_BORDER_COLOR || t == OP_REDRAW_BORDER ||
          t == OP_UPDATE_FOCUS_TRACKING || t == OP_MINIMIZE_WINDOW) {
         ApplyOperation(ctx, &ctx->transaction.OpAt(index));
      }
   }

   // Commit-end overlay flush: one wait + sync + paint per dirty ring, then
   // ONE DirectComposition commit. The last color of the commit wins (focus
   // marks at Pass 9/10 override the relayout inactives of Pass 4/6).
   FlushOverlayRings(ctx);

   // Clear the queue
   ctx->transaction.ClearOps();
   ctx->transaction.SetInTransaction(FALSE);

   ForceDwmComposite();
}

void DrainEventQueue(struct BFWMContext *ctx) {
   std::array<EventRecord, SpscQueue::kCapacity> records = {};
   size_t count = 0;

   // Report dropped events (overflow counter from producer thread), then reset
   LONG const dropped = ctx->event_queue.ConsumeDropped();
   if (dropped > 0) {
      Warn("DrainEventQueue: %ld events dropped due to SPSC queue overflow",
           dropped);
   }

   // Phase 1: Pop all pending events
   while (count < SpscQueue::kCapacity &&
          ctx->event_queue.Pop(records[count])) {
      count++;
   }

   if (count == 0)
      return;

   // Phase 2: Begin transaction, process events under lock, commit
   ctx->transaction.Begin();
   ScopedLock const lock(ctx->lock);

   for (size_t i = 0; i < count; i++) {
      switch (records[i].event) {
      case EVENT_SYSTEM_MOVESIZESTART:
         ProcessMoveSizeStart(ctx, records[i].hwnd);
         break;
      case EVENT_SYSTEM_MOVESIZEEND:
         ProcessMoveSizeEnd(ctx, records[i].hwnd);
         break;
      case EVENT_SYSTEM_MINIMIZESTART:
         ProcessMinimizeStart(ctx, records[i].hwnd);
         break;
      case EVENT_SYSTEM_MINIMIZEEND:
         ProcessMinimizeEnd(ctx, records[i].hwnd);
         break;
      case EVENT_OBJECT_DESTROY:
         ProcessObjectDestroy(ctx, records[i].hwnd);
         break;
      case EVENT_OBJECT_HIDE:
         ProcessObjectHide(ctx, records[i].hwnd);
         break;
      case EVENT_OBJECT_SHOW:
         ProcessObjectShow(ctx, records[i].hwnd);
         break;
      case EVENT_OBJECT_REORDER:
         ProcessObjectReorder(ctx, records[i].hwnd);
         break;
      case EVENT_OBJECT_CREATE:
      case EVENT_SYSTEM_FOREGROUND:
         ProcessCreateOrForeground(ctx, records[i].event, records[i].hwnd);
         break;
      case WM_APP_DPI_CHANGED: {
         // The record's hwnd slot carries the HMONITOR directly (see
         // DpiWatcherWndProc) — never resolve it from the watcher window,
         // which may have been destroyed and recreated since the event was
         // queued (a stale handle would resolve to the wrong monitor).
         auto *hmon = reinterpret_cast<HMONITOR>(records[i].hwnd);
         ctx->dpi->RegisterMonitor(hmon, records[i].dpi);
         ctx->dpi_update = TRUE;
         break;
      }
      default:
         break;
      }
   }

   BFWMTransactionCommit(ctx);
}
