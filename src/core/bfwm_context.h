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

#include "main_context.h"
#include "shared_state.h"
#include "worker_context.h"

#include "../config/keybinds.h"
#include "../config/lua/parser.h"
#include "../dpi/dpi.h"
#include "../input/queue.h"
#include "../monitor/monitor.h"
#include "../notification/snackbar.h"
#include "../system/exec_cache.h"
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

/** @brief Initial capacities for the context sub-registries. */

/**
 * @brief Root application context — owns all mutable state.
 *
 * All fields are initialized by BFWMContextInit() and cleaned up by
 * BFWMContextFree(). Subsystems access their respective sub-structures
 * through this context rather than through standalone globals.
 */
using BFWMContext = struct BFWMContext {
   /** @name Shared state (worker-visible config; see shared_state.h) */
   /** @{ */
   SharedState shared;
   /** @} */

   /** @name Worker state (keystroke+event queues; see worker_context.h) */
   /** @{ */
   WorkerContext worker;
   /** @} */

   /** @name Main-thread-only state (registries, transaction, ...; see
    * main_context.h) */
   /** @{ */
   MainContext main;
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
   ctx->main.bar_update = std::max(kind, ctx->main.bar_update);
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
 * Enumerates all physical displays (populating ctx->main.monitors), creates a
 * default workspace on each monitor, enumerates all existing top-level
 * windows (populating ctx->main.windows), assigns each to the workspace that
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
