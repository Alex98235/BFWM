#include "clay_gdi.h"
#include "../../config/lua/parser.h"
#include "../../win/gdi_raii.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <minwindef.h>
#include <string>
#include <stringapiset.h>
#include <windef.h>
#include <windows.h>
#include <wingdi.h>
#include <winnls.h>
#include <winnt.h>

enum { FONT_NAME_BUF_SIZE = 32 };

namespace {

inline auto TryFontName(const wchar_t *name, int size, int weight) -> BOOL {
   DcGuard const hdc(CreateCompatibleDC(nullptr));
   if (hdc.get() == nullptr)
      return FALSE;
   GdiObject const font(CreateFontW(size, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                                    ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                    DEFAULT_PITCH, name));
   SelectObjectGuard const font_guard(hdc.get(), font.get());
   std::wstring actual;
   actual.resize(FONT_NAME_BUF_SIZE);
   int const n = GetTextFaceW(hdc.get(), FONT_NAME_BUF_SIZE, actual.data());
   actual.resize(n > 0 ? static_cast<size_t>(n) : 0);
   return static_cast<BOOL>(name == actual);
}

} // namespace

void ClayGdiRendererInit(ClayGdiRendererConfig *cfg, BarConfig *bar_cfg) {
   cfg->font_name = bar_cfg->font.name;
   cfg->font_size = bar_cfg->font.size;
   cfg->font_weight = bar_cfg->font.weight;
   cfg->text_color = bar_cfg->colors.text;
   cfg->mem_dc = DcGuard(CreateCompatibleDC(nullptr));

   std::wstring wname;
   wname.resize(FONT_NAME_BUF_SIZE);
   int const wide_len =
       MultiByteToWideChar(CP_UTF8, 0, cfg->font_name.c_str(), -1, wname.data(),
                           FONT_NAME_BUF_SIZE);
   wname.resize(wide_len > 1 ? static_cast<size_t>(wide_len) - 1 : 0);

   if (TryFontName(wname.c_str(), cfg->font_size, cfg->font_weight) != 0) {
      cfg->font_name_w = wname;
      return;
   }

   // Font not found with config name. Try Nerd Font alternate names:
   // "JetBrainsMono Nerd Font" -> "JetBrainsMono NF", "JetBrainsMono NFM", etc.
   const wchar_t *pos = wcsstr(wname.c_str(), L" Nerd Font");
   if (pos != nullptr) {
      size_t const base_len = (pos - wname.c_str());
      std::wstring const alt(wname, 0, base_len);
      const std::array<std::wstring, 3> suffixes = {L" NF", L" NFM", L" NFP"};
      for (const auto &suffix : suffixes) {
         std::wstring candidate = alt;
         candidate += suffix;
         if (TryFontName(candidate.c_str(), cfg->font_size, cfg->font_weight) !=
             0) {
            cfg->font_name_w = candidate;
            return;
         }
      }
   }

   // Last resort: use the configured name anyway
   cfg->font_name_w = wname;
}

enum { MAX_TEXT_UTF16 = 512 };

auto ClayGdiMeasureText(Clay_StringSlice text, Clay_TextElementConfig *config,
                        void *userData) -> Clay_Dimensions {
   (void)config;
   auto *cfg = static_cast<ClayGdiRendererConfig *>(userData);

   int const req_len =
       MultiByteToWideChar(CP_UTF8, 0, text.chars, text.length, nullptr, 0);
   Clay_Dimensions dimensions = {
       .width = 0,
       .height = static_cast<float>(cfg->font_size),
   };
   if (req_len <= 0) {
      return dimensions;
   }
   int const wlen = std::min(req_len, MAX_TEXT_UTF16 - 1);
   std::wstring wbuf;
   wbuf.resize(static_cast<size_t>(wlen));
   MultiByteToWideChar(CP_UTF8, 0, text.chars, text.length, wbuf.data(), wlen);

   uint16_t const font_size = config->fontSize > 0
                                  ? config->fontSize
                                  : static_cast<uint16_t>(cfg->font_size);
   HDC hdc = cfg->mem_dc.get();
   GdiObject const font(
       CreateFontW(font_size, 0, 0, 0, cfg->font_weight, FALSE, FALSE, FALSE,
                   ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                   DEFAULT_QUALITY, DEFAULT_PITCH, cfg->font_name_w.c_str()));
   SelectObjectGuard const font_guard(hdc, font.get());
   SIZE size;
   GetTextExtentPoint32W(hdc, wbuf.data(), wlen, &size);
   // Single glyphs (icon characters) may have negative right side-bearing
   // (C in ABC widths), making the advance width (A+B+C) narrower than the
   // painted width (A+B). Compensate so containers size to the painted width.
   if (wlen == 1) {
      ABC abc;
      if ((GetCharABCWidthsW(hdc, wbuf[0], wbuf[0], &abc) != 0) && abc.abcC < 0)
         size.cx -= abc.abcC;
   }

   dimensions = {
       .width = static_cast<float>(size.cx),
       .height = static_cast<float>(size.cy),
   };
   return dimensions;
}

void ClayGdiRender(HDC hdc, Clay_RenderCommandArray *commands,
                   ClayGdiRendererConfig *cfg) {
   for (int32_t i = 0; i < commands->length; i++) {
      Clay_RenderCommand *cmd = &commands->internalArray[i];
      RECT r = {
          .left = static_cast<LONG>(cmd->boundingBox.x),
          .top = static_cast<LONG>(cmd->boundingBox.y),
          .right =
              static_cast<LONG>(cmd->boundingBox.x + cmd->boundingBox.width),
          .bottom =
              static_cast<LONG>(cmd->boundingBox.y + cmd->boundingBox.height),
      };

      switch (cmd->commandType) {
      case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
         Clay_RectangleRenderData *render_data = &cmd->renderData.rectangle;
         COLORREF const color = RGB((BYTE)render_data->backgroundColor.r,
                                    (BYTE)render_data->backgroundColor.g,
                                    (BYTE)render_data->backgroundColor.b);
         GdiObject const brush(CreateSolidBrush(color));
         FillRect(hdc, &r, static_cast<HBRUSH>(brush.get()));
         break;
      }
      case CLAY_RENDER_COMMAND_TYPE_BORDER: {
         Clay_BorderRenderData *border_data = &cmd->renderData.border;
         COLORREF const border_color =
             RGB((BYTE)border_data->color.r, (BYTE)border_data->color.g,
                 (BYTE)border_data->color.b);
         GdiObject const bg_brush(CreateSolidBrush(border_color));
         SelectObjectGuard const brush_guard(hdc, bg_brush.get());
         SelectObjectGuard const pen_guard(hdc, GetStockObject(NULL_PEN));
         // Draw top border
         if (border_data->width.top > 0) {
            RECT const top_rect = {
                .left = r.left,
                .top = r.top,
                .right = r.right,
                .bottom = r.top + static_cast<LONG>(border_data->width.top),
            };
            FillRect(hdc, &top_rect, static_cast<HBRUSH>(bg_brush.get()));
         }
         // Draw bottom border
         if (border_data->width.bottom > 0) {
            RECT const bottom_rect = {
                .left = r.left,
                .top = r.bottom - static_cast<LONG>(border_data->width.bottom),
                .right = r.right,
                .bottom = r.bottom,
            };
            FillRect(hdc, &bottom_rect, static_cast<HBRUSH>(bg_brush.get()));
         }
         // Draw left border
         if (border_data->width.left > 0) {
            RECT const left_rect = {
                .left = r.left,
                .top = r.top,
                .right = r.left + static_cast<LONG>(border_data->width.left),
                .bottom = r.bottom,
            };
            FillRect(hdc, &left_rect, static_cast<HBRUSH>(bg_brush.get()));
         }
         // Draw right border
         if (border_data->width.right > 0) {
            RECT const right_rect = {
                .left = r.right - static_cast<LONG>(border_data->width.right),
                .top = r.top,
                .right = r.right,
                .bottom = r.bottom,
            };
            FillRect(hdc, &right_rect, static_cast<HBRUSH>(bg_brush.get()));
         }
         break;
      }
      case CLAY_RENDER_COMMAND_TYPE_TEXT: {
         Clay_TextRenderData *text_data = &cmd->renderData.text;
         int const req_len =
             MultiByteToWideChar(CP_UTF8, 0, text_data->stringContents.chars,
                                 text_data->stringContents.length, nullptr, 0);
         if (req_len > 0) {
            int const wlen = std::min(req_len, MAX_TEXT_UTF16 - 1);
            std::wstring wbuf;
            wbuf.resize(static_cast<size_t>(wlen));
            MultiByteToWideChar(CP_UTF8, 0, text_data->stringContents.chars,
                                text_data->stringContents.length, wbuf.data(),
                                wlen);
            uint16_t const font_size =
                text_data->fontSize > 0 ? text_data->fontSize
                                        : static_cast<uint16_t>(cfg->font_size);
            GdiObject const font(CreateFontW(
                font_size, 0, 0, 0, cfg->font_weight, FALSE, FALSE, FALSE,
                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, DEFAULT_PITCH, cfg->font_name_w.c_str()));
            SelectObjectGuard const font_guard(hdc, font.get());
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB((BYTE)text_data->textColor.r,
                                  (BYTE)text_data->textColor.g,
                                  (BYTE)text_data->textColor.b));
            DrawTextW(hdc, wbuf.data(), wlen, &r,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_EDITCONTROL |
                          DT_NOCLIP | DT_NOPREFIX);
         }
         break;
      }
      case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
         SaveDC(hdc);
         IntersectClipRect(hdc, r.left, r.top, r.right, r.bottom);
         break;
      }
      case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
         RestoreDC(hdc, -1);
         break;
      }
      default:
         break;
      }
   }
}
