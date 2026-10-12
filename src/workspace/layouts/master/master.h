/**
 * @file master.h
 * @brief Master/Stack layout engine.
 *
 * A master/stack tiling layout where the left region (the "master") holds
 * a configurable number of windows stacked vertically, and remaining
 * windows stack vertically on the right.
 *
 * Inherits all window-list operations from SequenceLayout.
 * Only implements: get_rect (master/stack split math) and Recalculate.
 */

#ifndef BFWM_MASTER_H
#define BFWM_MASTER_H

#include "../layout_base.h"
#include <memory>
#include <windef.h>

static const double BASE_MASTER_FACTOR = 0.5;

/**
 * @brief Master/Stack layout engine.
 */
class MasterEngine : public SequenceLayout {
 public:
   MasterEngine() = default;

   // -- LayoutEngine interface ----------------------------------------------
   [[nodiscard]] auto type() const -> LayoutType override { return MASTER; }
   void apply() override {}
   auto get_rect(HWND hwnd, RECT *out_rect) -> bool override;
   auto get_neighbor(HWND hwnd, BFWMDirection direction) -> HWND override;

 protected:
   void Recalculate() override { ApplyBorderInset(); }

   // -- Master-specific -----------------------------------------------------
   int master_count = 1;
   double master_factor = BASE_MASTER_FACTOR;
};

/// Factory: create a MasterEngine, apply workspace/gaps, return as Engine.
auto MasterLayoutEngineCreate(RECT workspace_rect, int gap_between,
                              int gap_edge, int border_width)
    -> std::unique_ptr<LayoutEngine>;

#endif /* BFWM_MASTER_H */
