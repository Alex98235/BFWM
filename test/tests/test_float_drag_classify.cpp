#include "cbelt.h"
#include <windows.h>

#include "../../src/dpi/dpi.h"
#include "../../src/workspace/placement.h"

CBELT_GROUP("float_drag_classify")

namespace {

inline FloatDragMonitor Mon(int l, int t, int r, int b, UINT dpi) {
   return {{l, t, r, b}, dpi};
}

// Standard mixed-DPI pair: source 125% (dpi 120) on the left of boundary x=0,
// destination 100% (dpi 96) on the right.
constexpr int TOLERANCE = 3;

} // namespace

/* Center deep inside the source monitor — not near any boundary, so the
 * classifier must not fire. */
CBELT_TEST(center_deep_inside_source) {
   FloatDragMonitor mons[2] = {Mon(0, 0, 2560, 1440, 120),
                               Mon(2560, 0, 5120, 1440, 96)};
   POINT const center = {100, 100};
   cbelt_assert_equal((size_t)0,
                      ClassifyPinnedFloatDrag(center, TOLERANCE, mons, 2, 0));
   return TEST_SUCCESS;
}

/* Center deep inside the destination — still only one candidate monitor, so
 * the classifier stays put (the primary FindMonitorByPoint path handles this
 * in the caller). */
CBELT_TEST(center_deep_inside_dest) {
   FloatDragMonitor mons[2] = {Mon(0, 0, 2560, 1440, 120),
                               Mon(2560, 0, 5120, 1440, 96)};
   POINT const center = {4000, 500};
   cbelt_assert_equal((size_t)0,
                      ClassifyPinnedFloatDrag(center, TOLERANCE, mons, 2, 0));
   return TEST_SUCCESS;
}

/* Pinned forward: unaware window dragged from the 125% monitor onto the 100%
 * monitor, center pinned exactly on the boundary. Windows caps the overhang so
 * the old content classifier could not migrate; geometry resolves it. */
CBELT_TEST(pinned_forward_mixed_dpi) {
   FloatDragMonitor mons[2] = {Mon(-1080, 0, 0, 1920, 120),
                               Mon(0, 0, 2560, 1440, 96)};
   POINT const center = {0, 500};
   cbelt_assert_equal((size_t)1,
                      ClassifyPinnedFloatDrag(center, TOLERANCE, mons, 2, 0));
   return TEST_SUCCESS;
}

/* Pinned reverse: unaware window dragged from the 100% monitor onto the 125%
 * monitor — the reverse case that was previously impossible. */
CBELT_TEST(pinned_reverse_mixed_dpi) {
   FloatDragMonitor mons[2] = {Mon(0, 0, 2560, 1440, 96),
                               Mon(-1080, 0, 0, 1920, 120)};
   POINT const center = {0, 500};
   cbelt_assert_equal((size_t)1,
                      ClassifyPinnedFloatDrag(center, TOLERANCE, mons, 2, 0));
   return TEST_SUCCESS;
}

/* Equal-DPI boundary: both monitors 96 — the stuck-signature fallback must
 * not fire, preserving the original physical-center behaviour exactly. */
CBELT_TEST(equal_dpi_boundary_not_pinned) {
   FloatDragMonitor mons[2] = {Mon(-1080, 0, 0, 1920, 96),
                               Mon(0, 0, 2560, 1440, 96)};
   POINT const center = {0, 500};
   cbelt_assert_equal((size_t)0,
                      ClassifyPinnedFloatDrag(center, TOLERANCE, mons, 2, 0));
   return TEST_SUCCESS;
}

/* Center 2px into the source side of a mixed-DPI boundary — within the 3px
 * tolerance, so still classified as a pin. */
CBELT_TEST(pinned_within_tolerance) {
   FloatDragMonitor mons[2] = {Mon(-1080, 0, 0, 1920, 120),
                               Mon(0, 0, 2560, 1440, 96)};
   POINT const center = {-2, 500};
   cbelt_assert_equal((size_t)1,
                      ClassifyPinnedFloatDrag(center, TOLERANCE, mons, 2, 0));
   return TEST_SUCCESS;
}

/* Center exactly at the tolerance edge (3px into the source side) — still a
 * candidate for both monitors, so still a pin. */
CBELT_TEST(pinned_at_tolerance_edge) {
   FloatDragMonitor mons[2] = {Mon(-1080, 0, 0, 1920, 120),
                               Mon(0, 0, 2560, 1440, 96)};
   POINT const center = {-3, 500};
   cbelt_assert_equal((size_t)1,
                      ClassifyPinnedFloatDrag(center, TOLERANCE, mons, 2, 0));
   return TEST_SUCCESS;
}

/* Center 5px into the source side — beyond tolerance, the destination is no
 * longer a candidate, so this is not a pin. */
CBELT_TEST(beyond_tolerance_not_pinned) {
   FloatDragMonitor mons[2] = {Mon(-1080, 0, 0, 1920, 120),
                               Mon(0, 0, 2560, 1440, 96)};
   POINT const center = {-5, 500};
   cbelt_assert_equal((size_t)0,
                      ClassifyPinnedFloatDrag(center, TOLERANCE, mons, 2, 0));
   return TEST_SUCCESS;
}

/* Three monitors meeting at a corner: the center is within tolerance of all
 * three, so there are two non-source candidates — ambiguous, no migration. */
CBELT_TEST(three_monitor_corner_ambiguous) {
   FloatDragMonitor mons[3] = {Mon(-1080, 0, 0, 1920, 120),
                               Mon(0, 0, 2560, 1440, 96),
                               Mon(0, 1080, 2560, 2520, 144)};
   POINT const center = {0, 1080};
   cbelt_assert_equal((size_t)0,
                      ClassifyPinnedFloatDrag(center, TOLERANCE, mons, 3, 0));
   return TEST_SUCCESS;
}

/* Three monitors, center at a corner between two non-source monitors — the
 * source is not a boundary candidate, so the tie is ambiguous. */
CBELT_TEST(three_monitor_source_not_candidate) {
   FloatDragMonitor mons[3] = {Mon(-2000, 0, -1080, 1920, 120),
                               Mon(0, 0, 2560, 1440, 96),
                               Mon(0, 1080, 2560, 2520, 144)};
   POINT const center = {0, 1080};
   cbelt_assert_equal((size_t)0,
                      ClassifyPinnedFloatDrag(center, TOLERANCE, mons, 3, 0));
   return TEST_SUCCESS;
}

/* Degenerate inputs: null pointer / zero count return source_index unchanged.
 */
CBELT_TEST(null_or_zero_count) {
   FloatDragMonitor one = Mon(0, 0, 2560, 1440, 96);
   POINT const center = {100, 100};
   cbelt_assert_equal(
       (size_t)3, ClassifyPinnedFloatDrag(center, TOLERANCE, nullptr, 0, 3));
   cbelt_assert_equal((size_t)7,
                      ClassifyPinnedFloatDrag(center, TOLERANCE, &one, 0, 7));
   return TEST_SUCCESS;
}

/* Single monitor — never pinned. */
CBELT_TEST(single_monitor) {
   FloatDragMonitor mons[1] = {Mon(0, 0, 2560, 1440, 96)};
   POINT const center = {100, 100};
   cbelt_assert_equal((size_t)0,
                      ClassifyPinnedFloatDrag(center, TOLERANCE, mons, 1, 0));
   return TEST_SUCCESS;
}

/* Out-of-range source_index is defensively clamped to 0, so the pinned
 * boundary resolves to the non-source candidate. */
CBELT_TEST(source_index_out_of_range) {
   FloatDragMonitor mons[2] = {Mon(-1080, 0, 0, 1920, 120),
                               Mon(0, 0, 2560, 1440, 96)};
   POINT const center = {0, 500};
   cbelt_assert_equal((size_t)1,
                      ClassifyPinnedFloatDrag(center, TOLERANCE, mons, 2, 7));
   return TEST_SUCCESS;
}

/* A DPI of 0 (registry miss) is treated as the 96 base, so a 0/96 pair is
 * equal-DPI and not a mixed pin. */
CBELT_TEST(dpi_zero_treated_as_96) {
   FloatDragMonitor mons[2] = {Mon(-1080, 0, 0, 1920, 0),
                               Mon(0, 0, 2560, 1440, 96)};
   POINT const center = {0, 500};
   cbelt_assert_equal((size_t)0,
                      ClassifyPinnedFloatDrag(center, TOLERANCE, mons, 2, 0));
   return TEST_SUCCESS;
}
