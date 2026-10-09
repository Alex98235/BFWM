/**
 * @file bar.h
 * @brief Per-monitor status bar.
 *
 * Creates and manages a GDI-based status bar window on each monitor,
 * displaying workspace labels, window titles, clock, and system
 * status indicators using the Clay UI library.
 */

#ifndef BFWM_BAR_H
#define BFWM_BAR_H

#include "../config/lua/parser.h"
#include "renderer/clay_gdi.h"
#include <array>
#include <string>
#include <vector>
#include <windows.h>

struct BFWMContext;
class Monitor;
struct Clay_Context;
struct IndicatorProvider;

/// Accessor for the static provider descriptor table. Declared as a friend of
/// Bar so the table can reference the private provider refresh methods.
auto BarProviderTable() -> const IndicatorProvider *;

/** @brief Maximum number of workspace labels that can be displayed. */
enum { BAR_MAX_WS_LABELS = 32 };

/** @brief The maximum number of decimals of precision */
enum { MAX_FORMAT_PRECISION = 15 };

/**
 * @brief A single named value in an indicator's value bag.
 *
 * Numeric values (`is_num`) carry a default precision used when the format
 * string does not specify one; string values (`str`) are emitted verbatim.
 */
struct IndicatorValue {
   std::string name;
   bool is_num = false;
   double num = 0.0;
   std::string str;
   int precision = 0; // default decimals for numeric
};

/**
 * @brief Per-indicator, per-slot runtime state.
 *
 * `text` doubles as the composed render string and the poll cache (kept on
 * provider failure so the bar does not flicker). Distinct instances of the
 * same indicator type own distinct runtimes.
 */
struct IndicatorRuntime {
   std::string text;                   // composed text; also the poll cache
   Clay_Color color{};                 // resolved color
   std::string state;                  // discrete state ("" if none)
   std::string icon;                   // resolved glyph
   std::vector<IndicatorValue> values; // value bag
   std::vector<std::string> item_text; // workspaces: per-tab text
   std::vector<int> item_width;        // workspaces: per-tab measured width
   ULONGLONG last_poll_ms = 0;
   bool polled = false;    // true once a provider refresh has run
   bool valid = false;     // false => nothing to draw
   bool color_set = false; // runtime.color came from the provider output
};

/**
 * @brief Look up the state rule that applies to an indicator runtime.
 *
 * Resolution order: a `state=` match against the runtime's discrete state
 * wins; otherwise the highest numeric `at` whose threshold is <= the primary
 * value; otherwise the lowest numeric `at` rule. Returns nullptr when the
 * configuration has no rules.
 */
auto IndicatorFindStateRule(const BarIndicatorConfig &cfg,
                            const IndicatorRuntime &runtime)
    -> const IndicatorStateRule *;

/**
 * @brief Compose an indicator's text from its effective format dialect.
 *
 * Uses `{name}` / `{name:.Nf}` substitution, with the special `{icon}` and
 * `{state}` tokens. Unknown names are emitted literally and logged once at
 * Debug. Exposed (rather than private) so the format engine can be unit
 * tested without a live bar.
 */
auto IndicatorComposeFormat(const BarIndicatorConfig &cfg,
                            const IndicatorRuntime &runtime) -> std::string;

/// Whether an indicator is due to be (re)polled.
///
/// Exposed for unit testing. `per_frame` providers always return true; polled
/// providers return false only after a successful poll while still inside the
/// rate window — keyed on `runtime.polled`/`last_poll_ms`, never on
/// drawability.
auto IndicatorShouldPoll(bool per_frame, ULONGLONG rate_ms,
                         const IndicatorRuntime &runtime, ULONGLONG now)
    -> bool;

/// Polled-provider last-good rule: when a non-per-frame refresh produced
/// nothing drawable, restore the whole `previous` runtime (text, value bag,
/// state, icon, color). Returns true when it restored. Per-frame providers
/// never restore, so a failed per-frame refresh renders nothing instead of
/// reviving stale state. Exposed for unit testing.
auto IndicatorKeepPrevious(bool per_frame, const IndicatorRuntime &previous,
                           IndicatorRuntime &runtime) -> bool;

/// Resolve the matched rule's icon/color effects into `icon_out`/`color_out`.
///
/// Precedence for color: matched-rule (if `has_color`) > indicator (if
/// `color_set`) > `bar_default`. `icon_out` is the matched rule's icon, or
/// empty when no rule supplies one (callers keep any provider default).
auto IndicatorResolveEffects(const BarIndicatorConfig &cfg,
                             const IndicatorRuntime &runtime,
                             Clay_Color bar_default, std::string &icon_out,
                             Clay_Color &color_out) -> void;

/**
 * @brief Map a custom indicator's Lua `output` result onto a runtime (success
 * path). Clears and repopulates the value bag/state/icon/color/text and sets
 * `runtime.valid` when any renderable field is present. Exposed for unit
 * testing.
 */
void ApplyIndicatorOutput(const LuaIndicatorOutput &out,
                          IndicatorRuntime &runtime);

/// Provider descriptor for an indicator type, or nullptr when out of range.
auto ProviderFor(BarIndicatorType type) -> const struct IndicatorProvider *;

/**
 * @brief Result of a bar pointer hit-test.
 *
 * `slot` is the config indicator slot under the pointer, or -1. `item` is the
 * provider-specific item index (workspaces: the workspace array index), or -1
 * for single-value indicators.
 */
struct BarHit {
   int slot = -1;
   int item = -1;
};

/// Workspace tab id-space index: `slot * BAR_MAX_WS_LABELS + ws_index`.
///
/// This is the single source of truth for the workspace tab/sub-id arithmetic,
/// shared by rendering and hit-testing so they cannot drift.
auto IndicatorWorkspaceIndex(int slot, int ws_index) -> uint32_t;

/// Clay element id for an indicator.
///
/// Single-value indicators use `("ind", slot)`; collections use
/// `("ws", IndicatorWorkspaceIndex(slot, item_or_ws_index))`. The exact same
/// helper is used by rendering and hit-testing.
auto IndicatorElementId(bool collection, int slot, int item_or_ws_index)
    -> Clay_ElementId;

/**
 * @brief Per-monitor status bar window controller.
 *
 * Encapsulates the state and message handling for one bar window:
 * the Clay context and one IndicatorRuntime per configured indicator.
 *
 * The Bar strictly owns its window handle (private `hwnd`, set on
 * WM_CREATE). A Monitor owns the Bar instance through
 * `std::unique_ptr<Bar>`; destroying the Bar destroys the window.
 * The file-static window procedure in bar.cpp dispatches window
 * messages to the public On* instance methods, and the static API
 * manages the bar subsystem as a whole.
 */
class Bar {
 public:
   /**
    * @brief Store the monitor, config, and context references.
    *
    * Nothing fallible happens here (no window or Clay arena creation);
    * call Init() afterwards.
    */
   Bar(class Monitor *mon, const struct BarConfig *cfg,
       struct BFWMContext *ctx);

   /// Release owned resources: destroys the window (if any), frees the Clay
   /// arena, and destroys the GDI renderer.
   ~Bar();

   /// Non-copyable.
   Bar(const Bar &) = delete;
   auto operator=(const Bar &) -> Bar & = delete;

   /**
    * @brief One-time bar subsystem initialisation (window class).
    */
   static void Setup();

   /**
    * @brief Request a redraw of every bar across all monitors.
    * @param ctx The BFWM context
    */
   static void UpdateAll(struct BFWMContext *ctx);

   /**
    * @brief Reset all system-status poll timers (e.g. after resume).
    * @param ctx The BFWM context
    */
   static void ResetPollTimers(struct BFWMContext *ctx);

   /**
    * @brief Create the bar window and Clay state.
    * @return true on success; on failure everything allocated so far is
    *         released and the object may be retried
    */
   auto Init() -> bool;

   /// Whether the bar window was successfully created.
   [[nodiscard]] auto IsInitialized() const -> bool;

   /// Show the bar window.
   void Show();

   /// Hide the bar window.
   void Hide();

   /**
    * @brief Ensure this bar stays at the top of the normal z-order tier.
    */
   void EnsureTopZ();

   /**
    * @brief Request a redraw of this bar window.
    */
   void Update();

   // -- Window message handlers (dispatched from BarWndProc) --

   /// WM_CREATE handler (sets the window's own handle and the data slot).
   auto OnCreate(HWND hwnd, LPARAM l_param) -> LRESULT;

   /// WM_TIMER handler (rate-limited refresh).
   auto OnTimer() -> LRESULT;

   /// WM_PAINT handler (double-buffered Clay render).
   auto OnPaint() -> LRESULT;

   /// WM_LBUTTONDOWN handler (workspace tab hit-test).
   auto OnLButtonDown(LPARAM l_param) -> LRESULT;

   /// WM_MOUSEWHEEL handler (workspace cycling).
   auto OnMouseWheel(WPARAM w_param, LPARAM l_param) -> LRESULT;

   /// WM_DESTROY handler (tears down the timer and the window data slot).
   void OnDestroy();

   /// The monitor this bar belongs to (public access for the window proc).
   [[nodiscard]] auto monitor() const -> class Monitor * { return mon; }

   /// The owning BFWM context (public access for the window proc).
   [[nodiscard]] auto context() const -> struct BFWMContext * { return ctx; }

 private:
   /// Access the process-wide provider descriptor table (defined in bar.cpp).
   friend auto BarProviderTable() -> const IndicatorProvider *;

   /// Reset this bar's poll timers and cached indicator runtimes.
   void ResetPollTimers();

   /// Build the Clay layout for this bar at the given width.
   void BuildLayout(int width);

   /// Hit-test the rendered indicator slots against the current pointer.
   auto HitTest() -> BarHit;

   /// Dispatch a click on a hit indicator (Lua override or provider default).
   void DispatchIndicatorClick(const BarHit &hit);

   /// Dispatch a scroll on a hit indicator (Lua override or provider default).
   void DispatchIndicatorScroll(const BarHit &hit, int delta);

   /// Default click handler: activate the clicked workspace tab.
   // NOLINTBEGIN(readability-convert-member-functions-to-static)
   void DefaultWorkspacesClick(const BarIndicatorConfig &cfg, int item);

   /// Default scroll handler: cycle workspaces.
   void DefaultWorkspacesScroll(const BarIndicatorConfig &cfg, int delta);
   // NOLINTEND(readability-convert-member-functions-to-static)

   /// Whether any indicator uses the given alignment.
   auto HasAlign(BarIndicatorAlign align) -> BOOL;

   /// Effective font size for an indicator.
   auto IndicatorFontSize(const BarIndicatorConfig *indicator_config) const
       -> int;

   /// Compute a workspace tab's width from its rendered label.
   auto ComputeWorkspaceTabWidth(const BarIndicatorConfig *indicator_config,
                                 const char *label) -> int;

   /// Truncate text to fit max_width, appending an ellipsis.
   void TruncateTextWithEllipsis(std::wstring &wtitle, int max_width,
                                 int font_size);

   // -- Unified indicator pipeline (runs before measure + render) --

   /// Refresh every indicator slot (rate-limited per provider).
   void RefreshAllIndicators();

   /// Refresh one indicator slot and compose its runtime text.
   void RefreshIndicator(int slot);

   /// Resolve state/icon/color/format and compose the runtime text.
   void ComposeIndicator(int slot);

   // -- Provider refresh callbacks --
   // NOLINTBEGIN(readability-convert-member-functions-to-static)
   void RefreshWorkspaces(const BarIndicatorConfig &cfg,
                          IndicatorRuntime &runtime);
   void RefreshTitle(const BarIndicatorConfig &cfg, IndicatorRuntime &runtime);
   void RefreshClock(const BarIndicatorConfig &cfg, IndicatorRuntime &runtime);
   void RefreshVolume(const BarIndicatorConfig &cfg, IndicatorRuntime &runtime);
   void RefreshNetwork(const BarIndicatorConfig &cfg,
                       IndicatorRuntime &runtime);
   void RefreshCpu(const BarIndicatorConfig &cfg, IndicatorRuntime &runtime);
   void RefreshMemory(const BarIndicatorConfig &cfg, IndicatorRuntime &runtime);
   void RefreshCustom(const BarIndicatorConfig &cfg, IndicatorRuntime &runtime);
   // NOLINTEND(readability-convert-member-functions-to-static)

   /// Render one indicator slot (collections render their items).
   void RenderIndicator(int slot);

   /// Emit a Clay text element for a single-value indicator.
   void ClayIndicatorText(int slot, Clay_String str, Clay_Color color,
                          int min_width);

   /// Measure an indicator's rendered width.
   auto MeasureIndicatorWidth(int slot) -> float;

   /// Measure the total width of all indicators with the given alignment.
   auto MeasureGroupWidth(BarIndicatorAlign align) -> float;

   /// Render the indicators for one alignment group.
   void RenderIndicatorsForAlign(BarIndicatorAlign align);

   /// Compute the center-spacer layout parameters.
   void ComputeCenterLayout(float bar_width, BOOL has_left, BOOL has_center,
                            BOOL has_right, BOOL *use_abs_center,
                            float *spacerL_w);

   /// Render the bar content groups.
   void RenderBarContent(BOOL has_left, BOOL has_center, BOOL has_right,
                         BOOL use_abs_center, float spacerL_w);

   /// Back-reference to the app context
   struct BFWMContext *ctx = nullptr;
   /// The monitor this bar belongs to
   Monitor *mon = nullptr;
   /// Configuration pointer captured at construction (consumed by Init)
   const struct BarConfig *init_cfg = nullptr;
   /// Bar configuration from config.lua
   BarConfig config = {};
   /// Per-slot indicator runtime state
   std::array<IndicatorRuntime, BAR_MAX_INDICATORS> runtimes;
   /// Clay layout context
   Clay_Context *clay_context = nullptr;
   /// GDI renderer configuration
   ClayGdiRendererConfig clay_cfg;
   /// Clay memory arena
   void *clay_arena = nullptr;
   /// The window's own handle (set on WM_CREATE)
   HWND hwnd = nullptr;

   /// Whether we are inside a WM_PAINT handler
   BOOL in_paint = FALSE;
   /// Whether we are inside a Clay layout pass
   BOOL in_layout = FALSE;
   /// Whether we are dispatching an indicator callback (re-entrancy guard)
   bool in_callback = false;
};

#endif
