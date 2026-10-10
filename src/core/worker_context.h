/**
 * @file worker_context.h
 * @brief State owned by the input/event worker thread.
 *
 * WorkerContext holds the fields that the worker thread writes and the main
 * thread reads: the keyboard-modifier scratch state, the keystroke queue
 * (SPSC, worker → main), the event queue (SPSC, worker → main), and the
 * setup-in-progress flag.
 *
 * Threading contract:
 *   - `modifiers` is worker-exclusive scratch. The worker updates it on
 *     every key event and captures the current value into each keystroke
 *     before enqueuing. The main thread never reads WorkerContext::modifiers
 *     directly — it reads ModifierState from the dequeued Keystroke.
 *   - `keystroke_queue` is SPSC: the worker (producer) calls Enqueue; the
 *     main loop (consumer) calls Dequeue. Both operations are internally
 *     synchronised (CRITICAL_SECTION + semaphore in KeystrokeQueue).
 *   - `event_queue` is SPSC: the worker (producer) calls Push; the main
 *     loop (consumer) calls Pop. The SpscQueue is lock-free and uses
 *     std::atomic for memory ordering.
 *   - `setup_in_progress` is set by the main thread during BFWMContextSetup
 *     and read by the worker's WinEventProc to gate CREATE/FOREGROUND events.
 */

#ifndef BFWM_WORKER_CONTEXT_H
#define BFWM_WORKER_CONTEXT_H

#include "../input/keystroke.h"
#include "../input/queue.h"
#include "../transaction/spsc_queue.h"
#include <windows.h>

/**
 * @brief Worker-thread-owned state.
 *
 * See the file header for the threading contract.
 */
struct WorkerContext {
   /** @name Modifier state (worker-exclusive scratch) */
   /** @{ */
   ModifierState modifiers;
   /** @} */

   /** @name Keystroke queue (SPSC: worker pushes, main drains) */
   /** @{ */
   KeystrokeQueue keystroke_queue;
   /** @} */

   /** @name WinEvent queue (SPSC: worker pushes, main drains) */
   /** @{ */
   SpscQueue event_queue;
   /** @} */

   /** @brief TRUE during BFWMContextSetup (main writes, worker reads) */
   volatile BOOL setup_in_progress = FALSE;
};

#endif