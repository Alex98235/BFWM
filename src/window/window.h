/**
 * @file window.h
 * @brief Window data structure and global window registry.
 *
 * Defines the Window type representing a managed application window,
 * along with the WindowRegistry that owns and tracks windows (a vector
 * of unique_ptr for iteration/lifecycle plus an HWND-indexed hash map for
 * O(1) lookup).
 */

#ifndef BFWM_WINDOW_H
#define BFWM_WINDOW_H

#include <dwmapi.h>
#include <memory>
#include <optional>
#include <string>
#include <synchapi.h>
#include <unordered_map>
#include <vector>
#include <windef.h>
#include <winnt.h>

#include "../core/sync.h"
#include "../workspace/placement.h"

/**
 * @brief Initial capacity for the managed windows array.
 */
enum {
   INITIAL_MANAGEDWINDOWS_SIZE = 10,
   WINDOW_HASH_INIT_CAP = 32,
};

class Workspace;
class Overlay;

/**
 * @brief Represents a managed application window.
 *
 * Tracks the window handle, layout properties (position, floating/fullscreen
 * state), workspace membership, cached metadata, and state flags used for
 * change detection and relayout scheduling. Owns its border overlay via
 * std::unique_ptr; the destructor is defined out-of-line in window.cpp where
 * Overlay is complete.
 */
class Window {
 public:
   Window(HWND hwnd, std::wstring title);
   ~Window(); /* out-of-line, defined in window.cpp where Overlay is complete */

   /// Windows handle (unique identifier)
   [[nodiscard]] auto GetHwnd() const -> HWND { return hwnd_; }
   /// Cached window title (avoids frequent GetWindowText calls)
   [[nodiscard]] auto GetTitle() const -> std::wstring const & {
      return title_;
   }

   /** @name Floating / fullscreen state
    * @{ */
   [[nodiscard]] auto IsFloating() const -> BOOL { return is_floating_; }
   void SetFloating(BOOL value) { is_floating_ = value; }
   [[nodiscard]] auto IsFullscreen() const -> BOOL { return is_fullscreen_; }
   void SetFullscreen(BOOL value) { is_fullscreen_ = value; }
   /// TRUE if fullscreen which should not be handled by the layout (fullscreen
   /// window), restored on shrink
   [[nodiscard]] auto IsUnmanagedFullscreen() const -> BOOL {
      return is_unmanaged_fullscreen_;
   }
   void SetUnmanagedFullscreen(BOOL value) { is_unmanaged_fullscreen_ = value; }
   /// TRUE if floated by maximize button, re-inserted into tree on restore down
   [[nodiscard]] auto IsMaximized() const -> BOOL { return is_maximized_; }
   void SetMaximized(BOOL value) { is_maximized_ = value; }
   /// Window locked to floating — toggles blocked
   [[nodiscard]] auto ForceFloating() const -> BOOL { return force_floating_; }
   void SetForceFloating(BOOL value) { force_floating_ = value; }
   /// Window locked to tiled — toggles blocked
   [[nodiscard]] auto ForceTiled() const -> BOOL { return force_tiled_; }
   void SetForceTiled(BOOL value) { force_tiled_ = value; }

   /// Position before entering float or fullscreen
   [[nodiscard]] auto SavedRect() const -> RECT { return saved_rect_; }
   void SetSavedRect(RECT value) { saved_rect_ = value; }
   /// Mutable pointer for in-place Win32 RECT helpers (GetWindowRect,
   /// OffsetRect, ClampToRect...)
   auto SavedRectPtr() -> RECT * { return &saved_rect_; }

   /// Per-side outer-to-visible frame deltas: GetWindowRect minus
   /// DWMWA_EXTENDED_FRAME_BOUNDS (the invisible resize border). All zeros if
   /// the EFB query fails (plain outer-frame clamp fallback).
   [[nodiscard]] auto MeasureFrameInsets() const -> FrameInsets;

   /// Returns the position the window wants when clamped into `bounds` (a
   /// monitor work area): the size pre-scale for DPI-unaware windows on scaled
   /// monitors, then a visible-frame clamp via ClampRectWithInsets. Does not
   /// modify the window; the caller applies the result (and keeps its existing
   /// EqualRect guard).
   [[nodiscard]] auto ClampTo(RECT bounds, UINT dpi) const -> RECT;
   /// GWL_STYLE before fullscreen
   [[nodiscard]] auto SavedStyle() const -> LONG { return saved_style_; }
   void SetSavedStyle(LONG value) { saved_style_ = value; }
   [[nodiscard]] auto SavedExstyle() const -> LONG { return saved_exstyle_; }
   void SetSavedExstyle(LONG value) { saved_exstyle_ = value; }

   /// Target workspace from window rules, -1 if not set
   [[nodiscard]] auto RuleTargetWorkspace() const -> int {
      return rule_target_workspace_;
   }
   void SetRuleTargetWorkspace(int value) { rule_target_workspace_ = value; }
   /** @} */

   /** @name Border overlay
    * @{ */
   /// Per-window border overlay ring, null until created
   [[nodiscard]] auto GetOverlay() const -> Overlay * { return overlay_.get(); }
   /* Out-of-line in window.cpp where Overlay is complete (the unique_ptr
    * move-assignment in the body requires a complete type). */
   void SetOverlay(std::unique_ptr<Overlay> overlay);

   /* Facade over the Overlay's public API. Each method null-guards overlay_
    * internally, so callers need no GetOverlay() != nullptr ceremony; on a
    * null/inert overlay they are no-ops (FlushOverlay returns FALSE). The
    * overlay keeps self-measuring its own EFB-client insets — none of these
    * take or pass insets. Deliberately NOT auto-dirtying: callers decide
    * whether a mutation marks dirty (commit-end flush) or syncs immediately.
    * All out-of-line in window.cpp where Overlay is complete. */
   void MarkOverlayDirty();
   /// Immediate sync to the target's current geometry/visibility.
   void SyncOverlay(BOOL suppress);
   /// Change ring color; lands at the next commit-end flush.
   void SetOverlayColor(COLORREF color);
   /// Change ring thickness/radius; lands at the next commit-end flush.
   void SetOverlayBorder(int width, int radius);
   /// Per-commit z/visibility reconcile (backstop for dropped queue events).
   void ReconcileOverlay(BOOL suppress, BOOL force = FALSE);
   /// Converge a dirty ring: sync + repaint once. TRUE when the sync+paint ran.
   auto FlushOverlay() -> BOOL;
   /// Re-assert the ring's z-slot directly above its target.
   void AssertOverlayZOrder();
   /** @} */

   /** @name Async move backpressure
    * @{ */
   /// Whether a layout move has ever been issued for this window. A window
   /// that has never been issued a move is by definition at rest.
   [[nodiscard]] auto HasLastIssued() const -> bool {
      return last_issued_.has_value();
   }
   /// Last issued layout rect; callers MUST check HasLastIssued() first.
   [[nodiscard]] auto LastIssued() const -> RECT {
      // NOLINTNEXTLINE
      return *last_issued_;
   }
   void SetLastIssued(RECT value) { last_issued_ = value; }
   /// Whether a layout move is currently in flight (issued, not yet landed).
   [[nodiscard]] auto MoveInFlight() const -> BOOL { return move_in_flight_; }
   void SetMoveInFlight(BOOL value) { move_in_flight_ = value; }
   [[nodiscard]] auto LastIssueTime() const -> ULONGLONG {
      return last_issue_time_;
   }
   void SetLastIssueTime(ULONGLONG value) { last_issue_time_ = value; }
   /// Consecutive layout moves that failed to land (stall re-issues without
   /// a landing). Reset on any landing; drives the self-healing float.
   [[nodiscard]] auto FailedLandings() const -> UINT {
      return failed_landings_;
   }
   void SetFailedLandings(UINT value) { failed_landings_ = value; }
   /// Whether the last issued move crossed monitors; while set, the landing
   /// gate trusts the app's own post-DPI rect once the window reaches the
   /// destination monitor (see MaybeIssueMove in placement.cpp).
   [[nodiscard]] auto IsCrossMonitorTrusted() const -> BOOL {
      return cross_monitor_trusted_;
   }
   void SetCrossMonitorTrusted(BOOL value) { cross_monitor_trusted_ = value; }
   /** @} */

   /** @name Cloak state
    * @{ */
   [[nodiscard]] auto IsCloaked() const -> BOOL { return cloaked_; }
   void SetCloaked(BOOL value) { cloaked_ = value; }
   /// Marked when a cloak op fails with TYPE_E_ELEMENTNOTFOUND: the HWND is
   /// valid but the shell has no view for it (reuse / post-wake registry
   /// reset). ReclaimStaleWindows purges these instead of retrying forever.
   [[nodiscard]] auto IsStale() const -> BOOL { return stale_; }
   void SetStale(BOOL value) { stale_ = value; }
   /// Owning process captured at registration; detects cross-process HWND
   /// reuse after sleep/wake (a valid HWND now belonging to another process).
   [[nodiscard]] auto GetOwnerPid() const -> DWORD { return owner_pid_; }
   /** @} */

 private:
   HWND hwnd_;
   std::wstring title_;
   /// Owning process at registration (see GetOwnerPid).
   DWORD owner_pid_ = 0;
   BOOL stale_ = FALSE;
   BOOL is_floating_ = FALSE;
   BOOL is_fullscreen_ = FALSE;
   BOOL is_unmanaged_fullscreen_ = FALSE;
   BOOL is_maximized_ = FALSE;
   BOOL force_floating_ = FALSE;
   BOOL force_tiled_ = FALSE;
   RECT saved_rect_{};
   LONG saved_style_ = 0;
   LONG saved_exstyle_ = 0;
   int rule_target_workspace_ = -1;
   std::unique_ptr<Overlay> overlay_;
   std::optional<RECT> last_issued_ = std::nullopt;
   ULONGLONG last_issue_time_ = 0;
   BOOL move_in_flight_ = FALSE;
   UINT failed_landings_ = 0;
   BOOL cross_monitor_trusted_ = FALSE;
   BOOL cloaked_ = FALSE;
};

/**
 * @brief Registry of all managed windows with HWND-indexed hash map for O(1)
 *        lookup.
 *
 * The registry owns every managed Window through a vector of unique_ptr, which
 * is retained for iteration. On Register the window is added to both
 * structures; on Remove it is destroyed and removed from both. FindByHwnd uses
 * only the hash map. A CriticalSection keeps registry access thread-safe.
 */
class WindowRegistry {
   /* lock_ is declared FIRST so it is destroyed LAST (members destruct in
    * reverse declaration order): the lock must stay alive while the vector's
    * Windows — and their overlays — are torn down. */
   CriticalSection lock_;

 public:
   WindowRegistry() = default;
   ~WindowRegistry() = default; /* remaining windows die with the vector */

   /* Appends (takes ownership), inserts into the hash map, returns the raw
    * pointer (still valid, now registry-owned) so callers can keep using it. */
   auto Register(std::unique_ptr<Window> window) -> Window *;
   /* O(1) lookup via hash map. */
   auto FindByHwnd(HWND hwnd) -> Window *;
   /* Linear-scan by pointer identity, erases from vector (destroys the Window)
    * and map. Returns true if found. NOT safe to call again on the same
    * pointer afterwards. */
   auto Remove(Window *win) -> bool;

   /// Const view of all managed windows, for iteration/lookup by index.
   auto Windows() const -> std::vector<std::unique_ptr<Window>> const & {
      return windows_;
   }

 private:
   /// All managed windows, registry-owned. Only mutating members may modify it.
   std::vector<std::unique_ptr<Window>> windows_;
   std::unordered_map<HWND, Window *> by_hwnd_;
};

static inline auto IsWindowManagedByLayout(const Window *win) -> BOOL {
   return static_cast<BOOL>((win->IsFloating() == FALSE) &&
                            (win->IsFullscreen() == FALSE) &&
                            (win->IsUnmanagedFullscreen() == FALSE));
}

/**
 * @brief Fetch the managed Window* for hwnd, or nullptr if the hwnd is not a
 *        managed, layout-member window. Floated/fullscreen windows are not in
 *        the layout engine; skipping the engine lookup avoids
 *        "No node with window handle" diagnostics. Mirrors the PlacementApply
 *        guard.
 *
 * @tparam Context The BFWM context type (deduced as BFWMContext at call
 *        sites; window.h cannot name the complete type because
 *        bfwm_context.h includes window.h)
 * @param ctx  The BFWM context, or nullptr
 * @param hwnd The window handle to look up
 * @return The managed, layout-member Window*, or nullptr
 */
template <typename Context>
[[nodiscard]] inline auto FindWindowIfManaged(Context *ctx, HWND hwnd)
    -> Window * {
   Window *win = (ctx != nullptr) ? ctx->windows->FindByHwnd(hwnd) : nullptr;
   if ((win == nullptr) || (IsWindowManagedByLayout(win) == FALSE))
      return nullptr;
   return win;
}

#endif
