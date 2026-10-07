#include "cbelt.h"
#include <windows.h>

#include "../../src/workspace/placement.h"

CBELT_GROUP("placement_classify")

/* Reference implementation of the documented decision table, derived without
 * copying the code under test:
 *   - landed                                    -> Issue (counter resets)
 *   - !landed && !stalled                       -> Defer (move in flight)
 *   - !landed && stalled && failures+1 < limit  -> Issue (re-issue, count)
 *   - !landed && stalled && failures+1 >= limit -> Float */
namespace {

inline auto reference_decision(BOOL landed, BOOL stalled,
                               UINT failed_landings) -> PlacementMoveDecision {
   if (landed != 0) {
      return PLACEMENT_MOVE_ISSUE;
   }
   if (stalled == 0) {
      return PLACEMENT_MOVE_DEFER;
   }
   if (failed_landings + 1U >= FLOAT_AFTER_FAILED_LANDINGS) {
      return PLACEMENT_MOVE_FLOAT;
   }
   return PLACEMENT_MOVE_ISSUE;
}
} // namespace

/* Full sweep: landed x stalled x failed_landings in {0..4} must classify per
 * the documented table. */
CBELT_TEST(full_sweep_matches_reference) {
   for (int landed = 0; landed < 2; landed++) {
      for (int stalled = 0; stalled < 2; stalled++) {
         for (UINT failures = 0; failures <= 4; failures++) {
            PlacementMoveDecision got =
                ClassifyPlacementMove(landed, stalled, failures);
            PlacementMoveDecision want =
                reference_decision(landed, stalled, failures);
            cbelt_assert_equal(want, got);
         }
      }
   }
   return TEST_SUCCESS;
}

/* A landed window always issues, no matter how many failures preceded it
 * (the counter resets on landing). */
CBELT_TEST(landed_always_issues) {
   for (UINT failures = 0; failures <= 4; failures++) {
      cbelt_assert_equal(PLACEMENT_MOVE_ISSUE,
                         ClassifyPlacementMove(TRUE, FALSE, failures));
      cbelt_assert_equal(PLACEMENT_MOVE_ISSUE,
                         ClassifyPlacementMove(TRUE, TRUE, failures));
   }
   return TEST_SUCCESS;
}

/* Defer happens only while a move is in flight (not landed, not stalled). */
CBELT_TEST(defer_only_while_in_flight) {
   for (UINT failures = 0; failures <= 4; failures++) {
      cbelt_assert_equal(PLACEMENT_MOVE_DEFER,
                         ClassifyPlacementMove(FALSE, FALSE, failures));
   }
   return TEST_SUCCESS;
}

/* Float fires exactly when the failure count reaches the threshold: the
 * (failures + 1)-th consecutive stall is the one that floats. */
CBELT_TEST(float_at_threshold_boundary) {
   cbelt_assert_equal(PLACEMENT_MOVE_ISSUE,
                      ClassifyPlacementMove(FALSE, TRUE, 0));
   cbelt_assert_equal(PLACEMENT_MOVE_ISSUE,
                      ClassifyPlacementMove(FALSE, TRUE, 1));
   cbelt_assert_equal(PLACEMENT_MOVE_FLOAT,
                      ClassifyPlacementMove(FALSE, TRUE, 2));
   cbelt_assert_equal(PLACEMENT_MOVE_FLOAT,
                      ClassifyPlacementMove(FALSE, TRUE, 3));
   cbelt_assert_equal(PLACEMENT_MOVE_FLOAT,
                      ClassifyPlacementMove(FALSE, TRUE, 4));
   return TEST_SUCCESS;
}