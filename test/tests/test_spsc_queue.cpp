#include "cbelt.h"
#include <windows.h>

#include "../../src/transaction/spsc_queue.h"

CBELT_GROUP("spsc_queue")

namespace {

inline auto make_record(DWORD event, HWND hwnd = nullptr, LONG idObject = 0,
                        LONG idChild = 0, DWORD thread = 0, DWORD time = 0,
                        UINT dpi = 0) -> EventRecord {
   EventRecord r = {};
   r.event = event;
   r.hwnd = hwnd;
   r.idObject = idObject;
   r.idChild = idChild;
   r.dwEventThread = thread;
   r.dwmsEventTime = time;
   r.dpi = dpi;
   return r;
}

} // namespace

CBELT_TEST(init_is_empty) {
   SpscQueue q;

   cbelt_assert_true(0 == q.Size());
   cbelt_assert_true(0 == q.ConsumeDropped());

   EventRecord out;
   cbelt_assert_false(q.Pop(out));

   return TEST_SUCCESS;
}

CBELT_TEST(push_pop_one_preserves_data) {
   SpscQueue q;

   cbelt_assert_true(q.Push(make_record(42, reinterpret_cast<HWND>(0x1234))));
   cbelt_assert_true(1 == q.Size());

   EventRecord out;
   cbelt_assert_true(q.Pop(out));
   cbelt_assert_true(42 == out.event);
   cbelt_assert_true(reinterpret_cast<HWND>(0x1234) == out.hwnd);
   cbelt_assert_true(0 == q.Size());

   return TEST_SUCCESS;
}

CBELT_TEST(push_pop_many_preserves_fifo_order) {
   SpscQueue q;

   for (size_t i = 0; i < 16; i++) {
      cbelt_assert_true(q.Push(make_record(static_cast<DWORD>(i + 1))));
   }
   cbelt_assert_true(16 == q.Size());

   for (size_t i = 0; i < 16; i++) {
      EventRecord out;
      cbelt_assert_true(q.Pop(out));
      cbelt_assert_true(static_cast<DWORD>(i + 1) == out.event);
   }
   cbelt_assert_true(0 == q.Size());

   return TEST_SUCCESS;
}

CBELT_TEST(overflow_returns_false_and_does_not_lose_slots) {
   SpscQueue q;

   for (size_t i = 0; i < SpscQueue::kCapacity; i++) {
      cbelt_assert_true(q.Push(make_record(static_cast<DWORD>(i + 1))));
   }
   cbelt_assert_true(SpscQueue::kCapacity == q.Size());

   // Queue is full: one more push must be rejected, not overwrite a slot.
   bool ok = q.Push(make_record(999));
   cbelt_assert_false(ok);
   cbelt_assert_true(SpscQueue::kCapacity == q.Size());

   // The full set of real entries is still intact and in order.
   for (size_t i = 0; i < SpscQueue::kCapacity; i++) {
      EventRecord out;
      cbelt_assert_true(q.Pop(out));
      cbelt_assert_true(static_cast<DWORD>(i + 1) == out.event);
   }

   return TEST_SUCCESS;
}

CBELT_TEST(consume_dropped_reports_and_resets) {
   SpscQueue q;

   for (size_t i = 0; i < SpscQueue::kCapacity; i++) {
      cbelt_assert_true(q.Push(make_record(1)));
   }
   // Force a single overflow.
   cbelt_assert_false(q.Push(make_record(1)));

   // First consume returns the accumulated count and resets to zero.
   cbelt_assert_true(1 == q.ConsumeDropped());
   cbelt_assert_true(0 == q.ConsumeDropped());

   return TEST_SUCCESS;
}

CBELT_TEST(consume_dropped_accumulates_multiple_overflows) {
   SpscQueue q;

   for (size_t i = 0; i < SpscQueue::kCapacity; i++) {
      cbelt_assert_true(q.Push(make_record(1)));
   }
   // Five more pushes all overflow while the queue stays full.
   for (int i = 0; i < 5; i++) {
      cbelt_assert_false(q.Push(make_record(1)));
   }

   cbelt_assert_true(5 == q.ConsumeDropped());
   cbelt_assert_true(0 == q.ConsumeDropped());

   return TEST_SUCCESS;
}

CBELT_TEST(consume_dropped_reset_does_not_recur_spuriously) {
   SpscQueue q;

   for (size_t i = 0; i < SpscQueue::kCapacity; i++) {
      cbelt_assert_true(q.Push(make_record(1)));
   }
   cbelt_assert_false(q.Push(make_record(1))); // 1 drop
   cbelt_assert_true(1 == q.ConsumeDropped()); // reset

   // Normal operation after reset must not report phantom drops, and a
   // pushed value must still round-trip through the FIFO in order.
   EventRecord discard;
   cbelt_assert_true(q.Pop(discard));          // free one slot
   cbelt_assert_true(q.Push(make_record(7)));  // enqueue after the reset
   cbelt_assert_true(0 == q.ConsumeDropped()); // no phantom drops

   bool found_seven = false;
   EventRecord cur;
   while (q.Pop(cur)) {
      if (cur.event == 7)
         found_seven = true;
   }
   cbelt_assert_true(found_seven); // 7 was enqueued and drained

   return TEST_SUCCESS;
}

CBELT_TEST(wraparound_preserves_order_across_mask) {
   SpscQueue q;

   // Advance head/tail well past kCapacity to exercise the (head & (cap-1))
   // ring indexing across several wrap cycles.
   for (int cycle = 0; cycle < 3; cycle++) {
      for (size_t i = 0; i < SpscQueue::kCapacity; i++) {
         cbelt_assert_true(q.Push(make_record(static_cast<DWORD>(i + 1))));
      }
      for (size_t i = 0; i < SpscQueue::kCapacity; i++) {
         EventRecord out;
         cbelt_assert_true(q.Pop(out));
         cbelt_assert_true(static_cast<DWORD>(i + 1) == out.event);
      }
   }
   cbelt_assert_true(0 == q.Size());

   return TEST_SUCCESS;
}
