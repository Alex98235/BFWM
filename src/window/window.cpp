/**
 * @file window.cpp
 * @brief Window and WindowRegistry implementation (RAII ownership).
 *
 * The registry owns every managed window: a std::vector of unique_ptr drives
 * iteration and lifecycle, an HWND-indexed std::unordered_map provides O(1)
 * lookup, and a CriticalSection (declared before the members it guards, so it
 * outlives them) keeps registry access thread-safe. Windows are created with
 * std::make_unique and handed to Register(), which takes ownership;
 * ~WindowRegistry destroys any remaining windows.
 */

#include "window.h"
#include "border/overlay.h"

#include "../dpi/dpi.h"
#include "../math/rect.h"
#include "../workspace/placement.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <windows.h>

Window::Window(HWND hwnd, std::wstring title)
    : hwnd_(hwnd), title_(std::move(title)) {
   DWORD pid = 0;
   GetWindowThreadProcessId(hwnd, &pid);
   owner_pid_ = pid;
}

/* Out-of-line so the header never needs Overlay complete: the
 * unique_ptr<Overlay> and std::wstring members self-destruct here. */
Window::~Window() = default;

/* Out-of-line for the same reason as ~Window: the unique_ptr<Overlay>
 * move-assignment in the body requires a complete Overlay type. */
void Window::SetOverlay(std::unique_ptr<Overlay> overlay) {
   overlay_ = std::move(overlay);
}

/* ── Border overlay facade ────────────────────────────────────────────────
 * Thin null-guarded forwards to the Overlay's public API. The overlay is
 * null until SetOverlay; on a null/inert overlay every method is a no-op
 * (FlushOverlay returns FALSE), mirroring the overlay's own fail-inert
 * contract so callers need no GetOverlay() != nullptr ceremony. No behavior
 * is added here: no auto-dirty, no inset plumbing. */

void Window::MarkOverlayDirty() {
   if (overlay_ != nullptr)
      overlay_->OverlayMarkDirty();
}

void Window::SyncOverlay(BOOL suppress) {
   if (overlay_ != nullptr)
      overlay_->OverlaySyncPosition(suppress);
}

void Window::SetOverlayColor(COLORREF color) {
   if (overlay_ != nullptr)
      overlay_->OverlaySetColor(color);
}

void Window::SetOverlayBorder(int width, int radius) {
   if (overlay_ != nullptr)
      overlay_->OverlaySetBorder(width, radius);
}

void Window::ReconcileOverlay(BOOL suppress, BOOL force) {
   if (overlay_ != nullptr)
      overlay_->OverlayReconcile(suppress, force);
}

auto Window::FlushOverlay() -> BOOL {
   return (overlay_ != nullptr) ? overlay_->OverlayFlush() : FALSE;
}

void Window::AssertOverlayZOrder() {
   if (overlay_ != nullptr)
      overlay_->OverlayAssertZOrder();
}

auto Window::MeasureFrameInsets() const -> FrameInsets {
   RECT gwr = {};
   if (GetWindowRect(hwnd_, &gwr) == FALSE)
      return {};
   RECT efb = {};
   if (FAILED(DwmGetWindowAttribute(hwnd_, DWMWA_EXTENDED_FRAME_BOUNDS, &efb,
                                    sizeof(efb))))
      return {};
   FrameInsets insets = {};
   insets.left = std::max(0L, efb.left - gwr.left);
   insets.top = std::max(0L, efb.top - gwr.top);
   insets.right = std::max(0L, gwr.right - efb.right);
   insets.bottom = std::max(0L, gwr.bottom - efb.bottom);
   return insets;
}

auto Window::ClampTo(RECT bounds, UINT dpi) const -> RECT {
   RECT out = saved_rect_;

   /* Only DPI-unaware windows are bitmap-stretched by the OS on a cross-DPI
    * reassociation; aware windows and <=100% destinations need no scaling.
    * Exact-handle comparison (AreDpiAwarenessContextsEqual conflates UNAWARE
    * with UNAWARE_GDISCALED — both are stretched, so detect each explicitly).
    */
   DPI_AWARENESS_CONTEXT awareness = GetWindowDpiAwarenessContext(hwnd_);
   BOOL const unaware = static_cast<BOOL>(
       (awareness == DPI_AWARENESS_CONTEXT_UNAWARE) ||
       (awareness == DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED));
   if ((unaware == TRUE) && (dpi > DpiSystem::BaseDpi())) {
      /* Read the window's logical (96-DPI virtualized) rect from an UNAWARE
       * thread context: the natural logical size is reliable whether or not
       * the stretch already happened, and the top-left corner is preserved by
       * the OS, so sizing from the logical rect pre-accounts for the stretch.
       */
      DPI_AWARENESS_CONTEXT prev_ctx =
          SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
      RECT logical = {};
      BOOL const got =
          (prev_ctx != nullptr) ? GetWindowRect(hwnd_, &logical) : FALSE;
      if (prev_ctx != nullptr)
         SetThreadDpiAwarenessContext(prev_ctx);

      if (got == TRUE) {
         ClampRectScaledToDpi(&out, &logical, &bounds, dpi);
      } else {
         ClampToRect(bounds, &out);
      }
   } else {
      ClampToRect(bounds, &out);
   }

   /* Visible-frame clamp LAST: the size pre-scale (above) works on the
    * unscaled logical size and clamps to bounds; the inset clamp then lets
    * the outer rect extend past the bounds by up to the invisible-border
    * inset on each side so the visible frame stays flush with the edge. */
   FrameInsets full = MeasureFrameInsets();
   if (overlay_ != nullptr) {
      /* Also keep the painted border ring inside the bounds: compose the
       * ring's per-edge extent (relative to the EFB) with the invisible-frame
       * insets. The top ring offset is negative — the ring pokes above the
       * EFB — so the combined top inset is reduced accordingly. */
      auto const ring = overlay_->OverlayMeasureRingOffsets(dpi);
      full.left += ring.left;
      full.top += ring.top;
      full.right += ring.right;
      full.bottom += ring.bottom;
   }
   ClampRectWithInsets(&out, &bounds, full);
   return out;
}

auto WindowRegistry::Register(std::unique_ptr<Window> window) -> Window * {
   ScopedLock const guard(lock_);
   windows_.push_back(std::move(window));
   Window *raw = windows_.back().get();
   by_hwnd_[raw->GetHwnd()] = raw;
   return raw;
}

auto WindowRegistry::FindByHwnd(HWND hwnd) -> Window * {
   ScopedLock const guard(lock_);
   auto entry = by_hwnd_.find(hwnd);
   return (entry != by_hwnd_.end()) ? entry->second : nullptr;
}

auto WindowRegistry::Remove(Window *win) -> bool {
   ScopedLock const guard(lock_);
   bool found = false;
   for (size_t i = 0; i < windows_.size(); i++) {
      if (windows_[i].get() == win) {
         /* Read win->GetHwnd() BEFORE windows_.erase — the Window dies at
          * erase. */
         by_hwnd_.erase(win->GetHwnd());
         windows_.erase(windows_.begin() + (ptrdiff_t)i);
         found = true;
         break;
      }
   }
   return found;
}
