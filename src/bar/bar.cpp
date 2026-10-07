#include "bar.h"
#include "../config/defaults.h"
#include "../config/lua/parser.h"
#include "../core/bfwm_context.h"
#include "../dpi/dpi.h"
#include "../logging/logger.h"
#include "../monitor/monitor.h"
#include "../notification/snackbar.h"
#include "../system/system_status.h"
#include "../win/gdi_raii.h"
#include "../win/win_utils.h"
#include "../window/window.h"
#include "../workspace/workspace.h"
#include "renderer/clay_gdi.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <dwmapi.h>
#include <fileapi.h>
#include <handleapi.h>
#include <libloaderapi.h>
#include <minwindef.h>
#include <processenv.h>
#include <stringapiset.h>
#include <sysinfoapi.h>
#include <utility>
#include <windef.h>
#include <windows.h>
#include <windowsx.h>
#include <wingdi.h>
#include <winnls.h>
#include <winnt.h>

enum {
   BAR_TIMER_ID = 1,
   CLOCK_MIN_WIDTH = 60,
   COLOR_ALPHA = 255,
   WS_TAB_PADDING = 14,
   WS_TAB_MIN_WIDTH = 24,
   TITLE_BUF_SIZE = 256,
   MEASURE_BUF_SIZE = 256,
   DECIMAL_BASE = 10,
   TM_YEAR_BASE = 1900,
   LABEL_KEYWORD_LEN = 7,
   ICON_KEYWORD_LEN = 6,
   SSID_KEYWORD_LEN = 6,
   LOAD_KEYWORD_LEN = 5,
   PERCENT_KEYWORD_LEN = 8,
   LEVEL_KEYWORD_LEN = 6,
   TOTAL_GB_KEYWORD_LEN = 9,
   USED_GB_KEYWORD_LEN = 8,
   AVAIL_GB_KEYWORD_LEN = 9,
   NUM_BUF_SIZE = 32,
   FMTED_BUF_SIZE = 32,
   DEFAULT_POLL_RATE_MS = 500,
   TITLE_MAX_WIDTH = 400,
   TIMER_MS_DEFAULT = 1000,
   HALF_DIV = 2,
};

#define ROUNDING_HALF 0.5F
#define BYTES_PER_GB (1024.0F * 1024.0F * 1024.0F)

Bar::Bar(class Monitor *mon, const struct BarConfig *cfg,
         struct BFWMContext *ctx)
    : ctx(ctx), mon(mon), init_cfg(cfg) {}

Bar::~Bar() {
   if (hwnd != nullptr)
      DestroyWindow(hwnd);
   free(clay_arena);
}

namespace {
const wchar_t *BAR_CLASS = L"BFWMBar";

/**
 * @brief Restores the Clay global context on destruction.
 *
 * Saves the current context on construction and restores it when the
 * guard goes out of scope, so nested Clay users don't clobber each
 * other's global context.
 */
class ClayContextGuard {
 public:
   ClayContextGuard() : saved_(Clay_GetCurrentContext()) {}
   ~ClayContextGuard() { Clay_SetCurrentContext(saved_); }
   ClayContextGuard(const ClayContextGuard &) = delete;
   auto operator=(const ClayContextGuard &) -> ClayContextGuard & = delete;

 private:
   Clay_Context *saved_;
};

inline auto colorref_to_clay_color(COLORREF color_ref) -> Clay_Color {
   Clay_Color color = {.r = (float)GetRValue(color_ref),
                       .g = (float)GetGValue(color_ref),
                       .b = (float)GetBValue(color_ref),
                       .a = COLOR_ALPHA};
   return color;
}

inline void HandleClayError(Clay_ErrorData error) {
   Debug("Clay: %.*s", error.errorText.length, error.errorText.chars);
}

inline void substitute_ws_label(const char *fmt, size_t identifier,
                                const std::string &label, std::string &out) {
   const char *p = fmt;
   out.clear();
   while (*p != 0) {
      if (strncmp(p, "{id}", 4) == 0) {
         out += std::to_string(identifier);
         p += 4;
      } else if (strncmp(p, "{label}", LABEL_KEYWORD_LEN) == 0) {
         if (!label.empty())
            out += label;
         p += LABEL_KEYWORD_LEN;
      } else {
         out += *p++;
      }
   }
}

inline void SortWorkspaceOrder(Monitor *mon, int *order, size_t count) {
   for (size_t j = 0; j < count; j++)
      order[j] = (int)j;
   for (size_t j = 1; j < count; j++) {
      int const key = order[j];
      int k = (int)j - 1;
      while (k >= 0 && mon->Workspaces()[order[k]]->GetIdentifier() >
                           mon->Workspaces()[key]->GetIdentifier()) {
         order[k + 1] = order[k];
         k--;
      }
      order[k + 1] = key;
   }
}

inline void CountManagedWindows(Workspace *workspace, HWND focused_hwnd,
                                BarIndicatorConfig *indicator_config,
                                size_t *managed_count, size_t *focused_index,
                                BOOL *show_bar) {
   *managed_count = 0;
   *focused_index = 0;
   *show_bar = FALSE;
   if (!indicator_config->show_position_bar)
      return;
   for (auto *w : workspace->Windows()) {
      if ((w != nullptr) && (IsWindowManagedByLayout(w) != 0)) {
         if (w->GetHwnd() == focused_hwnd)
            *focused_index = *managed_count;
         (*managed_count)++;
      }
   }
   *show_bar = static_cast<BOOL>(*managed_count > 1);
}

inline void RenderPositionBar(int tab_idx, float tab_width, float managed_count,
                              float focused_index, Clay_Color active_clay,
                              Clay_Color inactive_clay) {
   float hl_w = tab_width / managed_count;
   hl_w = std::max(hl_w, 1.0F);
   float hl_off = tab_width * focused_index / managed_count;

   CLAY(CLAY_IDI("ws_bar", (uint32_t)tab_idx),
        {{{CLAY_SIZING_FIXED(tab_width), CLAY_SIZING_FIXED(3)},
          {},
          {},
          {},
          CLAY_LEFT_TO_RIGHT},
         inactive_clay,
         {},
         {},
         {},
         {},
         {},
         {},
         {},
         {},
         {},
         {}}) {
      CLAY(CLAY_IDI("ws_spc", (uint32_t)tab_idx),
           {{{CLAY_SIZING_FIXED(hl_off), CLAY_SIZING_FIXED(3)}, {}, {}, {}, {}},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {}}) {}
      CLAY(CLAY_IDI("ws_hl", (uint32_t)tab_idx),
           {{{CLAY_SIZING_FIXED(hl_w), CLAY_SIZING_FIXED(3)}, {}, {}, {}, {}},
            active_clay,
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {}}) {}
   }
}

/* Given a pointer just past the keyword inside `{keyword...}`, parse
 * an optional `:.Nf` suffix and return the number of decimal places.
 * Advances *end to just past the parsed suffix (before the closing `}`). */
inline auto parse_precision(const char **end) -> int {
   int decimals = 0;
   if (**end == ':') {
      (*end)++;
      if (**end == '.') {
         (*end)++;
         while (**end >= '0' && **end <= '9') {
            decimals = (decimals * DECIMAL_BASE) + (**end - '0');
            (*end)++;
         }
         if (**end == 'f')
            (*end)++;
      }
   }
   return decimals;
}

inline auto match_keyword(const char *p, float load, float mem_total_gb,
                          float mem_used_gb, float mem_avail_gb)
    -> std::pair<const char *, double> {
   if (strncmp(p, "{load", LOAD_KEYWORD_LEN) == 0) {
      return {p + LOAD_KEYWORD_LEN, load};
   }
   if (strncmp(p, "{total_gb", TOTAL_GB_KEYWORD_LEN) == 0) {
      return {p + TOTAL_GB_KEYWORD_LEN, mem_total_gb};
   }
   if (strncmp(p, "{used_gb", USED_GB_KEYWORD_LEN) == 0) {
      return {p + USED_GB_KEYWORD_LEN, mem_used_gb};
   }
   if (strncmp(p, "{avail_gb", AVAIL_GB_KEYWORD_LEN) == 0) {
      return {p + AVAIL_GB_KEYWORD_LEN, mem_avail_gb};
   }
   if (strncmp(p, "{percent", PERCENT_KEYWORD_LEN) == 0) {
      return {p + PERCENT_KEYWORD_LEN, load};
   }
   if (strncmp(p, "{level", LEVEL_KEYWORD_LEN) == 0) {
      return {p + LEVEL_KEYWORD_LEN, load / 100.0};
   }
   return {nullptr, 0};
}

inline void substitute_format(const char *fmt, const char *icon,
                              const char *ssid, float load, float mem_total_gb,
                              float mem_used_gb, float mem_avail_gb,
                              std::string &out) {
   const char *p = fmt;
   out.clear();
   while (*p != 0) {
      if (strncmp(p, "{icon}", ICON_KEYWORD_LEN) == 0) {
         if (icon != nullptr)
            out += icon;
         p += ICON_KEYWORD_LEN;
      } else if ((ssid != nullptr) &&
                 strncmp(p, "{ssid}", SSID_KEYWORD_LEN) == 0) {
         out += ssid;
         p += SSID_KEYWORD_LEN;
      } else {
         auto [keyword, val] =
             match_keyword(p, load, mem_total_gb, mem_used_gb, mem_avail_gb);

         if (keyword != nullptr) {
            const char *end = keyword;
            int const decimals = parse_precision(&end);
            if (*end == '}') {
               std::string num;
               num.resize(NUM_BUF_SIZE);
               int const fmt_len =
                   snprintf(num.data(), num.size(), "%%.%df", decimals);
               num.resize(fmt_len > 0
                              ? std::min<size_t>((size_t)fmt_len, num.size())
                              : 0);
               std::string fmted;
               fmted.resize(FMTED_BUF_SIZE);
               int const fmted_len =
                   snprintf(fmted.data(), fmted.size(), num.c_str(), val);
               fmted.resize(fmted_len > 0 ? std::min<size_t>((size_t)fmted_len,
                                                             fmted.size())
                                          : 0);
               out += fmted;
               p = end + 1;
            } else {
               out += *p++;
            }
         } else {
            out += *p++;
         }
      }
   }
}

inline void LoadPerUserFonts() {
   std::wstring font_dir;
   font_dir.resize(MAX_PATH);
   DWORD const len =
       GetEnvironmentVariableW(L"LOCALAPPDATA", font_dir.data(), MAX_PATH);
   if (len == 0 || len >= MAX_PATH)
      return;
   font_dir.resize(len);

   std::wstring search_path;
   search_path.resize(MAX_PATH);
   int const search_len =
       swprintf(search_path.data(), MAX_PATH,
                L"%ls\\Microsoft\\Windows\\Fonts\\*.ttf", font_dir.c_str());
   search_path.resize(
       search_len > 0 ? std::min<size_t>((size_t)search_len, MAX_PATH - 1) : 0);
   WIN32_FIND_DATAW ffd;
   HANDLE hFind = FindFirstFileW(search_path.c_str(), &ffd);
   if (hFind == INVALID_HANDLE_VALUE)
      return;

   do {
      std::wstring full_path;
      full_path.resize(MAX_PATH);
      int const full_len = swprintf(full_path.data(), MAX_PATH,
                                    L"%ls\\Microsoft\\Windows\\Fonts\\%ls",
                                    font_dir.c_str(), ffd.cFileName);
      full_path.resize(
          full_len > 0 ? std::min<size_t>((size_t)full_len, MAX_PATH - 1) : 0);
      AddFontResourceExW(full_path.c_str(), FR_PRIVATE, nullptr);
   } while (FindNextFileW(hFind, &ffd) != 0);
   FindClose(hFind);

   search_path.resize(MAX_PATH);
   int const search_len_otf =
       swprintf(search_path.data(), MAX_PATH,
                L"%ls\\Microsoft\\Windows\\Fonts\\*.otf", font_dir.c_str());
   search_path.resize(
       search_len_otf > 0
           ? std::min<size_t>((size_t)search_len_otf, MAX_PATH - 1)
           : 0);
   hFind = FindFirstFileW(search_path.c_str(), &ffd);
   if (hFind == INVALID_HANDLE_VALUE)
      return;

   do {
      std::wstring full_path;
      full_path.resize(MAX_PATH);
      int const full_len = swprintf(full_path.data(), MAX_PATH,
                                    L"%ls\\Microsoft\\Windows\\Fonts\\%ls",
                                    font_dir.c_str(), ffd.cFileName);
      full_path.resize(
          full_len > 0 ? std::min<size_t>((size_t)full_len, MAX_PATH - 1) : 0);
      AddFontResourceExW(full_path.c_str(), FR_PRIVATE, nullptr);
   } while (FindNextFileW(hFind, &ffd) != 0);
   FindClose(hFind);
}

auto CALLBACK BarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    -> LRESULT {
   Bar *bar = (Bar *)BFWMGetWindowData(hwnd);

   switch (msg) {
   default:
      break;
   case WM_CREATE: {
      auto *create_struct = (CREATESTRUCTW *)lParam;
      bar = (Bar *)create_struct->lpCreateParams;
      return bar->OnCreate(hwnd, lParam);
   }
   case WM_TIMER:
      return bar->OnTimer();
   case WM_PAINT:
      if (bar != nullptr)
         return bar->OnPaint();
      {
         PAINTSTRUCT paint_struct;
         BeginPaint(hwnd, &paint_struct);
         EndPaint(hwnd, &paint_struct);
         return DefWindowProcW(hwnd, msg, wParam, lParam);
      }
   case WM_LBUTTONDOWN:
      if ((bar != nullptr) && (bar->monitor() != nullptr) &&
          (bar->context()->suspended == 0))
         return bar->OnLButtonDown(lParam);
      break;
   case WM_MOUSEWHEEL:
      if ((bar != nullptr) && (bar->monitor() != nullptr) &&
          bar->monitor()->Workspaces().size() > 1 &&
          (bar->context()->suspended == 0))
         return bar->OnMouseWheel(wParam, lParam);
      break;
   case WM_DPICHANGED:
      // Per-monitor DPI changed for this bar's window: register the new DPI
      // and raise the composition-root flag. The main-loop dpi_update block
      // owns the RECREATE reaction (single owner), so we only set the flag
      // here — the worker-thread watcher path (WM_APP_DPI_CHANGED →
      // dpi_update → RECREATE) consumes the same flag, and RegisterMonitor is
      // an idempotent upsert, so double-firing is safe.
      if ((bar != nullptr) && (bar->monitor() != nullptr) &&
          (bar->context() != nullptr)) {
         bar->context()->dpi->RegisterMonitor(bar->monitor()->GetHandle(),
                                              LOWORD(wParam));
         bar->context()->dpi_update = TRUE;
      }
      return 0;
   case WM_DESTROY:
      bar->OnDestroy();
      return 0;
   }
   return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

auto Bar::OnCreate(HWND hwnd, LPARAM l_param) -> LRESULT {
   this->hwnd = hwnd;
   auto *create_struct = (CREATESTRUCTW *)l_param;
   BFWMSetWindowData(hwnd, create_struct->lpCreateParams);
   UINT timer_ms = TIMER_MS_DEFAULT;
   for (int i = 0; i < config.indicator_count; i++) {
      int const r = config.indicators[i].poll_rate_ms;
      if (r > 0 && std::cmp_less(r, timer_ms))
         timer_ms = (UINT)r;
   }
   SetTimer(hwnd, BAR_TIMER_ID, timer_ms, nullptr);
   return 0;
}

auto Bar::OnTimer() -> LRESULT {
   if ((ctx != nullptr) && (ctx->suspended == 0))
      InvalidateRect(hwnd, nullptr, FALSE);
   return 0;
}

auto Bar::OnPaint() -> LRESULT {
   if ((in_paint != 0) || (in_layout != 0)) {
      PAINTSTRUCT paint_struct;
      BeginPaint(hwnd, &paint_struct);
      EndPaint(hwnd, &paint_struct);
      return 0;
   }
   PAINTSTRUCT paint_struct;
   HDC hdc = BeginPaint(hwnd, &paint_struct);
   in_paint = TRUE;
   ClayContextGuard const clay_guard;
   Clay_SetCurrentContext(clay_context);
   RECT client;
   GetClientRect(hwnd, &client);
   int const bar_w = client.right - client.left;
   int const bar_h = client.bottom - client.top;

   // Double-buffer: render to memory DC then BitBlt to screen.
   DcGuard const mem_dc(CreateCompatibleDC(hdc));
   GdiObject const mem_bmp(CreateCompatibleBitmap(hdc, bar_w, bar_h));
   SelectObjectGuard const bmp_guard(mem_dc.get(), mem_bmp.get());

   Clay_Dimensions const dimensions = {.width = (float)bar_w,
                                       .height = (float)bar_h};
   Clay_SetLayoutDimensions(dimensions);
   Clay_Vector2 const pointer = {.x = 0, .y = 0};
   Clay_SetPointerState(pointer, FALSE);
   Clay_BeginLayout();
   BuildLayout(bar_w);

   Clay_RenderCommandArray cmds = Clay_EndLayout(0);
   if (cmds.length > 0)
      ClayGdiRender(mem_dc.get(), &cmds, &clay_cfg);

   if (config.border.width > 0) {
      GdiObject const border_pen(
          CreatePen(PS_INSIDEFRAME, config.border.width, config.border.color));
      SelectObjectGuard const pen_guard(mem_dc.get(), border_pen.get());
      SelectObject(mem_dc.get(), GetStockObject(NULL_BRUSH));
      RECT const border_rect = {
          .left = 0, .top = 0, .right = bar_w, .bottom = bar_h};
      if (config.corner_radius > 0) {
         RoundRect(mem_dc.get(), border_rect.left, border_rect.top,
                   border_rect.right, border_rect.bottom,
                   config.corner_radius * 2, config.corner_radius * 2);
      } else {
         Rectangle(mem_dc.get(), border_rect.left, border_rect.top,
                   border_rect.right, border_rect.bottom);
      }
   }

   // Flip the off-screen buffer to the display.
   BitBlt(hdc, 0, 0, bar_w, bar_h, mem_dc.get(), 0, 0, SRCCOPY);

   in_paint = FALSE;
   EndPaint(hwnd, &paint_struct);
   return 0;
}

/*
 * Hit-test the workspace tabs exactly as they are rendered. The layout pass
 * assigns each indicator an instance id from a shared per-type counter in
 * render order (left group, then center, then right group), and the workspace
 * tab Clay element ids are derived from that instance id. Mirror the same
 * order here so the element ids used for the hit test match the ones the
 * layout actually produced. Like the render path, the tabs are laid out in
 * ascending identifier order (SortWorkspaceOrder); the sorted mapping
 * order[w] is used both to construct the Clay element id and to return the
 * raw workspace array index, so the hit test decodes the same mapping the
 * renderer encoded. Returns the workspace array index under the pointer, or
 * -1 if the pointer is not over any workspace tab.
 */
auto Bar::FindWorkspaceTabUnderPointer(const Clay_String &text) -> int {
   std::array<int, BAR_INDICATOR_COUNT> type_counter = {};
   constexpr std::array<BarIndicatorAlign, 3> align_order = {
       BAR_ALIGN_LEFT, BAR_ALIGN_CENTER, BAR_ALIGN_RIGHT};
   for (auto a : align_order) {
      for (int i = 0; i < config.indicator_count; i++) {
         BarIndicatorConfig *indicator_config = &config.indicators[i];
         if (indicator_config->align != a)
            continue;
         int const instance_id = type_counter[indicator_config->type]++;
         if (indicator_config->type != BAR_INDICATOR_WORKSPACES)
            continue;
         std::array<int, BAR_MAX_WS_LABELS> order = {};
         SortWorkspaceOrder(mon, order.data(), mon->Workspaces().size());
         auto base = (uint32_t)(instance_id * BAR_MAX_WS_LABELS);
         for (size_t w = 0; w < mon->Workspaces().size(); w++) {
            int const tab_idx = order[w];
            Clay_ElementId const eid =
                Clay_GetElementIdWithIndex(text, base + (uint32_t)tab_idx);
            if (Clay_PointerOver(eid))
               return tab_idx;
         }
      }
   }
   return -1;
}

auto Bar::OnLButtonDown(LPARAM l_param) -> LRESULT {
   if (in_layout != 0)
      return 0;
   in_layout = TRUE;
   ClayContextGuard const clay_guard;
   Clay_SetCurrentContext(clay_context);
   POINT const point = {.x = GET_X_LPARAM(l_param), .y = GET_Y_LPARAM(l_param)};

   RECT client;
   GetClientRect(hwnd, &client);
   int const bar_w = client.right - client.left;
   int const bar_h = client.bottom - client.top;
   Clay_Dimensions const dimensions = {.width = (float)bar_w,
                                       .height = (float)bar_h};
   Clay_SetLayoutDimensions(dimensions);
   Clay_Vector2 const pointer = {.x = (float)point.x, .y = (float)point.y};
   Clay_SetPointerState(pointer, TRUE);
   Clay_BeginLayout();
   BuildLayout(bar_w);
   Clay_EndLayout(0);

   Clay_String const ws_prefix = {
       .isStaticallyAllocated = true, .length = 2, .chars = "ws"};
   int const ws_index = FindWorkspaceTabUnderPointer(ws_prefix);
   if (ws_index >= 0) {
      WorkspaceActivate(ctx, mon, mon->Workspaces()[ws_index]->GetIdentifier());
   }
   in_layout = FALSE;
   return 0;
}

auto Bar::OnMouseWheel(WPARAM w_param, LPARAM l_param) -> LRESULT {
   if (in_layout != 0)
      return 0;
   in_layout = TRUE;
   ClayContextGuard const clay_guard;
   Clay_SetCurrentContext(clay_context);
   POINT point = {.x = GET_X_LPARAM(l_param), .y = GET_Y_LPARAM(l_param)};
   ScreenToClient(hwnd, &point);

   RECT client;
   GetClientRect(hwnd, &client);
   int const bar_w = client.right - client.left;
   int const bar_h = client.bottom - client.top;
   Clay_Dimensions const dimensions = {.width = (float)bar_w,
                                       .height = (float)bar_h};
   Clay_SetLayoutDimensions(dimensions);
   Clay_Vector2 const pointer = {.x = (float)point.x, .y = (float)point.y};
   Clay_SetPointerState(pointer, TRUE);
   Clay_BeginLayout();
   BuildLayout(bar_w);
   Clay_EndLayout(0);

   Clay_String const ws_prefix = {
       .isStaticallyAllocated = true, .length = 2, .chars = "ws"};
   BOOL const over_workspaces =
       static_cast<BOOL>(FindWorkspaceTabUnderPointer(ws_prefix) >= 0);

   if (over_workspaces != 0) {
      std::array<int, BAR_MAX_WS_LABELS> order;
      size_t const workspace_count = mon->Workspaces().size();
      SortWorkspaceOrder(mon, order.data(), workspace_count);
      int active_idx = -1;
      for (size_t i = 0; i < workspace_count; i++) {
         if (mon->Workspaces()[order[i]] == mon->GetActiveWorkspace()) {
            active_idx = (int)i;
            break;
         }
      }
      if (active_idx >= 0) {
         int const delta = GET_WHEEL_DELTA_WPARAM(w_param);
         int const next_idx =
             (delta > 0) ? (int)(((size_t)active_idx + 1) % workspace_count)
                         : (int)(((size_t)active_idx + workspace_count - 1) %
                                 workspace_count);
         WorkspaceActivate(ctx, mon,
                           mon->Workspaces()[order[next_idx]]->GetIdentifier());
      }
   }
   in_layout = FALSE;
   return 0;
}

void Bar::OnDestroy() {
   HWND window = hwnd;
   KillTimer(window, BAR_TIMER_ID);
   Clay_SetCurrentContext(nullptr);
   BFWMSetWindowData(window, nullptr);
}

auto Bar::HasAlign(BarIndicatorAlign align) -> BOOL {
   for (int i = 0; i < config.indicator_count; i++) {
      if (config.indicators[i].align == align)
         return TRUE;
   }
   return FALSE;
}

auto Bar::IndicatorFontSize(BarIndicatorConfig *indicator_config) -> int {
   int font_size = indicator_config->font_size > 0 ? indicator_config->font_size
                                                   : clay_cfg.font_size;
   font_size = std::min(font_size, config.height);
   return font_size;
}

auto Bar::indicator_color(BarIndicatorConfig *indicator_config) const
    -> Clay_Color {
   if (indicator_config == nullptr)
      return colorref_to_clay_color(clay_cfg.text_color);
   if (indicator_config->color != 0U)
      return colorref_to_clay_color(indicator_config->color);
   return colorref_to_clay_color(clay_cfg.text_color);
}

auto Bar::ComputeWorkspaceTabWidth(BarIndicatorConfig *indicator_config,
                                   const char *label) -> int {
   int const font_size = IndicatorFontSize(indicator_config);
   Clay_TextElementConfig tcfg = {};
   tcfg.fontSize = (uint16_t)font_size;
   Clay_StringSlice const slice = {
       .length = (int)strlen(label), .chars = label, .baseChars = label};
   float const measured = ClayGdiMeasureText(slice, &tcfg, &clay_cfg).width;
   // The font is already scaled via the config; scale the logical tab padding
   // and min-width by this bar's monitor DPI so the tab geometry is
   // proportional to the rest of the bar.
   int const tab_padding =
       ctx->dpi->ScaleForMonitor(mon->GetHandle(), WS_TAB_PADDING);
   int const tab_min_width =
       ctx->dpi->ScaleForMonitor(mon->GetHandle(), WS_TAB_MIN_WIDTH);
   int tab_w = (int)lround(measured + ROUNDING_HALF) + tab_padding;
   tab_w = std::max<int>(tab_w, tab_min_width);
   if (indicator_config->max_width > 0 && tab_w > indicator_config->max_width)
      tab_w = indicator_config->max_width;
   return tab_w;
}

void Bar::BuildIndicatorWorkspaces(BarIndicatorConfig *indicator_config,
                                   int instance_id) {
   if (mon == nullptr)
      return;

   std::array<int, BAR_MAX_WS_LABELS> order = {};
   SortWorkspaceOrder(mon, order.data(), mon->Workspaces().size());

   Clay_Color const inactive_clay =
       colorref_to_clay_color(config.colors.inactive_workspace);
   Clay_Color const active_clay =
       colorref_to_clay_color(config.colors.active_workspace);
   Clay_Color border_clay = colorref_to_clay_color(config.colors.tab_border);
   Clay_Color text_clay = indicator_color(indicator_config);

   for (size_t idx = 0; idx < mon->Workspaces().size(); idx++) {
      int const j = order[idx];
      Workspace *workspace = mon->Workspaces()[j];
      COLORREF const tag_bg = (workspace == ctx->focused_workspace)
                                  ? config.colors.active_workspace
                                  : config.colors.inactive_workspace;

      const char *fmt = !indicator_config->format.empty()
                            ? indicator_config->format.c_str()
                            : "{id}";
      substitute_ws_label(fmt, workspace->GetIdentifier(),
                          workspace->GetLabel(), ws_labels[j]);
      int const tab_w =
          ComputeWorkspaceTabWidth(indicator_config, ws_labels[j].c_str());
      Clay_String const label_str = {.isStaticallyAllocated = true,
                                     .length =
                                         static_cast<int>(ws_labels[j].size()),
                                     .chars = ws_labels[j].c_str()};

      size_t managed_count = 0;
      size_t focused_index = 0;
      BOOL show_bar = FALSE;
      CountManagedWindows(workspace, ctx->focused_hwnd, indicator_config,
                          &managed_count, &focused_index, &show_bar);

      int const tab_idx = (instance_id * BAR_MAX_WS_LABELS) + j;
      CLAY(CLAY_IDI("ws", (uint32_t)tab_idx),
           {

               {
                   {.width = CLAY_SIZING_FIXED((float)tab_w),
                    .height = CLAY_SIZING_GROW(0, 0)},
                   {},
                   {},
                   {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
                   show_bar ? CLAY_TOP_TO_BOTTOM : CLAY_LEFT_TO_RIGHT,
               },
               colorref_to_clay_color(tag_bg),
               {},
               {},
               {},
               {},
               {},
               {},
               {},
               {border_clay, {1, 1, 1, 1, 0}},
               {},
               {}}) {
         if (show_bar != 0) {
            CLAY(CLAY_IDI("ws_txt", (uint32_t)tab_idx),
                 {
                     {{CLAY_SIZING_GROW(0, 0), CLAY_SIZING_GROW(0, 0)},
                      {},
                      {},
                      {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
                      {}},
                     {},
                     {},
                     {},
                     {},
                     {},
                     {},
                     {},
                     {},
                     {},
                     {},
                     {},
                 }) {
               CLAY_TEXT(label_str,
                         {{},
                          text_clay,
                          {},
                          (uint16_t)IndicatorFontSize(indicator_config),
                          {},
                          {},
                          CLAY_TEXT_WRAP_NONE,
                          {}});
            }

            RenderPositionBar(tab_idx, (float)tab_w, (float)managed_count,
                              (float)focused_index, active_clay, inactive_clay);
         } else {
            CLAY_TEXT(label_str, {{},
                                  text_clay,
                                  {},
                                  (uint16_t)IndicatorFontSize(indicator_config),
                                  {},
                                  {},
                                  CLAY_TEXT_WRAP_NONE,
                                  {}});
         }
      }
   }
}

void Bar::TruncateTitleWithEllipsis(std::wstring &wtitle, int max_width,
                                    int font_size) {
   if (wtitle.empty() || max_width <= 0)
      return;

   // Measure ellipsis width
   Clay_TextElementConfig tcfg = {};
   tcfg.fontSize = (uint16_t)font_size;
   Clay_StringSlice const dot_slice = {
       .length = 3, .chars = "...", .baseChars = "..."};
   float const dot_w = ClayGdiMeasureText(dot_slice, &tcfg, &clay_cfg).width;

   // Check if truncation is needed
   std::string utf8_buf;
   utf8_buf.resize((size_t)TITLE_BUF_SIZE * 3);
   int len =
       WideCharToMultiByte(CP_UTF8, 0, wtitle.c_str(), -1, utf8_buf.data(),
                           (int)utf8_buf.size(), nullptr, nullptr);
   if (len <= 0)
      return;

   Clay_StringSlice const full_slice = {.length = len - 1,
                                        .chars = utf8_buf.c_str(),
                                        .baseChars = utf8_buf.c_str()};
   float const full_w = ClayGdiMeasureText(full_slice, &tcfg, &clay_cfg).width;
   if (full_w <= (float)max_width)
      return;

   /*
    * On Windows, wchar_t is 2 bytes (UTF-16). Most common characters (A-Z,
    * digits, punctuation, most Latin, Greek, Cyrillic, even CJK) fit in a
    * single wchar_t. But characters like emojis, some rare CJK, and ancient
    * scripts don't fit in 16 bits, they need two wchar_t values glued
    * together. That's called a surrogate pair
    */
   const int SURROGATE_HIGH_MIN = 0xD800;
   const int SURROGATE_HIGH_MAX = 0xDBFF;
   const int SURROGATE_LOW_MIN = 0xDC00;
   const int SURROGATE_LOW_MAX = 0xDFFF;

   size_t wlen = wtitle.size();
   while (wlen > 0) {
      wlen--;
      if (wlen > 0 && wtitle[wlen] >= SURROGATE_LOW_MIN &&
          wtitle[wlen] <= SURROGATE_LOW_MAX &&
          wtitle[wlen - 1] >= SURROGATE_HIGH_MIN &&
          wtitle[wlen - 1] <= SURROGATE_HIGH_MAX)
         wlen--;
      wtitle.resize(wlen);

      len = WideCharToMultiByte(CP_UTF8, 0, wtitle.c_str(), -1, utf8_buf.data(),
                                (int)utf8_buf.size(), nullptr, nullptr);
      if (len <= 0)
         continue;

      Clay_StringSlice const slice = {.length = len - 1,
                                      .chars = utf8_buf.c_str(),
                                      .baseChars = utf8_buf.c_str()};
      float const w = ClayGdiMeasureText(slice, &tcfg, &clay_cfg).width;
      if (w + dot_w <= (float)max_width) {
         wtitle += L"...";
         return;
      }
   }
   wtitle = L"...";
}

void Bar::BuildIndicatorTitle(BarIndicatorConfig *indicator_config,
                              int instance_id) {
   (void)indicator_config;
   std::wstring wtitle;
   if ((ctx != nullptr) && (ctx->focused_hwnd != nullptr) &&
       (IsWindow(ctx->focused_hwnd) != 0)) {
      wtitle.resize(TITLE_BUF_SIZE);
      int const n =
          GetWindowTextW(ctx->focused_hwnd, wtitle.data(), TITLE_BUF_SIZE);
      wtitle.resize(n > 0 ? n : 0);
   }

   if (indicator_config->max_width > 0) {
      TruncateTitleWithEllipsis(wtitle, indicator_config->max_width,
                                IndicatorFontSize(indicator_config));
   }

   title_buf.resize((size_t)TITLE_BUF_SIZE * 2);
   int const len = WideCharToMultiByte(
       CP_UTF8, 0, wtitle.c_str(), -1, title_buf.data(),
       static_cast<int>(title_buf.size()), nullptr, nullptr);
   if (len > 0) {
      Clay_String const title_str = {.isStaticallyAllocated = true,
                                     .length = len - 1,
                                     .chars = title_buf.c_str()};

      CLAY(CLAY_IDI("Title", (uint32_t)instance_id),
           {

               {
                   {indicator_config->max_width > 0
                        ? CLAY_SIZING_FIXED((float)indicator_config->max_width)
                        : CLAY_SIZING_GROW(0, 0),
                    CLAY_SIZING_GROW(0, 0)},
                   {},
                   {},
                   {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
                   {},
               },
               {},
               {},
               {},
               {},
               {},
               {},
               {},
               {},
               {},
               {},
               {}}) {
         CLAY_TEXT(title_str, {{},
                               indicator_color(indicator_config),
                               {},
                               {},
                               (uint16_t)IndicatorFontSize(indicator_config),
                               {},
                               CLAY_TEXT_WRAP_NONE,
                               {}});
      }
   }
}

void Bar::ClayIndicatorText(BarIndicatorConfig *indicator_config,
                            int instance_id, const char *id_name,
                            Clay_String str, Clay_Color color, int min_width) {
   Clay_String const identifier = {.isStaticallyAllocated = true,
                                   .length = (int)strlen(id_name),
                                   .chars = id_name};
   int const content_width = (indicator_config->max_width > 0)
                                 ? indicator_config->max_width
                                 : min_width;
   CLAY(CLAY_SIDI(identifier, (uint32_t)instance_id),
        {{
             {content_width > 0 ? CLAY_SIZING_FIXED((float)content_width)
                                : CLAY_SIZING_FIT(0, 0),
              CLAY_SIZING_GROW(0, 0)},
             {6, 6, 0, 0},
             {},
             {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
             {},
         },
         {},
         {},
         {},
         {},
         {},
         {},
         {},
         {},
         {},
         {},
         {}}) {
      CLAY_TEXT(str, {
                         {},
                         color,
                         {},
                         (uint16_t)IndicatorFontSize(indicator_config),
                         {},
                         {},
                         CLAY_TEXT_WRAP_NONE,
                         {},
                     });
   }
}

void Bar::BuildIndicatorVolume(BarIndicatorConfig *indicator_config,
                               int instance_id) {
   ULONGLONG const now = GetTickCount64();
   ULONGLONG const rate = indicator_config->poll_rate_ms > 0
                              ? (ULONGLONG)indicator_config->poll_rate_ms
                              : DEFAULT_POLL_RATE_MS;

   Clay_Color c = indicator_color(indicator_config);
   BOOL muted = FALSE;

   if (now - volume_last_poll_ms >= rate) {
      VolumeStatus const volume_status = SystemGetVolumeStatus();
      volume_last_poll_ms = now;

      if (volume_status.available != 0) {
         const char *icon = nullptr;
         if ((volume_status.muted != 0) &&
             !indicator_config->icon_muted.empty()) {
            icon = indicator_config->icon_muted.c_str();
         } else if (indicator_config->icon_count > 0) {
            int idx = (int)lround((volume_status.level *
                                   (float)(indicator_config->icon_count - 1)) +
                                  ROUNDING_HALF);
            if (idx >= indicator_config->icon_count)
               idx = indicator_config->icon_count - 1;
            icon = indicator_config->icons[idx].c_str();
         }

         if ((icon != nullptr) && (icon[0] != 0)) {
            const char *fmt = !indicator_config->format.empty()
                                  ? indicator_config->format.c_str()
                                  : "{icon}";
            float const level_pct = volume_status.level * 100.0F;
            substitute_format(fmt, icon, nullptr, level_pct, 0.0F, 0.0F, 0.0F,
                              volume_buf);
            volume_cached = volume_buf;
         } else {
            volume_cached.clear();
         }

         muted = volume_status.muted;
         if ((muted != 0) && (indicator_config->color_muted != 0U))
            c = colorref_to_clay_color(indicator_config->color_muted);
      } else {
         volume_cached.clear();
      }
   } else if (!volume_cached.empty()) {
      volume_buf = volume_cached;
   }

   if (volume_buf.empty())
      return;
   Clay_String const str = {.isStaticallyAllocated = true,
                            .length = static_cast<int32_t>(volume_buf.size()),
                            .chars = volume_buf.c_str()};
   ClayIndicatorText(indicator_config, instance_id, "Volume", str, c, 0);
}

void Bar::BuildIndicatorNetwork(BarIndicatorConfig *indicator_config,
                                int instance_id) {
   ULONGLONG const now = GetTickCount64();
   ULONGLONG const rate = indicator_config->poll_rate_ms > 0
                              ? (ULONGLONG)indicator_config->poll_rate_ms
                              : DEFAULT_POLL_RATE_MS;

   Clay_Color c = indicator_color(indicator_config);

   if (now - network_last_poll_ms >= rate) {
      NetworkStatus const network_status = SystemGetNetworkStatus();
      network_last_poll_ms = now;

      if (network_status.available != 0) {
         const char *icon = nullptr;
         const char *fmt = nullptr;

         switch (network_status.type) {
         case NET_DISCONNECTED:
            icon = indicator_config->icon_disconnected.c_str();
            fmt = "{icon}";
            break;
         case NET_ETHERNET:
            icon = indicator_config->icon_ethernet.c_str();
            fmt = !indicator_config->format_ethernet.empty()
                      ? indicator_config->format_ethernet.c_str()
                      : "{icon}";
            break;
         case NET_WIFI:
            if (indicator_config->icon_count > 0) {
               int idx = (int)lround(
                   ((float)network_status.signal_percent *
                    (float)(indicator_config->icon_count - 1) / 100.0F) +
                   ROUNDING_HALF);
               if (idx >= indicator_config->icon_count)
                  idx = indicator_config->icon_count - 1;
               icon = indicator_config->icons[idx].c_str();
            }
            fmt = !indicator_config->format_wifi.empty()
                      ? indicator_config->format_wifi.c_str()
                      : "{icon}";
            break;
         }

         substitute_format(fmt, (icon != nullptr) ? icon : "",
                           network_status.ssid.c_str(), 0.0F, 0.0F, 0.0F, 0.0F,
                           network_buf);
         network_cached = network_buf;

         if (network_status.type == NET_DISCONNECTED &&
             (indicator_config->color_disconnected != 0U))
            c = colorref_to_clay_color(indicator_config->color_disconnected);
      } else {
         network_cached.clear();
      }
   } else if (!network_cached.empty()) {
      network_buf = network_cached;
   }

   if (network_buf.empty())
      return;
   Clay_String const str = {.isStaticallyAllocated = true,
                            .length = static_cast<int>(network_buf.size()),
                            .chars = network_buf.c_str()};
   ClayIndicatorText(indicator_config, instance_id, "Network", str, c, 0);
}

void Bar::BuildIndicatorCpu(BarIndicatorConfig *indicator_config,
                            int instance_id) {
   ULONGLONG const now = GetTickCount64();
   ULONGLONG const rate = indicator_config->poll_rate_ms > 0
                              ? (ULONGLONG)indicator_config->poll_rate_ms
                              : DEFAULT_POLL_RATE_MS;

   if (now - cpu_last_poll_ms >= rate) {
      CpuStatus const cpu_status = SystemGetCpuStatus();
      cpu_last_poll_ms = now;

      if (cpu_status.available != 0) {
         const char *icon = nullptr;
         if (indicator_config->icon_count > 0) {
            int idx = (int)lround(
                (cpu_status.load * (float)(indicator_config->icon_count - 1) /
                 100.0F) +
                ROUNDING_HALF);
            if (idx >= indicator_config->icon_count)
               idx = indicator_config->icon_count - 1;
            icon = indicator_config->icons[idx].c_str();
         }

         {
            const char *fmt = !indicator_config->format.empty()
                                  ? indicator_config->format.c_str()
                                  : "{icon}";
            substitute_format(fmt, (icon != nullptr) ? icon : "", nullptr,
                              cpu_status.load, 0.0F, 0.0F, 0.0F, cpu_buf);
         }

         cpu_cached = cpu_buf;
      }
   } else if (!cpu_cached.empty()) {
      cpu_buf = cpu_cached;
   }

   if (cpu_buf.empty())
      return;
   Clay_String const str = {.isStaticallyAllocated = true,
                            .length = static_cast<int>(cpu_buf.size()),
                            .chars = cpu_buf.c_str()};
   ClayIndicatorText(indicator_config, instance_id, "Cpu", str,
                     indicator_color(indicator_config), 0);
}

void Bar::BuildIndicatorMemory(BarIndicatorConfig *indicator_config,
                               int instance_id) {
   ULONGLONG const now = GetTickCount64();
   ULONGLONG const rate = indicator_config->poll_rate_ms > 0
                              ? (ULONGLONG)indicator_config->poll_rate_ms
                              : DEFAULT_POLL_RATE_MS;

   if (now - mem_last_poll_ms >= rate) {
      MemoryStatus const mem_status = SystemGetMemoryStatus();
      mem_last_poll_ms = now;

      if (mem_status.available != 0) {
         const char *icon = nullptr;
         if (indicator_config->icon_count > 0) {
            int idx = (int)lround(
                (mem_status.load * (float)(indicator_config->icon_count - 1) /
                 100.0F) +
                ROUNDING_HALF);
            if (idx >= indicator_config->icon_count)
               idx = indicator_config->icon_count - 1;
            icon = indicator_config->icons[idx].c_str();
         }

         {
            const char *fmt = !indicator_config->format.empty()
                                  ? indicator_config->format.c_str()
                                  : "{icon}";
            float const to_gb = 1.0F / BYTES_PER_GB;
            float const total_gb = (float)mem_status.total_bytes * to_gb;
            float const used_gb =
                (float)(mem_status.total_bytes - mem_status.available_bytes) *
                to_gb;
            float const avail_gb = (float)mem_status.available_bytes * to_gb;
            substitute_format(fmt, (icon != nullptr) ? icon : "", nullptr,
                              mem_status.load, total_gb, used_gb, avail_gb,
                              mem_buf);
         }

         mem_cached = mem_buf;
      }
   } else if (!mem_cached.empty()) {
      mem_buf = mem_cached;
   }

   if (mem_buf.empty())
      return;
   Clay_String const str = {.isStaticallyAllocated = true,
                            .length = static_cast<int>(mem_buf.size()),
                            .chars = mem_buf.c_str()};
   ClayIndicatorText(indicator_config, instance_id, "Memory", str,
                     indicator_color(indicator_config), 0);
}

void Bar::BuildIndicatorClock(BarIndicatorConfig *indicator_config,
                              int instance_id) {
   SYSTEMTIME system_time;
   GetLocalTime(&system_time);
   if (!indicator_config->format.empty()) {
      struct tm tm_time = {};
      tm_time.tm_sec = system_time.wSecond;
      tm_time.tm_min = system_time.wMinute;
      tm_time.tm_hour = system_time.wHour;
      tm_time.tm_mday = system_time.wDay;
      tm_time.tm_mon = system_time.wMonth - 1;
      tm_time.tm_year = system_time.wYear - TM_YEAR_BASE;
      tm_time.tm_wday = system_time.wDayOfWeek;
      tm_time.tm_isdst = -1;
      clock_buf.resize(64);
      strftime(clock_buf.data(), clock_buf.size(),
               indicator_config->format.c_str(), &tm_time);
      clock_buf.resize(std::strlen(clock_buf.c_str()));
   } else {
      clock_buf.resize(64);
      snprintf(clock_buf.data(), clock_buf.size(), "%02d:%02d",
               system_time.wHour, system_time.wMinute);
      clock_buf.resize(std::strlen(clock_buf.c_str()));
   }
   Clay_String const str = {.isStaticallyAllocated = true,
                            .length = static_cast<int>(clock_buf.size()),
                            .chars = clock_buf.c_str()};
   ClayIndicatorText(indicator_config, instance_id, "Clock", str,
                     indicator_color(indicator_config), 0);
}

auto Bar::MeasureIndicatorWidth(BarIndicatorConfig *indicator_config) -> float {
   if (indicator_config->max_width > 0)
      return (float)indicator_config->max_width;

   auto font_size = (uint16_t)IndicatorFontSize(indicator_config);
   Clay_TextElementConfig tcfg = {};
   tcfg.fontSize = font_size;
   std::string buf;
   int len = 0;

   switch (indicator_config->type) {
   case BAR_INDICATOR_WORKSPACES: {
      if (mon == nullptr)
         return 0;
      std::array<int, BAR_MAX_WS_LABELS> order;
      SortWorkspaceOrder(mon, order.data(), mon->Workspaces().size());
      float total = 0;
      for (size_t i = 0; i < mon->Workspaces().size(); i++) {
         int const j = order[i];
         Workspace *workspace = mon->Workspaces()[j];
         const char *fmt = !indicator_config->format.empty()
                               ? indicator_config->format.c_str()
                               : "{id}";
         substitute_ws_label(fmt, workspace->GetIdentifier(),
                             workspace->GetLabel(), buf);
         total +=
             (float)ComputeWorkspaceTabWidth(indicator_config, buf.c_str());
      }
      return total;
   }

   case BAR_INDICATOR_TITLE: {
      std::wstring wtitle;
      if ((ctx != nullptr) && (ctx->focused_hwnd != nullptr) &&
          (IsWindow(ctx->focused_hwnd) != 0)) {
         wtitle.resize(TITLE_BUF_SIZE);
         int const n =
             GetWindowTextW(ctx->focused_hwnd, wtitle.data(), TITLE_BUF_SIZE);
         wtitle.resize(n > 0 ? n : 0);
      }
      buf.resize(MEASURE_BUF_SIZE);
      len = WideCharToMultiByte(CP_UTF8, 0, wtitle.c_str(), -1, buf.data(),
                                (int)buf.size(), nullptr, nullptr);
      if (len <= 0)
         return 0;
      Clay_StringSlice const slice = {
          .length = len - 1, .chars = buf.c_str(), .baseChars = buf.c_str()};
      return ClayGdiMeasureText(slice, &tcfg, &clay_cfg).width;
   }

   case BAR_INDICATOR_CLOCK: {
      SYSTEMTIME clock_sys_time;
      GetLocalTime(&clock_sys_time);
      if (!indicator_config->format.empty()) {
         struct tm tm_time = {};
         tm_time.tm_sec = clock_sys_time.wSecond;
         tm_time.tm_min = clock_sys_time.wMinute;
         tm_time.tm_hour = clock_sys_time.wHour;
         tm_time.tm_mday = clock_sys_time.wDay;
         tm_time.tm_mon = clock_sys_time.wMonth - 1;
         tm_time.tm_year = clock_sys_time.wYear - TM_YEAR_BASE;
         tm_time.tm_wday = clock_sys_time.wDayOfWeek;
         tm_time.tm_isdst = -1;
         buf.resize(MEASURE_BUF_SIZE);
         strftime(buf.data(), buf.size(), indicator_config->format.c_str(),
                  &tm_time);
         buf.resize(std::strlen(buf.c_str()));
      } else {
         buf.resize(MEASURE_BUF_SIZE);
         snprintf(buf.data(), buf.size(), "%02d:%02d", clock_sys_time.wHour,
                  clock_sys_time.wMinute);
         buf.resize(std::strlen(buf.c_str()));
      }
      len = (int)buf.size();
      Clay_StringSlice const slice = {
          .length = len, .chars = buf.c_str(), .baseChars = buf.c_str()};
      return ClayGdiMeasureText(slice, &tcfg, &clay_cfg).width;
   }

   default:
      return 0;
   }
}

auto Bar::MeasureGroupWidth(BarIndicatorAlign align) -> float {
   float total = 0;
   int count = 0;
   for (int i = 0; i < config.indicator_count; i++) {
      if (config.indicators[i].align != align)
         continue;
      total += MeasureIndicatorWidth(&config.indicators[i]);
      count++;
   }
   if (count == 0)
      return 0;
   float padding;
   if (align == BAR_ALIGN_LEFT) {
      padding = (float)config.padding.left;
   } else if (align == BAR_ALIGN_RIGHT) {
      padding = (float)config.padding.right;
   } else {
      padding = 0.0F;
   }
   return total + padding + (float)((count - 1) * 4);
}

void Bar::RenderIndicatorsForAlign(BarIndicatorAlign align, int *type_counter) {
   for (int i = 0; i < config.indicator_count; i++) {
      BarIndicatorConfig *indicator_config = &config.indicators[i];
      if (indicator_config->align != align)
         continue;
      int const identifier = type_counter[indicator_config->type]++;
      switch (indicator_config->type) {
      case BAR_INDICATOR_WORKSPACES:
         BuildIndicatorWorkspaces(indicator_config, identifier);
         break;
      case BAR_INDICATOR_TITLE:
         BuildIndicatorTitle(indicator_config, identifier);
         break;
      case BAR_INDICATOR_CLOCK:
         BuildIndicatorClock(indicator_config, identifier);
         break;
      case BAR_INDICATOR_VOLUME:
         BuildIndicatorVolume(indicator_config, identifier);
         break;
      case BAR_INDICATOR_NETWORK:
         BuildIndicatorNetwork(indicator_config, identifier);
         break;
      case BAR_INDICATOR_CPU:
         BuildIndicatorCpu(indicator_config, identifier);
         break;
      case BAR_INDICATOR_MEMORY:
         BuildIndicatorMemory(indicator_config, identifier);
         break;
      default:
         break;
      }
   }
}

void Bar::ComputeCenterLayout(float bar_width, BOOL has_left, BOOL has_center,
                              BOOL has_right, BOOL *use_abs_center,
                              float *spacerL_w) {
   *use_abs_center = FALSE;
   *spacerL_w = 0;
   if (has_center == 0)
      return;
   float const C_total = MeasureGroupWidth(BAR_ALIGN_CENTER);
   float const half_remain = (bar_width - C_total) / (float)HALF_DIV;
   float const L_total =
       (has_left != 0) ? MeasureGroupWidth(BAR_ALIGN_LEFT) : 0;
   float const R_total =
       (has_right != 0) ? MeasureGroupWidth(BAR_ALIGN_RIGHT) : 0;
   if (half_remain >= L_total && half_remain >= R_total) {
      *use_abs_center = TRUE;
      *spacerL_w = half_remain - L_total;
   }
}

void Bar::RenderBarContent(int *type_counter, BOOL has_left, BOOL has_center,
                           BOOL has_right, BOOL use_abs_center,
                           float spacerL_w) {
   if (has_left != 0) {
      CLAY(CLAY_ID("LeftGroup"),
           {
               {
                   {CLAY_SIZING_FIT(0, 0), CLAY_SIZING_GROW(0, 0)},
                   {6, 0, 0, 0},
                   4,
                   {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER},
                   CLAY_LEFT_TO_RIGHT,
               },
               {},
               {},
               {},
               {},
               {},
               {},
               {},
               {},
               {},
               {},
               {},
           }) {
         RenderIndicatorsForAlign(BAR_ALIGN_LEFT, type_counter);
      }
   }

   if ((has_center != 0) || ((has_left != 0) && (has_right != 0)) ||
       ((has_left == 0) && (has_right != 0))) {
      if (use_abs_center != 0) {
         CLAY(CLAY_ID("SpacerL"),
              {{{CLAY_SIZING_FIXED(spacerL_w), CLAY_SIZING_GROW(0, 0)},
                {},
                {},
                {},
                {}},
               {},
               {},
               {},
               {},
               {},
               {},
               {},
               {},
               {},
               {},
               {}}) {}
      } else {
         CLAY(CLAY_ID("SpacerL"),
              {

                  {{CLAY_SIZING_GROW(0, 0), CLAY_SIZING_GROW(0, 0)},
                   {},
                   {},
                   {},
                   {}},
                  {},
                  {},
                  {},
                  {},
                  {},
                  {},
                  {},
                  {},
                  {},
                  {},
                  {}}) {}
      }
   }

   if (has_center != 0) {
      CLAY(CLAY_ID("CenterGroup"),
           {{
                {CLAY_SIZING_FIT(0, 0), CLAY_SIZING_GROW(0, 0)},
                {},
                4,
                {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER},
                CLAY_LEFT_TO_RIGHT,
            },
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {}}) {
         RenderIndicatorsForAlign(BAR_ALIGN_CENTER, type_counter);
      }
   }

   if (has_center != 0) {
      CLAY(CLAY_ID("SpacerR"),
           {{{CLAY_SIZING_GROW(0, 0), CLAY_SIZING_GROW(0, 0)}, {}, {}, {}, {}},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {}}) {}
   }

   if (has_right != 0) {
      CLAY(CLAY_ID("RightGroup"),
           {{
                {CLAY_SIZING_FIT(0, 0), CLAY_SIZING_GROW(0, 0)},
                {0, 6, 0, 0},
                4,
                {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER},
                CLAY_LEFT_TO_RIGHT,
            },
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {}}) {
         RenderIndicatorsForAlign(BAR_ALIGN_RIGHT, type_counter);
      }
   }
}

void Bar::BuildLayout(int width) {
   Clay_Color background = colorref_to_clay_color(config.colors.background);

   BOOL const has_left = HasAlign(BAR_ALIGN_LEFT);
   BOOL const has_center = HasAlign(BAR_ALIGN_CENTER);
   BOOL const has_right = HasAlign(BAR_ALIGN_RIGHT);
   std::array<int, BAR_INDICATOR_COUNT> type_counter = {};

   BOOL use_abs_center = FALSE;
   float spacerL_w = 0;
   ComputeCenterLayout((float)width, has_left, has_center, has_right,
                       &use_abs_center, &spacerL_w);

   CLAY(CLAY_ID("Bar"), {{
                             {CLAY_SIZING_GROW(0, 0), CLAY_SIZING_GROW(0, 0)},

                             {
                                 (uint16_t)config.padding.left,
                                 (uint16_t)config.padding.right,
                                 (uint16_t)config.padding.top,
                                 (uint16_t)config.padding.bottom,
                             },
                             {},
                             {},
                             CLAY_LEFT_TO_RIGHT,
                         },
                         background,
                         {},
                         {},
                         {},
                         {},
                         {},
                         {},
                         {},
                         {},
                         {},
                         {}}) {
      RenderBarContent(type_counter.data(), has_left, has_center, has_right,
                       use_abs_center, spacerL_w);
   }
}

void Bar::Setup() {
   static int bar_class_registered = 0;
   if (bar_class_registered != 0)
      return;
   LoadPerUserFonts();
   WNDCLASSW window_class = {};
   window_class.lpfnWndProc = BarWndProc;
   window_class.hInstance = GetModuleHandleW(nullptr);
   window_class.lpszClassName = BAR_CLASS;
   window_class.hbrBackground = nullptr;
   window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
   RegisterClassW(&window_class);
   bar_class_registered = 1;
   SystemStatusInit();
}

auto Bar::Init() -> bool {
   if (init_cfg != nullptr) {
      config = *init_cfg;
   } else {
      BarConfigDefaults(&config);
   }

   if (config.indicator_count == 0) {
      config.indicator_count = 3;
      config.indicators[0].type = BAR_INDICATOR_WORKSPACES;
      config.indicators[0].align = BAR_ALIGN_LEFT;
      config.indicators[1].type = BAR_INDICATOR_TITLE;
      config.indicators[1].align = BAR_ALIGN_LEFT;
      config.indicators[1].max_width = TITLE_MAX_WIDTH;
      config.indicators[2].type = BAR_INDICATOR_CLOCK;
      config.indicators[2].align = BAR_ALIGN_RIGHT;
   }

   // Phase 3 (per-monitor DPI): this bar renders in physical pixels on its
   // own monitor, so convert every logical (96-DPI) config dimension to
   // physical pixels here — the single consumption point for bar config.
   // Everything downstream (window geometry, layout padding, font sizes, tab
   // metrics, border/corner radius) reads this scaled copy. Non-dimension
   // fields (colours, poll rates, flags, font weight/name) stay logical; the
   // 0 sentinels (font_size = default, max_width = unlimited) stay 0.
   HMONITOR hmon = mon->GetHandle();
   config.height = ctx->dpi->ScaleForMonitor(hmon, config.height);
   config.margin.left = ctx->dpi->ScaleForMonitor(hmon, config.margin.left);
   config.margin.right = ctx->dpi->ScaleForMonitor(hmon, config.margin.right);
   config.margin.top = ctx->dpi->ScaleForMonitor(hmon, config.margin.top);
   config.margin.bottom = ctx->dpi->ScaleForMonitor(hmon, config.margin.bottom);
   config.padding.left = ctx->dpi->ScaleForMonitor(hmon, config.padding.left);
   config.padding.right = ctx->dpi->ScaleForMonitor(hmon, config.padding.right);
   config.padding.top = ctx->dpi->ScaleForMonitor(hmon, config.padding.top);
   config.padding.bottom =
       ctx->dpi->ScaleForMonitor(hmon, config.padding.bottom);
   config.corner_radius = ctx->dpi->ScaleForMonitor(hmon, config.corner_radius);
   config.border.width = ctx->dpi->ScaleForMonitor(hmon, config.border.width);
   config.font.size = ctx->dpi->ScaleForMonitor(hmon, config.font.size);
   for (int i = 0; i < config.indicator_count; i++) {
      config.indicators[i].font_size =
          ctx->dpi->ScaleForMonitor(hmon, config.indicators[i].font_size);
      config.indicators[i].max_width =
          ctx->dpi->ScaleForMonitor(hmon, config.indicators[i].max_width);
   }

   RECT const mon_rect = mon->GetRect();
   int const mon_width = mon_rect.right - mon_rect.left;
   int const bar_height = config.height;
   int bar_width = mon_width - config.margin.left - config.margin.right;
   int const bar_pos_x = mon_rect.left + config.margin.left;
   int const bar_pos_y = mon_rect.top + config.margin.top;
   bar_width = std::max(bar_width, 1);

   // Initialize Clay
   uint64_t const arena_size = Clay_MinMemorySize();
   clay_arena = calloc(1, (size_t)arena_size);
   if (clay_arena != nullptr) {
      Clay_Arena const arena =
          Clay_CreateArenaWithCapacityAndMemory((size_t)arena_size, clay_arena);
      Clay_Dimensions const dimensions = {.width = (float)bar_width,
                                          .height = (float)bar_height};
      Clay_ErrorHandler const error_handler = {
          .errorHandlerFunction = HandleClayError, .userData = nullptr};
      clay_context = Clay_Initialize(arena, dimensions, error_handler);
      ClayGdiRendererInit(&clay_cfg, &config);
      Clay_SetMeasureTextFunction(ClayGdiMeasureText, &clay_cfg);
   }

   if ((clay_arena == nullptr) || (clay_context == nullptr)) {
      // Clay failed to initialize (arena allocation or context creation).
      // OnPaint would render nothing, leaving a visible unpainted rectangle.
      // Free the arena (if allocated) and bail BEFORE creating the window.
      if (clay_arena != nullptr) {
         free(clay_arena);
         clay_arena = nullptr;
      }
      return false;
   }

   HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, BAR_CLASS,
                                 L"BFWMBar", WS_POPUP | WS_VISIBLE, bar_pos_x,
                                 bar_pos_y, bar_width, bar_height, nullptr,
                                 nullptr, GetModuleHandleW(nullptr), this);

   if (window == nullptr) {
      Snackbar::LogLastError(ctx, "BarCreate failed");
      if (clay_arena != nullptr) {
         free(clay_arena);
         clay_arena = nullptr;
      }
      return false;
   }

   if (config.corner_radius > 0) {
      HRGN window_region =
          CreateRoundRectRgn(0, 0, bar_width + 1, bar_height + 1,
                             config.corner_radius, config.corner_radius);
      if (window_region != nullptr) {
         if (SetWindowRgn(window, window_region, TRUE) == 0)
            DeleteObject(window_region);
      }
   }

   BFWMSetWindowZ(window, BFWM_Z_TOP);
   DebugW(L"Bar created on monitor %u at (%d,%d) %dx%d",
          mon->GetDisplayNumber(), bar_pos_x, bar_pos_y, bar_width, bar_height);
   return true;
}

auto Bar::IsInitialized() const -> bool { return hwnd != nullptr; }

void Bar::Show() {
   if (hwnd != nullptr)
      BFWMShowWindow(hwnd);
}

void Bar::Hide() {
   if (hwnd != nullptr)
      BFWMHideWindow(hwnd);
}

void Bar::Update() {
   if ((hwnd != nullptr) && (IsWindow(hwnd) != 0))
      InvalidateRect(hwnd, nullptr, FALSE);
}

void Bar::EnsureTopZ() {
   if (hwnd != nullptr)
      BFWMSetWindowZ(hwnd, BFWM_Z_TOP);
}

void Bar::UpdateAll(BFWMContext *ctx) {
   for (const auto &mon : ctx->monitors->Monitors()) {
      if (mon->GetBar() != nullptr)
         mon->GetBar()->Update();
   }
}

void Bar::ResetPollTimers(BFWMContext *ctx) {
   for (const auto &mon : ctx->monitors->Monitors()) {
      if (mon->GetBar() != nullptr)
         mon->GetBar()->ResetPollTimers();
   }
}

void Bar::ResetPollTimers() {
   volume_last_poll_ms = 0;
   network_last_poll_ms = 0;
   cpu_last_poll_ms = 0;
   mem_last_poll_ms = 0;
   volume_cached.clear();
   network_cached.clear();
   cpu_cached.clear();
   mem_cached.clear();
}
