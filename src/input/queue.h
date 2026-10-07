/**
 * @file queue.h
 * @brief Thread-safe bounded queue for keyboard events.
 *
 * THREADING MODEL
 * ===============
 *
 * This queue is designed for a single-producer / single-consumer setup:
 *
 *   - **Producer thread:** The low-level keyboard hook installed by
 *     SetupKeyboardHooks runs in the hook thread and calls Enqueue().
 *   - **Consumer thread:** The main loop in main() calls Dequeue()
 *     to drain the queue and dispatch each keystroke.
 *
 * Synchronisation:
 *
 *   - A CRITICAL_SECTION (`lock`) protects the shared mutable state
 *     (`head`, `tail`, `count`, and the `keys[]` buffer). Both
 *     Enqueue() and Dequeue() acquire this lock before
 *     reading or writing those fields.
 *   - A Win32 semaphore (`semaphore`) is used for consumer-side blocking:
 *     the producer calls ReleaseSemaphore after enqueuing, and the consumer
 *     calls WaitForSingleObject before dequeuing. This avoids busy-waiting.
 *
 * Initialisation:
 *
 *   The constructor MUST run before any thread accesses the queue. In
 *   BFWMWM this is done during BFWMContextInit() (called from main())
 *   _before_ the keyboard hook or worker thread is started, so there is no
 *   lazy / racy initialisation.
 *
 * Lifespan:
 *
 *   The destructor releases the semaphore and critical section when the
 *   queue is no longer needed.
 */

#ifndef BFWM_KEYSTROKE_QUEUE_H
#define BFWM_KEYSTROKE_QUEUE_H

#include <array>
#include <minwindef.h>
#include <process.h>
#include <windows.h>

#include "../input/keystroke.h"

/**
 * @brief Maximum number of keystrokes the queue can hold.
 */
enum { MAX_QUEUE_SIZE = 8 };

/**
 * @brief Thread-safe circular buffer for keystroke events.
 *
 * Construct to initialise, destruct to tear down. After construction the
 * queue is shared between the keyboard-hook thread (producer, calls Enqueue)
 * and the main loop (consumer, calls Dequeue). The CRITICAL_SECTION
 * serialises all accesses to head/tail/count/keys; the semaphore lets the
 * consumer block efficiently when the queue is empty.
 */
class KeystrokeQueue {
 public:
   KeystrokeQueue();
   ~KeystrokeQueue();
   KeystrokeQueue(const KeystrokeQueue &) = delete;
   auto operator=(const KeystrokeQueue &) -> KeystrokeQueue & = delete;

   /**
    * @brief Enqueue a keystroke into the circular buffer.
    *
    * Called from the keyboard-hook thread (producer). Takes the critical
    * section, writes into the buffer, releases it, then signals the
    * semaphore to wake the consumer.
    *
    * If the queue is full the oldest entry is silently dropped to make room.
    *
    * @param key The keystroke to enqueue (copied into the buffer)
    * @return true on success, false on failure
    */
   auto Enqueue(const Keystroke &key) -> bool;

   /**
    * @brief Dequeue a keystroke from the circular buffer.
    *
    * Called from the main loop (consumer). Blocks on the semaphore until a
    * keystroke is available, then takes the critical section, reads the
    * oldest entry, and returns it.
    *
    * @param out_key Reference to receive the dequeued keystroke
    * @return true on success, false on failure (semaphore wait failed
    *         unexpectedly)
    */
   auto Dequeue(Keystroke &out_key) -> bool;

   /**
    * @brief Current number of items in the queue.
    *
    * @return The number of keystrokes currently buffered
    */
   [[nodiscard]] auto Count() const -> size_t {
      return static_cast<size_t>(count);
   }

 private:
   /// Protects head, tail, count, keys[]
   CRITICAL_SECTION lock;
   /// Read index (consumer)
   int head = 0;
   /// Write index (producer)
   int tail = 0;
   /// Current number of items in the queue
   int count = 0;
   /// Fixed-size keystroke buffer
   std::array<Keystroke, MAX_QUEUE_SIZE> keys{};
   /// Semaphore for signalling queued keystrokes
   HANDLE semaphore;
};

#endif