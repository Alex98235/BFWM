/**
 * @file overlay.cpp
 * @brief ULW border overlay implementation.
 *
 * Per-window layered popup HWNDs that draw a rounded-rect border ring around a
 * tracked window. Geometry comes from DWMWA_EXTENDED_FRAME_BOUNDS, inset by
 * the measured EFB-vs-client delta so the ring hugs the content edge. The
 * ring is drawn with GDI+ (antialiased) into 32bpp BGRA DIBs and presented
 * with UpdateLayeredWindow (ULW_ALPHA + AC_SRC_ALPHA) — the classic layered
 * window path. Every GDI/GDI+ failure degrades to "border dies, WM survives":
 * log via ErrorW, never assert or crash.
 *
 * The ring is split into FOUR thin layered windows — full-width TOP/BOTTOM
 * (owners of the corner arcs) and full-height LEFT/RIGHT (the straight side
 * runs) — each with a tiny DIB, so a repaint uploads only the perimeter
 * (~38x less data than a full window surface at 1080p). Every strip paints
 * the SAME full rounded-rect path with an identical pen, offset into DIB-local
 * coordinates (ring frame minus the strip origin). Each corner square is
 * covered by two strips with a deliberate overlap band, so composition is
 * seamless regardless of z-order. The identical-pixel invariant is
 * byte-compared in debug builds. Strip geometry: OverlayComputeStripRects
 * (pure, unit-tested in test/tests/test_overlay_strip_geometry.cpp).
 */

#include "overlay.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "../../dpi/dpi.h"
#include "../../logging/logger.h"
#include "../../workspace/placement.h"
#include "../fullscreen/detect.h"

#define OVERLAY_CLASS L"BFWMBorderOverlay"
#define OVERLAY_TITLE L"BFWMBorderOverlay"

/* Corner-radius tolerance and rounded-rect arc angles (degrees). */
#define OVERLAY_RADIUS_EPSILON 0.5F
#define OVERLAY_ARC_QUARTER 90.0F
#define OVERLAY_ARC_HALF 180.0F

/* ---------------- GDI+ runtime ---------------- */

/* GDI+ is a process-global service; wrap its startup/shutdown in a Meyers
 * singleton so it starts once on first use and is shut down once at exit. */
namespace {

/* EnumWindows callback: count top-level windows owned by this process
 * (overlay strips, bars, monitor helper, snackbar). Used by the ctor
 * fail-path diagnostic to decompose the GetGuiResources USER-object count:
 * many own windows => stale-window/registry leak; few own windows with USER
 * near the quota => leaked non-window USER objects. */
auto CALLBACK CountOwnWindowProc(HWND hwnd, LPARAM lparam) -> BOOL {
   DWORD pid = 0;
   if ((GetWindowThreadProcessId(hwnd, &pid) != 0) &&
       (pid == GetCurrentProcessId())) {
      (*(DWORD *)lparam)++;
   }
   return TRUE;
}

class GdiplusRuntime {
 public:
   GdiplusRuntime() {
      Gdiplus::GdiplusStartupInput const input;
      ok_ = (Gdiplus::GdiplusStartup(&token_, &input, nullptr) == Gdiplus::Ok);
   }
   ~GdiplusRuntime() {
      if (ok_) {
         Gdiplus::GdiplusShutdown(token_);
      }
   }
   [[nodiscard]] auto ok() const -> bool { return ok_; }

   GdiplusRuntime(const GdiplusRuntime &) = delete;
   auto operator=(const GdiplusRuntime &) -> GdiplusRuntime & = delete;

   static auto Instance() -> GdiplusRuntime & {
      static GdiplusRuntime inst;
      return inst;
   }

 private:
   ULONG_PTR token_ = 0;
   bool ok_ = false;
};
} // namespace

/* The ctor keeps its never-fails contract: the fallible work lives in Init,
 * so a failure just leaves the object inert (no strip surfaces, every method a
 * no-op) and the caller can retry with a later Init() call. */
Overlay::Overlay(struct BFWMContext *ctx, Window *owner, COLORREF color,
                 int width, int radius)
    : ctx(ctx), owner_(owner),
      target((owner != nullptr) ? owner->GetHwnd() : nullptr), color(color),
      width(width), radius(radius) {
   /* Never-fails contract: the result is intentionally discarded — on failure
    * the object is left inert (all methods no-op) and the caller can retry
    * with a later Init() call. */
   (void)Init();
}

auto Overlay::Init() -> bool {
   /* Idempotent: an already-live overlay needs no re-init. A failed init tears
    * down every partial strip, so IsInitialized() stays false and a retry can
    * succeed. */
   if (IsInitialized()) {
      return true;
   }

   if ((this->target == nullptr) || (IsWindow(this->target) == 0)) {
      return false; /* inert: no surfaces, all methods no-op */
   }

   /* Register the overlay window class exactly once, on first use. The
    * function-local static lambda runs at first Init;
    * ERROR_CLASS_ALREADY_EXISTS (the class may already be registered from a
    * prior ring) is not a failure. */
   static const bool registered = []() -> bool {
      WNDCLASSW wndclassw = {};
      wndclassw.style = 0;
      wndclassw.lpfnWndProc = overlay_wndproc;
      wndclassw.cbClsExtra = 0;
      wndclassw.cbWndExtra = 0;
      wndclassw.hInstance = GetModuleHandleW(nullptr);
      wndclassw.hIcon = nullptr;
      wndclassw.hCursor = LoadCursorW(nullptr, IDC_ARROW);
      wndclassw.hbrBackground = nullptr;
      wndclassw.lpszMenuName = nullptr;
      wndclassw.lpszClassName = OVERLAY_CLASS;
      return RegisterClassW(&wndclassw) != 0 ||
             GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
   }();
   if (!registered) {
      ErrorW(L"Overlay: RegisterClassW failed (error %lu)",
             (unsigned long)GetLastError());
      return false; /* inert */
   }

   this->dirty = TRUE; /* first SyncPosition paints */

   /* Click-through (WS_EX_TRANSPARENT + the layered window's own zero-alpha
    * hit-testing), no Alt-Tab (WS_EX_TOOLWINDOW), no activation
    * (WS_EX_NOACTIVATE), per-pixel alpha (WS_EX_LAYERED). The ring interior
    * is fully transparent, so clicks pass through it. Strip mode creates one
    * window per surface; each is a thin layered popup like the single one. */

   /* Failure path: run the diagnostic, tear down the partial strips, leave
    * the object inert and report false — "border dies, WM survives". */
   auto fail = [&]() -> bool {
      /* Diagnostic: report the process handle economy at the moment
       * construction failed. 1158 (ERROR_NOT_ENOUGH_MEMORY) from
       * CreateWindowExW above is the classic USER/GDI quota or desktop-heap
       * exhaustion signature. Low counts here (< ~500) prove the exhaustion
       * is system-wide (external), not our leak; counts near 10,000 mean we
       * are the culprit. Logged before the teardown loop so the numbers
       * reflect the failure moment. */
      {
         DWORD const user_objects =
             GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
         DWORD const gdi_objects =
             GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);

         /* Decompose the USER-object count: count this process's own windows
          * and the managed-window registry size. Many own windows / a large
          * registry => a stale-window leak (each dead window pins 4 strips);
          * few own windows with USER still near the quota => leaked
          * non-window USER objects elsewhere. */
         DWORD own_windows = 0;
         EnumWindows(CountOwnWindowProc,
                     reinterpret_cast<LPARAM>(&own_windows));
         const size_t registry_size = (this->ctx->windows != nullptr)
                                          ? this->ctx->windows->Windows().size()
                                          : 0;

         ErrorW(L"Overlay: construction failed — USER objects %lu / GDI "
                L"objects %lu / own windows %lu / registry %zu / suspended "
                L"%d / resize_hwnd %p",
                (unsigned long)user_objects, (unsigned long)gdi_objects,
                (unsigned long)own_windows, registry_size,
                (int)this->ctx->suspended, (void *)this->ctx->resize_hwnd);
      }

      /* Partial construction: tear down the strips created so far and leave
       * the object inert (all surfaces null) — every public method no-ops on
       * it, so callers need no null checks. The border dies, the WM
       * survives. */
      for (int index = 0; index < OVERLAY_STRIP_COUNT; index++) {
         if ((this->surfaces[index].hwnd != nullptr) &&
             (IsWindow(this->surfaces[index].hwnd) != 0)) {
            DestroyWindow(this->surfaces[index].hwnd);
            this->surfaces[index].hwnd = nullptr;
         }
         overlay_surface_release_render(&this->surfaces[index]);
      }
      this->dirty = FALSE;
      return false;
   };

   for (int index = 0; index < OVERLAY_STRIP_COUNT; index++) {
      OverlaySurface *surf = &this->surfaces[index];
      surf->hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT |
                                       WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                   OVERLAY_CLASS, OVERLAY_TITLE,
                                   WS_POPUP | WS_DISABLED, 0, 0, 1, 1, nullptr,
                                   nullptr, GetModuleHandleW(nullptr), nullptr);
      if (surf->hwnd == nullptr) {
         ErrorW(L"Overlay: CreateWindowExW failed (surface %d, error %lu)",
                index, (unsigned long)GetLastError());
         return fail();
      }
      this->user_data[index].ovl = this;
      this->user_data[index].index = index;
      SetWindowLongPtrW(surf->hwnd, GWLP_USERDATA,
                        (LONG_PTR) & this->user_data[index]);

      BOOL exclude = TRUE;
      DwmSetWindowAttribute(surf->hwnd, DWMWA_EXCLUDED_FROM_PEEK, &exclude,
                            sizeof exclude);

      if (FAILED(this->overlay_surface_init_render(surf))) {
         Error("OVL init FAILED target=%p surface=%d", target, index);
         return fail(); /* border dies, WM survives */
      }
   }

   /* Decoration semantics: exactly one z-slot above the target, not topmost.
    * The band is re-chained as a whole (target → RIGHT → LEFT → BOTTOM →
    * TOP). */
   this->overlay_assert_band();

   /* Establish initial geometry/visibility against the target's live state. */
   this->OverlaySyncPosition(FALSE);
   return true;
}

auto Overlay::IsInitialized() const -> bool {
   /* The primary strip window is the live-state signal: every strip surface is
    * created or torn down together, so surfaces[0].hwnd == nullptr means the
    * whole ring is inert (init never succeeded or failed and was cleaned up).
    */
   return this->surfaces[0].hwnd != nullptr;
}

Overlay::~Overlay() {
   for (int index = 0; index < OVERLAY_STRIP_COUNT; index++) {
      if ((this->surfaces[index].hwnd != nullptr) &&
          (IsWindow(this->surfaces[index].hwnd) != 0)) {
         DestroyWindow(this->surfaces[index].hwnd);
      }
      overlay_surface_release_render(&this->surfaces[index]);
   }
}

/* ---------------- window proc ---------------- */

/* The overlay window's wndproc. WM_PAINT: ULW owns the layered surface, so a
 * paint request (e.g. after a DWM restart) is satisfied by re-uploading the
 * ring; BeginPaint/EndPaint validate the region so DefWindowProc does not loop.
 * GWLP_USERDATA points at the owning OverlayUserData, which resolves the strip
 * surface (index 0..3). */
auto CALLBACK Overlay::overlay_wndproc(HWND hwnd, UINT msg, WPARAM wparam,
                                       LPARAM lparam) -> LRESULT {
   (void)wparam;
   (void)lparam;
   switch (msg) {
   case WM_PAINT: {
      PAINTSTRUCT paint;
      BeginPaint(hwnd, &paint);
      EndPaint(hwnd, &paint);
      auto *user_data =
          (OverlayUserData *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
      if ((user_data != nullptr) && (user_data->ovl != nullptr)) {
         user_data->ovl->overlay_paint_strip_surface(
             &user_data->ovl->surfaces[user_data->index]);
      }
      return 0;
   }
   case WM_DPICHANGED: {
      /* The strip's monitor DPI changed (the strips are top-level layered
       * windows on the target's monitor). Ring geometry is scaled from the
       * target's monitor DPI, so mark the owning ring dirty — the commit-end
       * flush recomputes stroke/radius and re-uploads with the new DPI. No
       * immediate repaint: rings converge at the next commit flush (or the
       * periodic reconcile backstop). */
      auto *user_data =
          (OverlayUserData *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
      if ((user_data != nullptr) && (user_data->ovl != nullptr)) {
         user_data->ovl->OverlayMarkDirty();
      }
      return 0;
   }
   default:
      break;
   }
   return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void Overlay::OverlaySyncPosition(BOOL suppress) {
   overlay_sync_position(suppress);
}

void Overlay::OverlaySetColor(COLORREF color) {
   if ((IsWindow(this->surfaces[0].hwnd) == FALSE) ||
       (IsWindow(this->target) == FALSE)) {
      return;
   }
   if (this->color == color) {
      return;
   }
   /* Mark dirty; the repaint lands once at the commit-end OverlayFlush with
    * the final color. */
   this->color = color;
   this->dirty = TRUE;
}

void Overlay::OverlaySetBorder(int width, int radius) {
   this->width = width;
   this->radius = radius;
   this->dirty = TRUE; /* next flush recomputes geometry + repaints */
}

void Overlay::OverlayMarkDirty() {
   if ((IsWindow(this->surfaces[0].hwnd) == FALSE) ||
       (IsWindow(this->target) == FALSE)) {
      return;
   }
   this->dirty = TRUE;
   /* Every dirtying path touches z or visibility, so the per-commit
    * reconcile must not skip this ring even after the flush converges. */
   this->reconcile_needed = TRUE;
}

auto Overlay::overlay_landed_at_issued(Window *win) -> BOOL {
   RECT current_rect = {};
   if (GetWindowRect(this->target, &current_rect) == FALSE)
      return FALSE;
   RECT const issued = win->LastIssued();
   int const tol = DpiRoundingTolerance(this->ctx, this->target);
   return RectEqualsWithinTolerance(current_rect, issued, tol);
}

auto Overlay::OverlayFlush() -> BOOL {
   if ((IsWindow(this->surfaces[0].hwnd) == FALSE) ||
       (IsWindow(this->target) == FALSE)) {
      return FALSE;
   }

   /* Fullscreen windows keep their ring suppressed. */
   Window *win = this->owner_;
   BOOL const suppress = static_cast<BOOL>(
       (win != nullptr) && ((win->IsFullscreen() != FALSE) ||
                            (win->IsUnmanagedFullscreen() != FALSE)));

   /* Cross-monitor live tracking: during trust/hold a cross-monitor move may
    * keep the window at a rect != issued briefly (the app is adapting to
    * WM_DPICHANGED; the hold is capped at MOVE_STALL_TIMEOUT_MS), and the
    * landed gate below would defer the ring sync until MoveInFlight clears —
    * freezing the ring at the pre-move location for the whole hold. Sync the
    * ring to the window's ACTUAL rect on every commit instead. The change-gate
    * inside overlay_sync_position makes this a no-op when nothing moved; the
    * flag is cleared on landing (here or ProcessMoveSizeEnd) so the normal
    * gated path resumes. */
   if ((win != nullptr) && (win->MoveInFlight() == TRUE) &&
       (win->IsCrossMonitorTrusted() != FALSE)) {
      if (overlay_landed_at_issued(win) != FALSE) {
         win->SetMoveInFlight(
             FALSE); /* landed at the issued rect (within DPI tol) */
      }
      overlay_sync_position(suppress);
      return TRUE;
   }

   if ((this->dirty == FALSE) || (IsWindow(this->surfaces[0].hwnd) == FALSE) ||
       (IsWindow(this->target) == FALSE)) {
      return FALSE;
   }

   /* Landed gate: an async layout move (SWP_ASYNCWINDOWPOS) lands on the
    * target's thread after IssueMove returns, so a live EFB read at flush
    * time would still see the pre-move rect. Defer only while a layout move
    * is genuinely in flight (IssueMove set the flag and the window has not
    * yet landed at the issued rect). The flag is cleared when the move lands
    * (here) or when the window settles anywhere (ProcessMoveSizeEnd), so
    * geometry changes that never went through IssueMove — floating drags,
    * mouse resizes, app-driven moves — sync immediately to the live rect. */
   if ((win != nullptr) && (win->MoveInFlight() == TRUE)) {
      if (overlay_landed_at_issued(win) == FALSE) {
         return FALSE; /* still in flight — stay dirty, retry next commit */
      }
      win->SetMoveInFlight(
          FALSE); /* landed at the issued rect (within DPI tol) */
   }

   overlay_sync_position(suppress);
   return TRUE;
}

void Overlay::OverlayAssertZOrder() {
   if ((IsWindow(this->surfaces[0].hwnd) == FALSE) ||
       (IsWindow(this->target) == FALSE)) {
      return;
   }
   overlay_assert_band();
}

/* ---------------- per-commit reconcile ---------------- */

/* Per-commit z-order/visibility reconcile: the guaranteed backstop for
 * z-drift and visibility drift. Runs at the top of every commit's flush pass,
 * before OverlayFlush, for every window's ring — so it is immune to dropped
 * EVENT_OBJECT_REORDER/HIDE queue events and to BFWM's own z/visibility
 * changes never generating events (own-process events skipped via
 * WINEVENT_SKIPOWNPROCESS). Pure and cheap: no paint, no waits, no geometry
 * work — either re-assert z (no-op when already correct) or mark dirty and let
 * the existing flush converge on this or the next commit. */
auto Overlay::OverlayReconcileClassify(BOOL ring_visible, BOOL target_visible,
                                       BOOL target_iconic, BOOL cloaked,
                                       BOOL suppress)
    -> OverlayReconcileAction {
   if ((ring_visible == TRUE) && (target_visible == TRUE) &&
       (target_iconic == FALSE) && (suppress == FALSE)) {
      /* Ring up over a shown, unsuppressed target: keep it exactly one slot
       * above the target. Cloaked is irrelevant here — a visible ring was
       * already proven correct, so never hide it for a cloak flag. */
      return OVERLAY_RECONCILE_ASSERT_Z;
   }
   if ((ring_visible == FALSE) && (target_visible == TRUE) &&
       (target_iconic == FALSE) && (suppress == FALSE)) {
      /* Ring should be up but is not: mark dirty so the flush re-shows it.
       * A DWM-cloaked target stays hidden (the DWM treats it as gone) — NONE.
       */
      return (cloaked == TRUE) ? OVERLAY_RECONCILE_NONE
                               : OVERLAY_RECONCILE_MARK_DIRTY;
   }
   if (ring_visible == TRUE) {
      /* Ghost ring: the target is hidden, minimized or suppressed but the ring
       * is still up — mark dirty so the flush hides it. */
      return OVERLAY_RECONCILE_MARK_DIRTY;
   }
   /* Ring and target agree on hidden, or the target is cloaked, or the ring is
    * suppressed: nothing to do. */
   return OVERLAY_RECONCILE_NONE;
}

void Overlay::OverlayReconcile(BOOL suppress, BOOL force) {
   if ((IsWindow(this->surfaces[0].hwnd) == FALSE) ||
       (IsWindow(this->target) == FALSE)) {
      return;
   }

   /* Clean-ring gate: a ring that is neither dirty nor z-stale needs no
    * reconcile — skip all Win32 reads. The periodic backstop passes
    * force=TRUE so convergence never depends on a future event. */
   if ((force == FALSE) && (this->dirty == FALSE) &&
       (this->reconcile_needed == FALSE)) {
      return;
   }
   this->reconcile_needed = FALSE;

   /* Batched visibility: the ring is "visible" iff every surface is visible.
    * Target state is queried once per ring, not once per surface. */
   BOOL ring_visible = TRUE;
   for (int index = 0; index < OVERLAY_STRIP_COUNT; index++) {
      if (IsWindowVisible(this->surfaces[index].hwnd) == FALSE) {
         ring_visible = FALSE;
         break;
      }
   }
   BOOL const target_visible = IsWindowVisible(this->target);
   BOOL const target_iconic = IsIconic(this->target);

   /* Query cloak ONLY in the MARK_DIRTY-candidate state (ring hidden && target
    * visible && !iconic && !suppress). All other states ignore the cloak flag,
    * deliberately, to avoid a DWM call per ring per commit. Conservative on
    * failure: treat as cloaked so a ring never shows over an unknown target
    * state. */
   DWORD cloaked = 0;
   BOOL cloaked_flag = FALSE;
   if ((ring_visible == FALSE) && (target_visible == TRUE) &&
       (target_iconic == FALSE) && (suppress == FALSE)) {
      if (SUCCEEDED(DwmGetWindowAttribute(this->target, DWMWA_CLOAKED, &cloaked,
                                          sizeof cloaked))) {
         cloaked_flag = static_cast<BOOL>(cloaked == TRUE);
      } else {
         cloaked_flag = TRUE; /* conservative: on failure treat as cloaked */
      }
   }

   switch (OverlayReconcileClassify(ring_visible, target_visible, target_iconic,
                                    cloaked_flag, suppress)) {
   case OVERLAY_RECONCILE_ASSERT_Z:
      /* Re-assert the z-slot; no-ops when already correct. The whole band is
       * checked (target → RIGHT → LEFT → BOTTOM → TOP). */
      overlay_assert_band();
      break;
   case OVERLAY_RECONCILE_MARK_DIRTY:
      /* Let the existing commit-end flush converge (re-show or hide). */
      OverlayMarkDirty();
      break;
   case OVERLAY_RECONCILE_NONE:
   default:
      break;
   }
}

/* Periodic per-commit overlay reconcile for every tracked ring. Runs from
 * MainLoop at a fixed cadence so convergence never depends on a future user
 * interaction; re-derives fullscreen state live because the event-driven
 * checkpoint can miss app-driven fullscreen exits (F11/Alt+Enter emit
 * EVENT_OBJECT_LOCATIONCHANGE, which WinEventProc drops) — a stuck suppress
 * flag would otherwise hide the ring forever. Also catches undetected
 * fullscreen enters. Flushes + commits once when anything changed; no idle
 * churn. */
void Overlay::OverlayReconcileAll(struct BFWMContext *ctx) {
   ReclaimStaleWindows(ctx);

   BOOL any_dirty = FALSE;

   for (const auto &window : ctx->windows->Windows()) {
      Window *win = window.get();
      if ((win == nullptr) || (win->GetOverlay() == nullptr))
         continue;

      BOOL suppress = static_cast<BOOL>((win->IsFullscreen() == TRUE) ||
                                        (win->IsUnmanagedFullscreen() == TRUE));
      BOOL ring_visible = TRUE;
      for (int index = 0; index < OVERLAY_STRIP_COUNT; index++) {
         if (IsWindowVisible(win->GetOverlay()->surfaces[index].hwnd) ==
             FALSE) {
            ring_visible = FALSE;
            break;
         }
      }
      BOOL const target_visible = IsWindowVisible(win->GetOverlay()->target);
      BOOL const target_iconic = IsIconic(win->GetOverlay()->target);

      /* Live re-derivation of fullscreen state — do NOT trust the flags alone.
       */
      if ((suppress == TRUE) && (ring_visible == FALSE) &&
          (target_visible == TRUE) && (target_iconic == FALSE)) {
         /* Stuck-suppress candidate: flags say suppressed but the target looks
          * live. Re-derive; if it is not actually fullscreen, clear the flags.
          */
         FullscreenType const live =
             DetectFullscreenWindow(win->GetOverlay()->target);
         if (live != FS_EXCLUSIVE_FULLSCREEN && live != FS_BORDERLESS_WINDOW) {
            win->SetFullscreen(FALSE);
            win->SetUnmanagedFullscreen(FALSE);
            suppress = FALSE;
         }
      } else if ((suppress == FALSE) && (ring_visible == TRUE) &&
                 (target_visible == TRUE) && (target_iconic == FALSE)) {
         /* Undetected fullscreen enter: ring is up but the window became
          * fullscreen without the checkpoint noticing — suppress it. */
         FullscreenType const live =
             DetectFullscreenWindow(win->GetOverlay()->target);
         if (live == FS_EXCLUSIVE_FULLSCREEN || live == FS_BORDERLESS_WINDOW) {
            win->SetFullscreen(TRUE);
            suppress = TRUE;
         }
      }

      win->GetOverlay()->OverlayReconcile(suppress, TRUE);

      if (win->GetOverlay()->dirty == TRUE)
         any_dirty = TRUE;
   }

   /* Flush + commit only when something changed (no idle churn). */
   if (any_dirty == TRUE) {
      for (const auto &window : ctx->windows->Windows()) {
         Window *win = window.get();
         if ((win != nullptr) && (win->GetOverlay() != nullptr))
            win->GetOverlay()->OverlayFlush();
      }
   }
}

auto Overlay::OverlayIsDirty() const -> BOOL { return this->dirty; }

auto Overlay::OverlayGetHwnd() -> HWND { return this->surfaces[0].hwnd; }

/* ---------------------------------------------------------------------------
 * Pure strip geometry (no Win32 calls): unit-tested in
 * test/tests/test_overlay_strip_geometry.cpp. Decomposes a ring's centerline
 * frame into four thin layered windows — full-width TOP/BOTTOM (owners of the
 * corner arcs) and full-height LEFT/RIGHT (the straight side runs). Every
 * strip paints the same full rounded-rect path with identical pixels, and each
 * corner square is deliberately covered by two strips with an overlap band, so
 * composition is seamless regardless of z-order. All edges round outward
 * (floorf top/left, ceilf bottom/right) so a strip always contains its painted
 * content. Dimensions (stroke s, radius r, margin m):
 *   TOP/BOTTOM height = r + s + m + 2;  LEFT/RIGHT width = s + 2*m + 2.
 * ------------------------------------------------------------------------- */
void Overlay::OverlayComputeStripRects(const RECT *frame, float stroke,
                                       float radius, int margin,
                                       std::array<RECT, 4> *strips) {
   const float half = stroke / 2.0F;
   const float outer = half + (float)margin;
   const float cap = half + 2.0F;

   RECT const top = {
       .left = (LONG)floorf((float)frame->left - outer),
       .top = (LONG)floorf((float)frame->top - outer),
       .right = (LONG)ceilf((float)frame->right + outer),
       .bottom = (LONG)ceilf((float)frame->top + radius + cap),
   };

   RECT const bottom = {
       .left = (LONG)floorf((float)frame->left - outer),
       .top = (LONG)floorf((float)frame->bottom - radius - cap),
       .right = (LONG)ceilf((float)frame->right + outer),
       .bottom = (LONG)ceilf((float)frame->bottom + outer),
   };

   RECT const left = {
       .left = (LONG)floorf((float)frame->left - outer),
       .top = (LONG)floorf((float)frame->top - outer),
       .right = (LONG)ceilf((float)frame->left + half + (float)margin + 2.0F),
       .bottom = (LONG)ceilf((float)frame->bottom + outer),
   };

   RECT const right = {
       .left = (LONG)floorf((float)frame->right - half - (float)margin - 2.0F),
       .top = (LONG)floorf((float)frame->top - outer),
       .right = (LONG)ceilf((float)frame->right + outer),
       .bottom = (LONG)ceilf((float)frame->bottom + outer),
   };

   (*strips)[OVERLAY_STRIP_TOP] = top;
   (*strips)[OVERLAY_STRIP_BOTTOM] = bottom;
   (*strips)[OVERLAY_STRIP_LEFT] = left;
   (*strips)[OVERLAY_STRIP_RIGHT] = right;
}

/* ---------------- z-order ---------------- */

/* Z-order policy: the overlay band is a DECORATION of the target window, so
 * it must sit exactly above it, in the target's own z-band. The band is
 * target -> RIGHT -> LEFT -> BOTTOM -> TOP (the full-width arc owners on
 * top). */
auto Overlay::overlay_insert_after() -> HWND {
   /* Insert directly above the target, never above the window that happens
    * to be above it: that window may live in a higher WindowBand (UWP
    * CoreWindow apps, the taskbar), and SetWindowPos after a higher-band
    * window is silently ineffective — the band would stay wherever it was
    * and the ring would be buried under every overlapping window. Inserting
    * after the target always lands in the target's own band. */
   return this->target;
}

/* Validate the WHOLE band, not just target->RIGHT: a mid-band insertion (e.g.
 * a dialog landing between LEFT and BOTTOM) breaks side-edge coverage even
 * though the first link looks intact. Four own-process GetWindow calls —
 * cheap. */
auto Overlay::overlay_band_intact() -> BOOL {
   if (GetWindow(this->target, GW_HWNDPREV) !=
       this->surfaces[OVERLAY_STRIP_RIGHT].hwnd) {
      return FALSE;
   }
   if (GetWindow(this->surfaces[OVERLAY_STRIP_RIGHT].hwnd, GW_HWNDPREV) !=
       this->surfaces[OVERLAY_STRIP_LEFT].hwnd) {
      return FALSE;
   }
   if (GetWindow(this->surfaces[OVERLAY_STRIP_LEFT].hwnd, GW_HWNDPREV) !=
       this->surfaces[OVERLAY_STRIP_BOTTOM].hwnd) {
      return FALSE;
   }
   if (GetWindow(this->surfaces[OVERLAY_STRIP_BOTTOM].hwnd, GW_HWNDPREV) !=
       this->surfaces[OVERLAY_STRIP_TOP].hwnd) {
      return FALSE;
   }
   return TRUE;
}

/* Repair band drift: re-chain all four strips in one pass (target -> RIGHT ->
 * LEFT -> BOTTOM -> TOP). No-op when the band is already intact. */
void Overlay::overlay_assert_band() {
   if (overlay_band_intact() == TRUE) {
      return;
   }
   /* Direct SetWindowPos, NOT a deferred batch: BeginDeferWindowPos /
    * DeferWindowPos / EndDeferWindowPos is silently discarded for these
    * WS_EX_LAYERED popups (see overlay_sync_strips), and a mid-batch
    * DeferWindowPos failure leaked the HDWP (one USER object) — the
    * per-process USER-quota exhaustion behind error 1158. */
   HWND after = overlay_insert_after();
   for (int index = 0; index < OVERLAY_STRIP_COUNT; index++) {
      HWND strip_hwnd = this->surfaces[k_strip_chain[index]].hwnd;
      SetWindowPos(strip_hwnd, after, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                       SWP_NOSENDCHANGING | SWP_NOOWNERZORDER);
      after = strip_hwnd;
   }
}

/* ---------------- inset ---------------- */

/* Measure how far the client area sits inside the extended frame bounds.
 * For native windows this is the frame strip thickness on each side; for
 * frameless windows (Chromium, Electron) it is the 1px invisible frame strip
 * the content keeps inside its rect. The ring is shifted inward by this so it
 * covers the frame line instead of floating clear of it. */
void Overlay::overlay_measure_insets(HWND target, const RECT *efb,
                                     OverlayInsets *insets) {
   RECT client = {};
   GetClientRect(target, &client);
   POINT origin = {.x = 0, .y = 0};
   ClientToScreen(target, &origin);

   insets->left = origin.x - efb->left;
   insets->right = efb->right - (origin.x + (client.right - client.left));
   insets->bottom = efb->bottom - (origin.y + (client.bottom - client.top));

   insets->top =
       0; /* the EFB top is the caption bar — visible chrome, not a border */
}

/* Effective ring DPI: the DPI of the monitor the target currently sits on,
 * looked up in the per-monitor registry. GetDpiForWindow is deliberately NOT
 * used — DPI-unaware target apps are virtualized and report 96 even on a
 * scaled monitor, so their rings would stay thin while the layout inset
 * scales. */
auto Overlay::overlay_dpi() const -> UINT {
   HMONITOR hmon = MonitorFromWindow(this->target, MONITOR_DEFAULTTONEAREST);
   UINT dpi = DpiSystem::BaseDpi();
   if ((this->ctx != nullptr) && (this->ctx->dpi != nullptr)) {
      /* GetDpi returns 0 on a registry miss under the upcoming sentinel
       * semantics; under the current semantics it returns 96. Either way a 0
       * result must fall back to the base DPI. */
      UINT const queried = this->ctx->dpi->GetDpi(hmon);
      if (queried != 0) {
         dpi = queried;
      }
   }
   return dpi;
}

/* Ring extent relative to the extended frame bounds, per edge. Mirrors the
 * overlay_sync_strips ring-frame math exactly (EFB inflated by stroke/2,
 * shifted inward by the client insets, edges rounded outward) and extends it
 * to the painted outer edge of the stroke, so a clamp can keep the whole ring
 * inside a bounds rect. The top edge is negative (~ -stroke): the ring pokes
 * above the EFB because the caption-bar inset is 0 there. The stroke is
 * scaled from the PASSED dpi (the clamp's destination DPI), not overlay_dpi():
 * at clamp time the window may still be associated with the source monitor. */
auto Overlay::OverlayMeasureRingOffsets(UINT dpi) const -> OverlayRingOffsets {
   float const stroke = DpiSystem::ScaleF((float)this->width, dpi);

   RECT efb = {};
   if (FAILED(DwmGetWindowAttribute(this->target, DWMWA_EXTENDED_FRAME_BOUNDS,
                                    &efb, sizeof(efb))))
      return {}; /* border died — plain frame-inset clamp fallback */

   OverlayInsets insets = {};
   overlay_measure_insets(this->target, &efb, &insets);

   /* Ring centerline frame, exactly as overlay_sync_strips computes it. */
   float const frame_left =
       (float)efb.left - (stroke / 2.0F) + (float)insets.left;
   float const frame_top = (float)efb.top - (stroke / 2.0F) + (float)insets.top;
   float const frame_right =
       (float)efb.right + (stroke / 2.0F) - (float)insets.right;
   float const frame_bottom =
       (float)efb.bottom + (stroke / 2.0F) - (float)insets.bottom;
   RECT const ring_frame = {
       .left = (LONG)floorf(frame_left),
       .top = (LONG)floorf(frame_top),
       .right = (LONG)ceilf(frame_right),
       .bottom = (LONG)ceilf(frame_bottom),
   };

   /* Painted outer edge of the stroke centered on the frame. */
   float const outer_left = (float)ring_frame.left - (stroke / 2.0F);
   float const outer_top = (float)ring_frame.top - (stroke / 2.0F);
   float const outer_right = (float)ring_frame.right + (stroke / 2.0F);
   float const outer_bottom = (float)ring_frame.bottom + (stroke / 2.0F);

   /* Per-edge offset relative to the EFB, rounded conservatively so the clamp
    * never lets the ring poke out of the bounds: left/top floor, right/bottom
    * ceil. */
   OverlayRingOffsets offsets = {};
   offsets.left = (LONG)floorf(outer_left - (float)efb.left);
   offsets.top = (LONG)floorf(outer_top - (float)efb.top);
   offsets.right = (LONG)ceilf(outer_right - (float)efb.right);
   offsets.bottom = (LONG)ceilf(outer_bottom - (float)efb.bottom);
   return offsets;
}

/* ---------------- DIB + render resources ---------------- */

/* Recreate the 32bpp top-down BGRA DIB and reselect it into mem_dc whenever
 * the ring size changes. A new DIB is zero-initialized by CreateDIBSection,
 * so no ghosting is possible after a resize. */
void Overlay::overlay_surface_resize_dib(OverlaySurface *surf, int w, int h) {
   if (w == surf->dib_w && h == surf->dib_h) {
      return;
   }
   w = std::max(w, 1);
   h = std::max(h, 1);

   if (surf->dib != nullptr) {
      SelectObject(surf->mem_dc, surf->old_dib);
      DeleteObject(surf->dib);
      surf->dib = nullptr;
      surf->dib_bits = nullptr;
   }

   BITMAPINFO bitmap_info;
   memset(&bitmap_info, 0, sizeof bitmap_info);
   bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
   bitmap_info.bmiHeader.biWidth = w;
   bitmap_info.bmiHeader.biHeight = -h; /* top-down */
   bitmap_info.bmiHeader.biPlanes = 1;
   bitmap_info.bmiHeader.biBitCount = OVERLAY_BITS_PER_PIXEL; /* BGRA */
   bitmap_info.bmiHeader.biCompression = BI_RGB;

   void *bits = nullptr;
   surf->dib = CreateDIBSection(surf->mem_dc, &bitmap_info, DIB_RGB_COLORS,
                                &bits, nullptr, 0);
   if (surf->dib == nullptr) {
      ErrorW(L"Overlay: CreateDIBSection(%dx%d) failed", w, h);
      surf->dib_w = 0;
      surf->dib_h = 0;
      surf->dib_bits = nullptr;
      return;
   }
   surf->old_dib = (HBITMAP)SelectObject(surf->mem_dc, surf->dib);
   surf->dib_w = w;
   surf->dib_h = h;
   surf->dib_bits = bits;
}

void Overlay::overlay_surface_release_render(OverlaySurface *surf) {
   if (surf->dib != nullptr) {
      SelectObject(surf->mem_dc, surf->old_dib);
      DeleteObject(surf->dib);
      surf->dib = nullptr;
      surf->old_dib = nullptr;
      surf->dib_bits = nullptr;
   }
   surf->dib_w = 0;
   surf->dib_h = 0;
   if (surf->mem_dc != nullptr) {
      DeleteDC(surf->mem_dc);
      surf->mem_dc = nullptr;
   }
}

auto Overlay::overlay_surface_init_render(OverlaySurface *surf) -> HRESULT {
   if (!GdiplusRuntime::Instance().ok()) {
      ErrorW(L"Overlay: GdiplusStartup failed");
      return E_FAIL;
   }
   surf->mem_dc = CreateCompatibleDC(nullptr);
   if (surf->mem_dc == nullptr) {
      ErrorW(L"Overlay: CreateCompatibleDC failed");
      return E_FAIL;
   }
   overlay_surface_resize_dib(surf, 1, 1);
   return S_OK;
}

/* ---------------- paint ---------------- */

/* Present one surface with UpdateLayeredWindow. pptDst = nullptr: the position
 * comes from SetWindowPos/DeferWindowPos, never the upload. */
auto Overlay::overlay_ulw_upload(OverlaySurface *surf) -> HRESULT {
   SIZE size = {.cx = surf->dib_w, .cy = surf->dib_h};
   POINT src = {.x = 0, .y = 0};
   BLENDFUNCTION blend;
   blend.BlendOp = AC_SRC_OVER;
   blend.BlendFlags = 0;
   blend.SourceConstantAlpha = OVERLAY_ALPHA_OPAQUE;
   blend.AlphaFormat = AC_SRC_ALPHA;
   if (UpdateLayeredWindow(surf->hwnd, nullptr, nullptr, &size, surf->mem_dc,
                           &src, 0, &blend, ULW_ALPHA) == 0) {
      ErrorW(L"Overlay: UpdateLayeredWindow failed (%lu)",
             (unsigned long)GetLastError());
      return E_FAIL;
   }
   return S_OK;
}

/* Build the rounded-rect ring outline path (FillModeAlternate) for a given
 * ring rect. Every strip rasterizes this SAME path geometry — that is what
 * makes overlapping seam pixels byte-identical. Unchecked by design: the
 * builder can only fail on invalid arguments, which DrawPath would reflect.
 */
void Overlay::overlay_build_ring_path(Gdiplus::GraphicsPath &path, float left,
                                      float top, float right, float bottom,
                                      float radius) {
   float const r = radius;
   if (r <= OVERLAY_RADIUS_EPSILON) {
      /* Plain rectangle outline: no corner arcs. */
      path.AddLine(left, top, right, top);
      path.AddLine(right, top, right, bottom);
      path.AddLine(right, bottom, left, bottom);
      path.AddLine(left, bottom, left, top);
   } else {
      float const arc = 2.0F * r;
      path.AddLine(left + r, top, right - r, top);
      path.AddArc(right - arc, top, arc, arc, -OVERLAY_ARC_QUARTER,
                  OVERLAY_ARC_QUARTER);
      path.AddLine(right, top + r, right, bottom - r);
      path.AddArc(right - arc, bottom - arc, arc, arc, 0.0F,
                  OVERLAY_ARC_QUARTER);
      path.AddLine(right - r, bottom, left + r, bottom);
      path.AddArc(left, bottom - arc, arc, arc, OVERLAY_ARC_QUARTER,
                  OVERLAY_ARC_QUARTER);
      path.AddLine(left, bottom - r, left, top + r);
      path.AddArc(left, top, arc, arc, OVERLAY_ARC_HALF, OVERLAY_ARC_QUARTER);
   }
   path.CloseFigure();
}

/* PREMULTIPLY pass: GDI+ writes straight (non-premultiplied) alpha, but the
 * ULW upload uses AC_SRC_ALPHA, which expects premultiplied BGRA. Pixels with
 * full or zero alpha are skipped. `rects`/`rect_count` restrict the pass to
 * ring strips; pass nullptr to process the whole DIB (each strip DIB is tiny).
 */
void Overlay::overlay_premultiply(OverlaySurface *surf, const RECT *rects,
                                  int rect_count) {
   auto *bits = (UINT32 *)surf->dib_bits;
   UINT32 pixel;
   UINT32 alpha;
   UINT32 blue;
   UINT32 green;
   UINT32 red;
   if (rects == nullptr) {
      size_t const total = (size_t)surf->dib_w * (size_t)surf->dib_h;
      for (size_t i = 0; i < total; i++) {
         pixel = bits[i];
         alpha = (pixel >> OVERLAY_ALPHA_SHIFT) & OVERLAY_CHANNEL_MASK;
         if (alpha != 0 && alpha != OVERLAY_CHANNEL_MAX) {
            blue =
                ((pixel & OVERLAY_CHANNEL_MASK) * alpha) / OVERLAY_CHANNEL_MAX;
            green = (((pixel >> OVERLAY_GREEN_SHIFT) & OVERLAY_CHANNEL_MASK) *
                     alpha) /
                    OVERLAY_CHANNEL_MAX;
            red = (((pixel >> OVERLAY_RED_SHIFT) & OVERLAY_CHANNEL_MASK) *
                   alpha) /
                  OVERLAY_CHANNEL_MAX;
            bits[i] = (pixel & OVERLAY_ALPHA_MASK) |
                      (red << OVERLAY_RED_SHIFT) |
                      (green << OVERLAY_GREEN_SHIFT) | blue;
         }
      }
      return;
   }
   for (int s = 0; s < rect_count; s++) {
      for (int y = rects[s].top; y < rects[s].bottom; y++) {
         size_t const row = (size_t)y * (size_t)surf->dib_w;
         for (int x = rects[s].left; x < rects[s].right; x++) {
            pixel = bits[row + (size_t)x];
            alpha = (pixel >> OVERLAY_ALPHA_SHIFT) & OVERLAY_CHANNEL_MASK;
            if (alpha != 0 && alpha != OVERLAY_CHANNEL_MAX) {
               blue = ((pixel & OVERLAY_CHANNEL_MASK) * alpha) /
                      OVERLAY_CHANNEL_MAX;
               green =
                   (((pixel >> OVERLAY_GREEN_SHIFT) & OVERLAY_CHANNEL_MASK) *
                    alpha) /
                   OVERLAY_CHANNEL_MAX;
               red = (((pixel >> OVERLAY_RED_SHIFT) & OVERLAY_CHANNEL_MASK) *
                      alpha) /
                     OVERLAY_CHANNEL_MAX;
               bits[row + (size_t)x] = (pixel & OVERLAY_ALPHA_MASK) |
                                       (red << OVERLAY_RED_SHIFT) |
                                       (green << OVERLAY_GREEN_SHIFT) | blue;
            }
         }
      }
   }
}

/* Draw the shared ring path into one strip's DIB and present it. The strip
 * surface is tiny (a perimeter strip), and the world transform is translated
 * by the strip origin so the SAME path geometry lands in every surface — the
 * corner overlap bands are then byte-identical by construction. */
auto Overlay::overlay_paint_strip_surface(OverlaySurface *surf) const
    -> HRESULT {
   if ((surf->hwnd == nullptr) || (surf->mem_dc == nullptr) ||
       (surf->dib == nullptr) || (surf->dib_bits == nullptr)) {
      ErrorW(L"Overlay: strip paint guard (hwnd=%p dc=%p dib=%p bits=%p)",
             surf->hwnd, surf->mem_dc, surf->dib, surf->dib_bits);
      return E_FAIL;
   }
   if (this->geometry_valid == FALSE) {
      ErrorW(L"Overlay: strip paint before geometry valid");
      return E_FAIL;
   }

   /* COLORREF is 0x00BBGGRR; GDI+ ARGB is 0xAARRGGBB (opaque ring). */
   Gdiplus::ARGB const argb =
       OVERLAY_ALPHA_MASK |
       ((UINT32)GetRValue(this->color) << OVERLAY_RED_SHIFT) |
       ((UINT32)GetGValue(this->color) << OVERLAY_GREEN_SHIFT) |
       (UINT32)GetBValue(this->color);

   /* Fresh surface: the DIB is tiny, so a whole-surface clear is cheapest. */
   memset(surf->dib_bits, 0,
          (size_t)surf->dib_w * (size_t)surf->dib_h * sizeof(UINT32));

   /* GDI+ antialiased ring outline: FillModeAlternate (default) path,
    * round-join pen drawn in DIB-local coordinates. DIB-local coordinates: the
    * ring frame offset by the strip origin. Single mode feeds GDI+ small
    * non-negative DIB coords; screen-space coords (negative for edge-flush
    * windows, ~4k-8k on multi-monitor) made the old GdipDrawPath fail with
    * status 2 (InvalidParameter). The offset is an exact integer translation of
    * the same float geometry, so every strip rasterizes identical coverage —
    * the seam invariant holds. */
   Gdiplus::Graphics graphics(surf->mem_dc);
   graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

   Gdiplus::GraphicsPath path;
   overlay_build_ring_path(
       path, (float)this->ring_frame.left - (float)surf->rect.left,
       (float)this->ring_frame.top - (float)surf->rect.top,
       (float)this->ring_frame.right - (float)surf->rect.left,
       (float)this->ring_frame.bottom - (float)surf->rect.top,
       this->paint_radius);

   Gdiplus::Pen pen(Gdiplus::Color(argb), this->paint_stroke);
   /* The mingw/w32api Gdiplus::Pen wrapper hardcodes UnitWorld (no Unit ctor
    * parameter, no SetUnit). This is pixel-equivalent to the old UnitPixel
    * pen here: the Graphics is created straight from the DIB memory DC with an
    * identity world transform and the default UnitDisplay page unit, so world
    * units map 1:1 to device pixels. A UnitWorld pen could only scale under a
    * non-identity world/page transform, which this paint path never sets. */
   pen.SetLineJoin(Gdiplus::LineJoinRound);
   graphics.DrawPath(&pen, &path);

   if (graphics.GetLastStatus() != Gdiplus::Ok) {
      ErrorW(L"Overlay::overlay_paint_strip_surface: GDI+ status %d",
             graphics.GetLastStatus());
      return E_FAIL;
   }

   /* Whole-DIB premultiply: every pixel in the strip may carry AA alpha. */
   overlay_premultiply(surf, nullptr, 0);

   return overlay_ulw_upload(surf);
}

#ifndef NDEBUG
/* Field invariant check: the corner overlap bands between adjacent strips
 * must contain byte-identical pixels (same path, same pen, translate-only
 * transform), or the composed ring would show a seam at a joint. Converts the
 * identical-pixel assumption into a verified invariant on real hardware. */
auto Overlay::overlay_verify_seams() -> BOOL {
   /* One extra brace pair around the whole list: std::array is an aggregate
    * whose sole member is its internal C array, so the top-level list must
    * contain exactly one element (that array) rather than the four rows. */
   constexpr std::array<std::array<int, 2>, OVERLAY_STRIP_COUNT> k_seam_pairs =
       {{
           {{OVERLAY_STRIP_TOP, OVERLAY_STRIP_LEFT}},
           {{OVERLAY_STRIP_TOP, OVERLAY_STRIP_RIGHT}},
           {{OVERLAY_STRIP_BOTTOM, OVERLAY_STRIP_LEFT}},
           {{OVERLAY_STRIP_BOTTOM, OVERLAY_STRIP_RIGHT}},
       }};

   for (int pair_index = 0; pair_index < OVERLAY_STRIP_COUNT; pair_index++) {
      OverlaySurface *a = &this->surfaces[k_seam_pairs[pair_index][0]];
      OverlaySurface *b = &this->surfaces[k_seam_pairs[pair_index][1]];
      RECT overlap = {};
      if ((IntersectRect(&overlap, &a->rect, &b->rect) == FALSE) ||
          overlap.right <= overlap.left || overlap.bottom <= overlap.top) {
         continue; /* no overlap band — nothing to compare */
      }
      for (int y = overlap.top; y < overlap.bottom; y++) {
         for (int x = overlap.left; x < overlap.right; x++) {
            UINT32 const pixel_a =
                ((UINT32 *)a->dib_bits)[((size_t)(y - a->rect.top) *
                                         (size_t)a->dib_w) +
                                        (size_t)(x - a->rect.left)];
            UINT32 const pixel_b =
                ((UINT32 *)b->dib_bits)[((size_t)(y - b->rect.top) *
                                         (size_t)b->dib_w) +
                                        (size_t)(x - b->rect.left)];
            if (pixel_a != pixel_b) {
               ErrorW(L"Overlay: seam mismatch strip %d/%d at (%d,%d): "
                      L"0x%08lx vs 0x%08lx",
                      k_seam_pairs[pair_index][0], k_seam_pairs[pair_index][1],
                      x, y, (unsigned long)pixel_a, (unsigned long)pixel_b);
               return FALSE;
            }
         }
      }
   }
   return TRUE;
}
#endif /* !NDEBUG */

/* Hide every surface of the ring. HWND_NOTOPMOST (no SWP_NOZORDER) also
 * clears any topmost band. Direct SetWindowPos, not a deferred batch: deferred
 * repositioning of these WS_EX_LAYERED popups is silently discarded, so the
 * band would stay put and stay visible. */
void Overlay::overlay_hide_ring() {
   for (int index = 0; index < OVERLAY_STRIP_COUNT; index++) {
      SetWindowPos(this->surfaces[index].hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                   SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
   }
}

/* Sync the four strips to the ring frame derived from `efb` (live
 * DWMWA_EXTENDED_FRAME_BOUNDS plus its measured insets, or a caller-supplied
 * rect such as a pending move target with zero insets). Resizes strip DIBs on
 * a size change, repaints when dirty (seam-verified in debug builds), and
 * repositions the band — or bails when everything is already in place. */
void Overlay::overlay_sync_strips(const RECT *efb, const OverlayInsets *insets,
                                  DWORD gate_cloaked, BOOL was_visible) {
   UINT const dpi = overlay_dpi();
   float const stroke = DpiSystem::ScaleF((float)this->width, dpi);

   /* Ring frame = EFB inflated by width/2, shifted inward by the inset; the
    * window is the frame inflated by width/2 plus the safe margin so the
    * stroke centered on the frame edge stays inside the surface. */
   float const frame_left =
       (float)efb->left - (stroke / 2.0F) + (float)insets->left;
   float const frame_top =
       (float)efb->top - (stroke / 2.0F) + (float)insets->top;
   float const frame_right =
       (float)efb->right + (stroke / 2.0F) - (float)insets->right;
   float const frame_bottom =
       (float)efb->bottom + (stroke / 2.0F) - (float)insets->bottom;

   /* Four thin layered windows: ring geometry computed once per ring from the
    * target's monitor DPI and shared by all surfaces; each strip paints the
    * same ring path translated into its own surface. */
   RECT ring_frame;
   ring_frame.left = (LONG)floorf(frame_left);
   ring_frame.top = (LONG)floorf(frame_top);
   ring_frame.right = (LONG)ceilf(frame_right);
   ring_frame.bottom = (LONG)ceilf(frame_bottom);
   if (ring_frame.right <= ring_frame.left ||
       ring_frame.bottom <= ring_frame.top) {
      Warn("OVL degenerate frame target=%p %dx%d", this->target,
           ring_frame.right - ring_frame.left,
           ring_frame.bottom - ring_frame.top);
      return;
   }

   /* Clamp the corner radius to the ring's half-extents so tiny rings do not
    * degenerate into self-intersecting arc geometry. Square corners when
    * maximized/snapped (both set WS_MAXIMIZE). */
   float radius = (IsZoomed(this->target) != FALSE)
                      ? 0.0F
                      : DpiSystem::ScaleF((float)this->radius, dpi);
   float const max_radius = fminf((frame_right - frame_left) / 2.0F,
                                  (frame_bottom - frame_top) / 2.0F);
   radius = std::min(radius, max_radius);

   this->ring_frame = ring_frame;
   this->paint_stroke = stroke;
   this->paint_radius = radius;
   this->geometry_valid = TRUE;

   std::array<RECT, OVERLAY_STRIP_COUNT> strips;
   OverlayComputeStripRects(&ring_frame, stroke, radius, OVERLAY_SAFE_MARGIN,
                            &strips);

   BOOL size_changed = FALSE;
   for (int index = 0; index < OVERLAY_STRIP_COUNT; index++) {
      OverlaySurface *surf = &this->surfaces[index];
      surf->rect = strips[index];
      int w = surf->rect.right - surf->rect.left;
      int h = surf->rect.bottom - surf->rect.top;
      w = std::max(w, 1);
      h = std::max(h, 1);
      if (w != surf->dib_w || h != surf->dib_h) {
         overlay_surface_resize_dib(surf, w, h);
         size_changed = TRUE;
      }
   }

   /* Size change recreates the DIBs; only then (or a color change) does
    * anything repaint — pure position moves paint nothing. A hidden→shown
    * transition forces one fresh repaint regardless (covers fullscreen
    * exit). */
   if ((size_changed != FALSE) || (was_visible == FALSE)) {
      this->dirty = TRUE;
   }

   if (this->dirty != FALSE) {
      BOOL all_ok = TRUE;
      for (int index = 0; index < OVERLAY_STRIP_COUNT; index++) {
         if (FAILED(overlay_paint_strip_surface(&this->surfaces[index]))) {
            all_ok = FALSE; /* any strip failure keeps the ring dirty */
         }
      }
#ifndef NDEBUG
      /* Field invariant: overlapping corner pixels must be byte-identical
       * across adjacent strips, or the composition would show a seam. */
      if ((all_ok != FALSE) && (overlay_verify_seams() == FALSE)) {
         all_ok = FALSE;
      }
#endif
      if (all_ok != FALSE) {
         this->dirty = FALSE;
      }
   }

   /* Skip-when-clean early-out: geometry, visibility and the full z-band all
    * intact — no SetWindowPos churn on every commit. */
   BOOL unchanged = overlay_band_intact();
   if (unchanged != FALSE) {
      for (int index = 0; index < OVERLAY_STRIP_COUNT; index++) {
         RECT current = {};
         if ((GetWindowRect(this->surfaces[index].hwnd, &current) == FALSE) ||
             current.left != this->surfaces[index].rect.left ||
             current.top != this->surfaces[index].rect.top ||
             current.right != this->surfaces[index].rect.right ||
             current.bottom != this->surfaces[index].rect.bottom ||
             (IsWindowVisible(this->surfaces[index].hwnd) == FALSE)) {
            unchanged = FALSE;
            break;
         }
      }
   }
   if (unchanged != FALSE) {
      return;
   }

   /* Reposition all four in chain order, keeping the band glued to the target.
    * Direct SetWindowPos, NOT a deferred batch: the diagnostic build proved
    * BeginDeferWindowPos/DeferWindowPos/EndDeferWindowPos is silently
    * discarded for these WS_EX_LAYERED popups (all strips stayed at the
    * creation position (0,0) and unshown). */
   HWND after = overlay_insert_after();
   for (int index = 0; index < OVERLAY_STRIP_COUNT; index++) {
      int const surface = k_strip_chain[index];
      OverlaySurface *surf = &this->surfaces[surface];
      SetWindowPos(surf->hwnd, after, surf->rect.left, surf->rect.top,
                   surf->rect.right - surf->rect.left,
                   surf->rect.bottom - surf->rect.top,
                   SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOSENDCHANGING |
                       SWP_NOOWNERZORDER);
      after = surf->hwnd;
   }

   this->last_efb = *efb;
   this->last_cloaked = gate_cloaked;
   this->last_efb_valid = TRUE;
}

/* Sync the ring to the target's live geometry through the change-gate. The
 * caller (OverlayFlush) guarantees the target is at rest — a landed rect —
 * so the EFB read and the inset measurement are in the same space. */
void Overlay::overlay_sync_position(BOOL suppress) {
   for (int index = 0; index < OVERLAY_STRIP_COUNT; index++) {
      if (IsWindow(this->surfaces[index].hwnd) == 0) {
         return;
      }
   }

   DWORD gate_cloaked = 0;
   BOOL const cloak_ok = SUCCEEDED(DwmGetWindowAttribute(
       this->target, DWMWA_CLOAKED, &gate_cloaked, sizeof gate_cloaked));

   /* Change-gate: a clean ring whose target has not moved and is not newly
    * cloaked does no work — one EFB read + one cloak read replace the ~15
    * syscalls below. Move/resize is caught by the EFB compare; hide/
    * iconify/fullscreen transitions are owned by OverlayReconcile, which
    * marks the ring dirty, so a clean ring here implies visibility is
    * unchanged too. */
   RECT efb = {};
   BOOL const efb_ok = SUCCEEDED(DwmGetWindowAttribute(
       this->target, DWMWA_EXTENDED_FRAME_BOUNDS, &efb, sizeof efb));
   if ((this->dirty == FALSE) && (this->last_efb_valid != FALSE) &&
       (efb_ok != FALSE) && (cloak_ok != FALSE) &&
       gate_cloaked == this->last_cloaked && efb.left == this->last_efb.left &&
       efb.top == this->last_efb.top && efb.right == this->last_efb.right &&
       efb.bottom == this->last_efb.bottom) {
      return;
   }

   /* Desired visibility is keyed on cloak state, not IsWindowVisible. */
   BOOL const shown = static_cast<BOOL>(
       (gate_cloaked == 0U) && (IsWindowVisible(this->target) != FALSE) &&
       (IsIconic(this->target) == FALSE) && (suppress == 0));

   if (shown == 0) {
      overlay_hide_ring();
      /* Hidden rings are converged: clear dirty so the flush does not re-run
       * this hide and the deterministic z-assert churn that follows. The
       * hidden→shown transition below forces the repaint when the ring comes
       * back. */
      this->dirty = FALSE;
      if (efb_ok != FALSE) {
         this->last_efb = efb;
      }
      if (cloak_ok != FALSE) {
         this->last_cloaked = gate_cloaked;
      }
      this->last_efb_valid = TRUE;
      return;
   }

   BOOL was_visible = TRUE;
   for (int index = 0; index < OVERLAY_STRIP_COUNT; index++) {
      if (IsWindowVisible(this->surfaces[index].hwnd) == FALSE) {
         was_visible = FALSE;
         break;
      }
   }

   if (efb_ok == FALSE) {
      Error("OVL EFB FAIL target=%p hr=0x%08lx", this->target,
            (unsigned long)E_FAIL);
      return; /* border dies, WM survives */
   }

   /* The target is at rest (landed gate in OverlayFlush), so the EFB and the
    * inset measurement are in the same space: the deltas are the true
    * chrome/frame widths, never a move displacement. */
   OverlayInsets insets = {.left = 0, .top = 0, .right = 0, .bottom = 0};
   overlay_measure_insets(this->target, &efb, &insets);
   overlay_sync_strips(&efb, &insets, gate_cloaked, was_visible);
}

/* Reclaim windows whose target HWND no longer exists (missed
 * EVENT_OBJECT_DESTROY — e.g. the process terminated or the window was
 * destroyed faster than the hook delivered). Without this, a stale window pins
 * its four unowned strip windows forever: OverlayReconcile early-returns on
 * !IsWindow(target) and never tears them down, and the strips were created
 * with parent nullptr so Windows does not destroy them with the target. Each
 * leaked ring is 4 USER objects; enough of them exhausts the process USER
 * quota (error 1158). Reuses the kill-path teardown (CleanupTerminatedWindow)
 * so workspace removal, fullscreen accounting, focus fallback and layout stay
 * consistent. Backward iteration: removal shifts the registry array. */
void Overlay::ReclaimStaleWindows(struct BFWMContext *ctx) {
   for (size_t index = ctx->windows->Windows().size(); index > 0; index--) {
      Window *win = ctx->windows->Windows()[index - 1].get();
      if (win == nullptr)
         continue;
      HWND h = win->GetHwnd();
      // Already-marked stale (e.g. a cloak op hit TYPE_E_ELEMENTNOTFOUND), or
      // the HWND is no longer a live window.
      BOOL reclaim = win->IsStale();
      if (reclaim == FALSE) {
         if (IsWindow(h) == FALSE) {
            reclaim = TRUE;
         } else {
            // Cross-process HWND reuse: the handle is valid but now belongs to
            // a different process than when we registered it. The original
            // window is gone; reclaim so we stop operating on a stranger.
            DWORD cur_pid = 0;
            GetWindowThreadProcessId(h, &cur_pid);
            if ((cur_pid != 0) && (cur_pid != win->GetOwnerPid()))
               reclaim = TRUE;
         }
      }
      if (reclaim == TRUE) {
         WarnW(L"Reclaimed stale window (target gone or reused): %p", h);
         CleanupTerminatedWindow(ctx, h);
      }
   }
}
