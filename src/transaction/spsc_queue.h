#ifndef BFWM_SPSC_QUEUE_H
#define BFWM_SPSC_QUEUE_H

#include <array>
#include <atomic>
#include <cstddef>
#include <windows.h>

/**
 * @brief A single window event record for deferred processing.
 *
 * Pure data — no heap pointers. Copied by value into the SPSC ring buffer.
 */
using EventRecord = struct {
   DWORD event;         // WinEvent event constant (EVENT_OBJECT_DESTROY, etc.)
   HWND hwnd;           // Target window handle; for WM_APP_DPI_CHANGED events
                        // this carries the HMONITOR handle
   LONG idObject;       // OBJID_WINDOW typically
   LONG idChild;        // CHILDID_SELF typically
   DWORD dwEventThread; // Thread that generated the event
   DWORD dwmsEventTime; // Event timestamp
   UINT dpi;            // New DPI for WM_APP_DPI_CHANGED events
};

/**
 * @brief Lock-free SPSC ring buffer.
 *
 * head: producer writes here (incremented after write)
 * tail: consumer reads here (incremented after read)
 *
 * Empty: head == tail
 * Full: (head - tail) == CAPACITY
 *
 * SPSC contract:
 *   - Only ONE thread calls Push.
 *   - Only ONE (different) thread calls Pop.
 *   - Push and Pop never run concurrently on the same core
 *     (i.e. they are serialised by the OS or by the caller).
 *
 * head is written only by the producer; tail only by the consumer.
 * Memory ordering is enforced with std::atomic acquire/release pairs
 * (volatile is not sufficient in C++):
 *   - Push: acquire-load tail (space check), plain slot write, release-store
 *     head (publish).
 *   - Pop:  acquire-load head (emptiness check), plain slot read, release-store
 *     tail (advance).
 */
class SpscQueue {
 public:
   static constexpr size_t kCapacity = 2048;

   /**
    * @brief Push an event record to the queue (producer side).
    *
    * @param record The event to enqueue
    * @return true on success, false if queue was full (record dropped)
    */
   auto Push(const EventRecord &record) -> bool;

   /**
    * @brief Pop an event record from the queue (consumer side).
    *
    * @param record [out] Receives the popped event
    * @return true if an event was popped, false if queue was empty
    */
   auto Pop(EventRecord &record) -> bool;

   /**
    * @brief Return the number of events currently in the queue.
    */
   [[nodiscard]] auto Size() const -> size_t;

   /**
    * @brief Atomically take and reset the dropped-event counter.
    *
    * Returns the number of events dropped to overflow since the last call
    * and resets the counter to zero, so the caller reports drops per episode
    * rather than a permanently-stuck total. Safe against the producer's
    * concurrent fetch_add (both are atomic RMW on the same location).
    */
   [[nodiscard]] auto ConsumeDropped() -> LONG;

 private:
   std::array<EventRecord, kCapacity> slots_{};
   std::atomic<LONG> head_{0};
   std::atomic<LONG> tail_{0};
   std::atomic<LONG> dropped_{0};
};

#endif /* BFWM_SPSC_QUEUE_H */
