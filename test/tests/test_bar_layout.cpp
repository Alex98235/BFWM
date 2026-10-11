#include "cbelt.h"

#include "../../src/bar/bar.h"

CBELT_GROUP("bar_layout")

namespace {

constexpr float FLOAT_EPSILON = 0.0001F;

/// Near-equality for float layout arithmetic (no GDI/pixel dependence).
auto float_near(float expected, float actual) -> bool {
   float const diff = expected > actual ? expected - actual : actual - expected;
   return diff < FLOAT_EPSILON;
}

} // namespace

/* =========================================================================
 * MeasureGroupWidthFrom: sum drawable widths + 4px gaps + edge pad.
 * Non-drawable entries contribute neither width nor a gap, which is the
 * invariant the centered layout depends on.
 * ========================================================================= */

CBELT_TEST(measure_group_width_skips_non_drawable) {
   BarGroupItem const items[] = {
       {true, 100.0F},
       {false, 80.0F},
       {true, 50.0F},
   };
   float const got = MeasureGroupWidthFrom(items, 3, 6.0F);
   /* 100 + 50 = 150, one 4px gap between the two drawable items, + pad. */
   cbelt_assert(float_near(150.0F + 4.0F + 6.0F, got));
   return TEST_SUCCESS;
}

CBELT_TEST(measure_group_width_all_non_drawable_is_zero) {
   BarGroupItem const items[] = {
       {false, 80.0F},
       {false, 50.0F},
   };
   float const got = MeasureGroupWidthFrom(items, 2, 6.0F);
   cbelt_assert(float_near(0.0F, got));
   return TEST_SUCCESS;
}

CBELT_TEST(measure_group_width_single_item_no_gap) {
   BarGroupItem const items[] = {
       {true, 100.0F},
   };
   float const got = MeasureGroupWidthFrom(items, 1, 6.0F);
   cbelt_assert(float_near(100.0F + 6.0F, got));
   return TEST_SUCCESS;
}

/* =========================================================================
 * CollectionWidth: Σ item_width + 4·(n − 1).
 * ========================================================================= */

CBELT_TEST(collection_width_three_items) {
   int const widths[] = {20, 20, 20};
   cbelt_assert(float_near(68.0F, CollectionWidth(widths, 3)));
   return TEST_SUCCESS;
}

CBELT_TEST(collection_width_empty) {
   cbelt_assert(float_near(0.0F, CollectionWidth(nullptr, 0)));
   return TEST_SUCCESS;
}

CBELT_TEST(collection_width_single_item) {
   int const widths[] = {15};
   cbelt_assert(float_near(15.0F, CollectionWidth(widths, 1)));
   return TEST_SUCCESS;
}

/* =========================================================================
 * CenterSpacerFor: left spacer that centers the center group.
 * ========================================================================= */

CBELT_TEST(center_spacer_formula) {
   bool use_abs = false;
   float const spacer =
       CenterSpacerFor(1000.0F, 200.0F, 100.0F, 100.0F, &use_abs);
   cbelt_assert_true(use_abs);
   cbelt_assert(float_near(((1000.0F - 200.0F) / 2.0F) - 100.0F, spacer));
   return TEST_SUCCESS;
}

CBELT_TEST(center_spacer_moves_opposite_to_left_total) {
   /* The reported bug's sign: a wider left group must pull the spacer left. */
   bool first_abs = false;
   bool second_abs = false;
   float const first =
       CenterSpacerFor(1000.0F, 200.0F, 100.0F, 100.0F, &first_abs);
   float const second =
       CenterSpacerFor(1000.0F, 200.0F, 104.0F, 100.0F, &second_abs);
   cbelt_assert_true(first_abs && second_abs);
   cbelt_assert(float_near(first - 4.0F, second));
   return TEST_SUCCESS;
}

CBELT_TEST(center_spacer_not_absolute_when_left_does_not_fit) {
   /* half_remain = (100 - 0) / 2 = 50 < l_total = 60. */
   bool use_abs = true;
   float const spacer = CenterSpacerFor(100.0F, 0.0F, 60.0F, 0.0F, &use_abs);
   cbelt_assert_false(use_abs);
   cbelt_assert(float_near(0.0F, spacer));
   return TEST_SUCCESS;
}

CBELT_TEST(center_spacer_not_absolute_when_right_does_not_fit) {
   bool use_abs = true;
   float const spacer = CenterSpacerFor(100.0F, 0.0F, 0.0F, 60.0F, &use_abs);
   cbelt_assert_false(use_abs);
   cbelt_assert(float_near(0.0F, spacer));
   return TEST_SUCCESS;
}
