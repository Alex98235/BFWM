/**
 * @file rect.c
 * @brief Rectangle geometry utility implementations.
 */

#include "rect.h"
#include <minwindef.h>
#include <windef.h>

#include <algorithm>

void ClampToRect(RECT limit, RECT *rect) {
   int w = rect->right - rect->left;
   int h = rect->bottom - rect->top;
   int const limit_w = limit.right - limit.left;
   int const limit_h = limit.bottom - limit.top;

   if (w > limit_w) {
      rect->left = limit.left;
      rect->right = limit.right;
      w = limit_w;
   }

   if (h > limit_h) {
      rect->top = limit.top;
      rect->bottom = limit.bottom;
      h = limit_h;
   }

   rect->left = std::max(rect->left, limit.left);
   if (rect->left + w > limit.right)
      rect->left = limit.right - w;
   rect->right = rect->left + w;

   rect->top = std::max(rect->top, limit.top);
   if (rect->top + h > limit.bottom)
      rect->top = limit.bottom - h;
   rect->bottom = rect->top + h;
}

auto CenterRectInRect(RECT outer, RECT inner) -> RECT {
   int const outer_w = outer.right - outer.left;
   int const outer_h = outer.bottom - outer.top;
   int const inner_w = inner.right - inner.left;
   int const inner_h = inner.bottom - inner.top;

   RECT result;
   result.left = outer.left + ((outer_w - inner_w) / 2);
   result.top = outer.top + ((outer_h - inner_h) / 2);
   result.right = result.left + inner_w;
   result.bottom = result.top + inner_h;

   return result;
}

auto PointIntersectsRect(POINT intersect_point, RECT rect) -> BOOL {
   return static_cast<BOOL>(
       intersect_point.x >= rect.left && intersect_point.x <= rect.right &&
       intersect_point.y >= rect.top && intersect_point.y <= rect.bottom);
}

auto RectIsNeighbor(RECT src, RECT cand, BFWMDirection dir, int *out_dist)
    -> BOOL {
   switch (dir) {
   case DirLeft:
      if (cand.right <= src.left && cand.bottom > src.top &&
          cand.top < src.bottom) {
         *out_dist = src.left - cand.right;
         return TRUE;
      }
      break;
   case DirRight:
      if (cand.left >= src.right && cand.bottom > src.top &&
          cand.top < src.bottom) {
         *out_dist = cand.left - src.right;
         return TRUE;
      }
      break;
   case DirUp:
      if (cand.bottom <= src.top && cand.right > src.left &&
          cand.left < src.right) {
         *out_dist = src.top - cand.bottom;
         return TRUE;
      }
      break;
   case DirDown:
      if (cand.top >= src.bottom && cand.right > src.left &&
          cand.left < src.right) {
         *out_dist = cand.top - src.bottom;
         return TRUE;
      }
      break;
   default:
      break;
   }
   return FALSE;
}
