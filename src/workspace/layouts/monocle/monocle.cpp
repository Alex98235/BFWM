#include "monocle.h"
#include "../layout_base.h"
#include <windef.h>

// ---------------------------------------------------------------------------
// MonocleEngine
// ---------------------------------------------------------------------------

auto MonocleEngine::get_rect(HWND hwnd, RECT *out_rect) -> bool {
   (void)hwnd;
   if (out_rect == nullptr)
      return false;
   *out_rect = tiled_rect;
   return true;
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

auto MonocleLayoutEngineCreate(RECT workspace_rect, int gap_between,
                               int gap_edge, int border_width)
    -> std::unique_ptr<LayoutEngine> {
   auto engine = std::make_unique<MonocleEngine>();
   LayoutConfig const cfg = {.gap_between = gap_between,
                             .gap_edge = gap_edge,
                             .border_width = border_width,
                             .workspace_rect = workspace_rect};
   engine->apply_config(cfg);
   return engine;
}