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
#include <set>
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
   COLOR_ALPHA = 255,
   WS_TAB_PADDING = 14,
   WS_TAB_MIN_WIDTH = 24,
   TITLE_BUF_SIZE = 256,
   DECIMAL_BASE = 10,
   TM_YEAR_BASE = 1900,
   FMTED_BUF_SIZE = 32,
   DEFAULT_POLL_RATE_MS = 500,
   CUSTOM_POLL_RATE_MS = 1000,
   TITLE_MAX_WIDTH = 400,
   TIMER_MS_DEFAULT = 1000,
   HALF_DIV = 2,
};

#define ROUNDING_HALF 0.5F
#define BYTES_PER_GB (1024.0F * 1024.0F * 1024.0F)

/**
 * @brief Static description of one built-in indicator provider.
 *
 * The refresh callback fills an IndicatorRuntime's value bag (and any
 * provider-specific defaults); ComposeIndicator turns that bag into text. The
 * table is returned by BarProviderTable() (a friend of Bar) so entries can
 * reference the private provider methods.
 */
struct IndicatorProvider {
   const char *name;
   const char *default_format;
   int default_poll_ms; // 0 => per-frame
   bool per_frame;
   bool is_collection;
   void (Bar::*refresh)(const BarIndicatorConfig &, IndicatorRuntime &);
   void (Bar::*default_click)(const BarIndicatorConfig &, int item);
   void (Bar::*default_scroll)(const BarIndicatorConfig &, int delta);
};

auto BarProviderTable() -> const IndicatorProvider * {
   // clang-format off
   // NOLINTBEGIN(modernize-avoid-c-arrays, modernize-use-designated-initializers)
   // clang-format on
   static const IndicatorProvider kProviders[BAR_INDICATOR_COUNT] = {
       /* WORKSPACES */
       {
           .name = "workspaces",
           .default_format = "{id}",
           .default_poll_ms = 0,
           .per_frame = true,
           .is_collection = true,
           .refresh = &Bar::RefreshWorkspaces,
           .default_click = &Bar::DefaultWorkspacesClick,
           .default_scroll = &Bar::DefaultWorkspacesScroll,
       },
       /* TITLE */
       {
           .name = "title",
           .default_format = "",
           .default_poll_ms = 0,
           .per_frame = true,
           .is_collection = false,
           .refresh = &Bar::RefreshTitle,
           .default_click = nullptr,
           .default_scroll = nullptr,
       },
       /* CLOCK */
       {
           .name = "clock",
           .default_format = "%H:%M",
           .default_poll_ms = 0,
           .per_frame = true,
           .is_collection = false,
           .refresh = &Bar::RefreshClock,
           .default_click = nullptr,
           .default_scroll = nullptr,
       },
       /* VOLUME */
       {
           .name = "volume",
           .default_format = "{icon}{value:.0f}%",
           .default_poll_ms = DEFAULT_POLL_RATE_MS,
           .per_frame = false,
           .is_collection = false,
           .refresh = &Bar::RefreshVolume,
           .default_click = nullptr,
           .default_scroll = nullptr,
       },
       /* NETWORK */
       {
           .name = "network",
           .default_format = "{icon}{value:.0f}%",
           .default_poll_ms = DEFAULT_POLL_RATE_MS,
           .per_frame = false,
           .is_collection = false,
           .refresh = &Bar::RefreshNetwork,
           .default_click = nullptr,
           .default_scroll = nullptr,
       },
       /* CPU */
       {
           .name = "cpu",
           .default_format = "{icon}{value:.0f}%",
           .default_poll_ms = DEFAULT_POLL_RATE_MS,
           .per_frame = false,
           .is_collection = false,
           .refresh = &Bar::RefreshCpu,
           .default_click = nullptr,
           .default_scroll = nullptr,
       },
       /* MEMORY */
       {
           .name = "memory",
           .default_format = "{icon}{value:.1f}G",
           .default_poll_ms = DEFAULT_POLL_RATE_MS,
           .per_frame = false,
           .is_collection = false,
           .refresh = &Bar::RefreshMemory,
           .default_click = nullptr,
           .default_scroll = nullptr,
       },
       /* CUSTOM */
       {
           .name = "custom",
           .default_format = "",
           .default_poll_ms = CUSTOM_POLL_RATE_MS,
           .per_frame = false,
           .is_collection = false,
           .refresh = &Bar::RefreshCustom,
           .default_click = nullptr,
           .default_scroll = nullptr,
       },
   };
   // NOLINTEND(modernize-avoid-c-arrays, modernize-use-designated-initializers)
   return kProviders;
}

auto IndicatorWorkspaceIndex(int slot, int ws_index) -> uint32_t {
   return static_cast<uint32_t>((slot * BAR_MAX_WS_LABELS) + ws_index);
}

auto IndicatorElementId(bool collection, int slot, int item_or_ws_index)
    -> Clay_ElementId {
   if (!collection) {
      Clay_String const ind_id = {
          .isStaticallyAllocated = true,
          .length = 3,
          .chars = "ind",
      };
      return Clay_GetElementIdWithIndex(ind_id, static_cast<uint32_t>(slot));
   }
   Clay_String const ws_id = {
       .isStaticallyAllocated = true,
       .length = 2,
       .chars = "ws",
   };
   return Clay_GetElementIdWithIndex(
       ws_id, IndicatorWorkspaceIndex(slot, item_or_ws_index));
}

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

/**
 * @brief Exception-safe set/clear of a re-entrancy flag.
 *
 * Sets the flag on construction and restores its previous value on
 * destruction, so an exception thrown while the flag is held (e.g. from
 * WorkspaceActivate) cannot permanently wedge paint or input handling.
 */
template <typename Flag> class ScopedFlag {
 public:
   explicit ScopedFlag(Flag *flag) : flag_(flag), saved_(*flag) {
      *flag_ = static_cast<Flag>(1);
   }
   ~ScopedFlag() { *flag_ = saved_; }
   ScopedFlag(const ScopedFlag &) = delete;
   auto operator=(const ScopedFlag &) -> ScopedFlag & = delete;

 private:
   Flag *flag_;
   Flag saved_;
};

inline auto colorref_to_clay_color(COLORREF color_ref) -> Clay_Color {
   Clay_Color color = {
       .r = static_cast<float>(GetRValue(color_ref)),
       .g = static_cast<float>(GetGValue(color_ref)),
       .b = static_cast<float>(GetBValue(color_ref)),
       .a = COLOR_ALPHA,
   };
   return color;
}

inline void HandleClayError(Clay_ErrorData error) {
   Debug("Clay: %.*s", error.errorText.length, error.errorText.chars);
}

// ---------------------------------------------------------------------------
// Unified indicator value bag + format dialect
// ---------------------------------------------------------------------------

/// Append a numeric value to a runtime's value bag.
inline void AddNumericValue(IndicatorRuntime &runtime, const char *name,
                            double value, int precision) {
   IndicatorValue v;
   v.name = name;
   v.is_num = true;
   v.num = value;
   v.precision = precision;
   runtime.values.push_back(std::move(v));
}

/// Append a string value to a runtime's value bag.
inline void AddStringValue(IndicatorRuntime &runtime, const char *name,
                           const std::string &value) {
   IndicatorValue v;
   v.name = name;
   v.str = value;
   runtime.values.push_back(std::move(v));
}

/// Find a named value in the bag (nullptr when absent).
inline auto FindValue(const IndicatorRuntime &runtime, const std::string &name)
    -> const IndicatorValue * {
   for (const auto &value : runtime.values) {
      if (value.name == name)
         return &value;
   }
   return nullptr;
}

/// The runtime's designated primary numeric value (`{value}` alias).
inline auto PrimaryValue(const IndicatorRuntime &runtime) -> double {
   const IndicatorValue *value = FindValue(runtime, "value");
   return ((value != nullptr) && value->is_num) ? value->num : 0.0;
}

/// Format a number with the given number of decimal places.
inline auto FormatNumber(double value, int precision) -> std::string {
   std::array<char, FMTED_BUF_SIZE> buf = {};
   int const len = snprintf(buf.data(), buf.size(), "%.*f", precision, value);
   if (len <= 0)
      return {};
   return {buf.data(),
           std::min<size_t>(static_cast<size_t>(len), buf.size() - 1)};
}

/// Log an unknown `{name}` specifier once (at Warn).

inline void LogUnknownSpecifier(const std::string &name) {
   static std::set<std::string> logged;
   if (logged.insert(name).second) {
      Warn("Indicator format: unknown specifier '{%s}' (emitted literally)",
           name.c_str());
   }
}

/// Substitute the `{name}` / `{name:.Nf}` dialect over `fmt`.
// NOLINTNEXTLINE
inline auto FormatDialect(const IndicatorRuntime &runtime,
                          const std::string &fmt) -> std::string {
   std::string out;
   size_t i = 0;
   while (i < fmt.size()) {
      if (fmt[i] != '{') {
         out += fmt[i++];
         continue;
      }
      size_t const close = fmt.find('}', i + 1);
      if (close == std::string::npos) {
         out += fmt[i++];
         continue;
      }

      std::string const token = fmt.substr(i + 1, close - i - 1);
      std::string name = token;
      int explicit_precision = -1;
      size_t const colon = token.find(':');
      if (colon != std::string::npos) {
         name = token.substr(0, colon);
         std::string const spec = token.substr(colon + 1);
         if (spec.size() >= 2 && spec[0] == '.') {
            size_t p = 1;
            int decimals = 0;
            bool any = false;
            while (p < spec.size() && spec[p] >= '0' && spec[p] <= '9') {
               decimals = (decimals * DECIMAL_BASE) + (spec[p] - '0');
               p++;
               any = true;
            }
            if (any && p < spec.size() && spec[p] == 'f') {
               if (decimals < 0) {
                  decimals = 0;
               } else if (decimals > MAX_FORMAT_PRECISION) {
                  decimals = MAX_FORMAT_PRECISION;
               }
               explicit_precision = decimals;
            }
         }
      }

      if (name == "icon") {
         out += runtime.icon;
      } else if (name == "state") {
         out += runtime.state;
      } else {
         const IndicatorValue *value = FindValue(runtime, name);
         if (value == nullptr) {
            // Unknown name: emit the token literally, log once.
            out += fmt.substr(i, close - i + 1);
            LogUnknownSpecifier(name);
         } else if (value->is_num) {
            int const precision = (explicit_precision >= 0) ? explicit_precision
                                                            : value->precision;
            out += FormatNumber(value->num, precision);
         } else {
            out += value->str;
         }
      }
      i = close + 1;
   }
   return out;
}

/// Effective format for a config/runtime pair: matched-rule format, then the
/// indicator format, then the provider default.
inline auto EffectiveFormat(const BarIndicatorConfig &cfg,
                            const IndicatorRuntime &runtime) -> std::string {
   const IndicatorStateRule *rule = IndicatorFindStateRule(cfg, runtime);
   if ((rule != nullptr) && !rule->format.empty())
      return rule->format;
   if (!cfg.format.empty())
      return cfg.format;
   const int type = static_cast<int>(cfg.type);
   if (type >= 0 && type < BAR_INDICATOR_COUNT) {
      const char *default_format = BarProviderTable()[type].default_format;
      if (default_format != nullptr)
         return default_format;
   }
   return {};
}

inline void SortWorkspaceOrder(Monitor *mon, int *order, size_t count) {
   for (size_t j = 0; j < count; j++)
      order[j] = static_cast<int>(j);
   for (size_t j = 1; j < count; j++) {
      int const key = order[j];
      int k = static_cast<int>(j) - 1;
      while (k >= 0 && mon->Workspaces()[order[k]]->GetIdentifier() >
                           mon->Workspaces()[key]->GetIdentifier()) {
         order[k + 1] = order[k];
         k--;
      }
      order[k + 1] = key;
   }
}

inline void CountManagedWindows(Workspace *workspace, HWND focused_hwnd,
                                bool position_bar, size_t *managed_count,
                                size_t *focused_index, BOOL *show_bar) {
   *managed_count = 0;
   *focused_index = 0;
   *show_bar = FALSE;
   if (!position_bar)
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
       search_len > 0
           ? std::min<size_t>(static_cast<size_t>(search_len), MAX_PATH - 1)
           : 0);
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
          full_len > 0
              ? std::min<size_t>(static_cast<size_t>(full_len), MAX_PATH - 1)
              : 0);
      AddFontResourceExW(full_path.c_str(), FR_PRIVATE, nullptr);
   } while (FindNextFileW(hFind, &ffd) != 0);
   FindClose(hFind);

   search_path.resize(MAX_PATH);
   int const search_len_otf =
       swprintf(search_path.data(), MAX_PATH,
                L"%ls\\Microsoft\\Windows\\Fonts\\*.otf", font_dir.c_str());
   search_path.resize(
       search_len_otf > 0
           ? std::min<size_t>(static_cast<size_t>(search_len_otf), MAX_PATH - 1)
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
          full_len > 0
              ? std::min<size_t>(static_cast<size_t>(full_len), MAX_PATH - 1)
              : 0);
      AddFontResourceExW(full_path.c_str(), FR_PRIVATE, nullptr);
   } while (FindNextFileW(hFind, &ffd) != 0);
   FindClose(hFind);
}

auto CALLBACK BarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    -> LRESULT {
   Bar *bar = static_cast<Bar *>(BFWMGetWindowData(hwnd));

   switch (msg) {
   default:
      break;
   case WM_CREATE: {
      // NOLINTNEXTLINE
      auto *create_struct = (CREATESTRUCTW *)lParam;
      bar = static_cast<Bar *>(create_struct->lpCreateParams);
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
   // NOLINTNEXTLINE
   auto *create_struct = (CREATESTRUCTW *)l_param;
   BFWMSetWindowData(hwnd, create_struct->lpCreateParams);
   UINT timer_ms = TIMER_MS_DEFAULT;
   for (int i = 0; i < config.indicator_count; i++) {
      const BarIndicatorConfig &indicator = config.indicators[i];
      int const type = static_cast<int>(indicator.type);
      if (type < 0 || type >= BAR_INDICATOR_COUNT)
         continue;
      int r = indicator.poll_rate_ms;
      if (r <= 0)
         r = BarProviderTable()[type].default_poll_ms;
      if (r > 0 && std::cmp_less(r, timer_ms))
         timer_ms = static_cast<UINT>(r);
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

   Clay_Dimensions const dimensions = {
       .width = static_cast<float>(bar_w),
       .height = static_cast<float>(bar_h),
   };
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
          .left = 0,
          .top = 0,
          .right = bar_w,
          .bottom = bar_h,
      };
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
 * Hit-test the rendered indicator slots against the current pointer. Ids are
 * per config slot, so no render-order counter is needed:
 *   - single-value indicators render the element id ("ind", slot);
 *   - workspaces render per-tab ids ("ws", slot * BAR_MAX_WS_LABELS + j),
 *     where `j` is the workspace array index at sorted position w
 *     (SortWorkspaceOrder). The same mapping is used to return the workspace
 *     array index, so the hit test decodes exactly what the renderer encoded.
 * Clay_PointerOver is position-based, so iteration order is irrelevant.
 */
auto Bar::HitTest() -> BarHit {
   // NOTE (v1): Clay's pointer state is computed from the last completed
   // layout. A click/wheel is handled after the layout pass that produced it,
   // so the hit region can be one frame stale for a layout change still in
   // flight.
   for (int slot = 0; slot < config.indicator_count; slot++) {
      const IndicatorProvider *provider =
          ProviderFor(config.indicators[slot].type);
      if (provider == nullptr)
         continue;

      if (provider->is_collection) {
         if (mon == nullptr)
            continue;
         std::array<int, BAR_MAX_WS_LABELS> order = {};
         size_t const count =
             std::min<size_t>(mon->Workspaces().size(), BAR_MAX_WS_LABELS);
         SortWorkspaceOrder(mon, order.data(), count);
         for (size_t w = 0; w < count; w++) {
            int const tab_idx = order[w];
            if (Clay_PointerOver(IndicatorElementId(true, slot, tab_idx)))
               return {.slot = slot, .item = tab_idx};
         }
      } else if (Clay_PointerOver(IndicatorElementId(false, slot, -1))) {
         return {.slot = slot, .item = -1};
      }
   }
   return {};
}

void Bar::DefaultWorkspacesClick(const BarIndicatorConfig &cfg, int item) {
   (void)cfg;
   if ((mon == nullptr) || (item < 0) ||
       std::cmp_greater_equal(item, mon->Workspaces().size()))
      return;
   WorkspaceActivate(ctx, mon, mon->Workspaces()[item]->GetIdentifier());
}

void Bar::DefaultWorkspacesScroll(const BarIndicatorConfig &cfg, int delta) {
   (void)cfg;
   if (mon == nullptr)
      return;
   size_t const count =
       std::min<size_t>(mon->Workspaces().size(), BAR_MAX_WS_LABELS);
   if (count <= 1)
      return;

   std::array<int, BAR_MAX_WS_LABELS> order = {};
   SortWorkspaceOrder(mon, order.data(), count);
   int active_idx = -1;
   for (size_t i = 0; i < count; i++) {
      if (mon->Workspaces()[order[i]] == mon->GetActiveWorkspace()) {
         active_idx = static_cast<int>(i);
         break;
      }
   }
   if (active_idx < 0)
      return;

   int const next_idx =
       (delta > 0)
           ? static_cast<int>((static_cast<size_t>(active_idx) + 1) % count)
           : static_cast<int>((static_cast<size_t>(active_idx) + count - 1) %
                              count);
   WorkspaceActivate(ctx, mon,
                     mon->Workspaces()[order[next_idx]]->GetIdentifier());
}

void Bar::DispatchIndicatorClick(const BarHit &hit) {
   if ((hit.slot < 0) || (hit.slot >= config.indicator_count) ||
       (ctx == nullptr))
      return;
   BarIndicatorConfig const &cfg = config.indicators[hit.slot];
   const IndicatorProvider *provider = ProviderFor(cfg.type);
   if (provider == nullptr)
      return;

   if (!cfg.on_click.empty()) {
      bool const ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      bool const shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      bool const alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
      LuaConfigCallClick(&ctx->lua, ctx, cfg.on_click.c_str(), cfg.id.c_str(),
                         "left", ctrl, shift, alt);
      return;
   }
   if (provider->default_click != nullptr)
      (this->*provider->default_click)(cfg, hit.item);
}

void Bar::DispatchIndicatorScroll(const BarHit &hit, int delta) {
   if ((hit.slot < 0) || (hit.slot >= config.indicator_count) ||
       (ctx == nullptr) || (delta == 0))
      return;
   BarIndicatorConfig const &cfg = config.indicators[hit.slot];
   const IndicatorProvider *provider = ProviderFor(cfg.type);
   if (provider == nullptr)
      return;

   if (!cfg.on_scroll.empty()) {
      const char *dir = (delta > 0) ? "up" : "down";
      LuaConfigCallScroll(&ctx->lua, ctx, cfg.on_scroll.c_str(), cfg.id.c_str(),
                          dir);
      return;
   }
   if (provider->default_scroll != nullptr)
      (this->*provider->default_scroll)(cfg, delta);
}

auto Bar::OnLButtonDown(LPARAM l_param) -> LRESULT {
   if ((in_layout != 0) || in_callback)
      return 0;
   ScopedFlag<BOOL> const layout_guard(&in_layout);
   ClayContextGuard const clay_guard;
   Clay_SetCurrentContext(clay_context);
   POINT const point = {.x = GET_X_LPARAM(l_param), .y = GET_Y_LPARAM(l_param)};

   RECT client;
   GetClientRect(hwnd, &client);
   int const bar_w = client.right - client.left;
   int const bar_h = client.bottom - client.top;
   Clay_Dimensions const dimensions = {
       .width = static_cast<float>(bar_w),
       .height = static_cast<float>(bar_h),
   };
   Clay_SetLayoutDimensions(dimensions);
   Clay_Vector2 const pointer = {
       .x = static_cast<float>(point.x),
       .y = static_cast<float>(point.y),
   };
   Clay_SetPointerState(pointer, TRUE);
   Clay_BeginLayout();
   BuildLayout(bar_w);
   Clay_EndLayout(0);

   // Dispatch after EndLayout (pointer state is valid) and while in_layout is
   // still TRUE so a re-entrant input handler bails out.
   BarHit const hit = HitTest();
   if (hit.slot >= 0) {
      ScopedFlag<bool> const callback_guard(&in_callback);
      DispatchIndicatorClick(hit);
   }
   return 0;
}

auto Bar::OnMouseWheel(WPARAM w_param, LPARAM l_param) -> LRESULT {
   if ((in_layout != 0) || in_callback)
      return 0;
   ScopedFlag<BOOL> const layout_guard(&in_layout);
   ClayContextGuard const clay_guard;
   Clay_SetCurrentContext(clay_context);
   POINT point = {.x = GET_X_LPARAM(l_param), .y = GET_Y_LPARAM(l_param)};
   ScreenToClient(hwnd, &point);

   RECT client;
   GetClientRect(hwnd, &client);
   int const bar_w = client.right - client.left;
   int const bar_h = client.bottom - client.top;
   Clay_Dimensions const dimensions = {
       .width = static_cast<float>(bar_w),
       .height = static_cast<float>(bar_h),
   };
   Clay_SetLayoutDimensions(dimensions);
   Clay_Vector2 const pointer = {
       .x = static_cast<float>(point.x),
       .y = static_cast<float>(point.y),
   };
   Clay_SetPointerState(pointer, TRUE);
   Clay_BeginLayout();
   BuildLayout(bar_w);
   Clay_EndLayout(0);

   BarHit const hit = HitTest();
   if (hit.slot >= 0) {
      int const delta = GET_WHEEL_DELTA_WPARAM(w_param);
      ScopedFlag<bool> const callback_guard(&in_callback);
      DispatchIndicatorScroll(hit, delta);
   }
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

auto Bar::IndicatorFontSize(const BarIndicatorConfig *indicator_config) const
    -> int {
   int font_size = indicator_config->font_size > 0 ? indicator_config->font_size
                                                   : clay_cfg.font_size;
   font_size = std::min(font_size, config.height);
   return font_size;
}

auto Bar::ComputeWorkspaceTabWidth(const BarIndicatorConfig *indicator_config,
                                   const char *label) -> int {
   int const font_size = IndicatorFontSize(indicator_config);
   Clay_TextElementConfig tcfg = {};
   tcfg.fontSize = static_cast<uint16_t>(font_size);
   Clay_StringSlice const slice = {
       .length = static_cast<int>(strlen(label)),
       .chars = label,
       .baseChars = label,
   };
   float const measured = ClayGdiMeasureText(slice, &tcfg, &clay_cfg).width;
   // The font is already scaled via the config; scale the logical tab padding
   // and min-width by this bar's monitor DPI so the tab geometry is
   // proportional to the rest of the bar.
   int const tab_padding =
       ctx->dpi->ScaleForMonitor(mon->GetHandle(), WS_TAB_PADDING);
   int const tab_min_width =
       ctx->dpi->ScaleForMonitor(mon->GetHandle(), WS_TAB_MIN_WIDTH);
   int tab_w = static_cast<int>(lround(measured + ROUNDING_HALF)) + tab_padding;
   tab_w = std::max<int>(tab_w, tab_min_width);
   if (indicator_config->max_width > 0 && tab_w > indicator_config->max_width)
      tab_w = indicator_config->max_width;
   return tab_w;
}

// ---------------------------------------------------------------------------
// Exported format engine (also unit-tested directly)
// ---------------------------------------------------------------------------

auto IndicatorFindStateRule(const BarIndicatorConfig &cfg,
                            const IndicatorRuntime &runtime)
    -> const IndicatorStateRule * {
   // 1. A discrete `state=` match wins.
   if (!runtime.state.empty()) {
      for (int i = 0; i < cfg.state_count; i++) {
         const IndicatorStateRule &rule = cfg.states[i];
         if (!rule.state.empty() && rule.state == runtime.state)
            return &rule;
      }
   }

   // 2. Otherwise the highest numeric `at` whose threshold is <= primary.
   double const primary = PrimaryValue(runtime);
   const IndicatorStateRule *best = nullptr;
   const IndicatorStateRule *lowest = nullptr;
   for (int i = 0; i < cfg.state_count; i++) {
      const IndicatorStateRule &rule = cfg.states[i];
      if (!rule.state.empty() || !rule.has_at)
         continue;
      if (rule.at <= primary && ((best == nullptr) || rule.at > best->at))
         best = &rule;
      if ((lowest == nullptr) || rule.at < lowest->at)
         lowest = &rule;
   }

   // 3. Fallback: the lowest numeric rule.
   return (best != nullptr) ? best : lowest;
}

auto IndicatorComposeFormat(const BarIndicatorConfig &cfg,
                            const IndicatorRuntime &runtime) -> std::string {
   std::string const fmt = EffectiveFormat(cfg, runtime);
   if (fmt.empty())
      return runtime.text;
   return FormatDialect(runtime, fmt);
}

auto ProviderFor(BarIndicatorType type) -> const IndicatorProvider * {
   int const index = static_cast<int>(type);
   if (index < 0 || index >= BAR_INDICATOR_COUNT)
      return nullptr;
   return &BarProviderTable()[index];
}

auto IndicatorShouldPoll(bool per_frame, ULONGLONG rate_ms,
                         const IndicatorRuntime &runtime, ULONGLONG now)
    -> bool {
   if (per_frame)
      return true;
   if (!runtime.polled)
      return true;
   return (now - runtime.last_poll_ms) >= rate_ms;
}

auto IndicatorKeepPrevious(bool per_frame, const IndicatorRuntime &previous,
                           IndicatorRuntime &runtime) -> bool {
   if (per_frame || runtime.valid)
      return false;
   runtime = previous;
   return true;
}

auto IndicatorResolveEffects(const BarIndicatorConfig &cfg,
                             const IndicatorRuntime &runtime,
                             Clay_Color bar_default, std::string &icon_out,
                             Clay_Color &color_out) -> void {
   const IndicatorStateRule *rule = IndicatorFindStateRule(cfg, runtime);
   icon_out =
       ((rule != nullptr) && !rule->icon.empty()) ? rule->icon : std::string{};
   if ((rule != nullptr) && rule->has_color) {
      color_out = colorref_to_clay_color(rule->color);
   } else if (runtime.color_set) {
      color_out = runtime.color; // provider-supplied (custom output color)
   } else if (cfg.color_set) {
      color_out = colorref_to_clay_color(cfg.color);
   } else {
      color_out = bar_default;
   }
}

void ApplyIndicatorOutput(const LuaIndicatorOutput &out,
                          IndicatorRuntime &runtime) {
   runtime.values.clear();
   runtime.state.clear();
   runtime.icon.clear();
   runtime.color_set = false;
   runtime.text.clear();

   if (out.has_value)
      AddNumericValue(runtime, "value", out.value, 0);
   for (const LuaIndicatorValue &value : out.values) {
      if (value.is_num) {
         AddNumericValue(runtime, value.name.c_str(), value.num,
                         value.precision);
      } else {
         AddStringValue(runtime, value.name.c_str(), value.str);
      }
   }
   if (out.has_state)
      runtime.state = out.state;
   if (out.has_icon)
      runtime.icon = out.icon;
   if (out.has_color) {
      runtime.color = colorref_to_clay_color(out.color);
      runtime.color_set = true;
   }
   if (out.has_text) {
      runtime.text = out.text;
      AddStringValue(runtime, "text", out.text);
   }

   runtime.valid = out.has_text || out.has_icon || out.has_state ||
                   out.has_value || !out.values.empty();
}

// ---------------------------------------------------------------------------
// Provider refresh callbacks
// ---------------------------------------------------------------------------

void Bar::RefreshWorkspaces(const BarIndicatorConfig &cfg,
                            IndicatorRuntime &runtime) {
   runtime.item_text.clear();
   runtime.item_width.clear();
   if (mon == nullptr) {
      runtime.valid = false;
      return;
   }

   std::array<int, BAR_MAX_WS_LABELS> order = {};
   size_t const count =
       std::min<size_t>(mon->Workspaces().size(), BAR_MAX_WS_LABELS);
   SortWorkspaceOrder(mon, order.data(), count);
   for (size_t idx = 0; idx < count; idx++) {
      int const j = order[idx];
      Workspace *workspace = mon->Workspaces()[j];

      IndicatorRuntime tab_rt;
      AddStringValue(tab_rt, "id", std::to_string(workspace->GetIdentifier()));
      AddStringValue(tab_rt, "label", workspace->GetLabel());

      std::string const text = IndicatorComposeFormat(cfg, tab_rt);
      runtime.item_text.push_back(text);
      runtime.item_width.push_back(
          ComputeWorkspaceTabWidth(&cfg, text.c_str()));
   }
   runtime.valid = !runtime.item_text.empty();
}

void Bar::RefreshTitle(const BarIndicatorConfig &cfg,
                       IndicatorRuntime &runtime) {
   std::wstring wtitle;
   if ((ctx != nullptr) && (ctx->focused_hwnd != nullptr) &&
       (IsWindow(ctx->focused_hwnd) != 0)) {
      wtitle.resize(TITLE_BUF_SIZE);
      int const n =
          GetWindowTextW(ctx->focused_hwnd, wtitle.data(), TITLE_BUF_SIZE);
      wtitle.resize(n > 0 ? n : 0);
   }

if (cfg.max_width > 0)
       TruncateTextWithEllipsis(wtitle, cfg.max_width, IndicatorFontSize(&cfg));

   std::string utf8;
   if (!wtitle.empty()) {
      utf8.resize((wtitle.size() * 4) + 1);
      int const len =
          WideCharToMultiByte(CP_UTF8, 0, wtitle.c_str(), -1, utf8.data(),
                              static_cast<int>(utf8.size()), nullptr, nullptr);
      utf8.resize(len > 0 ? static_cast<size_t>(len - 1) : 0);
   }

   AddStringValue(runtime, "title", utf8);
   runtime.text = utf8; // raw title default when no format is configured
   runtime.valid = true;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Bar::RefreshClock(const BarIndicatorConfig &cfg,
                       IndicatorRuntime &runtime) {
   SYSTEMTIME system_time;
   GetLocalTime(&system_time);
   if (!cfg.format.empty()) {
      struct tm tm_time = {};
      tm_time.tm_sec = system_time.wSecond;
      tm_time.tm_min = system_time.wMinute;
      tm_time.tm_hour = system_time.wHour;
      tm_time.tm_mday = system_time.wDay;
      tm_time.tm_mon = system_time.wMonth - 1;
      tm_time.tm_year = system_time.wYear - TM_YEAR_BASE;
      tm_time.tm_wday = system_time.wDayOfWeek;
      tm_time.tm_isdst = -1;
      runtime.text.resize(64);
      strftime(runtime.text.data(), runtime.text.size(), cfg.format.c_str(),
               &tm_time);
      runtime.text.resize(std::strlen(runtime.text.c_str()));
   } else {
      runtime.text.resize(64);
      snprintf(runtime.text.data(), runtime.text.size(), "%02d:%02d",
               system_time.wHour, system_time.wMinute);
      runtime.text.resize(std::strlen(runtime.text.c_str()));
   }
   runtime.valid = true;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Bar::RefreshVolume(const BarIndicatorConfig &cfg,
                        IndicatorRuntime &runtime) {
   (void)cfg;
   VolumeStatus const status = SystemGetVolumeStatus();
   if (status.available == 0) {
      runtime.valid = false;
      return;
   }
   AddNumericValue(runtime, "value", static_cast<double>(status.level) * 100.0,
                   0);
   if (status.muted != 0)
      runtime.state = "muted";
   runtime.valid = true;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Bar::RefreshNetwork(const BarIndicatorConfig &cfg,
                         IndicatorRuntime &runtime) {
   (void)cfg;
   NetworkStatus const status = SystemGetNetworkStatus();
   if (status.available == 0) {
      runtime.valid = false;
      return;
   }
   switch (status.type) {
   case NET_DISCONNECTED:
      runtime.state = "disconnected";
      AddNumericValue(runtime, "value", 0.0, 0);
      break;
   case NET_ETHERNET:
      runtime.state = "ethernet";
      AddNumericValue(runtime, "value", 0.0, 0);
      break;
   case NET_WIFI:
      AddNumericValue(runtime, "value",
                      static_cast<double>(status.signal_percent), 0);
      break;
   }
   AddStringValue(runtime, "ssid", status.ssid);
   runtime.valid = true;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Bar::RefreshCpu(const BarIndicatorConfig &cfg, IndicatorRuntime &runtime) {
   (void)cfg;
   CpuStatus const status = SystemGetCpuStatus();
   if (status.available == 0) {
      runtime.valid = false;
      return;
   }
   AddNumericValue(runtime, "value", static_cast<double>(status.load), 0);
   runtime.valid = true;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Bar::RefreshMemory(const BarIndicatorConfig &cfg,
                        IndicatorRuntime &runtime) {
   (void)cfg;
   MemoryStatus const status = SystemGetMemoryStatus();
   if (status.available == 0) {
      runtime.valid = false;
      return;
   }
   double const to_gb = 1.0 / BYTES_PER_GB;
   double const total_gb = static_cast<double>(status.total_bytes) * to_gb;
   double const used_gb =
       static_cast<double>(status.total_bytes - status.available_bytes) * to_gb;
   double const avail_gb = static_cast<double>(status.available_bytes) * to_gb;
   AddNumericValue(runtime, "value", used_gb, 1);
   AddNumericValue(runtime, "used_gb", used_gb, 1);
   AddNumericValue(runtime, "total_gb", total_gb, 1);
   AddNumericValue(runtime, "avail_gb", avail_gb, 1);
   runtime.valid = true;
}

void Bar::RefreshCustom(const BarIndicatorConfig &cfg,
                        IndicatorRuntime &runtime) {
   LuaIndicatorOutput out;
   bool const success = (ctx != nullptr) &&
                        LuaConfigCallOutput(&ctx->lua, ctx, cfg.output.c_str(),
                                            cfg.id.c_str(), &out);
   if (!success || !out.valid) {
      // Signal failure; RefreshIndicator restores the whole previous runtime
      // (text, value bag, state, icon, color+flag), so nothing is recomposed
      // from an already-cleared bag. The first-ever failure restores the
      // default (invalid) runtime, i.e. the indicator stays hidden.
      runtime.valid = false;
      return;
   }
   ApplyIndicatorOutput(out, runtime);
}

// ---------------------------------------------------------------------------
// Compose / refresh pipeline
// ---------------------------------------------------------------------------

void Bar::ComposeIndicator(int slot) {
   BarIndicatorConfig const &cfg = config.indicators[slot];
   IndicatorRuntime &runtime = runtimes[slot];

   const IndicatorProvider *provider = ProviderFor(cfg.type);
   if (provider == nullptr)
      return;

   std::string resolved_icon;
   Clay_Color resolved_color;
   // NOTE (n3): IndicatorResolveEffects and EffectiveFormat/
   // IndicatorComposeFormat each resolve the matched state rule, so a matched
   // rule is looked up twice per compose. This is perf-only (no behavior
   // change) and is left for a later refactor.
   IndicatorResolveEffects(cfg, runtime,
                           colorref_to_clay_color(clay_cfg.text_color),
                           resolved_icon, resolved_color);
   if (!resolved_icon.empty())
      runtime.icon = resolved_icon;
   runtime.color = resolved_color;

   if (cfg.type == BAR_INDICATOR_CLOCK) {
      // RefreshClock already produced strftime text.
   } else if (cfg.type == BAR_INDICATOR_TITLE) {
      std::string const effective = EffectiveFormat(cfg, runtime);
      if (!effective.empty())
         runtime.text = IndicatorComposeFormat(cfg, runtime);
      // else: raw title already in runtime.text (set by RefreshTitle)
   } else if (!provider->is_collection) {
      runtime.text = IndicatorComposeFormat(cfg, runtime);
   }
   // Truncate non-collection text with ellipsis when max_width is exceeded.
   if (cfg.max_width > 0 && !runtime.text.empty() && !provider->is_collection &&
       cfg.type != BAR_INDICATOR_CLOCK) {
      int const font_size = IndicatorFontSize(&cfg);
      Clay_TextElementConfig tcfg = {};
      tcfg.fontSize = static_cast<uint16_t>(font_size);
      Clay_StringSlice const slice = {
          .length = static_cast<int>(runtime.text.size()),
          .chars = runtime.text.c_str(),
          .baseChars = runtime.text.c_str(),
      };
      float const text_w = ClayGdiMeasureText(slice, &tcfg, &clay_cfg).width;
      if (text_w > static_cast<float>(cfg.max_width)) {
         std::wstring wtext;
         int const req_len = MultiByteToWideChar(CP_UTF8, 0, runtime.text.c_str(),
                                                 -1, nullptr, 0);
         if (req_len > 0) {
            wtext.resize(static_cast<size_t>(req_len));
            MultiByteToWideChar(CP_UTF8, 0, runtime.text.c_str(), -1,
                                wtext.data(), req_len);
            wtext.resize(static_cast<size_t>(req_len) - 1);
            TruncateTextWithEllipsis(wtext, cfg.max_width, font_size);
            std::string new_text;
            new_text.resize((wtext.size() * 4) + 1);
            int const out_len = WideCharToMultiByte(
                CP_UTF8, 0, wtext.c_str(), -1, new_text.data(),
                static_cast<int>(new_text.size()), nullptr, nullptr);
            if (out_len > 0) {
               new_text.resize(static_cast<size_t>(out_len) - 1);
               runtime.text = std::move(new_text);
            }
         }
      }
   }

   // Collections (workspaces) compose per-item text during refresh.

   if (provider->is_collection) {
      runtime.valid = !runtime.item_text.empty();
   } else if (cfg.type == BAR_INDICATOR_CUSTOM) {
      // Custom output may supply only a state/icon/value (used by a format or
      // states rule), so treat any of them as drawable.
      runtime.valid = !runtime.text.empty() || !runtime.icon.empty() ||
                      !runtime.state.empty() || !runtime.values.empty();
   } else {
      runtime.valid = !runtime.text.empty();
   }
}

void Bar::RefreshIndicator(int slot) {
   BarIndicatorConfig const &cfg = config.indicators[slot];
   IndicatorRuntime &runtime = runtimes[slot];
   const IndicatorProvider *provider = ProviderFor(cfg.type);
   if (provider == nullptr)
      return;

   auto rate = static_cast<ULONGLONG>(DEFAULT_POLL_RATE_MS);
   if (cfg.poll_rate_ms > 0) {
      rate = static_cast<ULONGLONG>(cfg.poll_rate_ms);
   } else if (provider->default_poll_ms > 0) {
      rate = static_cast<ULONGLONG>(provider->default_poll_ms);
   }

   ULONGLONG const now = GetTickCount64();
   if (!IndicatorShouldPoll(provider->per_frame, rate, runtime, now))
      return;

   // Only polled providers keep a last-good snapshot: deep-copying the runtime
   // every layout for per-frame providers (workspaces/title/clock) is wasteful,
   // and a per-frame provider that fails should render nothing rather than
   // revive stale state.
   IndicatorRuntime previous;
   if (!provider->per_frame)
      previous = runtime;

   runtime.values.clear();
   runtime.item_text.clear();
   runtime.item_width.clear();
   runtime.state.clear();
   runtime.icon.clear();
   runtime.color = Clay_Color{};
   runtime.color_set = false;
   runtime.polled = true;
   runtime.last_poll_ms = now;

   (this->*provider->refresh)(cfg, runtime);

   if (IndicatorKeepPrevious(provider->per_frame, previous, runtime)) {
      // Keep the last good runtime (no flicker) but retry after the poll rate.
      runtime.polled = true;
      runtime.last_poll_ms = now;
      return;
   }
   ComposeIndicator(slot);
}

void Bar::RefreshAllIndicators() {
   for (int i = 0; i < config.indicator_count; i++)
      RefreshIndicator(i);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void Bar::ClayIndicatorText(int slot, Clay_String str, Clay_Color color,
                            int min_width) {
   BarIndicatorConfig const &cfg = config.indicators[slot];
   int const content_width = (cfg.max_width > 0) ? cfg.max_width : min_width;
   CLAY(IndicatorElementId(false, slot, -1),
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
                         (uint16_t)IndicatorFontSize(&cfg),
                         {},
                         {},
                         CLAY_TEXT_WRAP_NONE,
                         {},
                     });
   }
}

void Bar::RenderIndicator(int slot) {
   BarIndicatorConfig const &cfg = config.indicators[slot];
   IndicatorRuntime const &runtime = runtimes[slot];
   if (!runtime.valid)
      return;

   const IndicatorProvider *provider = ProviderFor(cfg.type);
   if (provider == nullptr)
      return;

   if (provider->is_collection) {
      if (mon == nullptr)
         return;

      Clay_Color const inactive_clay =
          colorref_to_clay_color(config.colors.inactive_workspace);
      Clay_Color const active_clay =
          colorref_to_clay_color(config.colors.active_workspace);
      Clay_Color const border_clay =
          colorref_to_clay_color(config.colors.tab_border);
      Clay_Color const text_clay = runtime.color;

      std::array<int, BAR_MAX_WS_LABELS> order = {};
      size_t const count =
          std::min<size_t>(mon->Workspaces().size(), BAR_MAX_WS_LABELS);
      SortWorkspaceOrder(mon, order.data(), count);

      for (size_t idx = 0; idx < count && idx < runtime.item_text.size();
           idx++) {
         int const j = order[idx];
         Workspace *workspace = mon->Workspaces()[j];
         COLORREF const tag_bg = (workspace == ctx->focused_workspace)
                                     ? config.colors.active_workspace
                                     : config.colors.inactive_workspace;

         std::string const &label = runtime.item_text[idx];
         int const tab_w =
             (idx < runtime.item_width.size()) ? runtime.item_width[idx] : 0;
         Clay_String const label_str = {
             .isStaticallyAllocated = true,
             .length = static_cast<int>(label.size()),
             .chars = label.c_str(),
         };

         size_t managed_count = 0;
         size_t focused_index = 0;
         BOOL show_bar = FALSE;
         CountManagedWindows(workspace, ctx->focused_hwnd, cfg.position_bar,
                             &managed_count, &focused_index, &show_bar);

         uint32_t const tab_idx = IndicatorWorkspaceIndex(slot, j);
         CLAY(IndicatorElementId(true, slot, j),
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
               CLAY(CLAY_IDI("ws_txt", tab_idx),
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
                  CLAY_TEXT(label_str, {{},
                                        text_clay,
                                        {},
                                        (uint16_t)IndicatorFontSize(&cfg),
                                        {},
                                        {},
                                        CLAY_TEXT_WRAP_NONE,
                                        {}});
               }

               RenderPositionBar(static_cast<int>(tab_w),
                                 static_cast<float>(tab_w),
                                 static_cast<float>(managed_count),
                                 static_cast<float>(focused_index), active_clay,
                                 inactive_clay);
            } else {
               CLAY_TEXT(label_str, {{},
                                     text_clay,
                                     {},
                                     (uint16_t)IndicatorFontSize(&cfg),
                                     {},
                                     {},
                                     CLAY_TEXT_WRAP_NONE,
                                     {}});
            }
         }
      }
      return;
   }

   Clay_String const str = {
       .isStaticallyAllocated = true,
       .length = static_cast<int>(runtime.text.size()),
       .chars = runtime.text.c_str(),
   };
   ClayIndicatorText(slot, str, runtime.color, 0);
}

void Bar::TruncateTextWithEllipsis(std::wstring &wtitle, int max_width,
                                   int font_size) {
   if (wtitle.empty() || max_width <= 0)
      return;

   // Measure ellipsis width
   Clay_TextElementConfig tcfg = {};
   tcfg.fontSize = static_cast<uint16_t>(font_size);
   Clay_StringSlice const dot_slice = {
       .length = 3,
       .chars = "...",
       .baseChars = "...",
   };
   float const dot_w = ClayGdiMeasureText(dot_slice, &tcfg, &clay_cfg).width;

   // Check if truncation is needed
   std::string utf8_buf;
   utf8_buf.resize((static_cast<size_t>(TITLE_BUF_SIZE) * 4) + 1);
   int len =
       WideCharToMultiByte(CP_UTF8, 0, wtitle.c_str(), -1, utf8_buf.data(),
                           static_cast<int>(utf8_buf.size()), nullptr, nullptr);
   if (len <= 0)
      return;

   Clay_StringSlice const full_slice = {
       .length = len - 1,
       .chars = utf8_buf.c_str(),
       .baseChars = utf8_buf.c_str(),
   };
   float const full_w = ClayGdiMeasureText(full_slice, &tcfg, &clay_cfg).width;
   if (full_w <= static_cast<float>(max_width))
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
                                static_cast<int>(utf8_buf.size()), nullptr,
                                nullptr);
      if (len <= 0)
         continue;

      Clay_StringSlice const slice = {
          .length = len - 1,
          .chars = utf8_buf.c_str(),
          .baseChars = utf8_buf.c_str(),
      };
      float const w = ClayGdiMeasureText(slice, &tcfg, &clay_cfg).width;
      if (w + dot_w <= static_cast<float>(max_width)) {
         wtitle += L"...";
         return;
      }
   }
   wtitle = L"...";
}

auto Bar::MeasureIndicatorWidth(int slot) -> float {
   BarIndicatorConfig const &cfg = config.indicators[slot];
   IndicatorRuntime const &runtime = runtimes[slot];

   if (cfg.max_width > 0)
      return static_cast<float>(cfg.max_width);

   const IndicatorProvider *provider = ProviderFor(cfg.type);
   if (provider == nullptr)
      return 0;

   if (provider->is_collection) {
      float total = 0;
      for (int const width : runtime.item_width)
         total += static_cast<float>(width);
      return total;
   }

   if (runtime.text.empty())
      return 0;

   Clay_TextElementConfig tcfg = {};
   tcfg.fontSize = static_cast<uint16_t>(IndicatorFontSize(&cfg));
   Clay_StringSlice const slice = {
       .length = static_cast<int>(runtime.text.size()),
       .chars = runtime.text.c_str(),
       .baseChars = runtime.text.c_str(),
   };
   return ClayGdiMeasureText(slice, &tcfg, &clay_cfg).width;
}

auto Bar::MeasureGroupWidth(BarIndicatorAlign align) -> float {
   float total = 0;
   int count = 0;
   for (int i = 0; i < config.indicator_count; i++) {
      if (config.indicators[i].align != align)
         continue;
      total += MeasureIndicatorWidth(i);
      count++;
   }
   if (count == 0)
      return 0;
   float padding;
   if (align == BAR_ALIGN_LEFT) {
      padding = static_cast<float>(config.padding.left);
   } else if (align == BAR_ALIGN_RIGHT) {
      padding = static_cast<float>(config.padding.right);
   } else {
      padding = 0.0F;
   }
   return total + padding + static_cast<float>((count - 1) * 4);
}

void Bar::RenderIndicatorsForAlign(BarIndicatorAlign align) {
   for (int i = 0; i < config.indicator_count; i++) {
      if (config.indicators[i].align != align)
         continue;
      RenderIndicator(i);
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
   float const half_remain =
       (bar_width - C_total) / static_cast<float>(HALF_DIV);
   float const L_total =
       (has_left != 0) ? MeasureGroupWidth(BAR_ALIGN_LEFT) : 0;
   float const R_total =
       (has_right != 0) ? MeasureGroupWidth(BAR_ALIGN_RIGHT) : 0;
   if (half_remain >= L_total && half_remain >= R_total) {
      *use_abs_center = TRUE;
      *spacerL_w = half_remain - L_total;
   }
}

void Bar::RenderBarContent(BOOL has_left, BOOL has_center, BOOL has_right,
                           BOOL use_abs_center, float spacerL_w) {
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
         RenderIndicatorsForAlign(BAR_ALIGN_LEFT);
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
         RenderIndicatorsForAlign(BAR_ALIGN_CENTER);
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
         RenderIndicatorsForAlign(BAR_ALIGN_RIGHT);
      }
   }
}

void Bar::BuildLayout(int width) {
   // Refresh all indicator data before measuring/rendering so both passes see
   // the same runtimes.
   RefreshAllIndicators();

   Clay_Color background = colorref_to_clay_color(config.colors.background);

   BOOL const has_left = HasAlign(BAR_ALIGN_LEFT);
   BOOL const has_center = HasAlign(BAR_ALIGN_CENTER);
   BOOL const has_right = HasAlign(BAR_ALIGN_RIGHT);

   BOOL use_abs_center = FALSE;
   float spacerL_w = 0;
   ComputeCenterLayout(static_cast<float>(width), has_left, has_center,
                       has_right, &use_abs_center, &spacerL_w);

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
      RenderBarContent(has_left, has_center, has_right, use_abs_center,
                       spacerL_w);
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
      config.indicators[0].id = "workspaces";
      config.indicators[0].format = "{id}";
      config.indicators[1].type = BAR_INDICATOR_TITLE;
      config.indicators[1].align = BAR_ALIGN_CENTER;
      config.indicators[1].id = "title";
      config.indicators[1].max_width = TITLE_MAX_WIDTH;
      config.indicators[2].type = BAR_INDICATOR_CLOCK;
      config.indicators[2].align = BAR_ALIGN_RIGHT;
      config.indicators[2].id = "clock";
      config.indicators[2].format = "%m/%d, %H:%M";
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
   clay_arena = calloc(1, static_cast<size_t>(arena_size));
   if (clay_arena != nullptr) {
      Clay_Arena const arena = Clay_CreateArenaWithCapacityAndMemory(
          static_cast<size_t>(arena_size), clay_arena);
      Clay_Dimensions const dimensions = {
          .width = static_cast<float>(bar_width),
          .height = static_cast<float>(bar_height),
      };
      Clay_ErrorHandler const error_handler = {
          .errorHandlerFunction = HandleClayError,
          .userData = nullptr,
      };
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
   for (auto &runtime : runtimes) {
      runtime.last_poll_ms = 0;
      runtime.polled = false;
      runtime.valid = false;
   }
}
