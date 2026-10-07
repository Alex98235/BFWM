/**
 * @file snackbar.h
 * @brief Notification snackbar — on-screen toast notifications.
 *
 * Provides an overlay window that stacks short notification messages
 * in a configurable screen corner.  Supports per-monitor placement,
 * log-level filtering (bypassed by --notify / BFWM.notify()), hover
 * pause, click-to-dismiss, and optional click-to-expand for long messages.
 */

#ifndef BFWM_SNACKBAR_H
#define BFWM_SNACKBAR_H

#include <array>
#include <string>
#include <windows.h>

#include "../bar/renderer/clay_gdi.h"
#include "../logging/logger.h"

/** @brief Capacity limits for the notification snackbar. */
enum {
   SNACKBAR_QUEUE_MAX = 32,
   SNACKBAR_ACTIVE_MAX = 16,
};

/** @brief Window class name for the IPC message-only window used by --notify */
#define SNACKBAR_NOTIFY_CLASS L"BFWMNotify"

/**
 * @brief Log-level threshold for the snackbar.
 *
 * Only notifications whose SnackbarLogLevel is at or below the
 * configured threshold are displayed.  Higher values are more verbose.
 * The --notify CLI flag and BFWM.notify() always bypass this check.
 */
using SnackbarLogLevel = enum {
   /// Suppress all notifications
   SNACKBAR_LOG_NONE = 0,
   /// Errors and fatal messages only
   SNACKBAR_LOG_ERROR = 1,
   /// Warnings and above
   SNACKBAR_LOG_WARN = 2,
   /// Informational notices and above
   SNACKBAR_LOG_INFO = 3,
   /// Everything, including debug
   SNACKBAR_LOG_DEBUG = 4,
};

/** @brief Screen corner where the snackbar overlay appears */
using SnackbarPosition = enum {
   /// Bottom-right corner
   SNACKBAR_POSITION_BOTTOM_RIGHT,
   /// Bottom-left corner
   SNACKBAR_POSITION_BOTTOM_LEFT,
   /// Top-right corner
   SNACKBAR_POSITION_TOP_RIGHT,
   /// Top-left corner
   SNACKBAR_POSITION_TOP_LEFT,
};

/**
 * @brief Per-severity colors for the left-hand level bar.
 */
using SnackbarLevelColors = struct {
   /// LOG_ERROR / LOG_FATAL color
   COLORREF error;
   /// LOG_WARN color
   COLORREF warn;
   /// LOG_INFO color (programmatic)
   COLORREF info;
   /// LOG_DEBUG color
   COLORREF debug;
   /// Default bar color (--notify / BFWM.notify())
   COLORREF normal;
};

/**
 * @brief Snackbar configuration (set via the Snackbar.* Lua table).
 *
 * All fields have sensible defaults defined in defaults.h.
 */
using SnackbarConfig = struct SnackbarConfig {
   /// Master toggle
   BOOL enabled;
   /// Minimum severity for programmatic messages
   SnackbarLogLevel log_level;
   /// Milliseconds before auto-dismiss
   int display_duration_ms;

   /// Screen corner
   SnackbarPosition position;

   /** @name Per-side margins from monitor work-area edge (pixels) */
   /** @{ */
   int margin_left;
   int margin_right;
   int margin_top;
   int margin_bottom;
   /** @} */

   /// Minimum overlay width (pixels, 0 = no minimum)
   int min_width;
   /// 0 = 30% of monitor width; >0 = explicit px
   int max_width;

   /** @name Inner padding inside each notification (pixels) */
   /** @{ */
   int padding_left;
   int padding_right;
   int padding_top;
   int padding_bottom;
   /** @} */

   /// Overlay background color
   COLORREF background;
   /// Notification text color
   COLORREF text;
   /// Divider color between stacked notifications
   COLORREF divider;
   /// Divider thickness (pixels)
   int divider_height;

   /// Font size (points)
   int font_size;
   /// Font face name
   std::string font_name;

   /// Window corner rounding (0 = sharp)
   int corner_radius;
   /// Window opacity (0–255)
   int opacity;
   /// Click to dismiss
   BOOL close_on_click;
   /// Hover pauses the dismiss timer
   BOOL pause_on_hover;
   /// Click a collapsed notification to expand it
   BOOL click_to_expand;
   /// Max visible notifications (1–16)
   int max_queue;
   /// 0=primary, -1=focused, N=display N
   int monitor;

   /// Per-severity level bar colors
   SnackbarLevelColors colors;
};

/**
 * @brief Convert a logger LogLevel to the corresponding SnackbarLogLevel.
 * @param level The logger-level enum value.
 * @return The matching snackbar level.
 */
static inline auto SnackbarLevelFromLogLevel(LogLevel level)
    -> SnackbarLogLevel {
   switch (level) {
   case LogLevel::Debug:
      return SNACKBAR_LOG_DEBUG;
   case LogLevel::Info:
      return SNACKBAR_LOG_INFO;
   case LogLevel::Warn:
      return SNACKBAR_LOG_WARN;
   case LogLevel::Error:
   case LogLevel::Fatal:
      return SNACKBAR_LOG_ERROR;
   default:
      return SNACKBAR_LOG_NONE;
   }
}

struct BFWMContext;
class Monitor;

/**
 * @brief Notification snackbar subsystem.
 *
 * Owns the layered overlay window, the message-only IPC window and the
 * Clay layout state.  Construct the instance, then call Init() to perform
 * the fallible setup (retryable on failure); the instance itself is owned
 * by BFWMContext::snackbar_state.  Messaging is exposed through the
 * static API below.
 */
class Snackbar {
 public:
   /// @name Construction / initialisation
   /// @{

   explicit Snackbar(struct BFWMContext *ctx);
   ~Snackbar();
   Snackbar(const Snackbar &) = delete;
   auto operator=(const Snackbar &) -> Snackbar & = delete;

   /**
    * @brief Perform the fallible window/Clay setup.
    *
    * Creates the overlay and IPC windows and initialises the Clay renderer.
    * On failure the partially-created state is cleaned up, the instance
    * reports IsInitialized() == false, and Init() may be retried.
    * @return true on success, false on failure.
    */
   auto Init() -> bool;

   /**
    * @brief Whether the overlay window is live and usable.
    * @return true once Init() has succeeded, false otherwise.
    */
   [[nodiscard]] auto IsInitialized() const -> bool;
   /// @}

   /// @name Public static API
   /// @{

   /**
    * @brief Per-frame tick — expire old items, pull from the queue,
    *        re-lay out, and reposition the window.
    * @param ctx The BFWM context.
    */
   static void Tick(struct BFWMContext *ctx);

   /**
    * @brief Enqueue a notification for display.
    * @param ctx     The BFWM context.
    * @param level   Severity level.
    * @param message Wide-character message text.
    * @param force   If TRUE, always show and use forced color for level bar.
    */
   static void Show(struct BFWMContext *ctx, SnackbarLogLevel level,
                    std::wstring *message, BOOL force);

   /**
    * @brief Enqueue a notification for display (LogLevel convenience).
    * @param level Logger severity level; converted via
    *              SnackbarLevelFromLogLevel() before buffering.
    */
   static void Show(struct BFWMContext *ctx, LogLevel level,
                    std::wstring *message, BOOL force);

   /**
    * @brief Convenience wrapper: log at Error level and show in snackbar.
    */
   static void Error(struct BFWMContext *ctx, const wchar_t *fmt, ...);

   /**
    * @brief Convenience wrapper: log at Warn level and show in snackbar.
    */
   static void Warn(struct BFWMContext *ctx, const wchar_t *fmt, ...);

   /**
    * @brief Convenience wrapper: log at Info level and show in snackbar.
    */
   static void Info(struct BFWMContext *ctx, const wchar_t *fmt, ...);

   /**
    * @brief Log GetLastError() to both the logger and the snackbar.
    *
    * Formats the system error message (like BFWMLogLastError) and
    * pushes it as an error-level notification.
    */
   static void LogLastError(struct BFWMContext *ctx, const char *prefix);
   /// @}

 private:
   // ============================================================
   // INTERNAL TYPES
   // ============================================================

   /// A single notification in the queue or active set.
   struct SnackbarNotification {
      std::string text;
      std::string truncated;
      SnackbarLogLevel level;
      ULONGLONG timestamp;
      ULONGLONG dismiss_deadline;
      int id;
      BOOL dismissed;
      BOOL forced;
      BOOL collapsed;
      BOOL hovered;
      int rendered_height;
   };

   // ============================================================
   // MEMBER STATE (was a zeroed C struct; now member-initialised)
   // ============================================================

   struct BFWMContext *ctx = nullptr;
   HWND hwnd = nullptr;
   HWND notify_hwnd = nullptr;
   Clay_Context *clay_context = nullptr;
   ClayGdiRendererConfig clay_cfg;
   void *clay_arena = nullptr;
   BOOL window_visible = FALSE;

   std::array<SnackbarNotification, SNACKBAR_QUEUE_MAX> queue{};
   int queue_head = 0;
   int queue_count = 0;

   std::array<SnackbarNotification, SNACKBAR_ACTIVE_MAX> active{};
   int active_count = 0;

   int next_id = 0;
   int total_width = 0;
   int total_height = 0;
   BOOL hovered = FALSE;
   ULONGLONG hover_enter_tick = 0;
   int effective_max_width = 0;
   BOOL force_render = FALSE;

   /// Target monitor resolved at rebuild time (cache for scale/position)
   const Monitor *target_monitor_ = nullptr;
   /// Target monitor DPI resolved at rebuild time (cache for scaling)
   UINT target_dpi_ = 0;

   // ============================================================
   // MESSAGE QUEUE API
   // ============================================================

   void ShowMessage(SnackbarLogLevel level, std::wstring *message, BOOL force);
   void TickNotifications();

   // ============================================================
   // WINDOW MESSAGE HANDLERS
   // ============================================================

   auto OnNcHitTest(LPARAM l_param) -> LRESULT;
   auto OnPaint() -> LRESULT;
   auto OnLButtonDown(LPARAM l_param) -> LRESULT;
   auto OnMouseMove() -> LRESULT;
   auto OnMouseLeave() -> LRESULT;
   auto OnDpiChanged(WPARAM w_param, LPARAM l_param) -> LRESULT;

   // ============================================================
   // LAYOUT & QUEUE HELPERS
   // ============================================================

   auto FindMonitor() -> const Monitor *;
   /// Effective DPI for the snackbar's monitor (system DPI when none found)
   [[nodiscard]] auto CurrentDpi() const -> UINT;
   /// Scale a logical config value by the snackbar's current DPI
   auto ScaleForCurrentDpi(int logical) -> int;
   static auto EffectiveWidth(const SnackbarConfig *cfg, const Monitor *mon,
                              UINT dpi) -> int;
   auto BuildText(SnackbarNotification *n, const SnackbarConfig *cfg,
                  int max_text_w) -> Clay_String;
   void BuildItem(const SnackbarConfig *cfg, int i, Clay_Color background);
   void BuildLayout(const SnackbarConfig *cfg, bool fill_width);
   void UpdateWindow();
   void DeriveDimensions(Clay_RenderCommandArray *cmds);
   auto RemoveExpired(ULONGLONG now) -> BOOL;
   auto DrainQueue(const SnackbarConfig *cfg) -> BOOL;
   void RebuildLayout(const SnackbarConfig *cfg);

   // ============================================================
   // STATIC UTILITIES
   // ============================================================

   static auto LevelColor(SnackbarLogLevel level, BOOL forced,
                          const SnackbarLevelColors *colors) -> COLORREF;
   static auto ColorRefToClay(COLORREF color_ref) -> Clay_Color;
   static void SnackbarClayError(Clay_ErrorData error);
   static void WCharToChar(std::wstring const *src, std::string *dst);

   // ============================================================
   // WINDOW PROCEDURES
   // ============================================================

   static auto CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w_param,
                                LPARAM l_param) -> LRESULT;
   static auto CALLBACK NotifyWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                      LPARAM lParam) -> LRESULT;
};

#endif
