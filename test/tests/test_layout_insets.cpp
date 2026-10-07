#include "cbelt.h"
#include <windows.h>

#include "../../src/dpi/dpi.h"
#include "../../src/workspace/layouts/dwindle/dwindle.h"
#include "../../src/workspace/layouts/monocle/monocle.h"

CBELT_GROUP("layout_insets")

/* The layout engines are pure: they consume pre-scaled physical gap/border
 * values and produce physical rects. These tests feed DpiSystem::Scale()
 * output at 100/150/200% and pin the exact inset math:
 *   - dwindle: gap_between/2 + border_width per edge, on top of the
 *     gap_edge-inset workspace rect
 *   - monocle: gap_edge + border_width per edge (gap_between unused) */

namespace {

const RECT kWorkspace = {.left = 0, .top = 0, .right = 1920, .bottom = 1080};

inline auto scaled(int logical, UINT dpi) -> int {
   return DpiSystem::Scale(logical, dpi);
}

} // namespace

/* =========================================================================
 * Monocle — gap_edge + border_width inset
 * ========================================================================= */

CBELT_TEST(monocle_insets_100_percent) {
   MonocleLayout *ml = MonocleLayoutCreate(kWorkspace);
   LayoutConfig cfg = {0, scaled(6, 96), scaled(2, 96), kWorkspace};
   MonocleLayoutApplyConfig(ml, cfg);

   RECT out;
   cbelt_assert(MonocleGetWindowRect(ml, reinterpret_cast<HWND>(1), &out));
   cbelt_assert_equal((LONG)8, out.left);
   cbelt_assert_equal((LONG)8, out.top);
   cbelt_assert_equal((LONG)1912, out.right);
   cbelt_assert_equal((LONG)1072, out.bottom);

   MonocleLayoutFree(ml);
   return TEST_SUCCESS;
}

CBELT_TEST(monocle_insets_150_percent) {
   MonocleLayout *ml = MonocleLayoutCreate(kWorkspace);
   LayoutConfig cfg = {0, scaled(6, 144), scaled(2, 144), kWorkspace};
   MonocleLayoutApplyConfig(ml, cfg);

   RECT out;
   cbelt_assert(MonocleGetWindowRect(ml, reinterpret_cast<HWND>(1), &out));
   /* 6 logical -> 9, 2 logical -> 3: inset 12 per edge */
   cbelt_assert_equal((LONG)12, out.left);
   cbelt_assert_equal((LONG)12, out.top);
   cbelt_assert_equal((LONG)1908, out.right);
   cbelt_assert_equal((LONG)1068, out.bottom);

   MonocleLayoutFree(ml);
   return TEST_SUCCESS;
}

CBELT_TEST(monocle_insets_200_percent) {
   MonocleLayout *ml = MonocleLayoutCreate(kWorkspace);
   LayoutConfig cfg = {0, scaled(6, 192), scaled(2, 192), kWorkspace};
   MonocleLayoutApplyConfig(ml, cfg);

   RECT out;
   cbelt_assert(MonocleGetWindowRect(ml, reinterpret_cast<HWND>(1), &out));
   /* 6 logical -> 12, 2 logical -> 4: inset 16 per edge */
   cbelt_assert_equal((LONG)16, out.left);
   cbelt_assert_equal((LONG)16, out.top);
   cbelt_assert_equal((LONG)1904, out.right);
   cbelt_assert_equal((LONG)1064, out.bottom);

   MonocleLayoutFree(ml);
   return TEST_SUCCESS;
}

/* =========================================================================
 * Dwindle — single window, gap_between/2 + border_width inset
 * ========================================================================= */

CBELT_TEST(dwindle_single_insets_100_percent) {
   DwindleLayout *dl = DwindleLayoutCreate(kWorkspace, 0.5);
   cbelt_assert(DwindleInsertWindow(dl, reinterpret_cast<HWND>(1),
                                    reinterpret_cast<HWND>(0)));
   LayoutConfig cfg = {scaled(6, 96), 0, scaled(2, 96), kWorkspace};
   DwindleLayoutApplyConfig(dl, cfg);

   RECT out;
   cbelt_assert(DwindleGetWindowRect(dl, reinterpret_cast<HWND>(1), &out));
   /* inset = 6/2 + 2 = 5 */
   cbelt_assert_equal((LONG)5, out.left);
   cbelt_assert_equal((LONG)5, out.top);
   cbelt_assert_equal((LONG)1915, out.right);
   cbelt_assert_equal((LONG)1075, out.bottom);

   DwindleLayoutFree(dl);
   return TEST_SUCCESS;
}

CBELT_TEST(dwindle_single_insets_150_percent) {
   DwindleLayout *dl = DwindleLayoutCreate(kWorkspace, 0.5);
   cbelt_assert(DwindleInsertWindow(dl, reinterpret_cast<HWND>(1),
                                    reinterpret_cast<HWND>(0)));
   LayoutConfig cfg = {scaled(6, 144), 0, scaled(2, 144), kWorkspace};
   DwindleLayoutApplyConfig(dl, cfg);

   RECT out;
   cbelt_assert(DwindleGetWindowRect(dl, reinterpret_cast<HWND>(1), &out));
   /* gap 9, border 3: inset = 9/2 + 3 = 7 */
   cbelt_assert_equal((LONG)7, out.left);
   cbelt_assert_equal((LONG)7, out.top);
   cbelt_assert_equal((LONG)1913, out.right);
   cbelt_assert_equal((LONG)1073, out.bottom);

   DwindleLayoutFree(dl);
   return TEST_SUCCESS;
}

CBELT_TEST(dwindle_single_insets_200_percent) {
   DwindleLayout *dl = DwindleLayoutCreate(kWorkspace, 0.5);
   cbelt_assert(DwindleInsertWindow(dl, reinterpret_cast<HWND>(1),
                                    reinterpret_cast<HWND>(0)));
   LayoutConfig cfg = {scaled(6, 192), 0, scaled(2, 192), kWorkspace};
   DwindleLayoutApplyConfig(dl, cfg);

   RECT out;
   cbelt_assert(DwindleGetWindowRect(dl, reinterpret_cast<HWND>(1), &out));
   /* gap 12, border 4: inset = 12/2 + 4 = 10 */
   cbelt_assert_equal((LONG)10, out.left);
   cbelt_assert_equal((LONG)10, out.top);
   cbelt_assert_equal((LONG)1910, out.right);
   cbelt_assert_equal((LONG)1070, out.bottom);

   DwindleLayoutFree(dl);
   return TEST_SUCCESS;
}

/* =========================================================================
 * Dwindle — gap_edge insets the workspace rect before the split
 * ========================================================================= */

CBELT_TEST(dwindle_edge_gap_150_percent) {
   DwindleLayout *dl = DwindleLayoutCreate(kWorkspace, 0.5);
   cbelt_assert(DwindleInsertWindow(dl, reinterpret_cast<HWND>(1),
                                    reinterpret_cast<HWND>(0)));
   LayoutConfig cfg = {scaled(6, 144), scaled(6, 144), scaled(2, 144),
                       kWorkspace};
   DwindleLayoutApplyConfig(dl, cfg);

   RECT out;
   cbelt_assert(DwindleGetWindowRect(dl, reinterpret_cast<HWND>(1), &out));
   /* workspace inset by edge 9, then gap/2 4 + border 3 -> 16 */
   cbelt_assert_equal((LONG)16, out.left);
   cbelt_assert_equal((LONG)16, out.top);
   cbelt_assert_equal((LONG)1904, out.right);
   cbelt_assert_equal((LONG)1064, out.bottom);

   DwindleLayoutFree(dl);
   return TEST_SUCCESS;
}

/* =========================================================================
 * Dwindle — two-window split: scaled gap lands exactly between the windows
 * ========================================================================= */

CBELT_TEST(dwindle_two_window_split_insets_150_percent) {
   DwindleLayout *dl = DwindleLayoutCreate(kWorkspace, 0.5);
   cbelt_assert(DwindleInsertWindow(dl, reinterpret_cast<HWND>(1),
                                    reinterpret_cast<HWND>(0)));
   cbelt_assert(DwindleInsertWindow(dl, reinterpret_cast<HWND>(2),
                                    reinterpret_cast<HWND>(1)));
   LayoutConfig cfg = {scaled(6, 144), 0, scaled(2, 144), kWorkspace};
   DwindleLayoutApplyConfig(dl, cfg);

   RECT r1;
   RECT r2;
   cbelt_assert(DwindleGetWindowRect(dl, reinterpret_cast<HWND>(1), &r1));
   cbelt_assert(DwindleGetWindowRect(dl, reinterpret_cast<HWND>(2), &r2));

   /* split at 960; inset = 9/2 + 3 = 7 per edge */
   cbelt_assert_equal((LONG)7, r1.left);
   cbelt_assert_equal((LONG)7, r1.top);
   cbelt_assert_equal((LONG)953, r1.right);
   cbelt_assert_equal((LONG)1073, r1.bottom);

   cbelt_assert_equal((LONG)967, r2.left);
   cbelt_assert_equal((LONG)7, r2.top);
   cbelt_assert_equal((LONG)1913, r2.right);
   cbelt_assert_equal((LONG)1073, r2.bottom);

   /* visual gap = 2 * inset = scaled gap (8) + 2 * scaled border (6) */
   cbelt_assert_equal((LONG)14, r2.left - r1.right);

   DwindleLayoutFree(dl);
   return TEST_SUCCESS;
}

CBELT_TEST(dwindle_two_window_split_insets_200_percent) {
   DwindleLayout *dl = DwindleLayoutCreate(kWorkspace, 0.5);
   cbelt_assert(DwindleInsertWindow(dl, reinterpret_cast<HWND>(1),
                                    reinterpret_cast<HWND>(0)));
   cbelt_assert(DwindleInsertWindow(dl, reinterpret_cast<HWND>(2),
                                    reinterpret_cast<HWND>(1)));
   LayoutConfig cfg = {scaled(6, 192), 0, scaled(2, 192), kWorkspace};
   DwindleLayoutApplyConfig(dl, cfg);

   RECT r1;
   RECT r2;
   cbelt_assert(DwindleGetWindowRect(dl, reinterpret_cast<HWND>(1), &r1));
   cbelt_assert(DwindleGetWindowRect(dl, reinterpret_cast<HWND>(2), &r2));

   /* split at 960; inset = 12/2 + 4 = 10 per edge */
   cbelt_assert_equal((LONG)10, r1.left);
   cbelt_assert_equal((LONG)10, r1.top);
   cbelt_assert_equal((LONG)950, r1.right);
   cbelt_assert_equal((LONG)1070, r1.bottom);

   cbelt_assert_equal((LONG)970, r2.left);
   cbelt_assert_equal((LONG)10, r2.top);
   cbelt_assert_equal((LONG)1910, r2.right);
   cbelt_assert_equal((LONG)1070, r2.bottom);

   /* visual gap = 2 * inset = scaled gap (12) + 2 * scaled border (8) */
   cbelt_assert_equal((LONG)20, r2.left - r1.right);

   DwindleLayoutFree(dl);
   return TEST_SUCCESS;
}