#include "cbelt.h"
#include <windows.h>

#include "../../src/workspace/placement.h"

CBELT_GROUP("placement_classify")

/* Decision table after refactor (no self-healing float):
 *   landed = TRUE                               -> Issue
 *   landed = FALSE, stalled = FALSE             -> Defer  (move still in flight)
 *   landed = FALSE, stalled = TRUE              -> Issue  (re-issue) */

static auto reference(BOOL landed, BOOL stalled) -> PlacementMoveDecision {
   if (landed != 0)
      return PLACEMENT_MOVE_ISSUE;
   if (stalled == 0)
      return PLACEMENT_MOVE_DEFER;
   return PLACEMENT_MOVE_ISSUE;
}

/* Full sweep of all 4 input combinations must match the reference. */
CBELT_TEST(full_sweep) {
   cbelt_assert_equal(reference(TRUE, FALSE),
                      ClassifyPlacementMove(TRUE, FALSE));
   cbelt_assert_equal(reference(TRUE, TRUE),
                      ClassifyPlacementMove(TRUE, TRUE));
   cbelt_assert_equal(reference(FALSE, FALSE),
                      ClassifyPlacementMove(FALSE, FALSE));
   cbelt_assert_equal(reference(FALSE, TRUE),
                      ClassifyPlacementMove(FALSE, TRUE));
   return TEST_SUCCESS;
}

/* A landed window always issues. */
CBELT_TEST(landed_always_issues) {
   cbelt_assert_equal(PLACEMENT_MOVE_ISSUE, ClassifyPlacementMove(TRUE, FALSE));
   cbelt_assert_equal(PLACEMENT_MOVE_ISSUE, ClassifyPlacementMove(TRUE, TRUE));
   return TEST_SUCCESS;
}

/* Defer happens only while a move is in flight (not landed, not stalled). */
CBELT_TEST(defer_only_while_in_flight) {
   cbelt_assert_equal(PLACEMENT_MOVE_DEFER, ClassifyPlacementMove(FALSE, FALSE));
   return TEST_SUCCESS;
}

/* An un-landed stalled window is re-issued (the revert-converge path handles
 * persistent refusal). */
CBELT_TEST(stalled_reissues) {
   cbelt_assert_equal(PLACEMENT_MOVE_ISSUE, ClassifyPlacementMove(FALSE, TRUE));
   return TEST_SUCCESS;
}