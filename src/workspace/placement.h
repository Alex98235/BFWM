/**
 * @file placement.h
 * @brief Window placement pipeline.
 *
 * Consolidates the backpressure-gated layout-move pass (PlacementApply) and
 * the un-gated single-shot move issue (PlacementIssueMove). The backpressure
 * gate keeps at most one move in flight per window (see IssueMove /
 * MaybeIssueMove in placement.cpp), and the shared IssueMove is the single
 * place that pairs the issued rect with the border ring's dirty marking.
 */

#ifndef BFWM_PLACEMENT_H
#define BFWM_PLACEMENT_H

#include <windows.h>

struct BFWMContext;
class Window;
class Workspace;

/// Consecutive failed landings (stall re-issues without a landing) before a
/// window is self-healed to floating.
enum { FLOAT_AFTER_FAILED_LANDINGS = 3U };

/**
 * @brief Decision for one window in the backpressure-gated move pass.
 */
enum PlacementMoveDecision {
   PLACEMENT_MOVE_ISSUE, ///< issue (or re-issue) the move
   PLACEMENT_MOVE_DEFER, ///< move still in flight — wait
   PLACEMENT_MOVE_FLOAT, ///< self-heal: convert the refusing window to
                         ///< floating
};

/**
 * @brief Pure classifier: what should the move pass do for one window?
 *
 *   - landed (current rect == last issued rect)  -> Issue (counter resets)
 *   - move still in flight (not landed, not stalled) -> Defer
 *   - stalled without landing: the k-th consecutive failure; Float once the
 *     failure count reaches FLOAT_AFTER_FAILED_LANDINGS, else Issue.
 *
 * @param landed          Whether the window sits at its last issued rect
 * @param stalled         Whether the stall timeout elapsed since the issue
 * @param failed_landings Consecutive failures so far (before this decision)
 * @return The action the move pass must take
 */
auto ClassifyPlacementMove(BOOL landed, BOOL stalled, UINT failed_landings)
    -> PlacementMoveDecision;

/// One monitor candidate for the floating-drag classifier: physical bounds +
/// effective DPI (0 = unknown, treated as 96).
struct FloatDragMonitor {
   RECT rect;
   UINT dpi;
};

// Detect whether `center` is pinned on a mixed-DPI monitor boundary (within
// `tolerance` pixels of at least two monitor rects whose DPIs differ) and, if
// so, return the index of the destination monitor: the single boundary monitor
// that is not the source. Returns `source_index` when the center is not pinned,
// when the boundary monitors share a DPI, or when the tie is ambiguous (more
// than one non-source candidate).
auto ClassifyPinnedFloatDrag(POINT center, int tolerance,
                             const FloatDragMonitor *monitors, size_t count,
                             size_t source_index) -> size_t;

// Replaces the size of `rect` with the size of `logical_rect` scaled to `dpi`
// (logical size × dpi/96), keeping `rect`'s top-left, then clamps to
// `work_area`. For dpi <= 96 (or dpi == 0 = unknown) this is a plain clamp
// (scale 1.0 / no-op). For DPI-unaware windows this pre-accounts for the OS
// bitmap stretch that happens when the window re-associates with a scaled
// monitor, so the clamped placement does not overhang the work area after the
// stretch.
void ClampRectScaledToDpi(RECT *rect, RECT const *logical_rect,
                          RECT const *work_area, UINT dpi);

/// Per-side outer-to-visible frame deltas: GetWindowRect minus
/// DWMWA_EXTENDED_FRAME_BOUNDS (the invisible resize border). Positive values
/// mean the visible frame sits inside the outer rect by that many pixels on
/// that side.
struct FrameInsets {
   LONG left;
   LONG top;
   LONG right;
   LONG bottom;
};

// Clamps `rect` so that rect expanded by `insets` (the visible frame) stays
// within `bounds`. The rect may extend past `bounds` by up to the inset on
// each side — the invisible frame goes off-screen instead of leaving a gap
// between the visible frame and the monitor edge. If the visible frame is
// larger than `bounds`, it is stretched to fill `bounds` (mirrors
// ClampToRect). For all-zero insets this is a plain ClampToRect.
void ClampRectWithInsets(RECT *rect, RECT const *bounds,
                         FrameInsets const &insets);

/**
 * @brief Issue backpressure-gated async moves for every managed window in the
 *        workspace.
 *
 * Recomputed each pass: moves_in_flight is reset to FALSE at the top and set
 * TRUE while any window still needs a move (issued or waiting for an in-flight
 * move to land), driving the main-loop convergence pass. Runs inside a
 * transaction when WorkspaceApplyLayout is the caller; the inactive-border
 * pass is applied by WorkspaceApplyLayout after this returns.
 *
 * @param workspace The workspace to place
 * @param ctx The BFWM context (moves_in_flight is recomputed each pass)
 */
void PlacementApply(Workspace *workspace, struct BFWMContext *ctx);

/**
 * @brief Ungated single-shot move issue (no backpressure gate).
 *
 * Issues a move and records it as last_issued for the gate's landed check,
 * without touching moves_in_flight (single-shot paths such as
 * OP_MOVE_WINDOW / floating-center moves must not trigger the convergence
 * pass while a window is mid-move). The border ring is marked dirty here,
 * preserving the ring-on-landed-rect invariant.
 *
 * @param win  The window to move
 * @param rect The desired frame rect
 * @param ctx  The BFWM context (unused except for API symmetry)
 */
void PlacementIssueMove(Window *win, const RECT *rect, struct BFWMContext *ctx);

/// DPI rounding tolerance (px) for the landed-rect comparison: 0 for DPI-aware
/// or 100%-scale windows, std::max(1, ceil(scale*2)) for DPI-unaware windows on
/// a scaled monitor (the physical<->logical round-trip can leave the landed
/// rect a few px off the issued rect). Shared by the placement move pass and
/// OverlayFlush so both detect "landed" identically.
auto DpiRoundingTolerance(struct BFWMContext *ctx, HWND hwnd) -> int;

/// Compare two RECTs for equality within a per-field pixel tolerance (0 =
/// exact).
auto RectEqualsWithinTolerance(RECT a, RECT b, int tol) -> BOOL;

#endif /* BFWM_PLACEMENT_H */
