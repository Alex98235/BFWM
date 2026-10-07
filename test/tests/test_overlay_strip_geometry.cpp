#include "cbelt.h"
#include <cmath>
#include <windows.h>

#include "../../src/window/border/overlay.h"

CBELT_GROUP("overlay_strip_geometry")

/* The ring decomposes into four thin layered windows:
 *   - TOP/BOTTOM: full-width strips owning the corner arcs
 *   - LEFT/RIGHT: full-height strips owning the straight side runs
 * Every strip paints the SAME full rounded-rect path with identical pixels,
 * so the corner squares, which are deliberately covered by two strips,
 * composite seamlessly regardless of z-order. These tests pin that geometry
 * contract: exact dimensions, painted-pixel coverage, and corner overlap. */

namespace {
/* Reference implementation of the documented spec:
 *   outer = stroke/2 + margin      (strip edge beyond the frame centerline)
 *   cap   = stroke/2 + 2           (painted extent + anti-aliasing pad)
 *   TOP/BOTTOM: full-width, height radius + stroke + margin + 2
 *   LEFT/RIGHT: full-height, width stroke + 2*margin + 2
 * All edges round outward (floorf top/left, ceilf bottom/right). */
inline void reference_rects(const RECT *frame, float stroke, float radius,
                            int margin, std::array<RECT, 4> *strips) {
   const float half = stroke / 2.0F;
   const float outer = half + (float)margin;
   const float cap = half + 2.0F;

   RECT top = {
       .left = (LONG)floorf((float)frame->left - outer),
       .top = (LONG)floorf((float)frame->top - outer),
       .right = (LONG)ceilf((float)frame->right + outer),
       .bottom = (LONG)ceilf((float)frame->top + radius + cap),
   };
   RECT bottom = {
       .left = (LONG)floorf((float)frame->left - outer),
       .top = (LONG)floorf((float)frame->bottom - radius - cap),
       .right = (LONG)ceilf((float)frame->right + outer),
       .bottom = (LONG)ceilf((float)frame->bottom + outer),
   };
   RECT left = {
       .left = (LONG)floorf((float)frame->left - outer),
       .top = (LONG)floorf((float)frame->top - outer),
       .right = (LONG)ceilf((float)frame->left + half + (float)margin + 2.0F),
       .bottom = (LONG)ceilf((float)frame->bottom + outer),
   };
   RECT right = {
       .left = (LONG)floorf((float)frame->right - half - (float)margin - 2.0F),
       .top = (LONG)floorf((float)frame->top - outer),
       .right = (LONG)ceilf((float)frame->right + outer),
       .bottom = (LONG)ceilf((float)frame->bottom + outer),
   };

   (*strips)[Overlay::OVERLAY_STRIP_TOP] = top;
   (*strips)[Overlay::OVERLAY_STRIP_BOTTOM] = bottom;
   (*strips)[Overlay::OVERLAY_STRIP_LEFT] = left;
   (*strips)[Overlay::OVERLAY_STRIP_RIGHT] = right;
}

inline auto rects_equal(const RECT *a, const RECT *b) -> bool {
   return a->left == b->left && a->top == b->top && a->right == b->right &&
          a->bottom == b->bottom;
}

inline auto rect_contains(const RECT *r, LONG x, LONG y) -> bool {
   return x >= r->left && x < r->right && y >= r->top && y < r->bottom;
}

inline auto overlap_width(const RECT *a, const RECT *b) -> LONG {
   const LONG left = a->left > b->left ? a->left : b->left;
   const LONG right = a->right < b->right ? a->right : b->right;
   return right - left;
}

inline auto overlap_height(const RECT *a, const RECT *b) -> LONG {
   const LONG top = a->top > b->top ? a->top : b->top;
   const LONG bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
   return bottom - top;
}

using Combo = struct {
   RECT frame;
   float stroke;
   float radius;
   int margin;
};

const std::array<Combo, 7> combos = {{
    /* defaults at 100% DPI: stroke 2, radius 8, margin 3 */
    {{.left = 100, .top = 200, .right = 900, .bottom = 700},
     2.0F,
     8.0F,
     3},
    /* 200% DPI */
    {{.left = 100, .top = 200, .right = 900, .bottom = 700},
     4.0F,
     16.0F,
     3},
    /* 150% DPI */
    {{.left = 100, .top = 200, .right = 900, .bottom = 700},
     3.0F,
     12.0F,
     3},
    /* square corners */
    {{.left = 100, .top = 200, .right = 900, .bottom = 700},
     2.0F,
     0.0F,
     3},
    /* fractional DPI scaling */
    {{.left = 100, .top = 200, .right = 900, .bottom = 700},
     2.2F,
     8.8F,
     3},
    /* small frame with a large (caller-clamped) radius */
    {{.left = 0, .top = 0, .right = 50, .bottom = 30},
     2.0F,
     25.0F,
     3},
    /* zero margin */
    {{.left = 100, .top = 200, .right = 900, .bottom = 700},
     2.0F,
     8.0F,
     0},
}};
} // namespace

/* The helper must agree with the documented spec exactly. */
CBELT_TEST(strip_rects_match_reference) {
   for (size_t i = 0; i < sizeof(combos) / sizeof(combos[0]); i++) {
      std::array<RECT, 4> got = {};
      std::array<RECT, 4> want = {};
      Overlay::OverlayComputeStripRects(&combos[i].frame, combos[i].stroke,
                               combos[i].radius, combos[i].margin, &got);
      reference_rects(&combos[i].frame, combos[i].stroke, combos[i].radius,
                      combos[i].margin, &want);
      for (int k = 0; k < 4; k++) {
         cbelt_assert(rects_equal(&got[k], &want[k]));
      }
   }
   return TEST_SUCCESS;
}

/* Every painted ring pixel must be covered: the edge lines and their
 * round-join caps land in TOP/BOTTOM, the side runs in LEFT/RIGHT, and each
 * arc/line tangent point sits in BOTH its edge strip and its side strip (the
 * overlap that makes seams z-order-independent). */
CBELT_TEST(corner_and_band_containment) {
   for (size_t i = 0; i < sizeof(combos) / sizeof(combos[0]); i++) {
      const RECT *f = &combos[i].frame;
      const float s = combos[i].stroke;
      const float r = combos[i].radius;
      std::array<RECT, 4> strips = {};
      Overlay::OverlayComputeStripRects(f, s, r, combos[i].margin, &strips);
      const LONG half = (LONG)(s / 2.0F);
      const LONG rt = (LONG)r;
      const LONG Left = f->left;
      const LONG Top = f->top;
      const LONG Right = f->right;
      const LONG Bottom = f->bottom;
      const LONG mx = (Left + Right) / 2;
      const LONG my = (Top + Bottom) / 2;

      /* top edge line + caps live in TOP */
      cbelt_assert(rect_contains(&strips[Overlay::OVERLAY_STRIP_TOP], Left + rt, Top));
      cbelt_assert(rect_contains(&strips[Overlay::OVERLAY_STRIP_TOP], Right - rt, Top));
      cbelt_assert(
          rect_contains(&strips[Overlay::OVERLAY_STRIP_TOP], Left + rt - half, Top));
      cbelt_assert(rect_contains(&strips[Overlay::OVERLAY_STRIP_TOP], mx, Top + half));
      /* bottom edge line lives in BOTTOM */
      cbelt_assert(
          rect_contains(&strips[Overlay::OVERLAY_STRIP_BOTTOM], Left + rt, Bottom));
      cbelt_assert(
          rect_contains(&strips[Overlay::OVERLAY_STRIP_BOTTOM], Right - rt, Bottom));
      cbelt_assert(
          rect_contains(&strips[Overlay::OVERLAY_STRIP_BOTTOM], mx, Bottom - half));
      /* side runs live in LEFT/RIGHT */
      cbelt_assert(rect_contains(&strips[Overlay::OVERLAY_STRIP_LEFT], Left, Top + rt));
      cbelt_assert(rect_contains(&strips[Overlay::OVERLAY_STRIP_LEFT], Left + half, my));
      cbelt_assert(
          rect_contains(&strips[Overlay::OVERLAY_STRIP_RIGHT], Right, Top + rt));
      cbelt_assert(
          rect_contains(&strips[Overlay::OVERLAY_STRIP_RIGHT], Right - half, my));

      /* arc/line tangent points: in BOTH the edge strip and the side strip */
      cbelt_assert(rect_contains(&strips[Overlay::OVERLAY_STRIP_TOP], Left, Top + rt));
      cbelt_assert(rect_contains(&strips[Overlay::OVERLAY_STRIP_LEFT], Left, Top + rt));
      cbelt_assert(rect_contains(&strips[Overlay::OVERLAY_STRIP_TOP], Right, Top + rt));
      cbelt_assert(
          rect_contains(&strips[Overlay::OVERLAY_STRIP_RIGHT], Right, Top + rt));
      cbelt_assert(
          rect_contains(&strips[Overlay::OVERLAY_STRIP_BOTTOM], Left, Bottom - rt));
      cbelt_assert(
          rect_contains(&strips[Overlay::OVERLAY_STRIP_LEFT], Left, Bottom - rt));
      cbelt_assert(
          rect_contains(&strips[Overlay::OVERLAY_STRIP_BOTTOM], Right, Bottom - rt));
      cbelt_assert(
          rect_contains(&strips[Overlay::OVERLAY_STRIP_RIGHT], Right, Bottom - rt));
   }
   return TEST_SUCCESS;
}

/* Each corner square is covered by two strips with an overlap band of at
 * least 2px in both axes, so seams never rely on an exact abut. */
CBELT_TEST(corner_squares_overlap_by_at_least_two) {
   for (size_t i = 0; i < sizeof(combos) / sizeof(combos[0]); i++) {
      std::array<RECT, 4> strips = {};
      Overlay::OverlayComputeStripRects(&combos[i].frame, combos[i].stroke,
                               combos[i].radius, combos[i].margin, &strips);
      const RECT *top = &strips[Overlay::OVERLAY_STRIP_TOP];
      const RECT *bottom = &strips[Overlay::OVERLAY_STRIP_BOTTOM];
      const RECT *left = &strips[Overlay::OVERLAY_STRIP_LEFT];
      const RECT *right = &strips[Overlay::OVERLAY_STRIP_RIGHT];
      cbelt_assert(overlap_width(top, left) >= 2);
      cbelt_assert(overlap_height(top, left) >= 2);
      cbelt_assert(overlap_width(top, right) >= 2);
      cbelt_assert(overlap_height(top, right) >= 2);
      cbelt_assert(overlap_width(bottom, left) >= 2);
      cbelt_assert(overlap_height(bottom, left) >= 2);
      cbelt_assert(overlap_width(bottom, right) >= 2);
      cbelt_assert(overlap_height(bottom, right) >= 2);
   }
   return TEST_SUCCESS;
}

/* Exact dimensions for integer inputs (defaults at 100% DPI): TOP/BOTTOM
 * height = radius + stroke + margin + 2, LEFT/RIGHT width = stroke + 2*margin
 * + 2, and the edge strips span the full frame width/height. */
CBELT_TEST(edge_strip_dimensions_match_spec) {
   const RECT f = {.left = 100, .top = 200, .right = 900, .bottom = 700};
   std::array<RECT, 4> strips = {};
   const float radius = 8.0F;
   Overlay::OverlayComputeStripRects(&f, 2.0F, radius, 3, &strips);

   cbelt_assert_equal((LONG)15, strips[Overlay::OVERLAY_STRIP_TOP].bottom -
                                    strips[Overlay::OVERLAY_STRIP_TOP].top);
   cbelt_assert_equal((LONG)15, strips[Overlay::OVERLAY_STRIP_BOTTOM].bottom -
                                    strips[Overlay::OVERLAY_STRIP_BOTTOM].top);
   cbelt_assert_equal(f.left - 4, strips[Overlay::OVERLAY_STRIP_TOP].left);
   cbelt_assert_equal(f.right + 4, strips[Overlay::OVERLAY_STRIP_TOP].right);
   cbelt_assert_equal((LONG)10, strips[Overlay::OVERLAY_STRIP_LEFT].right -
                                    strips[Overlay::OVERLAY_STRIP_LEFT].left);
   cbelt_assert_equal((LONG)10, strips[Overlay::OVERLAY_STRIP_RIGHT].right -
                                    strips[Overlay::OVERLAY_STRIP_RIGHT].left);
   cbelt_assert_equal(f.top - 4, strips[Overlay::OVERLAY_STRIP_LEFT].top);
   cbelt_assert_equal(f.bottom + 4, strips[Overlay::OVERLAY_STRIP_LEFT].bottom);
   return TEST_SUCCESS;
}

/* Across a broad parameter sweep the strips stay well-formed (positive size),
 * and seam containment holds for every physically valid radius (callers clamp
 * radius to half the frame's smaller dimension). */
CBELT_TEST(well_formed_across_parameter_sweep) {
   const LONG w_begin = 20;
   const LONG w_end = 2000;
   const LONG w_increment = 197;
   const LONG h_begin = 16;
   const LONG h_end = 1200;
   const LONG h_increment = 131;
   const float s_begin = 1.0F;
   const float s_end = 12.0F;
   const float s_increment = 7.5F;
   const float r_begin = 0.0F;
   const float r_end = 48.0F;
   const float r_increment = s_increment;
   for (LONG w = w_begin; w <= w_end; w += w_increment) {
      for (LONG h = h_begin; h <= h_end; h += h_increment) {
         for (float s = s_begin; s <= s_end; s += s_increment) {
            for (float r = r_begin; r <= r_end; r += r_increment) {
               for (int m = 0; m <= 8; m += 3) {
                  const RECT f = {
                      .left = 10, .top = 20, .right = 10 + w, .bottom = 20 + h};

                  /* well-formed for any radius, clamped or not */
                  std::array<RECT, 4> raw = {};
                  Overlay::OverlayComputeStripRects(&f, s, r, m, &raw);
                  for (int k = 0; k < 4; k++) {
                     cbelt_assert(raw[k].left < raw[k].right);
                     cbelt_assert(raw[k].top < raw[k].bottom);
                  }

                  /* containment for the caller-clamped radius */
                  const float r_eff =
                      fminf(r, fminf((float)w, (float)h) / 2.0F);
                  const LONG rt = (LONG)r_eff;
                  std::array<RECT, 4> strips = {};
                  Overlay::OverlayComputeStripRects(&f, s, r_eff, m, &strips);
                  cbelt_assert(rect_contains(&strips[Overlay::OVERLAY_STRIP_TOP], f.left,
                                             f.top + rt));
                  cbelt_assert(rect_contains(&strips[Overlay::OVERLAY_STRIP_LEFT],
                                             f.left, f.top + rt));
                  cbelt_assert(rect_contains(&strips[Overlay::OVERLAY_STRIP_TOP],
                                             f.right, f.top + rt));
                  cbelt_assert(rect_contains(&strips[Overlay::OVERLAY_STRIP_RIGHT],
                                             f.right, f.top + rt));
                  cbelt_assert(rect_contains(&strips[Overlay::OVERLAY_STRIP_BOTTOM],
                                             f.left, f.bottom - rt));
                  cbelt_assert(rect_contains(&strips[Overlay::OVERLAY_STRIP_LEFT],
                                             f.left, f.bottom - rt));
                  cbelt_assert(rect_contains(&strips[Overlay::OVERLAY_STRIP_BOTTOM],
                                             f.right, f.bottom - rt));
                  cbelt_assert(rect_contains(&strips[Overlay::OVERLAY_STRIP_RIGHT],
                                             f.right, f.bottom - rt));
               }
            }
         }
      }
   }
   return TEST_SUCCESS;
}
