/**
 * @file rect.h
 * @brief Rectangle geometry utility functions.
 *
 * Provides common rectangle operations such as clamping, centering,
 * point intersection testing, and directional neighbor detection.
 */

#ifndef BFWM_RECT_TRANSFORM_H
#define BFWM_RECT_TRANSFORM_H

#include "../core/bfwm_def.h"
#include <windows.h>

/**
 * @brief Clamp a rectangle so it fits within a limiting rectangle.
 *
 * Shrinks the rectangle if it exceeds the limit's dimensions and
 * repositions it to stay within the limit boundary.
 *
 * @param limit The bounding rectangle
 * @param rect  The rectangle to clamp (modified in place)
 */
void ClampToRect(RECT limit, RECT *rect);

/**
 * @brief Centre one rectangle inside another.
 *
 * Returns a new rectangle the size of @p inner positioned at the
 * centre of @p outer.
 *
 * @param outer The outer bounding rectangle
 * @param inner The rectangle to centre (its dimensions are preserved)
 * @return A new rectangle centred within @p outer
 */
auto CenterRectInRect(RECT outer, RECT inner) -> RECT;

/**
 * @brief Test whether a point lies inside a rectangle.
 *
 * @param intersect_point The point to test
 * @param rect            The rectangle
 * @return TRUE if the point is inside (inclusive of edges), FALSE otherwise
 */
auto PointIntersectsRect(POINT intersect_point, RECT rect) -> BOOL;

/**
 * @brief Check if two rects are directional neighbors and get the distance.
 *
 * Determines whether @p cand is a valid neighbor of @p src in the given
 * direction. "Neighbor" means the candidate is positioned entirely on the
 * specified side of the source (no overlap) with overlapping perpendicular
 * span. @p out_dist receives the pixel gap between them.
 *
 * @param src      The source rectangle
 * @param cand     The candidate neighbour rectangle
 * @param dir      The direction to check (DirLeft/DirRight/DirUp/DirDown)
 * @param out_dist Receives the distance between the two rects in pixels
 * @return TRUE if cand is a valid neighbour in the given direction
 */
auto RectIsNeighbor(RECT src, RECT cand, BFWMDirection dir, int *out_dist)
    -> BOOL;

#endif
