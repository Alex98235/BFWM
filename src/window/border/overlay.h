/**
 * @file overlay.h
 * @brief Per-window border overlay: a layered popup HWND that renders a
 *        colored ring around a tracked window.
 *
 * Render path: each ring is painted with GDI+ into a 32bpp BGRA DIB and
 * presented per-window via UpdateLayeredWindow (ULW_ALPHA + AC_SRC_ALPHA);
 * there is no batched present step. The overlay is a decoration of its target
 * — exactly one z-slot above it, input-transparent, excluded from Alt-Tab/peek,
 * and filtered out of the WM's own management via WS_EX_TOOLWINDOW. Geometry
 * is driven externally: the caller invokes OverlaySyncPosition from
 * OP_MOVE_WINDOW apply and OverlayAssertZOrder from EVENT_OBJECT_REORDER. A
 * periodic per-commit reconcile (OverlayReconcileAll from MainLoop) backstops
 * z-order and visibility drift for every tracked ring, immune to dropped
 * EVENT_OBJECT_* queue events.
 */

#ifndef BFWM_BORDER_OVERLAY_H
#define BFWM_BORDER_OVERLAY_H

#define WIN32_LEAN_AND_MEAN

#include <array>
#include <dwmapi.h>
#include <windows.h>
#include <winerror.h>

#include <gdiplus.h>

#include "../../core/bfwm_context.h"
#include "../cleanup.h"

/* One layered window plus its render surface (memory DC + 32bpp BGRA DIB).
 * In strip mode `rect` is the window's screen rect — the origin used to
 * translate the shared ring path into this DIB. */
using OverlaySurface = struct {
   HWND hwnd;
   RECT rect;
   int dib_w;
   int dib_h;
   HDC mem_dc;
   HBITMAP dib;
   HBITMAP old_dib;
   void *dib_bits; /* DIB pixel bits (premultiply pass before ULW) */
};

class Overlay;

/* GWLP_USERDATA binding for overlay_wndproc: resolves the owning ring and the
 * specific strip surface (index 0..3). */
using OverlayUserData = struct {
   Overlay *ovl;
   int index;
};

enum { OVERLAY_STRIP_COUNT = 4 };

class Overlay {
 public:
   /** Strip indices for OverlayComputeStripRects: the ring decomposes into four
    *  thin layered windows — full-width TOP/BOTTOM (owners of the corner arcs)
    *  and full-height LEFT/RIGHT (the straight side runs). */
   enum {
      OVERLAY_STRIP_TOP = 0,
      OVERLAY_STRIP_BOTTOM = 1,
      OVERLAY_STRIP_LEFT = 2,
      OVERLAY_STRIP_RIGHT = 3,
   };
   /**
    * @brief Decision for the per-commit overlay reconcile pass.
    *
    * Reconcile runs at the top of every commit's overlay flush, for every
    * window's ring, BEFORE OverlayFlush. It is the guaranteed backstop for
    * z-drift and visibility drift: immune to dropped EVENT_OBJECT_REORDER/HIDE
    * queue events and to BFWM's own changes never generating events
    * (own-process events are skipped via WINEVENT_SKIPOWNPROCESS). The pass is
    * pure and cheap — no paint, no waits, no geometry work — and convergence is
    * guaranteed at the next commit after any drift.
    */
   enum OverlayReconcileAction {
      OVERLAY_RECONCILE_ASSERT_Z,   ///< shown ring over shown, unsuppressed
                                    ///< target
      OVERLAY_RECONCILE_MARK_DIRTY, ///< visibility drift — let the
                                    ///< flush converge
      OVERLAY_RECONCILE_NONE,       ///< agreed hidden / cloaked / suppressed
   };

   /**
    * @brief Destroy the overlay and free all resources.
    *
    */
   ~Overlay();

   /**
    * @brief Construct a border overlay for a tracked window.
    *
    * Creates the layered popup HWNDs, initialises the GDI+/DIB render
    * resources, and establishes initial geometry/visibility against the
    * target's current state. The ring is not shown until OverlaySyncPosition
    * finds the target visible.
    *
    * Construction never fails: on any failure (invalid target, window-class
    * registration, surface creation, GDI+ startup) the overlay is left inert —
    * no strip surfaces exist and every method becomes a no-op. Callers need no
    * null checks; the border simply dies and the WM survives.
    *
    * @param ctx    The BFWM context (owner)
    * @param owner  The owning window; the overlay keeps a non-owning
    *               back-pointer and derives its target HWND from it
    * @param color  Border COLORREF (0x00BBGGRR)
    * @param width  Border thickness in logical pixels
    * @param radius Corner radius in logical pixels (0 = square)
    */
   Overlay(struct BFWMContext *ctx, Window *owner, COLORREF color, int width,
           int radius);

   /**
    * @brief Initialize the overlay ring's resources.
    *
    * Creates the layered popup HWNDs, initialises the GDI+/DIB render
    * resources, and establishes initial geometry/visibility against the
    * target's current state. The ctor calls this on construction; it may also
    * be called again later to retry an earlier failure.
    *
    * Idempotent: once the overlay is live, subsequent calls return true
    * immediately. On failure the overlay is left inert — no strip surfaces
    * exist, every method becomes a no-op, and the object can be retried with
    * another Init() call.
    *
    * @return true when the overlay is live after the call, false on failure
    */
   [[nodiscard]] auto Init() -> bool;

   /**
    * @brief Whether the overlay is live (initialized).
    *
    * false when construction/Init failed or was never attempted: no strip
    * surfaces exist and every method is a no-op.
    *
    * @return true when the overlay is live, false when inert
    */
   [[nodiscard]] auto IsInitialized() const -> bool;

   /**
    * @brief Sync the overlay to the target's current geometry and visibility.
    *
    * A change-gate caches the last-synced EFB + cloak state: when the ring is
    * clean and the target's frame bounds and cloak state are unchanged, the
    * sync returns after one EFB read + one cloak read (no DPI/inset recompute,
    * no geometry work). Only on an actual change — or a dirty ring — is the
    * full geometry recomputed. Pure position moves do zero paint; the DIB is
    * only recreated on a size change.
    *
    * @param suppress Suppress the ring even when the target is visible
    *                 (fullscreen windows, etc.)
    */
   void OverlaySyncPosition(BOOL suppress);

   /**
    * @brief Change the ring color. The color lands at the next commit-end
    * OverlayFlush with the commit's final color — the repaint is deferred,
    * never immediate, so a focus transition produces one paint per ring.
    *
    * @param color New border COLORREF (0x00BBGGRR)
    */
   void OverlaySetColor(COLORREF color);

   /**
    * @brief Change the ring thickness and corner radius.
    *
    * Mark-dirty-only like OverlaySetColor: the new geometry/radius land at the
    * next commit-end OverlayFlush. A width change also alters the ring frame,
    * so the flush's OverlaySyncPosition recomputes geometry and recreates the
    * DIB on size change.
    *
    * @param width  New border thickness in logical pixels
    * @param radius New corner radius in logical pixels (0 = square)
    */
   void OverlaySetBorder(int width, int radius);

   /* Painted ring extent relative to the extended frame bounds, per edge.
    * Positive = ring lies inside the EFB on that side; negative = ring pokes
    * out beyond the EFB (top edge, where the overlay inset is 0). */
   struct OverlayRingOffsets {
      LONG left;
      LONG top;
      LONG right;
      LONG bottom;
   };

   /**
    * @brief Measure the ring's painted extent relative to the target's
    *        extended frame bounds, per edge.
    *
    * Mirrors the overlay_sync_strips ring-frame math (EFB inflated by
    * stroke/2, shifted inward by the client insets, rounded outward) and
    * extends it to the painted outer edge of the stroke. The float-clamp path
    * composes these offsets with the window's invisible-frame insets so the
    * whole ring stays inside the work area; the top offset is negative
    * (~ -stroke) because the ring pokes above the EFB there.
    *
    * @param dpi Destination DPI for the stroke scaling (the clamp's target
    *            monitor DPI, not the source — the window may still be
    *            associated with the source monitor at clamp time)
    * @return Per-edge ring offsets relative to the EFB; all zero when the EFB
    *         query fails (plain frame-inset clamp fallback)
    */
   [[nodiscard]] auto OverlayMeasureRingOffsets(UINT dpi) const
       -> OverlayRingOffsets;

   /**
    * @brief Mark the ring dirty so the commit-end flush converges it.
    *
    * Geometry changes (moves, relayouts, direct floating moves) mark the ring
    * dirty instead of syncing immediately; the actual sync + paint happens
    * once per ring in OverlayFlush at commit end. When the dirtying move is an
    * async layout move, the flush defers until the move has landed, so the
    * ring is always positioned from an exact, landed rect.
    *
    */
   void OverlayMarkDirty();

   /**
    * @brief Converge a dirty ring: sync the ring to the target and repaint
    * once.
    *
    * Called at commit end for every dirty overlay — the single sync+paint
    * point. No-op when the ring is clean. When the target has an async layout
    * move in flight (IssueMove set the in-flight flag and the window has not
    * yet landed at the issued rect), the flush defers: the ring stays dirty
    * and the next commit (convergence pass, ~33ms cadence) retries, so the
    * ring is only ever positioned from a landed, exact rect. The flag is
    * cleared when the move lands or when the window settles anywhere
    * (ProcessMoveSizeEnd), so all other geometry changes — floating drags,
    * mouse resizes, app-driven moves — sync immediately to the live rect.
    *
    * @return TRUE when the sync + paint path ran; FALSE when the ring was
    * clean or the flush was deferred (move still in flight — stays dirty and
    * the next commit retries).
    */
   auto OverlayFlush() -> BOOL;

   /**
    * @brief Re-assert the overlay's z-order slot directly above its target.
    *
    * Implements the EVENT_OBJECT_REORDER handler. No-op when the overlay is
    * already in the correct slot. Never moves or resizes the overlay.
    *
    */
   void OverlayAssertZOrder();

   /**
    * @brief Pure visibility decision table for the reconcile pass.
    *
    * Maps the ring/target visibility state to a reconcile action. PURE logic:
    * no Win32 calls, so it is unit-testable. Exact truth table (32 combos):
    *   - ring visible && target visible && !iconic && !suppress → ASSERT_Z
    *     (cloaked irrelevant here)
    *   - ring hidden  && target visible && !iconic && !suppress →
    *     cloaked ? NONE : MARK_DIRTY
    *   - ring visible (anything else) → MARK_DIRTY (ghost ring: target hidden,
    *     minimized or suppressed but the ring is still up — flush hides it)
    *   - otherwise → NONE (agreed hidden / target cloaked / suppressed)
    *
    * @param ring_visible   TRUE when the ring HWND is visible
    * @param target_visible TRUE when the target HWND is visible
    * @param target_iconic  TRUE when the target is minimized (iconic)
    * @param cloaked        TRUE when the target is DWM-cloaked
    * @param suppress       TRUE when the ring is suppressed (fullscreen, etc.)
    * @return The reconcile action for the given state
    */
   static auto OverlayReconcileClassify(BOOL ring_visible, BOOL target_visible,
                                        BOOL target_iconic, BOOL cloaked,
                                        BOOL suppress)
       -> OverlayReconcileAction;

   /**
    * @brief Per-commit z-order/visibility reconcile for one overlay.
    *
    * Called at the top of FlushOverlayRings for every window's overlay, before
    * OverlayFlush. Re-asserts z when the ring is shown over a shown,
    * unsuppressed target; otherwise marks the ring dirty so the existing flush
    * converges (re-shows it over a drifted-visible target or hides a ghost
    * ring). The cloak query is deliberately skipped in every state except the
    * MARK_DIRTY-candidate one to avoid a DWM call per ring per commit.
    *
    * The per-commit path is gated: when the ring is clean and nothing has
    * marked it (dirty or reconcile_needed), the reconcile returns before any
    * Win32 call — a clean ring costs zero syscalls per commit. The periodic
    * backstop (OverlayReconcileAll) passes force=TRUE so convergence never
    * depends on a future event.
    *
    * @param suppress Suppress the ring even when the target is visible
    *                 (fullscreen windows, etc.)
    * @param force    Bypass the clean-ring gate (periodic backstop)
    */
   void OverlayReconcile(BOOL suppress, BOOL force = FALSE);

   /**
    * @brief Periodic per-commit overlay reconcile for every tracked window's
    * ring.
    *
    * Runs from MainLoop at a fixed cadence, so convergence never depends on a
    * future user interaction. Re-derives fullscreen state live: the
    * event-driven checkpoint (CheckWindowTransitions) can miss app-driven
    * fullscreen exits (F11/Alt+Enter emit EVENT_OBJECT_LOCATIONCHANGE, which is
    * dropped), leaving the suppress flags stuck TRUE and the ring hidden
    * forever. This pass clears stuck flags and also catches undetected
    * fullscreen enters. Flushes + commits once when anything changed; no idle
    * churn.
    *
    * @param ctx The BFWM context (owner)
    */
    static void OverlayReconcileAll(struct BFWMContext *ctx);
    /// Purge windows whose HWND died or was reused (e.g. across sleep/wake).
    static void ReclaimStaleWindows(struct BFWMContext *ctx);

   /**
    * @brief Whether the ring currently needs a flush to converge.
    *
    * Non-mutating read used by the commit-end flush summary to count dirty
    * rings. Callers guard against a null overlay pointer before calling.
    *
    * @return TRUE when the ring is dirty, FALSE when clean.
    */
   [[nodiscard]] auto OverlayIsDirty() const -> BOOL;

   /**
    * @brief Return the overlay window's HWND.
    *
    * @return The overlay HWND, or nullptr
    */
   auto OverlayGetHwnd() -> HWND;

   /**
    * @brief Decompose a ring's centerline frame into four strip rects.
    *
    * Pure geometry (no Win32 calls), so it is unit-testable. Each strip is
    * inflated to fully contain its painted pixels — pen half-width, the
    * round-join caps at the arc/line tangents, plus a 2px anti-aliasing pad —
    * and every corner square is deliberately covered by two strips with an
    * overlap band. Since every strip paints the same full ring path with
    * identical pixels, the overlap makes composition seamless regardless of
    * z-order. All edges round outward.
    *
    * With stroke s, radius r, margin m:
    *   - TOP/BOTTOM: full-width, height r + s + m + 2 (own the corner arcs)
    *   - LEFT/RIGHT: full-height, width s + 2*m + 2
    *
    * @param frame   Ring centerline rect (integer, screen coords)
    * @param stroke  Pen width in device pixels
    * @param radius  Corner radius in device pixels (0 = square)
    * @param margin  Transparent pad beyond the painted band, in pixels
    * @param strips  OUT: four RECTs, indexed by OVERLAY_STRIP_*
    */
   static void OverlayComputeStripRects(const RECT *frame, float stroke,
                                        float radius, int margin,
                                        std::array<RECT, 4> *strips);

 private:
   /* Sacrificial transparent margin around the ring, in DIPs. The overlay
    * window is inflated by it so the ring rect can sit a clear (transparent)
    * pixel band inside the window edge; the margin is position-neutral (window
    * + ring rect = frame regardless of MARGIN). */
   enum { OVERLAY_SAFE_MARGIN = 3 };

   enum {
      OVERLAY_BITS_PER_PIXEL = 32,
      OVERLAY_ALPHA_OPAQUE = 255,
   };

   /* ARGB/BGRA channel layout for the 32bpp DIB (GDI+ writes 0xAARRGGBB). */
   enum {
      OVERLAY_ALPHA_MASK = (int)0xFF000000U,
      OVERLAY_ALPHA_SHIFT = 24,
      OVERLAY_RED_SHIFT = 16,
      OVERLAY_GREEN_SHIFT = 8,
   };

   /* Per-channel pixel masks for the premultiply pass. */
   enum {
      OVERLAY_CHANNEL_MASK = 0xFF,
      OVERLAY_CHANNEL_MAX = 255,
   };

   using OverlayInsets = struct {
      int left;
      int top;
      int right;
      int bottom;
   };

   static constexpr std::array<int, OVERLAY_STRIP_COUNT> k_strip_chain = {
       OVERLAY_STRIP_RIGHT, OVERLAY_STRIP_LEFT, OVERLAY_STRIP_BOTTOM,
       OVERLAY_STRIP_TOP};

   struct BFWMContext *ctx; /* owner context */
   Window *owner_; /* non-owning back-pointer to the owning window; the Window
                     owns this Overlay via unique_ptr, so the pointer is valid
                     for the overlay's whole lifetime */
   HWND target;
   COLORREF color;
   int width;          /* border thickness, logical px */
   int radius;         /* corner radius, logical px */
   BOOL dirty = FALSE; /* content stale; needs repaint + ULW upload */
   /* Z/visibility stale: set by OverlayMarkDirty so the per-commit reconcile
    * gate (clean ring → zero syscalls) still catches z-drift without a
    * periodic scan. Cleared by OverlayReconcile after the gate. */
   BOOL reconcile_needed = FALSE;
   std::array<OverlaySurface, OVERLAY_STRIP_COUNT> surfaces = {};
   std::array<OverlayUserData, OVERLAY_STRIP_COUNT> user_data = {};
   /* Strip-mode paint geometry, computed once per OverlaySyncPosition and
    * shared by every strip: identical geometry is what makes the seam pixels
    * byte-identical. */
   RECT ring_frame = {};      /* ring centerline rect, screen coords */
   float paint_stroke = 0.0F; /* scaled stroke width, px */
   float paint_radius = 0.0F; /* scaled corner radius, px, clamped to
                                 half-extents */
   BOOL geometry_valid = FALSE;
   /* Change-gate cache: last-synced frame bounds and cloak state. A ring
    * whose target is unchanged skips the per-commit read stack (one EFB read
    * + one cloak read replace ~15 syscalls); hide/iconify/fullscreen are
    * owned by OverlayReconcile which marks the ring dirty. */
   RECT last_efb = {};
   DWORD last_cloaked = 0;
   BOOL last_efb_valid = FALSE;

   auto overlay_insert_after() -> HWND;
   auto overlay_band_intact() -> BOOL;
   void overlay_assert_band();
   static void overlay_measure_insets(HWND target, const RECT *efb,
                                      OverlayInsets *insets);
   /* Effective ring DPI: the DPI of the monitor the target currently sits on,
    * from the per-monitor registry. GetDpiForWindow is deliberately NOT used:
    * DPI-unaware target apps are virtualized and report 96 even on a scaled
    * monitor, leaving their rings thin while the layout inset scales. */
   [[nodiscard]] auto overlay_dpi() const -> UINT;
   static void overlay_surface_resize_dib(OverlaySurface *surf, int w, int h);
   static void overlay_surface_release_render(OverlaySurface *surf);
   static auto overlay_surface_init_render(OverlaySurface *surf) -> HRESULT;
   static auto overlay_ulw_upload(OverlaySurface *surf) -> HRESULT;
   static void overlay_build_ring_path(Gdiplus::GraphicsPath &path, float left,
                                       float top, float right, float bottom,
                                       float radius);
   static void overlay_premultiply(OverlaySurface *surf, const RECT *rects,
                                   int rect_count);
   auto overlay_paint_strip_surface(OverlaySurface *surf) const -> HRESULT;
   auto overlay_verify_seams() -> BOOL;

   void overlay_hide_ring();
   void overlay_sync_strips(const RECT *efb, const OverlayInsets *insets,
                            DWORD gate_cloaked, BOOL was_visible);
     void overlay_sync_position(BOOL suppress);

     /* Tolerance-aware landing check matching MaybeIssueMove's landed
      * detection, so a DPI-unaware window on a scaled monitor (whose landed
      * rect rounds vs the issued rect) still clears MoveInFlight and converges
      * the ring instead of deferring forever. */
     auto overlay_landed_at_issued(Window *win) -> BOOL;

    static auto CALLBACK overlay_wndproc(HWND hwnd, UINT msg, WPARAM wparam,
                                        LPARAM lparam) -> LRESULT;
};

#endif /* BFWM_BORDER_OVERLAY_H */
