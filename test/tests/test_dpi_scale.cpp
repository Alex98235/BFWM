#include "cbelt.h"
#include <cmath>
#include <windows.h>

#include "../../src/dpi/dpi.h"

CBELT_GROUP("dpi_scale")

/* DpiSystem pure scaling math — no context, no registry, no windowing.
 * Scale is round-half-up: (v * dpi + 48) / 96 for non-negative input,
 * sign-symmetric for negatives. */

CBELT_TEST(scale_at_100_percent) {
   cbelt_assert_equal(1, DpiSystem::Scale(1, 96));
   cbelt_assert_equal(3, DpiSystem::Scale(3, 96));
   cbelt_assert_equal(100, DpiSystem::Scale(100, 96));
   return TEST_SUCCESS;
}

CBELT_TEST(scale_at_150_percent) {
   /* (v * 144 + 48) / 96 with integer division */
   cbelt_assert_equal(2, DpiSystem::Scale(1, 144));   /* 1.5   -> 2 */
   cbelt_assert_equal(3, DpiSystem::Scale(2, 144));   /* 3.0   -> 3 */
   cbelt_assert_equal(5, DpiSystem::Scale(3, 144));   /* 4.5   -> 5 (round-half-up) */
   cbelt_assert_equal(6, DpiSystem::Scale(4, 144));   /* 6.0   -> 6 */
   cbelt_assert_equal(15, DpiSystem::Scale(10, 144)); /* 15.0  -> 15 */
   return TEST_SUCCESS;
}

CBELT_TEST(scale_at_200_percent) {
   cbelt_assert_equal(2, DpiSystem::Scale(1, 192));  /* 2.0  -> 2 */
   cbelt_assert_equal(4, DpiSystem::Scale(2, 192));  /* 4.0  -> 4 */
   cbelt_assert_equal(6, DpiSystem::Scale(3, 192));  /* 6.0  -> 6 */
   cbelt_assert_equal(8, DpiSystem::Scale(4, 192));  /* 8.0  -> 8 */
   cbelt_assert_equal(10, DpiSystem::Scale(5, 192)); /* 10.0 -> 10 */
   return TEST_SUCCESS;
}

CBELT_TEST(scale_at_125_percent) {
   /* (v * 120 + 48) / 96 with integer division. 120/96 = 1.25x, so the
    * exact products are quarter-step values; the +48 offset rounds
    * half-up (e.g. 7 * 1.25 = 8.75 -> 9). */
   cbelt_assert_equal(1, DpiSystem::Scale(1, 120));  /* 1.25  -> 1 */
   cbelt_assert_equal(3, DpiSystem::Scale(2, 120));  /* 2.5   -> 3 */
   cbelt_assert_equal(4, DpiSystem::Scale(3, 120));  /* 3.75  -> 4 */
   cbelt_assert_equal(5, DpiSystem::Scale(4, 120));  /* 5.0   -> 5 */
   cbelt_assert_equal(8, DpiSystem::Scale(6, 120));  /* 7.5   -> 8 */
   cbelt_assert_equal(9, DpiSystem::Scale(7, 120));  /* 8.75  -> 9 (round-half-up) */
   cbelt_assert_equal(10, DpiSystem::Scale(8, 120)); /* 10.0  -> 10 */
   cbelt_assert_equal(13, DpiSystem::Scale(10, 120)); /* 12.5 -> 13 */
   /* Sign-symmetric negatives */
   cbelt_assert_equal(-9, DpiSystem::Scale(-7, 120));
   cbelt_assert_equal(-13, DpiSystem::Scale(-10, 120));
   /* Unscale round-trips at 120 */
   cbelt_assert_equal(7, DpiSystem::Unscale(9, 120));
   cbelt_assert_equal(10, DpiSystem::Unscale(13, 120));
   cbelt_assert_equal(2, DpiSystem::Unscale(3, 120));
   return TEST_SUCCESS;
}

CBELT_TEST(scale_negative_sign_symmetric) {
   cbelt_assert_equal(-1, DpiSystem::Scale(-1, 96));
   cbelt_assert_equal(-2, DpiSystem::Scale(-1, 144));
   cbelt_assert_equal(-5, DpiSystem::Scale(-3, 144));
   cbelt_assert_equal(-6, DpiSystem::Scale(-3, 192));
   cbelt_assert_equal(-10, DpiSystem::Scale(-5, 192));
   return TEST_SUCCESS;
}

CBELT_TEST(scale_zero) {
   cbelt_assert_equal(0, DpiSystem::Scale(0, 96));
   cbelt_assert_equal(0, DpiSystem::Scale(0, 144));
   cbelt_assert_equal(0, DpiSystem::Scale(0, 192));
   return TEST_SUCCESS;
}

CBELT_TEST(scale_float) {
   cbelt_assert(fabsf(DpiSystem::ScaleF(2.5F, 120) - 3.125F) < 1e-4F);
   cbelt_assert(fabsf(DpiSystem::ScaleF(1.0F, 96) - 1.0F) < 1e-4F);
   cbelt_assert(fabsf(DpiSystem::ScaleF(3.0F, 144) - 4.5F) < 1e-4F);
   cbelt_assert(fabsf(DpiSystem::ScaleF(-2.0F, 192) - (-4.0F)) < 1e-4F);
   return TEST_SUCCESS;
}

CBELT_TEST(scale_rect) {
   RECT const logical = {.left = 10, .top = 20, .right = 100, .bottom = 50};
   RECT const scaled = DpiSystem::ScaleRect(logical, 144);
   cbelt_assert_equal((LONG)15, scaled.left);
   cbelt_assert_equal((LONG)30, scaled.top);
   cbelt_assert_equal((LONG)150, scaled.right);
   cbelt_assert_equal((LONG)75, scaled.bottom);
   return TEST_SUCCESS;
}

CBELT_TEST(unscale_round_trip_clean) {
   /* Values that scale cleanly round-trip exactly. */
   const UINT dpis[] = {96, 120, 144, 192};
   const int logicals[] = {0, 1, 2, 3, 6, 10, 12, 24, 48, 100};
   for (UINT dpi : dpis) {
      for (int logical : logicals) {
         int const physical = DpiSystem::Scale(logical, dpi);
         cbelt_assert_equal(logical, DpiSystem::Unscale(physical, dpi));
      }
   }
   return TEST_SUCCESS;
}

CBELT_TEST(unscale_round_trip_bounded) {
   /* For arbitrary values the round-trip stays within ±1 logical px. */
   const UINT dpis[] = {96, 120, 144, 192};
   for (UINT dpi : dpis) {
      for (int logical = -50; logical <= 50; logical++) {
         int const physical = DpiSystem::Scale(logical, dpi);
         int const back = DpiSystem::Unscale(physical, dpi);
         cbelt_assert(back >= logical - 1 && back <= logical + 1);
      }
   }
   return TEST_SUCCESS;
}

CBELT_TEST(registry_fallback_unknown_monitor) {
   DpiSystem dpi;
   HMONITOR const unknown = reinterpret_cast<HMONITOR>(0xDEADBEEF);
   /* Empty registry: the miss is a benign first-use (no debug assert), and
    * the sentinel is 0 — never a valid DPI (real effective DPIs are >= 96),
    * so a miss is distinguishable from a genuine 96-DPI monitor. */
   cbelt_assert_equal(0U, dpi.GetDpi(unknown));
   cbelt_assert(fabsf(dpi.GetScale(unknown) - 0.0F) < 1e-4F);
   return TEST_SUCCESS;
}

CBELT_TEST(registry_register_and_lookup) {
   DpiSystem dpi;
   HMONITOR const mon = reinterpret_cast<HMONITOR>(0x1234);
   cbelt_assert_equal(0U, dpi.GetDpi(mon));
   dpi.RegisterMonitor(mon, 144);
   cbelt_assert_equal(144U, dpi.GetDpi(mon));
   cbelt_assert(fabsf(dpi.GetScale(mon) - 1.5F) < 1e-4F);
   cbelt_assert_equal(3, dpi.ScaleForMonitor(mon, 2));
   return TEST_SUCCESS;
}

CBELT_TEST(registry_upsert_and_clear) {
   DpiSystem dpi;
   HMONITOR const mon = reinterpret_cast<HMONITOR>(0x5678);
   dpi.RegisterMonitor(mon, 120);
   dpi.RegisterMonitor(mon, 192); /* upsert replaces the entry */
   cbelt_assert_equal(192U, dpi.GetDpi(mon));
   dpi.Clear();
   cbelt_assert_equal(0U, dpi.GetDpi(mon));
   return TEST_SUCCESS;
}

CBELT_TEST(registry_count_and_monitor_at) {
   DpiSystem dpi;
   HMONITOR const mon_a = reinterpret_cast<HMONITOR>(0x1111);
   HMONITOR const mon_b = reinterpret_cast<HMONITOR>(0x2222);
   cbelt_assert(dpi.Count() == 0);
   cbelt_assert(dpi.MonitorAt(0) == nullptr); /* out-of-range -> null */

   dpi.RegisterMonitor(mon_a, 144);
   dpi.RegisterMonitor(mon_b, 120);
   cbelt_assert(dpi.Count() == 2);
   cbelt_assert(dpi.MonitorAt(0) == mon_a);
   cbelt_assert(dpi.MonitorAt(1) == mon_b);
   cbelt_assert_equal(144U, dpi.GetDpi(dpi.MonitorAt(0)));
   cbelt_assert_equal(120U, dpi.GetDpi(dpi.MonitorAt(1)));
   cbelt_assert(dpi.MonitorAt(2) == nullptr);

   dpi.Clear();
   cbelt_assert(dpi.Count() == 0);
   return TEST_SUCCESS;
}