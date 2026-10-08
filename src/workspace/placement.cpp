/**
 * @file placement.c
 * @brief Window placement pipeline.
 *
 * Consolidates the layout-move pass (PlacementApply) and the un-gated
 * single-shot move issue (PlacementIssueMove). The backpressure gate keeps at
 * most one move in flight per window: a move is re-issued only when the window
 * has landed at last_issued (or the stall timeout expired), so a slow app can
 * never accumulate a backlog of identical moves. IssueMove is the single place
 * that issues a layout move; the border ring is marked dirty there and the
 * commit-end flush syncs it only once the move has landed (ring-on-landed-rect
 * invariant — the ring never previews an un-landed position).
 */

#include "placement.h"

#include "../core/bfwm_context.h"
#include "../dpi/dpi.h"
#include "../logging/logger.h"
#include "../math/rect.h"
#include "../win/win_utils.h"
#include "../window/border/overlay.h"
#include "../window/window.h"
#include "../workspace/workspace.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <dwmapi.h>
#include <minwindef.h>
#include <windows.h>

/// Max time an async move may stay in flight before we re-issue anyway
/// (safety valve so a hung/refusing app can never stall the resize).
enum { MOVE_STALL_TIMEOUT_MS = 250U };

/// Max time a window may stay in flight before the layout is reverted
/// (restored to last landed positions via the engine's resize_to_rect).
enum { REVERT_TIMEOUT_MS = 2000U };

auto ClassifyPlacementMove(BOOL landed, BOOL stalled) -> PlacementMoveDecision {
   if (landed == TRUE)
      return PLACEMENT_MOVE_ISSUE;
   if (stalled == FALSE)
      return PLACEMENT_MOVE_DEFER;
   /* Stalled without landing: re-issue. The window may have dropped the
    * async message; convergence will catch it when it lands. */
   return PLACEMENT_MOVE_ISSUE;
}

auto ClassifyPinnedFloatDrag(POINT center, int tolerance,
                             const FloatDragMonitor *monitors, size_t count,
                             size_t source_index) -> size_t {
   if ((monitors == nullptr) || (count == 0))
      return source_index;
   size_t const src = (source_index < count) ? source_index : 0;

   // Effective DPI: a registry miss (0) is treated as the 96 base.
   auto dpi = [&monitors](size_t i) -> UINT {
      UINT const d = monitors[i].dpi;
      return (d != 0) ? d : DpiSystem::BaseDpi();
   };

   // The center lies within @p rect expanded by `tolerance` on all four sides.
   auto within_tolerance = [center, tolerance](RECT const &r) -> BOOL {
      return static_cast<BOOL>((center.x >= r.left - tolerance) &&
                               (center.x <= r.right + tolerance) &&
                               (center.y >= r.top - tolerance) &&
                               (center.y <= r.bottom + tolerance));
   };

   // First pass: count the boundary candidates and detect a mixed-DPI pin.
   size_t cand_count = 0;
   UINT first_dpi = 0;
   BOOL have_first = FALSE;
   BOOL mixed = FALSE;
   for (size_t i = 0; i < count; i++) {
      if (within_tolerance(monitors[i].rect) == TRUE) {
         cand_count++;
         UINT const d = dpi(i);
         if (have_first == FALSE) {
            first_dpi = d;
            have_first = TRUE;
         } else if (d != first_dpi) {
            mixed = TRUE;
         }
      }
   }
   if ((cand_count < 2) || (mixed == FALSE))
      return source_index;

   // Second pass: the destination is the single boundary candidate that is
   // not the source — a pinned window can only have crossed from its own
   // workspace's monitor, so geometry alone breaks the tie.
   size_t non_src = src;
   size_t non_src_count = 0;
   for (size_t i = 0; i < count; i++) {
      if ((i != src) && (within_tolerance(monitors[i].rect) == TRUE)) {
         non_src = i;
         non_src_count++;
      }
   }
   if (non_src_count == 1)
      return non_src;
   return source_index;
}

void ClampRectScaledToDpi(RECT *rect, RECT const *logical_rect,
                          RECT const *work_area, UINT dpi) {
   /* Pre-account for the OS bitmap stretch: replace the rect's size with the
    * logical size scaled to the destination DPI, keeping the top-left corner
    * (which the OS preserves through the stretch). dpi <= 96 or unknown (0)
    * is scale 1.0 — a plain clamp. */
   if (dpi > DpiSystem::BaseDpi()) {
      LONG const logical_w = logical_rect->right - logical_rect->left;
      LONG const logical_h = logical_rect->bottom - logical_rect->top;
      LONG const scale = (LONG)dpi;
      LONG const base = (LONG)DpiSystem::BaseDpi();
      rect->right = rect->left + (LONG)(logical_w * scale / base);
      rect->bottom = rect->top + (LONG)(logical_h * scale / base);
   }
   ClampToRect(*work_area, rect);
}

void ClampRectWithInsets(RECT *rect, RECT const *bounds,
                         FrameInsets const &insets) {
   /* The visible frame is the outer rect shifted inward by the per-side
    * insets (GetWindowRect minus DWMWA_EXTENDED_FRAME_BOUNDS — the invisible
    * resize border). Clamp the visible frame into bounds exactly like
    * ClampToRect (including the stretch-to-fill case), then map back to the
    * outer rect by undoing the insets: the outer rect may extend past the
    * bounds by up to the inset on each side, so the invisible frame goes
    * off-screen instead of leaving a visible gap between the visible frame
    * and the monitor edge. All-zero insets degenerate to a plain
    * ClampToRect. */
   RECT visible = {
       .left = rect->left + insets.left,
       .top = rect->top + insets.top,
       .right = rect->right - insets.right,
       .bottom = rect->bottom - insets.bottom,
   };
   ClampToRect(*bounds, &visible);

   rect->left = visible.left - insets.left;
   rect->top = visible.top - insets.top;
   rect->right = visible.right + insets.right;
   rect->bottom = visible.bottom + insets.bottom;
}

namespace {

/**
 * @brief Issue a layout move for a window and record it as in-flight.
 *
 * The single place that issues a layout move. Does NOT touch moves_in_flight —
 * single-shot callers (PlacementIssueMove) must not trigger the convergence
 * pass while a window is mid-move; the gated path (MaybeIssueMove) sets the
 * flag explicitly after issuing. The border ring is marked dirty, not aimed:
 * the commit-end flush syncs it only once the move has landed, so the ring is
 * always positioned from an exact, landed rect.
 *
 * A DPI-unaware target on a scaled monitor is issued the rect in its own
 * (logical) coordinate space while the thread is switched into the target's
 * awareness context: the desired physical rect (already in destination-monitor
 * space — layout engines compute from the destination work area) is converted
 * to logical via DpiSystem::Unscale so the DWM composes it back to the exact
 * physical rect on the destination monitor. Aware targets and 100%-scale
 * destinations are issued unchanged with no context switch (zero behaviour
 * change for PM-aware windows).
 *
 * @param win  The window to move
 * @param rect The desired frame rect (destination-monitor physical px)
 * @param ctx  The BFWM context (DPI registry for the destination scale)
 */
inline void IssueMove(Window *win, const RECT *rect, struct BFWMContext *ctx) {
   RECT issued = *rect;
   DPI_AWARENESS_CONTEXT prev_ctx = nullptr;

   /* Cross-monitor moves arm the landing-gate trust (see MaybeIssueMove):
    * once the window demonstrably reaches the destination monitor, the app
    * owns its post-DPI size (aware terminals re-lay-out in WM_DPICHANGED),
    * so the gate accepts the app's landed rect instead of fighting it. The
    * destination is the monitor the PHYSICAL rect targets (layout engines
    * compute from the destination work area), resolved before any unaware
    * unscaling. */
   HMONITOR src_hmon =
       MonitorFromWindow(win->GetHwnd(), MONITOR_DEFAULTTONEAREST);
   HMONITOR dst_hmon = MonitorFromRect(&issued, MONITOR_DEFAULTTONEAREST);
   BOOL const cross_monitor = static_cast<BOOL>(src_hmon != dst_hmon);

   DPI_AWARENESS_CONTEXT target_ctx =
       GetWindowDpiAwarenessContext(win->GetHwnd());
   BOOL const unaware = static_cast<BOOL>(
       (AreDpiAwarenessContextsEqual(target_ctx,
                                     DPI_AWARENESS_CONTEXT_UNAWARE) != 0) ||
       (AreDpiAwarenessContextsEqual(
            target_ctx, DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED) != 0));
   if ((unaware == TRUE) && (ctx != nullptr) && (ctx->dpi != nullptr)) {
      /* The desired rect is already destination physical px. Passing it from
       * a PMv2-aware thread makes the system reinterpret it against the
       * monitor the window is still on (the source), so a cross-monitor move
       * scales the window by the ratio of the two scales. Issue the logical
       * rect (physical / dest_scale) inside the target's own awareness
       * context instead: the DWM then composes it back to the exact physical
       * rect on the destination monitor. */
      UINT const dest_dpi = ctx->dpi->GetDpi(dst_hmon);
      float const scale = (float)dest_dpi / (float)DpiSystem::BaseDpi();
      if (scale > 1.0F) {
         prev_ctx = SetThreadDpiAwarenessContext(target_ctx);
         if (prev_ctx != nullptr) {
            /* Switch succeeded: issue the rect in the target's logical
             * coordinate space (physical / dest_scale, rounded half-up) so
             * the DWM composes it back to the exact physical rect. On
             * failure (prev_ctx == nullptr) the physical rect and the PMv2
             * thread context are kept — best-effort, no worse than the
             * pre-fix behaviour. */
            issued.left = DpiSystem::Unscale(rect->left, dest_dpi);
            issued.top = DpiSystem::Unscale(rect->top, dest_dpi);
            issued.right = DpiSystem::Unscale(rect->right, dest_dpi);
            issued.bottom = DpiSystem::Unscale(rect->bottom, dest_dpi);
         }
      }
   }

   BFWMApplyLayoutPosition(win->GetHwnd(), &issued);
   if (prev_ctx != nullptr)
      SetThreadDpiAwarenessContext(prev_ctx);

   if (cross_monitor == TRUE) {
      /* Arm the landing-gate trust (see MaybeIssueMove). Do NOT reset
       * failed_landings here: a hung never-crossing window is re-issued on
       * every settle-expiry, and each re-issue would re-zero the counter and
       * never reach FLOAT. */
      win->SetCrossMonitorTrusted(TRUE);
   }

   /* Record the ORIGINAL physical rect: the landing gate reads the window's
    * rect in physical px from our PMv2-aware thread, so the issued reference
    * must stay in the same space as the tolerance check in MaybeIssueMove. */
   win->SetLastIssued(*rect);
   win->SetLastIssueTime(GetTickCount64());
   win->SetMoveInFlight(TRUE);
   win->MarkOverlayDirty();
}

/**
 * @brief Backpressure-gated move issue for one window (layout pass).
 *
 * Skips when the window is already at the desired rect. Otherwise only issues
 * when the previous move has landed (current rect == last issued rect) or the
 * stall timeout elapsed; while a move is in flight the window is skipped (no
 * re-send, no ring re-aim) and moves_in_flight stays TRUE so the convergence
 * pass keeps checking until the move lands.
 *
 * Self-healing float: a window that repeatedly stalls without ever landing
 * (it refuses every issued rect) is converted to floating at its actual
 * rect after FLOAT_AFTER_FAILED_LANDINGS consecutive failures, so a clamped
 * app can never wedge the layout. The convergence pass redistributes the
 * freed slot; the user can re-tile with the float toggle.
 *
 * @param win       The window to consider
 * @param desired   The desired frame rect from the layout engine
 * @param workspace The window's workspace (engine removal on float)
 * @param ctx       The BFWM context
 */
inline void MaybeIssueMove(Window *win, const RECT *desired,
                           Workspace * /*workspace*/, struct BFWMContext *ctx) {
   RECT current_rect;
   BOOL const got_current = GetWindowRect(win->GetHwnd(), &current_rect);
   /* Cross-monitor trust: a cross-monitor issue arms this flag. Once the
    * window demonstrably reaches the destination monitor, the app's
    * WM_DPICHANGED handler has already run (delivered synchronously with the
    * move that landed it), so the app owns its post-DPI size — a window that
    * landed at the issued rect converges immediately, otherwise the size is
    * re-asserted this pass (a plain same-monitor WM_SIZE) instead of being
    * held for a settle window. Same-monitor moves never set the flag, so this
    * block is inert for them. */
   if ((got_current == TRUE) && win->HasLastIssued() &&
       (win->IsCrossMonitorTrusted() != 0)) {
      BOOL const desired_changed =
          static_cast<BOOL>((desired->left != win->LastIssued().left) ||
                            (desired->top != win->LastIssued().top) ||
                            (desired->right != win->LastIssued().right) ||
                            (desired->bottom != win->LastIssued().bottom));
      if (desired_changed != FALSE) {
         /* (a) New user intent / back-and-forth direction change: drop the
          * trust and fall through to the normal gate, clear the stall
          * precondition so the fresh move is issued immediately. */
         win->SetCrossMonitorTrusted(FALSE);
         win->SetLastIssueTime(0);
      } else {
         HMONITOR cur_hmon =
             MonitorFromWindow(win->GetHwnd(), MONITOR_DEFAULTTONEAREST);
         HMONITOR dst_hmon = MonitorFromRect(desired, MONITOR_DEFAULTTONEAREST);
         if (cur_hmon == dst_hmon) {
            int const tol = DpiRoundingTolerance(ctx, win->GetHwnd());
            if (RectEqualsWithinTolerance(current_rect, *desired, tol) != 0) {
               /* (b) Reached the destination and landed at the issued rect:
                * converged. Adopt the landed rect, clear in-flight state, and
                * stop — the commit-end flush syncs the ring to the app's
                * rect. */
               win->SetLastLanded(current_rect);
               win->SetLastIssued(current_rect);
               win->SetMoveInFlight(FALSE);
               win->SetCrossMonitorTrusted(FALSE);
               win->MarkOverlayDirty();
               return;
            }
            /* (b') Reached the destination but the size mismatches: the app
             * settled at its own grid-derived post-DPI size and its
             * WM_DPICHANGED handler already ran (synchronous with the move
             * that landed it). Re-assert = plain same-monitor WM_SIZE that
             * terminals accept instantly. Bypass the stall precondition so
             * the normal gate issues THIS pass — no settle wait. */
            win->SetCrossMonitorTrusted(FALSE);
            win->SetLastIssueTime(0);
         } else if ((GetTickCount64() - win->LastIssueTime()) <
                    MOVE_STALL_TIMEOUT_MS) {
            /* (c) In transit: the window has not processed the posted move
             * yet. Hold briefly (no re-issue; avoids queued churn). Capped at
             * MOVE_STALL_TIMEOUT_MS so a hung window escalates fast. */
            ctx->moves_in_flight = TRUE;
            return;
         } else {
            /* (c') Never reached the destination within the stall window:
             * genuinely stuck. Drop the trust and fall through — the normal
             * gate's stall re-issues accumulate failed_landings (IssueMove no
             * longer resets it on re-issue) and FLOAT fires at 3. */
            win->SetCrossMonitorTrusted(FALSE);
         }
      }
   }

   /* A DPI-unaware target on a scaled monitor lands at a rect that differs
    * from the issued physical rect by the logical rounding round-trip, so
    * exact-equality landing checks would never match and the window would be
    * floated. Tolerance is 0 for aware targets and at 100% scale, preserving
    * exact behaviour there. */
   int const tolerance = DpiRoundingTolerance(ctx, win->GetHwnd());
   if ((got_current == TRUE) &&
       (RectEqualsWithinTolerance(current_rect, *desired, tolerance) != 0))
      return;

   /* Backpressure gate: only issue when the previous move landed
    * (current rect ~= last issued rect) or the stall timeout elapsed.
    * Otherwise the move is still in flight — do NOT re-send (that was
    * the flood that built the catch-up backlog). The border ring stays
    * dirty and the commit-end flush defers until the move lands. */
   BOOL const landed = static_cast<BOOL>(
       (got_current == TRUE) &&
       ((!win->HasLastIssued()) ||
        (RectEqualsWithinTolerance(current_rect, win->LastIssued(),
                                   tolerance) != 0)));
   BOOL const stalled = static_cast<BOOL>(
       (GetTickCount64() - win->LastIssueTime()) > MOVE_STALL_TIMEOUT_MS);

   switch (ClassifyPlacementMove(landed, stalled)) {
   case PLACEMENT_MOVE_ISSUE:
      IssueMove(win, desired, ctx);
      ctx->moves_in_flight = TRUE;
      break;

   case PLACEMENT_MOVE_DEFER:
      ctx->moves_in_flight = TRUE;
      break;
   }
}

} // namespace

/* Shared with overlay.cpp (OverlayFlush's landed gate) so both detect "landed"
 * identically — a DPI-unaware window on a scaled monitor lands a few px off the
 * issued rect and must not be treated as still in flight. External linkage
 * (declared in placement.h) so the border overlay can call them. */
auto DpiRoundingTolerance(struct BFWMContext *ctx, HWND hwnd) -> int {
   if ((ctx == nullptr) || (ctx->dpi == nullptr))
      return 0;
   /* Aware (PMv1/PMv2/system) targets land at the exact physical rect. */
   DPI_AWARENESS_CONTEXT awareness = GetWindowDpiAwarenessContext(hwnd);
   BOOL const unaware = static_cast<BOOL>(
       (AreDpiAwarenessContextsEqual(awareness,
                                     DPI_AWARENESS_CONTEXT_UNAWARE) != 0) ||
       (AreDpiAwarenessContextsEqual(
            awareness, DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED) != 0));
   if (unaware == FALSE)
      return 0;
   /* Strict at 100%: the logical round-trip is exact, so a tolerance here
    * could mask a genuinely stuck window (keeps behaviour identical to the
    * old exact-equality gate at 96 DPI). */
   float const scale =
       ctx->dpi->GetScale(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST));
   if (scale <= 1.0F)
      return 0;
   /* Covers the physical<->logical<->physical rounding round-trip (up to one
    * whole logical px per axis at 150%+) plus minor WM_GETMINMAXINFO drift.
    * 3px @150%, 4px @200%, 3px @125%. */
   return std::max(1, (int)std::ceil(scale * 2.0F));
}

auto RectEqualsWithinTolerance(RECT a, RECT b, int tol) -> BOOL {
   if (tol <= 0) {
      return static_cast<BOOL>(a.left == b.left && a.top == b.top &&
                               a.right == b.right && a.bottom == b.bottom);
   }
   return static_cast<BOOL>(std::abs(a.left - b.left) <= tol &&
                            std::abs(a.top - b.top) <= tol &&
                            std::abs(a.right - b.right) <= tol &&
                            std::abs(a.bottom - b.bottom) <= tol);
}

void PlacementApply(Workspace *workspace, struct BFWMContext *ctx) {
   if (workspace->GetEngine() == nullptr)
      return;
   workspace->GetEngine()->apply();

   /* Recomputed each pass: TRUE if any window below still needs a move
    * (issued or waiting for an in-flight move to land). Drives the
    * main-loop convergence pass. */
   ctx->moves_in_flight = FALSE;

   for (size_t i = 0; i < workspace->Windows().size(); i++) {
      HWND hwnd = workspace->Windows()[i]->GetHwnd();
      if (IsWindowManagedByLayout(workspace->Windows()[i]) == FALSE)
         continue;
      if ((IsWindowVisible(hwnd) == FALSE) || IsIconic(hwnd) == TRUE)
         continue;

      BOOL is_cloaked = FALSE;
      DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &is_cloaked,
                            sizeof(is_cloaked));
      if (is_cloaked == TRUE)
         continue;

      RECT desired_rect;
      if (!workspace->GetEngine()->get_rect(hwnd, &desired_rect)) {
         /* Self-healing reconcile: this window is managed by the workspace
          * but absent from the layout engine's tree — an orphan produced
          * when an engine rebuild re-inserts only currently-managed windows
          * and a temporarily-set flag (floating/fullscreen/unmanaged-
          * fullscreen) later clears without a re-insert event. Re-insert it
          * now; the engine's insert only mutates its own tree/list, never
          * workspace->Windows(), so it is safe to call while iterating that
          * vector. If the re-query still fails, skip as before (defensive). */
         workspace->GetEngine()->insert(hwnd, ctx->focused_hwnd);
         if (!workspace->GetEngine()->get_rect(hwnd, &desired_rect)) {
            Error("No valid rect for window: %p", hwnd);
            continue;
         }
      }

      MaybeIssueMove(workspace->Windows()[i], &desired_rect, workspace, ctx);
   }
}

void PlacementIssueMove(Window *win, const RECT *rect,
                        struct BFWMContext *ctx) {
   IssueMove(win, rect, ctx);
}
