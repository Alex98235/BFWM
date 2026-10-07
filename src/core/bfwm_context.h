/**
 * @file bfwm_context.h
 * @brief Central application context holding all window manager state.
 *
 * BFWMContext is the single root object that owns all mutable state in the
 * window manager: monitor/window/keybind registries, Lua config, keystroke
 * queue, modifier state, configuration, workspace references, and
 * synchronization primitives.
 *
 * All subsystems accept a BFWMContext* parameter instead of relying on
 * module-level globals. This enables deterministic init/cleanup ordering,
 * testability (tests create their own BFWMContext), and a clear ownership
 * model.
 *
 * Usage pattern:
 * @code
 *    BFWMContext *ctx = BFWMContextInit();
 *    // ... use ctx throughout the application ...
 *    BFWMContextFree(ctx);
 * @endcode
 */

#ifndef BFWM_CONTEXT_H
#define BFWM_CONTEXT_H

#include "../config/keybinds.h"
#include "../config/lua/parser.h"
#include "../dpi/dpi.h"
#include "../input/queue.h"
#include "../monitor/monitor.h"
#include "../notification/snackbar.h"
#include "../transaction/spsc_queue.h"
#include "../transaction/transaction.h"
#include "../window/rules/rules.h"
#include "../window/window.h"
#include "../workspace/workspace.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>

class Snackbar;

/** @brief Initial capacities for the context sub-registries. */
enum {
   BFC_INITIAL_MONITOR_CAP = 4,
   BFC_INITIAL_WINDOW_CAP = 8,
   BFC_INITIAL_KBD_CAP = 64,
};

/** @brief Bar refresh requests raised by application modules and consumed by
 *  the composition root (main.c PresentBars + BarUpdateAll). */
enum BarUpdateKind {
   BAR_UPDATE_NONE = 0,
   BAR_UPDATE_DIRTY,
   BAR_UPDATE_RECREATE,
};

/**
 * @brief Root application context — owns all mutable state.
 *
 * All fields are initialized by BFWMContextInit() and cleaned up by
 * BFWMContextFree(). Subsystems access their respective sub-structures
 * through this context rather than through standalone globals.
 */
using BFWMContext = struct BFWMContext {
   /** @name Core registries */
   /** @{ */
   /// Array of discovered physical monitors
   std::unique_ptr<MonitorRegistry> monitors;
   /// Per-monitor DPI registry
   std::unique_ptr<DpiSystem> dpi;
   /// Managed top-level windows
   std::unique_ptr<WindowRegistry> windows;
   /// Keystroke-to-action bindings
   KeybindDictionary keybinds;
   /// Array of window rules (NON-OWNING vector — the WindowRule objects are
   /// freed via WindowRuleDestroy by the existing loops; the vector only owns
   /// the pointer array)
   std::vector<WindowRule *> window_rules;
   /// Number of window rules
   size_t window_rule_count = 0;
   /// Lua state and config file path
   LuaConfig lua;
   /// Thread-safe circular keystroke buffer
   KeystrokeQueue keystroke_queue;
   /// Current Ctrl/Shift/Alt/Super state
   ModifierState modifiers;
   /** @} */

   HWND focused_hwnd = nullptr;

   /** @name Runtime configuration */
   /** @{ */
   struct {
      /// Pixel gap between tiled windows
      int gap_between = 0;
      /// Pixel gap at screen edges
      int gap_edge = 0;
      /// Runtime toggle for all gaps
      BOOL gaps_enabled = FALSE;
      /// Active window border color
      COLORREF border_color = 0;
      /// Inactive window border color
      COLORREF inactive_border = 0;
      /// Active border thickness in logical pixels
      int border_width = 0;
      /// Active border corner radius in logical pixels (0 = square)
      int border_radius = 0;
      /// Auto-focus on mouse hover
      BOOL focus_follows_mouse = FALSE;
      /// Move cursor to newly focused window
      BOOL mouse_follows_focus = FALSE;
      /// Reserved bar height in pixels
      int bar_height = 0;
      /// Bar appearance and indicator config
      BarConfig bar_cfg;
      /// VK code to unlock resize (0 = always unlocked)
      DWORD unlock_modifier = 0;
      /// VK code to unlock drag-move (0 = always unlocked)
      DWORD unlock_move_modifier = 0;
      LayoutType
          /// Default layout algorithm for new workspaces
          default_layout = DWINDLE;
      std::array<struct WorkspaceConfig, 32> workspace_configs = {};
      int workspace_config_count = 0;
      std::array<int, 16> disabled_monitors = {};
      int disabled_monitor_count = 0;
      SnackbarConfig snackbar;
   } config;

   /** @name Runtime disabled monitor UIDs (derived from config at startup) */
   /** @{ */
   std::array<std::wstring, 16> disabled_uids = {};
   int disabled_uid_count = 0;
   /** @} */
   /** @} */

   /** @name Synchronisation */
   /** @{ */
   /// Protects shared state across threads
   CRITICAL_SECTION lock;
   /// FALSE signals the main loop to exit
   BOOL running = FALSE;
   /// TRUE during system sleep transition
   BOOL suspended = FALSE;
   /// ReconcileMonitors deferred
   BOOL reconcile_pending = FALSE;
   /// Tick when deferred reconcile fires
   ULONGLONG reconcile_deadline = 0;
   /// First tick when we started waiting
   ULONGLONG reconcile_attempt = 0;
   /// Monitors before sleep (0 = not sleeping)
   size_t pre_sleep_monitor_count = 0;
   /** @} */

   /** @name Workspace */
   /** @{ */
   /// Currently visible workspace
   Workspace *focused_workspace = nullptr;
   /** @} */

   /** @name Threading */
   /** @{ */
   /// Handle from _beginthreadex for the worker
   uintptr_t worker_thread = 0;
   /// Main thread ID (for PostThreadMessage)
   DWORD main_thread_id = 0;

   /** @} */

   /** @name WinEvent hook */
   /** @{ */
   /// Handle from SetWinEventHook
   HWINEVENTHOOK event_hook = nullptr;
   /** @} */

   /** @name Hidden helper window for broadcast messages */
   /** @{ */
   /// Hidden window for WM_DISPLAYCHANGE etc.
   HWND monitor_helper_hwnd = nullptr;
   /** @} */

   /** @name Transaction */
   /** @{ */
   /// Pending operation batching state
   TransactionState transaction;
   /// Lock-free SPSC event queue for WinEvent hook → main thread
   SpscQueue event_queue;
   /** @} */

   /** @name Notification snackbar */
   /** @{ */
   /// Owned Snackbar instance, or NULL before Snackbar::Init
   Snackbar *snackbar = nullptr;
   /// Win32 mutex for single-instance guard
   HANDLE instance_mutex = nullptr;
   /** @} */

   /** @name Re-entrancy guard */
   /** @{ */
   /// TRUE while WorkspaceApplyLayout is active
   BOOL applying_layout = FALSE;
   /// TRUE while any async layout move is in flight or pending; drives the
   /// main-loop convergence pass (see MainLoop in main.c).
   BOOL moves_in_flight = FALSE;
   /// Next GetTickCount64() deadline for the throttled move-convergence pass.
   ULONGLONG move_check_deadline = 0;
   /// Pending bar refresh request (BAR_UPDATE_NONE when clean); consumed at
   /// the end of each MainLoop iteration by the composition root.
   enum BarUpdateKind bar_update = BAR_UPDATE_NONE;
   /// TRUE when a monitor DPI changed; main loop re-tiles + recreates bars
   BOOL dpi_update = FALSE;
   /// TRUE during BFWMContextSetup
   volatile BOOL setup_in_progress = FALSE;
   /// HWND being mouse-resized, or NULL
   HWND resize_hwnd = nullptr;
   /// GetTickCount64() of the last keyboard-driven resize action; suppresses
   /// the periodic overlay reconcile tick for a short window after a resize
   /// key so it cannot inject an extra commit mid-resize.
   ULONGLONG last_keyboard_resize = 0;
   /// HWND in ToggleFullscreenForHwnd, or NULL
   HWND toggling_fullscreen_hwnd = nullptr;
   /** @} */
};

/**
 * @brief Request a bar refresh; max() semantics — RECREATE wins over DIRTY.
 *
 * Called from application modules (workspace, handlers, monitor) that must not
 * depend on bar.h. The request is consumed at the end of the MainLoop
 * iteration by the composition root (main.c).
 *
 * @param ctx  The BFWM context
 * @param kind The requested refresh kind
 */
static inline void BarUpdateRequest(struct BFWMContext *ctx,
                                    enum BarUpdateKind kind) {
   ctx->bar_update = std::max(kind, ctx->bar_update);
}

/**
 * @brief Allocate and fully initialise a BFWMContext.
 *
 * Default-constructs the struct (NSDMI initialises every scalar member),
 * creates the critical section, and initialises all sub-registries (monitors,
 * windows, keybinds, Lua config, keystroke queue) in dependency-safe order.
 * Default runtime configuration values are applied (6px gap, black borders,
 * focus-follows-mouse on).
 *
 * @return BFWMContext* Fully initialised context. Never returns NULL
 *         (allocation failure throws std::bad_alloc).
 */
auto BFWMContextInit() -> BFWMContext *;

/**
 * @brief Set up the BFWMContext after initialisation.
 *
 * Enumerates all physical displays (populating ctx->monitors), creates a
 * default workspace on each monitor, enumerates all existing top-level
 * windows (populating ctx->windows), assigns each to the workspace that
 * covers its monitor, and applies the initial layout.
 *
 * This is intended to be called once from main() after BFWMContextInit(),
 * Lua config, and the worker thread have all been started.
 *
 * @param ctx The BFWM context to set up
 * @return true on success, false on failure
 */
auto BFWMContextSetup(BFWMContext *ctx) -> bool;

/**
 * @brief Tear down a BFWMContext and free all owned resources.
 *
 * Frees sub-registries in reverse dependency order (keybinds → Lua →
 * keystroke queue → windows → monitors), destroys the critical section,
 * and frees the context struct itself. Safe to call with NULL (no-op).
 *
 * @param BFWM_context The context to free, or NULL
 */
void BFWMContextFree(BFWMContext *BFWM_context);

#endif
