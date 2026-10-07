#include "cbelt.h"
#include <array>
#include <windows.h>

#include "../../src/input/queue.h"

CBELT_GROUP("keystroke_queue")

namespace {

inline auto make_key(DWORD vkCode, BOOL ctrl, BOOL shift, BOOL alt,
                     BOOL super) -> Keystroke {
   Keystroke ks = {};
   ks.key.vkCode = vkCode;
   ks.key.flags = 0;
   ks.key.scanCode = 0;
   ks.key.dwExtraInfo = 0;
   ks.modifiers.ctrl = ctrl;
   ks.modifiers.shift = shift;
   ks.modifiers.alt = alt;
   ks.modifiers.super = super;
   return ks;
}
} // namespace

CBELT_TEST(init_creates_valid_queue) {
   KeystrokeQueue q;

   cbelt_assert(q.Count() == 0);

   Keystroke ks = make_key('A', FALSE, FALSE, FALSE, FALSE);
   cbelt_assert(q.Enqueue(ks) == true);

   Keystroke out;
   memset(&out, 0, sizeof(out));
   cbelt_assert(q.Dequeue(out) == true);
   cbelt_assert(out.key.vkCode == 'A');
   cbelt_assert(q.Count() == 0);

   return TEST_SUCCESS;
}

CBELT_TEST(enqueue_one_dequeue_one) {
   KeystrokeQueue q;

   Keystroke ks = make_key('A', FALSE, FALSE, FALSE, FALSE);

   bool ok = q.Enqueue(ks);
   cbelt_assert(ok == true);
   cbelt_assert(q.Count() == 1);

   Keystroke out;
   memset(&out, 0, sizeof(out));
   ok = q.Dequeue(out);
   cbelt_assert(ok == true);
   cbelt_assert(out.key.vkCode == 'A');
   cbelt_assert(out.modifiers.ctrl == FALSE);
   cbelt_assert(q.Count() == 0);

   return TEST_SUCCESS;
}

CBELT_TEST(enqueue_multiple_dequeue_all) {
   KeystrokeQueue q;

   std::array<DWORD, 5> keys = {'A', 'B', 'C', 'D', 'E'};
   for (int i = 0; i < 5; i++) {
      Keystroke ks = make_key(keys[i], FALSE, FALSE, FALSE, FALSE);
      bool ok = q.Enqueue(ks);
      cbelt_assert(ok == true);
   }

   cbelt_assert(q.Count() == 5);

   for (int i = 0; i < 5; i++) {
      Keystroke out;
      memset(&out, 0, sizeof(out));
      bool ok = q.Dequeue(out);
      cbelt_assert(ok == true);
      cbelt_assert(out.key.vkCode == keys[i]);
   }

   cbelt_assert(q.Count() == 0);

   return TEST_SUCCESS;
}

CBELT_TEST(enqueue_with_modifiers) {
   KeystrokeQueue q;

   Keystroke ks = make_key('X', TRUE, TRUE, FALSE, FALSE);
   q.Enqueue(ks);

   Keystroke out;
   q.Dequeue(out);
   cbelt_assert(out.key.vkCode == 'X');
   cbelt_assert(out.modifiers.ctrl == TRUE);
   cbelt_assert(out.modifiers.shift == TRUE);
   cbelt_assert(out.modifiers.alt == FALSE);

   return TEST_SUCCESS;
}

CBELT_TEST(overflow_drops_oldest) {
   KeystrokeQueue q;

   for (int i = 0; i < 8; i++) {
      Keystroke ks = make_key('A' + i, FALSE, FALSE, FALSE, FALSE);
      q.Enqueue(ks);
   }

   cbelt_assert(q.Count() == 8);

   Keystroke ks_extra = make_key('Z', FALSE, FALSE, FALSE, FALSE);
   bool ok = q.Enqueue(ks_extra);
   cbelt_assert(ok == true);
   cbelt_assert(q.Count() == 8);

   Keystroke out;
   q.Dequeue(out);
   cbelt_assert(out.key.vkCode == 'B');

   for (int i = 2; i < 8; i++) {
      q.Dequeue(out);
      cbelt_assert(out.key.vkCode == static_cast<DWORD>('A' + i));
   }

   q.Dequeue(out);
   cbelt_assert(out.key.vkCode == 'Z');

   return TEST_SUCCESS;
}

CBELT_TEST(wraparound_behaviour) {
   KeystrokeQueue q;

   for (int i = 0; i < 3; i++) {
      Keystroke ks = make_key('A' + i, FALSE, FALSE, FALSE, FALSE);
      q.Enqueue(ks);

      Keystroke out;
      q.Dequeue(out);
   }

   for (int i = 0; i < 8; i++) {
      Keystroke ks = make_key('P' + i, FALSE, FALSE, FALSE, FALSE);
      q.Enqueue(ks);
   }

   cbelt_assert(q.Count() == 8);

   for (int i = 0; i < 8; i++) {
      Keystroke out;
      q.Dequeue(out);
      cbelt_assert(out.key.vkCode == static_cast<DWORD>('P' + i));
   }

   return TEST_SUCCESS;
}

CBELT_TEST(keystroke_deep_copy_on_enqueue) {
   KeystrokeQueue q;

   Keystroke ks = make_key('K', TRUE, FALSE, TRUE, FALSE);
   q.Enqueue(ks);

   ks.key.vkCode = 'X';
   ks.modifiers.ctrl = FALSE;

   Keystroke out;
   q.Dequeue(out);
   cbelt_assert(out.key.vkCode == 'K');
   cbelt_assert(out.modifiers.ctrl == TRUE);
   cbelt_assert(out.modifiers.alt == TRUE);

   return TEST_SUCCESS;
}
