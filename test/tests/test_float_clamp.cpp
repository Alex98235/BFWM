#include "cbelt.h"
#include <windows.h>

#include "../../src/math/rect.h"
#include "../../src/workspace/placement.h"

CBELT_GROUP("float_clamp")

/* ClampRectScaledToDpi is pure (no win32 calls): the size of `rect` is
 * replaced with the logical size scaled to `dpi` (logical × dpi/96), keeping
 * the top-left, then clamped to the work area. dpi <= 96 (or 0 = unknown) is
 * a plain clamp. */
namespace {

inline auto ExpectRect(long l, long t, long r, long b, RECT const &got)
    -> TestResult {
   cbelt_assert_equal(l, (long)got.left);
   cbelt_assert_equal(t, (long)got.top);
   cbelt_assert_equal(r, (long)got.right);
   cbelt_assert_equal(b, (long)got.bottom);
   return TEST_SUCCESS;
}

} // namespace

/* Capture case: an unaware window clamped before the OS stretch. The size is
 * pre-scaled to 333x592 @125% (416x740) so the post-stretch rect stays inside
 * the work area: 416 = 333*120/96, top-left held at -165,465. */
CBELT_TEST(capture_case_pre_stretch) {
   RECT rect = {.left = -165, .top = 465, .right = 168, .bottom = 1057};
   RECT const logical = {
       .left = -165, .top = 465, .right = 168, .bottom = 1057};
   RECT const work = {.left = -1080, .top = 0, .right = 0, .bottom = 1920};
   ClampRectScaledToDpi(&rect, &logical, &work, 120);
   return ExpectRect(-416, 465, 0, 1205, rect);
}

/* The OS already stretched the window (rect is 416 wide), but the logical
 * size still wins — the result is identical to the pre-stretch case. */
CBELT_TEST(already_stretched) {
   RECT rect = {.left = -207, .top = 465, .right = 209, .bottom = 1205};
   RECT const logical = {
       .left = -165, .top = 465, .right = 168, .bottom = 1057};
   RECT const work = {.left = -1080, .top = 0, .right = 0, .bottom = 1920};
   ClampRectScaledToDpi(&rect, &logical, &work, 120);
   return ExpectRect(-416, 465, 0, 1205, rect);
}

/* dpi == 96 (100% destination): plain clamp, no size change. */
CBELT_TEST(dpi_96_plain_clamp) {
   RECT rect = {.left = -165, .top = 465, .right = 168, .bottom = 1057};
   RECT const logical = {
       .left = -165, .top = 465, .right = 168, .bottom = 1057};
   RECT const work = {.left = -1080, .top = 0, .right = 0, .bottom = 1920};
   ClampRectScaledToDpi(&rect, &logical, &work, 96);
   return ExpectRect(-333, 465, 0, 1057, rect);
}

/* dpi == 0 (unknown): treated as scale 1.0 — plain clamp. */
CBELT_TEST(dpi_zero_plain_clamp) {
   RECT rect = {.left = -165, .top = 465, .right = 168, .bottom = 1057};
   RECT const logical = {
       .left = -165, .top = 465, .right = 168, .bottom = 1057};
   RECT const work = {.left = -1080, .top = 0, .right = 0, .bottom = 1920};
   ClampRectScaledToDpi(&rect, &logical, &work, 0);
   return ExpectRect(-333, 465, 0, 1057, rect);
}

/* Fully inside the work area: size adjusted to logical*scale, position
 * untouched. */
CBELT_TEST(fully_inside_size_adjusted) {
   RECT rect = {.left = 100, .top = 100, .right = 400, .bottom = 400};
   RECT const logical = {
       .left = 100, .top = 100, .right = 433, .bottom = 692}; // 333x592
   RECT const work = {.left = 0, .top = 0, .right = 2560, .bottom = 1440};
   ClampRectScaledToDpi(&rect, &logical, &work, 120);
   return ExpectRect(100, 100, 516, 840, rect); // 100+416, 100+740
}

/* Vertical overhang: the stretched height exceeds work.bottom, so the bottom
 * is pulled up to the work area edge. */
CBELT_TEST(vertical_overhang) {
   RECT rect = {.left = 100, .top = 1200, .right = 400, .bottom = 1600};
   RECT const logical = {
       .left = 100, .top = 1200, .right = 433, .bottom = 1792}; // 333x592
   RECT const work = {.left = 0, .top = 0, .right = 2560, .bottom = 1440};
   ClampRectScaledToDpi(&rect, &logical, &work, 120);
   return ExpectRect(100, 700, 516, 1440, rect);
}

/* Both dimensions overhang: each axis is clamped independently. */
CBELT_TEST(both_dimension_overhang) {
   RECT rect = {.left = 100, .top = 1200, .right = 400, .bottom = 1600};
   RECT const logical = {
       .left = 100, .top = 1200, .right = 433, .bottom = 1792};
   RECT const work = {.left = 0, .top = 0, .right = 500, .bottom = 1440};
   ClampRectScaledToDpi(&rect, &logical, &work, 120);
   return ExpectRect(84, 700, 500, 1440, rect);
}

/* The scaled size is larger than the work area: stretched to the full limit
 * (ClampToRect semantics: if w > limit_w, stretch to the limit). */
CBELT_TEST(larger_than_work_area_stretched) {
   RECT rect = {.left = -200, .top = 0, .right = 400, .bottom = 800};
   RECT const logical = {
       .left = -200, .top = 0, .right = 400, .bottom = 800}; // 600x800
   RECT const work = {.left = 0, .top = 0, .right = 500, .bottom = 1000};
   ClampRectScaledToDpi(&rect, &logical, &work, 120);
   return ExpectRect(0, 0, 500, 1000, rect);
}

/* The logical size always wins for unaware windows: a 520-wide physical rect
 * is re-sized to 333*1.25 = 416 regardless of its current width. */
CBELT_TEST(logical_size_wins) {
   RECT rect = {.left = -165, .top = 465, .right = 355, .bottom = 1057};
   RECT const logical = {
       .left = -165, .top = 465, .right = 168, .bottom = 1057}; // 333x592
   RECT const work = {.left = -1080, .top = 0, .right = 0, .bottom = 1920};
   ClampRectScaledToDpi(&rect, &logical, &work, 120);
   return ExpectRect(-416, 465, 0, 1205, rect);
}

/* --- ClampRectWithInsets (visible-frame clamp) --- */

/* (a) Regression: all-zero insets must behave exactly like ClampToRect. */
CBELT_TEST(clamp_insets_zero_is_plain_clamp) {
   RECT const bounds = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   FrameInsets const zero = {.left = 0, .top = 0, .right = 0, .bottom = 0};
   RECT rect = {.left = 2000, .top = -10, .right = 2500, .bottom = 400};
   RECT plain = rect;
   ClampToRect(bounds, &plain);
   ClampRectWithInsets(&rect, &bounds, zero);
   return ExpectRect(plain.left, plain.top, plain.right, plain.bottom, rect);
}

/* (b) Symmetric insets: a rect overhanging the left edge is clamped so the
 * visible frame is flush with bounds.left while the outer rect extends past
 * the bounds by the inset (the invisible border goes off-screen). */
CBELT_TEST(clamp_insets_left_edge_visible_flush) {
   RECT const bounds = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   FrameInsets const insets = {.left = 7, .top = 7, .right = 7, .bottom = 7};
   RECT rect = {.left = -10, .top = 100, .right = 300, .bottom = 400};
   ClampRectWithInsets(&rect, &bounds, insets);
   /* visible frame = {0, 107, 296, 393} -> outer = {-7, 100, 303, 400} */
   return ExpectRect(-7, 100, 303, 400, rect);
}

/* (c) Top-edge bug repro: dragging a window flush puts the outer rect 7px
 * above the work-area top (its visible frame at 0). The old plain
 * outer-rect clamp would snap the outer rect to top = 0, leaving a visible
 * 7px gap below the edge; the inset clamp keeps the visible frame flush at 0
 * and lets the outer rect sit off-screen by the inset. */
CBELT_TEST(clamp_insets_top_edge_no_gap) {
   RECT const bounds = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   FrameInsets const insets = {.left = 7, .top = 7, .right = 7, .bottom = 7};
   /* Already flush: the outer rect at top -7 (visible frame at 0) is kept. */
   RECT flush = {.left = 100, .top = -7, .right = 400, .bottom = 300};
   ClampRectWithInsets(&flush, &bounds, insets);
   TestResult const flush_result = ExpectRect(100, -7, 400, 300, flush);
   /* Overhanging: the visible frame (top = -10 + 7 = -3) is pulled back to
    * flush at 0, landing the outer rect at -7 (bottom rides along from 300 to
    * 303, the height preserved) — never the old gap-at-0 result. */
   RECT over = {.left = 100, .top = -10, .right = 400, .bottom = 300};
   ClampRectWithInsets(&over, &bounds, insets);
   TestResult const over_result = ExpectRect(100, -7, 400, 303, over);
   if ((flush_result == TEST_FAILURE) || (over_result == TEST_FAILURE))
      return TEST_FAILURE;
   return TEST_SUCCESS;
}

/* (d) Asymmetric insets: each edge clamps independently by its own inset. */
CBELT_TEST(clamp_insets_asymmetric) {
   RECT const bounds = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   FrameInsets const insets = {.left = 3, .top = 7, .right = 11, .bottom = 5};
   RECT rect = {.left = -10, .top = -10, .right = 300, .bottom = 400};
   ClampRectWithInsets(&rect, &bounds, insets);
   /* visible = {0, 0, 296, 398} -> outer = {-3, -7, 307, 403} */
   return ExpectRect(-3, -7, 307, 403, rect);
}

/* (e) The visible frame larger than the bounds is stretched to fill them
 * (ClampToRect semantics), with the outer rect then extending past the bounds
 * by the insets. */
CBELT_TEST(clamp_insets_larger_than_bounds_stretch) {
   RECT const bounds = {.left = 0, .top = 0, .right = 100, .bottom = 100};
   FrameInsets const insets = {.left = 5, .top = 5, .right = 5, .bottom = 5};
   RECT rect = {.left = -10, .top = -10, .right = 200, .bottom = 200};
   ClampRectWithInsets(&rect, &bounds, insets);
   /* visible stretched to {0, 0, 100, 100} -> outer = {-5, -5, 105, 105} */
   return ExpectRect(-5, -5, 105, 105, rect);
}

/* (f) A fully-inside rect (visible frame inside the bounds) is unchanged. */
CBELT_TEST(clamp_insets_fully_inside_unchanged) {
   RECT const bounds = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   FrameInsets const insets = {.left = 7, .top = 7, .right = 7, .bottom = 7};
   RECT rect = {.left = 100, .top = 100, .right = 400, .bottom = 400};
   ClampRectWithInsets(&rect, &bounds, insets);
   return ExpectRect(100, 100, 400, 400, rect);
}

/* --- Ring-offset insets (negative = ring pokes past the GWR edge) --- */

/* Ring pokes 3px past the GWR top-left: the window is pulled in so the ring's
 * outer edge lands exactly on the bounds. ClampRectWithInsets preserves the
 * visible frame's size (503x403 = GWR 500x400 + 3px ring on left/top), so the
 * GWR lands at {3,3} with the ring flush at {0,0}. */
CBELT_TEST(clamp_negative_inset_pulls_window_in) {
   RECT const bounds = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   FrameInsets const insets = {.left = -3, .top = -3, .right = 0, .bottom = 0};
   RECT rect = {.left = 0, .top = 0, .right = 500, .bottom = 400};
   ClampRectWithInsets(&rect, &bounds, insets);
   /* visible = {-3,-3,500,400} (503x403) -> clamped left/top to 0 ->
    * visible = {0,0,503,403} -> outer = {3,3,503,403} */
   return ExpectRect(3, 3, 503, 403, rect);
}

/* Mixed insets: the top ring pokes out 3px while the sides are inset 8px.
 * The visible frame {-3 top} is pulled flush to 0; the preserved height shifts
 * the bottom (595 = 592 + 3), so the outer bottom lands at 603, not 600. */
CBELT_TEST(clamp_mixed_negative_positive) {
   RECT const bounds = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};
   FrameInsets const insets = {.left = 8, .top = -3, .right = 8, .bottom = 8};
   RECT rect = {.left = 0, .top = 0, .right = 800, .bottom = 600};
   ClampRectWithInsets(&rect, &bounds, insets);
   /* visible = {8,-3,792,592} (784x595) -> top clamped to 0 ->
    * visible = {8,0,792,595} -> outer = {0,3,800,603} */
   return ExpectRect(0, 3, 800, 603, rect);
}

/* Ring pokes 4px past the GWR right/bottom edge: the window is pulled in so
 * the ring lands exactly on bounds.right/bottom. The preserved visible size
 * also shifts the left/top edges (46/36) to absorb the overhang. */
CBELT_TEST(clamp_negative_inset_right_bottom) {
   RECT const bounds = {.left = 0, .top = 0, .right = 100, .bottom = 80};
   FrameInsets const insets = {.left = 0, .top = 0, .right = -4, .bottom = -4};
   RECT rect = {.left = 50, .top = 40, .right = 100, .bottom = 80};
   ClampRectWithInsets(&rect, &bounds, insets);
   /* visible = {50,40,104,84} (54x44) -> right/bottom overhang ->
    * visible = {46,36,100,80} -> outer = {46,36,96,76} */
   return ExpectRect(46, 36, 96, 76, rect);
}

/* A ring inset (60) larger than the rect's width (50): the visible frame
 * (110 wide) exceeds the bounds width, so ClampRectWithInsets stretches it to
 * fill the bounds and maps back — the GWR is pushed right so the ring lands on
 * bounds.left. */
CBELT_TEST(clamp_ring_offsets_large) {
   RECT const bounds = {.left = 0, .top = 0, .right = 100, .bottom = 100};
   FrameInsets const insets = {.left = -60, .top = 0, .right = 0, .bottom = 0};
   RECT rect = {.left = 0, .top = 0, .right = 50, .bottom = 50};
   ClampRectWithInsets(&rect, &bounds, insets);
   /* visible = {-60,0,50,50} (110x50) -> width > bounds: stretched to fill ->
    * visible = {0,0,100,50} -> outer = {60,0,100,50} */
   return ExpectRect(60, 0, 100, 50, rect);
}
