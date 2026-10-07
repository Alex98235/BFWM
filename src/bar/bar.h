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
#include <windows.h>

struct BFWMContext;
class Monitor;
struct Clay_Context;

/** @brief Maximum number of workspace labels that can be displayed. */
enum { BAR_MAX_WS_LABELS = 32 };

/**
 * @brief Per-monitor status bar window controller.
 *
 * Encapsulates the state and message handling for one bar window:
 * the Clay context, cached indicator strings, and poll timers for
 * rate-limited system queries.
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
   /// Reset this bar's poll timers and cached indicator strings.
   void ResetPollTimers();

   /// Build the Clay layout for this bar at the given width.
   void BuildLayout(int width);

   /// Hit-test the workspace tabs exactly as rendered.
   /// @return The workspace array index under the pointer, or -1.
   auto FindWorkspaceTabUnderPointer(const Clay_String &text) -> int;

   /// Whether any indicator uses the given alignment.
   auto HasAlign(BarIndicatorAlign align) -> BOOL;

   /// Effective font size for an indicator.
   auto IndicatorFontSize(BarIndicatorConfig *indicator_config) -> int;

   /// Indicator text colour (explicit override or bar default).
   auto indicator_color(BarIndicatorConfig *indicator_config) const
       -> Clay_Color;

   /// Compute a workspace tab's width from its rendered label.
   auto ComputeWorkspaceTabWidth(BarIndicatorConfig *indicator_config,
                                 const char *label) -> int;

   /// Build the workspace-tab indicator.
   void BuildIndicatorWorkspaces(BarIndicatorConfig *indicator_config,
                                 int instance_id);

   /// Truncate a title to fit max_width, appending an ellipsis.
   void TruncateTitleWithEllipsis(std::wstring &wtitle, int max_width,
                                  int font_size);

   /// Build the window-title indicator.
   void BuildIndicatorTitle(BarIndicatorConfig *indicator_config,
                            int instance_id);

   /// Emit a Clay text element for a generic indicator.
   void ClayIndicatorText(BarIndicatorConfig *indicator_config, int instance_id,
                          const char *id_name, Clay_String str,
                          Clay_Color color, int min_width);

   /// Build the volume indicator.
   void BuildIndicatorVolume(BarIndicatorConfig *indicator_config,
                             int instance_id);

   /// Build the network indicator.
   void BuildIndicatorNetwork(BarIndicatorConfig *indicator_config,
                              int instance_id);

   /// Build the CPU indicator.
   void BuildIndicatorCpu(BarIndicatorConfig *indicator_config,
                          int instance_id);

   /// Build the memory indicator.
   void BuildIndicatorMemory(BarIndicatorConfig *indicator_config,
                             int instance_id);

   /// Build the clock indicator.
   void BuildIndicatorClock(BarIndicatorConfig *indicator_config,
                            int instance_id);

   /// Measure an indicator's rendered width.
   auto MeasureIndicatorWidth(BarIndicatorConfig *indicator_config) -> float;

   /// Measure the total width of all indicators with the given alignment.
   auto MeasureGroupWidth(BarIndicatorAlign align) -> float;

   /// Render the indicators for one alignment group.
   void RenderIndicatorsForAlign(BarIndicatorAlign align, int *type_counter);

   /// Compute the center-spacer layout parameters.
   void ComputeCenterLayout(float bar_width, BOOL has_left, BOOL has_center,
                            BOOL has_right, BOOL *use_abs_center,
                            float *spacerL_w);

   /// Render the bar content groups.
   void RenderBarContent(int *type_counter, BOOL has_left, BOOL has_center,
                         BOOL has_right, BOOL use_abs_center, float spacerL_w);

   /// Back-reference to the app context
   struct BFWMContext *ctx = nullptr;
   /// The monitor this bar belongs to
   Monitor *mon = nullptr;
   /// Configuration pointer captured at construction (consumed by Init)
   const struct BarConfig *init_cfg = nullptr;
   /// Bar configuration from config.lua
   BarConfig config = {};
   /// Clay layout context
   Clay_Context *clay_context = nullptr;
   /// GDI renderer configuration
   ClayGdiRendererConfig clay_cfg;
   /// Clay memory arena
   void *clay_arena = nullptr;
   /// The window's own handle (set on WM_CREATE)
   HWND hwnd = nullptr;

   /// Formatted workspace labels
   std::array<std::string, BAR_MAX_WS_LABELS> ws_labels;
   /// Current focused window title
   std::string title_buf;
   /// Formatted clock string
   std::string clock_buf;
   /// Formatted volume string
   std::string volume_buf;
   /// Formatted network string
   std::string network_buf;
   /// Formatted CPU string
   std::string cpu_buf;
   /// Formatted memory string
   std::string mem_buf;

   /// Last volume poll timestamp
   ULONGLONG volume_last_poll_ms = 0;
   /// Last network poll timestamp
   ULONGLONG network_last_poll_ms = 0;
   /// Last CPU poll timestamp
   ULONGLONG cpu_last_poll_ms = 0;
   /// Last memory poll timestamp
   ULONGLONG mem_last_poll_ms = 0;

   /// Cached volume string (avoid flicker)
   std::string volume_cached;
   /// Cached network string
   std::string network_cached;
   /// Cached CPU string
   std::string cpu_cached;
   /// Cached memory string
   std::string mem_cached;

   /// Whether we are inside a WM_PAINT handler
   BOOL in_paint = FALSE;
   /// Whether we are inside a Clay layout pass
   BOOL in_layout = FALSE;
};

#endif
