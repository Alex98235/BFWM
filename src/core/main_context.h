/**
 * @file main_context.h
 * @brief Main-thread-only application state — the runtime registries,
 *        transaction, and main-loop control flags.
 *
 * Every field here is accessed only from the main thread.
 *
 * Threading contract:
 *   - All fields are main-thread only. The worker thread never mutates
 *     MainContext and reads only `windows`, `focused_hwnd`, and
 *     `event_hook`, via the WorkerContext::main back-pointer.
 *   - `lock` serialises event processing against the main loop refresh,
 *     always on the main thread.
 */

#ifndef BFWM_MAIN_CONTEXT_H
#define BFWM_MAIN_CONTEXT_H

#include "../dpi/dpi.h"
#include "../monitor/monitor.h"
#include "../notification/snackbar.h"
#include "../system/exec_cache.h"
#include "../transaction/transaction.h"
#include "../window/rules/rules.h"
#include "../window/window.h"
#include "../workspace/workspace.h"

#include "focus_intent.h"

#include <algorithm>
#include <array>
#include <string>
#include <windows.h>

class Snackbar;

/** @brief Bar refresh requests raised by application modules and consumed by
 *  the composition root (main.c PresentBars + BarUpdateAll). */
enum BarUpdateKind {
   BAR_UPDATE_NONE = 0,
   BAR_UPDATE_DIRTY,
   BAR_UPDATE_RECREATE,
};

/** @brief Initial capacities for the context sub-registries. */
enum {
   BFC_INITIAL_MONITOR_CAP = 4,
   BFC_INITIAL_WINDOW_CAP = 8,
   BFC_INITIAL_KBD_CAP = 64,
};

struct SharedState;
struct WorkerContext;

/**
 * @brief Main-thread-only application state.
 *
 * See the file header for the threading contract.
 */
struct MainContext {
   /** @name Core registries */
   /** @{ */
   /// Array of discovered physical monitors
   std::unique_ptr<MonitorRegistry> monitors;
   /// Per-monitor DPI registry
   std::unique_ptr<DpiSystem> dpi;
   /// Managed top-level windows
   std::unique_ptr<WindowRegistry> windows;
   /// Array of window rules (NON-OWNING vector — the WindowRule objects are
   /// freed via WindowRuleDestroy by the existing loops; the vector only owns
   /// the pointer array)
   std::vector<WindowRule *> window_rules;
   /// Number of window rules
   size_t window_rule_count = 0;
   /// Lua state and config file path
   LuaConfig lua;
   /// Main-thread-only non-blocking child-process stdout cache
   /// (BFWM.exec_cache)
   ExecCache *exec_cache = nullptr;
   /** @} */

   HWND focused_hwnd = nullptr;

   /** @name Runtime disabled monitor UIDs (derived from config at startup) */
   /** @{ */
   std::array<std::wstring, 16> disabled_uids = {};
   int disabled_uid_count = 0;
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
   /// HWND being mouse-resized, or NULL
   HWND resize_hwnd = nullptr;
   /// GetTickCount64() of the last keyboard-driven resize action; suppresses
   /// the periodic overlay reconcile tick for a short window after a resize
   /// key so it cannot inject an extra commit mid-resize.
   ULONGLONG last_keyboard_resize = 0;
   /// HWND in ToggleFullscreenForHwnd, or NULL
   HWND toggling_fullscreen_hwnd = nullptr;
   /// General focus-intent model: suppresses Windows-supplied foreground
   /// events until the planned focus target actually lands.
   FocusIntent focus_intent;
   /** @} */
};

/**
 * @brief Request a bar refresh; max() semantics — RECREATE wins over DIRTY.
 *
 * @param main The main context
 * @param kind The requested refresh kind
 */
static inline void BarUpdateRequest(struct MainContext *main,
                                    enum BarUpdateKind kind) {
   main->bar_update = std::max(kind, main->bar_update);
}

// Initialisation and teardown of the application contexts.
void BFWMInit(SharedState *shared, MainContext *main);
auto BFWMSetup(SharedState *shared, WorkerContext *worker, MainContext *main)
    -> bool;
void BFWMFree(SharedState *shared, MainContext *main);

#endif