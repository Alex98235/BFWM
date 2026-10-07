#include "cbelt.h"
#include <windows.h>

#include "../../src/window/border/overlay.h"

CBELT_GROUP("overlay_reconcile")

/* Reference implementation of the documented truth table: derives the expected
 * action for every combination without copying the code under test.
 *   - ring && target && !iconic && !suppress            -> ASSERT_Z (cloaked
 *     irrelevant here)
 *   - !ring && target && !iconic && !suppress           -> cloaked ? NONE
 *     : MARK_DIRTY
 *   - ring (anything else)                              -> MARK_DIRTY (ghost
 *     ring: target hidden/minimized/suppressed)
 *   - otherwise                                         -> NONE */
namespace {

inline auto reference_action(BOOL ring_visible, BOOL target_visible,
                             BOOL target_iconic, BOOL cloaked,
                             BOOL suppress) -> Overlay::OverlayReconcileAction {
   if (ring_visible && target_visible && !target_iconic && !suppress) {
      return Overlay::OVERLAY_RECONCILE_ASSERT_Z;
   }
   if (!ring_visible && target_visible && !target_iconic && !suppress) {
      return cloaked ? Overlay::OVERLAY_RECONCILE_NONE : Overlay::OVERLAY_RECONCILE_MARK_DIRTY;
   }
   if (ring_visible) {
      return Overlay::OVERLAY_RECONCILE_MARK_DIRTY;
   }
   return Overlay::OVERLAY_RECONCILE_NONE;
}
} // namespace

/* Every one of the 32 combinations of the five boolean inputs must classify
 * per the documented truth table. */
CBELT_TEST(full_32_combination_truth_table) {
   for (int ring = 0; ring < 2; ring++) {
      for (int target = 0; target < 2; target++) {
         for (int iconic = 0; iconic < 2; iconic++) {
            for (int cloaked = 0; cloaked < 2; cloaked++) {
               for (int suppress = 0; suppress < 2; suppress++) {
                  Overlay::OverlayReconcileAction got = Overlay::OverlayReconcileClassify(
                      ring, target, iconic, cloaked, suppress);
                  Overlay::OverlayReconcileAction want =
                      reference_action(ring, target, iconic, cloaked, suppress);
                  cbelt_assert_equal(want, got);
               }
            }
         }
      }
   }
   return TEST_SUCCESS;
}

/* ASSERT_Z is never emitted when the ring is hidden. */
CBELT_TEST(assert_z_never_with_hidden_ring) {
   for (int target = 0; target < 2; target++) {
      for (int iconic = 0; iconic < 2; iconic++) {
         for (int cloaked = 0; cloaked < 2; cloaked++) {
            for (int suppress = 0; suppress < 2; suppress++) {
               cbelt_assert(Overlay::OverlayReconcileClassify(0, target, iconic, cloaked,
                                                     suppress) !=
                            Overlay::OVERLAY_RECONCILE_ASSERT_Z);
            }
         }
      }
   }
   return TEST_SUCCESS;
}

/* ASSERT_Z is never emitted when the target is hidden. */
CBELT_TEST(assert_z_never_with_hidden_target) {
   for (int ring = 0; ring < 2; ring++) {
      for (int iconic = 0; iconic < 2; iconic++) {
         for (int cloaked = 0; cloaked < 2; cloaked++) {
            for (int suppress = 0; suppress < 2; suppress++) {
               cbelt_assert(Overlay::OverlayReconcileClassify(ring, 0, iconic, cloaked,
                                                     suppress) !=
                            Overlay::OVERLAY_RECONCILE_ASSERT_Z);
            }
         }
      }
   }
   return TEST_SUCCESS;
}

/* ASSERT_Z is never emitted when the target is iconic. */
CBELT_TEST(assert_z_never_with_iconic_target) {
   for (int ring = 0; ring < 2; ring++) {
      for (int target = 0; target < 2; target++) {
         for (int cloaked = 0; cloaked < 2; cloaked++) {
            for (int suppress = 0; suppress < 2; suppress++) {
               cbelt_assert(Overlay::OverlayReconcileClassify(ring, target, 1, cloaked,
                                                     suppress) !=
                            Overlay::OVERLAY_RECONCILE_ASSERT_Z);
            }
         }
      }
   }
   return TEST_SUCCESS;
}

/* ASSERT_Z is never emitted when the ring is suppressed. */
CBELT_TEST(assert_z_never_when_suppressed) {
   for (int ring = 0; ring < 2; ring++) {
      for (int target = 0; target < 2; target++) {
         for (int iconic = 0; iconic < 2; iconic++) {
            for (int cloaked = 0; cloaked < 2; cloaked++) {
               cbelt_assert(Overlay::OverlayReconcileClassify(ring, target, iconic,
                                                     cloaked, 1) !=
                            Overlay::OVERLAY_RECONCILE_ASSERT_Z);
            }
         }
      }
   }
   return TEST_SUCCESS;
}

/* MARK_DIRTY is emitted exactly for (a) a shown ring with target
 * hidden/iconic/suppressed, and (b) a hidden ring with target shown,
 * unsuppressed, not-iconic and not-cloaked. */
CBELT_TEST(mark_dirty_exactly_for_visibility_drift) {
   /* (a) ring shown: MARK_DIRTY unless the target is shown, not iconic and
    * not suppressed (that case is ASSERT_Z). */
   for (int target = 0; target < 2; target++) {
      for (int iconic = 0; iconic < 2; iconic++) {
         for (int cloaked = 0; cloaked < 2; cloaked++) {
            for (int suppress = 0; suppress < 2; suppress++) {
               Overlay::OverlayReconcileAction got = Overlay::OverlayReconcileClassify(
                   1, target, iconic, cloaked, suppress);
               if (target && !iconic && !suppress) {
                  cbelt_assert(got == Overlay::OVERLAY_RECONCILE_ASSERT_Z);
               } else {
                  cbelt_assert(got == Overlay::OVERLAY_RECONCILE_MARK_DIRTY);
               }
            }
         }
      }
   }

   /* (b) ring hidden, target shown, not iconic, not suppressed, not cloaked:
    * MARK_DIRTY. Cloaked or suppressed stays NONE. */
   for (int cloaked = 0; cloaked < 2; cloaked++) {
      for (int suppress = 0; suppress < 2; suppress++) {
         Overlay::OverlayReconcileAction got =
             Overlay::OverlayReconcileClassify(0, 1, 0, cloaked, suppress);
         if (!cloaked && !suppress) {
            cbelt_assert(got == Overlay::OVERLAY_RECONCILE_MARK_DIRTY);
         } else {
            cbelt_assert(got == Overlay::OVERLAY_RECONCILE_NONE);
         }
      }
   }
   return TEST_SUCCESS;
}

/* NONE covers everything else, including the cloaked target with a hidden
 * ring. */
CBELT_TEST(none_covers_agreed_hidden_and_cloaked) {
   /* Both ring and target hidden: always NONE, whatever the other flags. */
   for (int iconic = 0; iconic < 2; iconic++) {
      for (int cloaked = 0; cloaked < 2; cloaked++) {
         for (int suppress = 0; suppress < 2; suppress++) {
            cbelt_assert(Overlay::OverlayReconcileClassify(0, 0, iconic, cloaked,
                                                  suppress) ==
                         Overlay::OVERLAY_RECONCILE_NONE);
         }
      }
   }

   /* Cloaked target with ring hidden and target shown+not-iconic+unsuppressed:
    * NONE (never show a ring over a DWM-cloaked target). */
   cbelt_assert(Overlay::OverlayReconcileClassify(0, 1, 0, 1, 0) ==
                Overlay::OVERLAY_RECONCILE_NONE);

   /* Ring shown over a cloaked-but-otherwise-visible target is still ASSERT_Z
    * (cloaked irrelevant once the ring is already up). */
   cbelt_assert(Overlay::OverlayReconcileClassify(1, 1, 0, 1, 0) ==
                Overlay::OVERLAY_RECONCILE_ASSERT_Z);
   return TEST_SUCCESS;
}
