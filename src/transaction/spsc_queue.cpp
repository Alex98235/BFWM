#include "spsc_queue.h"

/* ------------------------------------------------------------------ */
/*  Producer — Push                                                    */
/* ------------------------------------------------------------------ */

auto SpscQueue::Push(const EventRecord &record) -> bool {
   /*
    *  SPSC contract:
    *    - Only ONE thread calls Push.
    *    - Only ONE (different) thread calls Pop.
    *    - Push and Pop never run concurrently on the same core
    *      (i.e. they are serialised by the OS or by the caller).
    *
    *  head is written only by the producer.
    *  tail is written only by the consumer.
    *
    *  Memory ordering: the acquire-load of tail synchronises with the
    *  consumer's release-store of tail (slot released back to us), and the
    *  release-store of head publishes the slot write to the consumer's
    *  acquire-load of head.  volatile alone is not sufficient in C++.
    */

   LONG const current_head = head_.load(std::memory_order_relaxed);
   LONG const current_tail = tail_.load(std::memory_order_acquire);

   /* Full ?  head - tail == CAPACITY */
   if (current_head - current_tail >= (LONG)kCapacity) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return false;
   }

   /* Write the record into the slot (plain copy, struct is POD). */
   slots_[current_head & (kCapacity - 1)] = record;

   /* Publish: make the slot visible to the consumer. */
   head_.store(current_head + 1, std::memory_order_release);

   return true;
}

/* ------------------------------------------------------------------ */
/*  Consumer — Pop                                                     */
/* ------------------------------------------------------------------ */

auto SpscQueue::Pop(EventRecord &record) -> bool {
   /* Snapshot tail first.  Only this thread writes tail. */
   LONG const current_tail = tail_.load(std::memory_order_relaxed);
   LONG const current_head = head_.load(std::memory_order_acquire);

   /* Empty ? */
   if (current_head == current_tail) {
      return false;
   }

   /* Copy the slot out. */
   record = slots_[current_tail & (kCapacity - 1)];

   /* Release the slot back to the producer. */
   tail_.store(current_tail + 1, std::memory_order_release);

   return true;
}

/* ------------------------------------------------------------------ */
/*  Query helpers                                                      */
/* ------------------------------------------------------------------ */

auto SpscQueue::Size() const -> size_t {
   /* Best-effort snapshot. */
   return (size_t)(head_.load(std::memory_order_acquire) -
                   tail_.load(std::memory_order_acquire));
}

auto SpscQueue::ConsumeDropped() -> LONG {
   return dropped_.exchange(0, std::memory_order_relaxed);
}
