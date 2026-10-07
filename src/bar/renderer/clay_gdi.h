/**
 * @file clay_gdi.h
 * @brief GDI renderer backend for the Clay UI library.
 *
 * Provides text measurement and rendering using GDI for the bar's
 * Clay-based layout system.
 */

#ifndef BFWM_CLAY_GDI_RENDERER_H
#define BFWM_CLAY_GDI_RENDERER_H

#include "../../config/lua/parser.h"
#include "../../win/gdi_raii.h"
#include "clay.h"
#include <string>
#include <windows.h>

/**
 * @brief Configuration for the GDI-based Clay renderer.
 */
using ClayGdiRendererConfig = struct ClayGdiRendererConfig {
   /// Font face name (narrow)
   std::string font_name;
   /// Font face name (wide)
   std::wstring font_name_w;
   /// Font size in points
   int font_size = 0;
   /// Font weight (e.g. FW_NORMAL)
   int font_weight = 0;
   /// Default text colour
   COLORREF text_color = 0;
   /// Cached memory DC used for text measurement (RAII-owned)
   DcGuard mem_dc;
};

/**
 * @brief Initialise the Clay GDI renderer.
 *
 * Creates GDI font objects matching the bar configuration and a cached
 * memory DC used for text measurement.
 *
 * @param cfg      Renderer configuration (fonts, colours)
 * @param bar_cfg  Bar layout configuration
 */
void ClayGdiRendererInit(ClayGdiRendererConfig *cfg, BarConfig *bar_cfg);

/**
 * @brief Measure text dimensions for a Clay layout element.
 *
 * Callback used by Clay during layout computation.
 *
 * @param text     The text slice to measure
 * @param config   Text element configuration (font, size, etc.)
 * @param userData User data (unused)
 * @return The measured dimensions of the text
 */
auto ClayGdiMeasureText(Clay_StringSlice text, Clay_TextElementConfig *config,
                        void *userData) -> Clay_Dimensions;

/**
 * @brief Render a Clay command array using GDI.
 *
 * Iterates over the render commands and draws rectangles, text, etc.
 * onto the given HDC.
 *
 * @param hdc      Device context to draw onto
 * @param commands The render command array from Clay
 * @param cfg      Renderer configuration
 */
void ClayGdiRender(HDC hdc, Clay_RenderCommandArray *commands,
                   ClayGdiRendererConfig *cfg);

#endif
