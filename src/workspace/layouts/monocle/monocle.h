/**
 * @file monocle.h
 * @brief Monocle layout engine.
 *
 * A full-screen stacking layout where only one window is visible at a
 * time. Windows are stacked in insertion order and the user cycles
 * through them.
 *
 * Inherits all window-list operations from SequenceLayout.
 * Only implements: get_rect (all windows share tiled_rect) and Recalculate.
 */

#ifndef BFWM_MONOCLE_H
#define BFWM_MONOCLE_H

#include "../layout_base.h"
#include <memory>
#include <windef.h>

/**
 * @brief Monocle layout engine.
 */
class MonocleEngine : public SequenceLayout {
 public:
   MonocleEngine() = default;

   // -- LayoutEngine interface ----------------------------------------------
   [[nodiscard]] auto type() const -> LayoutType override { return MONOCLE; }
   void apply() override { Recalculate(); }
   auto get_rect(HWND hwnd, RECT *out_rect) -> bool override;

 protected:
   void Recalculate() override { ApplyBorderInset(); }
};

/// Factory: create a MonocleEngine, apply workspace/gaps, return as Engine.
auto MonocleLayoutEngineCreate(RECT workspace_rect, int gap_between,
                               int gap_edge, int border_width)
    -> std::unique_ptr<LayoutEngine>;

#endif /* BFWM_MONOCLE_H */
