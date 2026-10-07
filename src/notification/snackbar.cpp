/**
 * @file snackbar.cpp
 * @brief Notification snackbar implementation.
 *
 * Architecture overview:
 *   - A WS_EX_LAYERED popup window for the visual overlay.
 *   - A message-only window (SNACKBAR_NOTIFY_CLASS) for WM_COPYDATA
 *     IPC from second-instance --notify forwards.
 *   - Clay layout engine for responsive text layout and wrapping.
 *   - Clay GDI renderer for text and rectangle rendering.
 *   - Ring-buffer queue feeding into a fixed-size active set, with
 *     automatic expiry based on display_duration_ms.
 */

#include "snackbar.h"
#include "../bar/renderer/clay_gdi.h"
#include "../config/defaults.h"
#include "../config/lua/parser.h"
#include "../core/bfwm_context.h"
#include "../dpi/dpi.h"
#include "../logging/logger.h"
#include "../monitor/monitor.h"
#include "../win/win_utils.h"
#include "../workspace/workspace.h"

#include <cmath>
#include <cstdarg>
#include <errhandlingapi.h>
#include <libloaderapi.h>
#include <minwindef.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <stringapiset.h>
#include <sysinfoapi.h>
#include <windef.h>
#include <windows.h>
#include <windowsx.h>
#include <winerror.h>
#include <wingdi.h>
#include <winnls.h>
#include <winnt.h>

#include <algorithm>

// ============================================================
// CONSTANTS
// ============================================================

namespace {
const wchar_t *SNACKBAR_CLASS = L"BFWMSnackbar";

enum {
   TEXT_BUF_SIZE = 512,
   TEXT_BUF_LAST_IDX = 511,
   TRUNCATED_BUF_SIZE = 96,
   MIN_SNACKBAR_WIDTH = 300,
   ITEM_GAP = 8,
   MIN_TEXT_WIDTH = 10,

   OPACITY_MAX = 255,
   INITIAL_LAYOUT_HEIGHT = 100,
   LAYOUT_HEIGHT = 1000,
   MISC_BUF_SIZE = 1024,
   MAX_COPY_LEN = 1023,
};

enum { LEVEL_BAR_WIDTH = 4, GAP_BETWEEN_ITEMS = 2 };

/** @brief Max pre-init messages buffered before Snackbar::Init runs */
enum { SNACKBAR_PREINIT_MAX = 16 };

/** @brief A message buffered before the snackbar overlay exists */
using SnackbarPreInit = struct SnackbarPreInit {
   SnackbarLogLevel level;
   std::wstring message;
   BOOL force;
};

/** @brief Static ring buffer for messages queued before Snackbar::Init */
std::array<SnackbarPreInit, SNACKBAR_PREINIT_MAX> preinit_buf = {};
int preinit_count;

} // namespace

#define SNACKBAR_WIDTH_RATIO 0.30F
#define ROUNDING_OFFSET 0.5F

// ============================================================
// SNACKBAR LIFECYCLE
// ============================================================

Snackbar::Snackbar(struct BFWMContext *ctx) : ctx(ctx) {}

Snackbar::~Snackbar() {
   if (notify_hwnd != nullptr) {
      DestroyWindow(notify_hwnd);
      notify_hwnd = nullptr;
   }
   if (hwnd != nullptr) {
      DestroyWindow(hwnd);
      hwnd = nullptr;
   }
   Clay_SetCurrentContext(nullptr);
   free(clay_arena);
}

auto Snackbar::Init() -> bool {
   if (hwnd != nullptr)
      return true;

   HINSTANCE hInst = GetModuleHandleW(nullptr);

   WNDCLASSW window_class = {};
   window_class.lpfnWndProc = Snackbar::WndProc;
   window_class.hInstance = hInst;
   window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
   window_class.hbrBackground = nullptr;
   window_class.lpszClassName = SNACKBAR_CLASS;
   if ((RegisterClassW(&window_class) == 0U) &&
       GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      ::Warn("Snackbar::Init: RegisterClassW(%ls) failed", SNACKBAR_CLASS);
      return false;
   }

   hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                          SNACKBAR_CLASS, L"", WS_POPUP, 0, 0, 0, 0, nullptr,
                          nullptr, hInst, nullptr);
   if (hwnd == nullptr) {
      ::Warn("Snackbar::Init: CreateWindowExW failed");
      return false;
   }
   BFWMSetWindowData(hwnd, this);
   BFWMSetWindowZ(hwnd, BFWM_Z_TOP);

   WNDCLASSW nwc = {};
   nwc.lpfnWndProc = Snackbar::NotifyWndProc;
   nwc.hInstance = hInst;
   nwc.lpszClassName = SNACKBAR_NOTIFY_CLASS;
   RegisterClassW(&nwc);

   notify_hwnd = CreateWindowExW(0, SNACKBAR_NOTIFY_CLASS, L"", WS_POPUP, 0, 0,
                                 0, 0, HWND_MESSAGE, nullptr, hInst, nullptr);
   if (notify_hwnd != nullptr)
      BFWMSetWindowData(notify_hwnd, ctx);

   const SnackbarConfig *cfg = &ctx->config.snackbar;
   uint64_t const arena_size = Clay_MinMemorySize();
   clay_arena = calloc(1, (size_t)arena_size);
   if (clay_arena == nullptr) {
      ::Warn("Snackbar::Init: clay arena allocation failed");
      if (notify_hwnd != nullptr) {
         DestroyWindow(notify_hwnd);
         notify_hwnd = nullptr;
      }
      if (hwnd != nullptr) {
         DestroyWindow(hwnd);
         hwnd = nullptr;
      }
      return false;
   }
   Clay_Arena const arena =
       Clay_CreateArenaWithCapacityAndMemory((size_t)arena_size, clay_arena);
   Clay_Dimensions const dimensions = {
       .width = (float)ScaleForCurrentDpi(cfg->max_width),
       .height = INITIAL_LAYOUT_HEIGHT};
   Clay_ErrorHandler const error_handler = {
       .errorHandlerFunction = SnackbarClayError, .userData = nullptr};

   clay_context = Clay_Initialize(arena, dimensions, error_handler);
   if (clay_context == nullptr) {
      ::Warn("Snackbar::Init: Clay_Initialize failed");
      free(clay_arena);
      clay_arena = nullptr;
      if (notify_hwnd != nullptr) {
         DestroyWindow(notify_hwnd);
         notify_hwnd = nullptr;
      }
      if (hwnd != nullptr) {
         DestroyWindow(hwnd);
         hwnd = nullptr;
      }
      return false;
   }

   BarConfig tmp_bar_cfg;
   BarConfigDefaults(&tmp_bar_cfg);
   tmp_bar_cfg.font.size = ScaleForCurrentDpi(cfg->font_size);
   tmp_bar_cfg.font.weight = FW_NORMAL;
   tmp_bar_cfg.font.name = cfg->font_name;
   tmp_bar_cfg.colors.text = cfg->text;
   ClayGdiRendererInit(&clay_cfg, &tmp_bar_cfg);
   Clay_SetMeasureTextFunction(ClayGdiMeasureText, &clay_cfg);

   Clay_SetCurrentContext(nullptr);

   /* Drain any messages that were queued before the snackbar was ready */
   for (int i = 0; i < preinit_count; i++) {
      ShowMessage(preinit_buf[i].level, &preinit_buf[i].message,
                  preinit_buf[i].force);
   }
   preinit_count = 0;

   return true;
}

auto Snackbar::IsInitialized() const -> bool { return hwnd != nullptr; }

// ============================================================
// PUBLIC STATIC API
// ============================================================

void Snackbar::Tick(struct BFWMContext *ctx) {
   if ((ctx == nullptr) || ctx->snackbar == nullptr ||
       !ctx->snackbar->IsInitialized())
      return;
   ctx->snackbar->TickNotifications();
}

void Snackbar::Show(struct BFWMContext *ctx, LogLevel level,
                    std::wstring *message, BOOL force) {
   Snackbar::Show(ctx, SnackbarLevelFromLogLevel(level), message, force);
}

void Snackbar::Show(struct BFWMContext *ctx, SnackbarLogLevel level,
                    std::wstring *message, BOOL force) {
   if (ctx == nullptr)
      return;
   if (ctx->snackbar == nullptr) {
      if (preinit_count < SNACKBAR_PREINIT_MAX) {
         preinit_buf[preinit_count].level = level;
         preinit_buf[preinit_count].message = *message;
         preinit_buf[preinit_count].force = force;
         preinit_count++;
      }
      return;
   }
   if (!ctx->snackbar->IsInitialized())
      return;
   ctx->snackbar->ShowMessage(level, message, force);
}

void Snackbar::ShowMessage(SnackbarLogLevel level, std::wstring *message,
                           BOOL force) {

   const SnackbarConfig *cfg = &ctx->config.snackbar;
   if (cfg->enabled == 0)
      return;
   if ((force == 0) && level > cfg->log_level)
      return;

   SnackbarNotification n = {};

   WCharToChar(message, &n.text);
   n.level = level;
   n.timestamp = GetTickCount64();
   n.dismiss_deadline = n.timestamp + cfg->display_duration_ms;
   n.id = next_id++;
   n.dismissed = FALSE;
   n.forced = force;
   n.collapsed = cfg->click_to_expand;
   n.hovered = FALSE;
   n.rendered_height = 0;

   if (queue_count >= SNACKBAR_QUEUE_MAX) {
      queue_head = (queue_head + 1) % SNACKBAR_QUEUE_MAX;
      queue_count--;
   }

   int const tail = (queue_head + queue_count) % SNACKBAR_QUEUE_MAX;
   queue[tail] = n;
   queue_count++;
}

void Snackbar::TickNotifications() {
   if (hwnd == nullptr)
      return;

   const SnackbarConfig *cfg = &ctx->config.snackbar;
   if (cfg->enabled == 0) {
      if (window_visible != 0) {
         ShowWindow(hwnd, SW_HIDE);
         window_visible = FALSE;
      }
      return;
   }

   ULONGLONG const now = GetTickCount64();

   /* Phase 1: remove expired / click-dismissed notifications */
   BOOL changed = RemoveExpired(now);

   /* Phase 1b: force re-layout on click-to-expand */
   if (force_render != 0) {
      force_render = FALSE;
      changed = TRUE;
   }

   /* Phase 2: pull queued notifications into the active set */
   if (DrainQueue(cfg) != 0)
      changed = TRUE;

   if (changed == 0)
      return;

   /* Phase 3: re-compute layout and reposition the window */
   RebuildLayout(cfg);
}

// ============================================================
// CONVENIENCE WRAPPERS
// ============================================================

void Snackbar::Error(struct BFWMContext *ctx, const wchar_t *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   std::wstring buf(TEXT_BUF_SIZE, L'\0');
   vswprintf(buf.data(), TEXT_BUF_SIZE, fmt, args);
   va_end(args);
   buf.resize(wcslen(buf.c_str()));
   std::string narrow(MISC_BUF_SIZE, '\0');
   WCharToChar(&buf, &narrow);
   ::Error("%s", narrow.data());
   Snackbar::Show(ctx, SNACKBAR_LOG_ERROR, &buf, FALSE);
}

void Snackbar::Warn(struct BFWMContext *ctx, const wchar_t *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   std::wstring buf(TEXT_BUF_SIZE, L'\0');
   vswprintf(buf.data(), TEXT_BUF_SIZE, fmt, args);
   va_end(args);
   buf.resize(wcslen(buf.c_str()));
   std::string narrow(MISC_BUF_SIZE, '\0');
   WCharToChar(&buf, &narrow);
   ::Warn("%s", narrow.data());
   Snackbar::Show(ctx, SNACKBAR_LOG_WARN, &buf, FALSE);
}

void Snackbar::Info(struct BFWMContext *ctx, const wchar_t *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   std::wstring buf(TEXT_BUF_SIZE, L'\0');
   vswprintf(buf.data(), TEXT_BUF_SIZE, fmt, args);
   va_end(args);
   buf.resize(wcslen(buf.c_str()));
   std::string narrow(MISC_BUF_SIZE, '\0');
   WCharToChar(&buf, &narrow);
   ::Info("%s", narrow.data());
   Snackbar::Show(ctx, SNACKBAR_LOG_INFO, &buf, FALSE);
}

void Snackbar::LogLastError(struct BFWMContext *ctx, const char *prefix) {
   DWORD const error = GetLastError();
   LPSTR messageBuffer = nullptr;

   FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS,
                  nullptr, error, 0, (LPSTR)&messageBuffer, 0, nullptr);

   std::string buf(MISC_BUF_SIZE, '\0');
   size_t prefix_len = 0;
   if (prefix != nullptr) {
      prefix_len = strlen(prefix) + 2;
      sprintf_s(buf.data(), MISC_BUF_SIZE, "%s: ", prefix);
   }

   if (messageBuffer != nullptr) {
      sprintf_s(buf.data() + prefix_len, MISC_BUF_SIZE - prefix_len,
                "error %lu: %s", error, messageBuffer);
      LocalFree(messageBuffer);
   } else {
      sprintf_s(buf.data() + prefix_len, MISC_BUF_SIZE - prefix_len,
                "error %lu\n", error);
   }

   ::Error("%s", buf.data());

   int const actual_len = (int)strlen(buf.data());
   std::wstring wbuf(MISC_BUF_SIZE, L'\0');
   int const conv_len = MultiByteToWideChar(CP_UTF8, 0, buf.data(), actual_len,
                                            wbuf.data(), (int)wbuf.size());
   wbuf.resize(conv_len > 0 ? (size_t)conv_len : 0);
   Snackbar::Show(ctx, SNACKBAR_LOG_ERROR, &wbuf, FALSE);
}

// ============================================================
// SNACKBAR WINDOW PROCEDURE
// ============================================================

auto CALLBACK Snackbar::WndProc(HWND hwnd, UINT msg, WPARAM w_param,
                                LPARAM l_param) -> LRESULT {
   auto *s = (Snackbar *)BFWMGetWindowData(hwnd);
   (void)w_param;
   if (s == nullptr)
      return DefWindowProcW(hwnd, msg, w_param, l_param);

   switch (msg) {
   case WM_NCHITTEST:
      return s->OnNcHitTest(l_param);
   case WM_PAINT:
      return s->OnPaint();
   case WM_LBUTTONDOWN:
      return s->OnLButtonDown(l_param);
   case WM_MOUSEMOVE:
      return s->OnMouseMove();
   case WM_MOUSELEAVE:
      return s->OnMouseLeave();
   case WM_DPICHANGED:
      return s->OnDpiChanged(w_param, l_param);
   case WM_DESTROY:
      return 0;
   default:
      break;
   }
   return DefWindowProcW(hwnd, msg, w_param, l_param);
}

auto Snackbar::OnNcHitTest(LPARAM l_param) -> LRESULT {
   POINT point = {.x = GET_X_LPARAM(l_param), .y = GET_Y_LPARAM(l_param)};
   ScreenToClient(hwnd, &point);
   int y = 0;
   for (int i = 0; i < active_count; i++) {
      int const h =
          active[i].rendered_height + ScaleForCurrentDpi(GAP_BETWEEN_ITEMS);
      if (point.y >= y && point.y < y + h)
         return HTCLIENT;
      y += h;
   }
   return HTTRANSPARENT;
}

auto Snackbar::OnPaint() -> LRESULT {
   if (active_count == 0) {
      PAINTSTRUCT paint_struct;
      BeginPaint(hwnd, &paint_struct);
      EndPaint(hwnd, &paint_struct);
      return 0;
   }

   PAINTSTRUCT paint_struct;
   HDC hdc = BeginPaint(hwnd, &paint_struct);
   if (hdc == nullptr)
      return 0;

   RECT client_rect;
   GetClientRect(hwnd, &client_rect);
   COLORREF const background = ctx->config.snackbar.background;
   HBRUSH bg_brush = CreateSolidBrush(background);
   FillRect(hdc, &client_rect, bg_brush);
   DeleteObject(bg_brush);

   Clay_Dimensions const dimensions = {
       .width = (float)(client_rect.right - client_rect.left),
       .height = (float)(client_rect.bottom - client_rect.top)};
   Clay_Vector2 const position{.x = 0, .y = 0};
   Clay_Context *saved = Clay_GetCurrentContext();
   Clay_SetCurrentContext(clay_context);
   Clay_SetLayoutDimensions(dimensions);
   Clay_SetPointerState(position, FALSE);
   Clay_BeginLayout();
   BuildLayout(&ctx->config.snackbar, true);
   Clay_RenderCommandArray cmds = Clay_EndLayout(0);
   if (cmds.length > 0)
      ClayGdiRender(hdc, &cmds, &clay_cfg);

   Clay_SetCurrentContext(saved);
   EndPaint(hwnd, &paint_struct);
   return 0;
}

auto Snackbar::OnLButtonDown(LPARAM l_param) -> LRESULT {
   const SnackbarConfig *cfg = &ctx->config.snackbar;
   int const mouse_y = GET_Y_LPARAM(l_param);
   int top = 0;
   for (int i = 0; i < active_count; i++) {
      int const item_h =
          active[i].rendered_height + ScaleForCurrentDpi(GAP_BETWEEN_ITEMS);
      if (mouse_y >= top && mouse_y < top + item_h) {
         if ((cfg->click_to_expand != 0) && (active[i].collapsed != 0)) {
            active[i].collapsed = FALSE;
            force_render = TRUE;
         } else if (cfg->close_on_click != 0) {
            active[i].dismissed = TRUE;
         }
         break;
      }
      top += item_h;
   }
   return 0;
}

auto Snackbar::OnMouseMove() -> LRESULT {
   const SnackbarConfig *cfg = &ctx->config.snackbar;
   if (cfg->pause_on_hover == 0)
      return 0;
   if (hovered == 0) {
      hovered = TRUE;
      hover_enter_tick = GetTickCount64();
      for (int i = 0; i < active_count; i++)
         active[i].hovered = TRUE;
   }
   TRACKMOUSEEVENT tme = {.cbSize = sizeof(tme),
                          .dwFlags = TME_LEAVE,
                          .hwndTrack = hwnd,
                          .dwHoverTime = 0};
   TrackMouseEvent(&tme);
   return 0;
}

auto Snackbar::OnMouseLeave() -> LRESULT {
   hovered = FALSE;
   for (int i = 0; i < active_count; i++)
      active[i].hovered = FALSE;
   return 0;
}

auto Snackbar::OnDpiChanged(WPARAM w_param, LPARAM l_param) -> LRESULT {
   (void)l_param;
   /* wParam carries the new DPI in the low word (same convention as the
    * monitor watcher in wm_worker.cpp). */
   UINT const new_dpi = LOWORD(w_param);
   HMONITOR hmon = (target_monitor_ != nullptr)
                       ? target_monitor_->GetHandle()
                       : MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
   ctx->dpi->RegisterMonitor(hmon, new_dpi);
   if (active_count > 0)
      RebuildLayout(&ctx->config.snackbar);
   return 0;
}

// ============================================================
// NOTIFY IPC WINDOW
// ============================================================

auto CALLBACK Snackbar::NotifyWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                      LPARAM lParam) -> LRESULT {
   if (msg == WM_COPYDATA) {
      auto *ctx = (struct BFWMContext *)BFWMGetWindowData(hwnd);
      auto *cds = (COPYDATASTRUCT *)lParam;
      if ((ctx != nullptr) && (cds != nullptr) && (cds->lpData != nullptr) &&
          cds->cbData > 0) {
         size_t const len = cds->cbData / sizeof(wchar_t);
         std::wstring buf((const wchar_t *)cds->lpData, len);
         Snackbar::Show(ctx, SNACKBAR_LOG_INFO, &buf, TRUE);
      }
      return TRUE;
   }
   return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================
// MONITOR & WIDTH HELPERS
// ============================================================

auto Snackbar::FindMonitor() -> const Monitor * {
   const SnackbarConfig *cfg = &ctx->config.snackbar;
   if (cfg->monitor == 0) {
      POINT const zero = {.x = 0, .y = 0};
      HMONITOR primary = MonitorFromPoint(zero, MONITOR_DEFAULTTOPRIMARY);
      for (const auto &monitor : ctx->monitors->Monitors()) {
         if (monitor->GetHandle() == primary)
            return monitor;
      }
   } else if (cfg->monitor == -1 && (ctx->focused_workspace != nullptr)) {
      return FindMonitorByWorkspace(ctx, ctx->focused_workspace);
   } else if (cfg->monitor > 0) {
      return FindMonitorByDisplayNumber(ctx, (UINT)cfg->monitor);
   }
   return ctx->monitors->Empty() ? nullptr : ctx->monitors->At(0);
}

auto Snackbar::CurrentDpi() const -> UINT {
   /* Cached at rebuild time so every scaling call in a layout pass agrees
    * with the monitor the window is actually positioned on. Falls back to
    * the system DPI when no rebuild has happened yet. */
   return target_dpi_ != 0 ? target_dpi_ : GetDpiForSystem();
}

auto Snackbar::ScaleForCurrentDpi(int logical) -> int {
   return DpiSystem::Scale(logical, CurrentDpi());
}

auto Snackbar::EffectiveWidth(const SnackbarConfig *cfg, const Monitor *mon,
                              UINT dpi) -> int {
   if (cfg->max_width >= 1)
      return DpiSystem::Scale(cfg->max_width, dpi);
   int const max_width =
       (mon != nullptr) ? (mon->GetWorkArea().right - mon->GetWorkArea().left)
                        : GetSystemMetrics(SM_CXSCREEN);
   int const pct = (int)((float)max_width * SNACKBAR_WIDTH_RATIO);
   int const min_width = DpiSystem::Scale(MIN_SNACKBAR_WIDTH, dpi);
   return pct < min_width ? min_width : pct;
}

// ============================================================
// CLAY LAYOUT BUILDER
// ============================================================

auto Snackbar::BuildText(SnackbarNotification *n, const SnackbarConfig *cfg,
                         int max_text_w) -> Clay_String {
   if ((cfg->click_to_expand != 0) && (n->collapsed != 0)) {
      int const slen = static_cast<int32_t>(n->text.size());

      Clay_TextElementConfig tcfg = {};
      tcfg.fontSize = (uint16_t)ScaleForCurrentDpi(cfg->font_size);
      Clay_StringSlice const full_slice = {
          .length = slen, .chars = n->text.data(), .baseChars = n->text.data()};
      float const full_w =
          ClayGdiMeasureText(full_slice, &tcfg, &clay_cfg).width;

      if (full_w <= (float)max_text_w) {
         n->collapsed = FALSE;
         Clay_String string = {.isStaticallyAllocated = false,
                               .length = slen,
                               .chars = n->text.data()};
         return string;
      }

      Clay_StringSlice const dot_slice = {
          .length = 3, .chars = "...", .baseChars = "..."};
      float const dot_w = ClayGdiMeasureText(dot_slice, &tcfg, &clay_cfg).width;

      size_t low = 0;
      size_t high = slen;
      while (low < high) {
         int const mid = static_cast<int32_t>((low + high + 1) / 2);
         Clay_StringSlice const slice = {.length = mid,
                                         .chars = n->text.data(),
                                         .baseChars = n->text.data()};
         float const w = ClayGdiMeasureText(slice, &tcfg, &clay_cfg).width;
         if (w + dot_w <= (float)max_text_w) {
            low = mid;
         } else {
            high = mid - 1;
         }
      }

      n->truncated.clear();
      n->truncated.assign(n->text, 0, low);
      n->truncated.append("...");
      Clay_String string = {.isStaticallyAllocated = false,
                            .length = static_cast<int32_t>(low + 3),
                            .chars = n->truncated.data()};

      return string;
   }

   Clay_String string = {.isStaticallyAllocated = false,
                         .length = static_cast<int32_t>(n->text.size()),
                         .chars = n->text.data()};

   return string;
}

void Snackbar::BuildItem(const SnackbarConfig *cfg, int i,
                         Clay_Color background) {
   SnackbarNotification *n = &active[i];
   COLORREF const level_color = LevelColor(n->level, n->forced, &cfg->colors);
   int max_text_w = effective_max_width - ScaleForCurrentDpi(LEVEL_BAR_WIDTH) -
                    ScaleForCurrentDpi(ITEM_GAP) -
                    ScaleForCurrentDpi(cfg->padding_left) -
                    ScaleForCurrentDpi(cfg->padding_right);
   max_text_w = std::max<int>(max_text_w, ScaleForCurrentDpi(MIN_TEXT_WIDTH));

   Clay_String const text_str = BuildText(n, cfg, max_text_w);
   Clay_TextElementConfigWrapMode wrap =
       ((cfg->click_to_expand != 0) && (n->collapsed != 0))
           ? CLAY_TEXT_WRAP_NONE
           : CLAY_TEXT_WRAP_WORDS;

   CLAY(CLAY_IDI("Item", (uint32_t)i),
        {.layout = {.sizing = {},
                    .padding = {0, 0, 0, 0},
                    .childGap = 8,
                    .childAlignment = {},
                    .layoutDirection = CLAY_LEFT_TO_RIGHT},
         .backgroundColor = background,
         .overlayColor = {},
         .cornerRadius = {},
         .aspectRatio = {},
         .image = {},
         .floating = {},
         .custom = {},
         .clip = {},
         .border = {},
         .transition = {},
         .userData = {}}) {
      CLAY(CLAY_IDI("LevelBar", (uint32_t)i),
           {{{CLAY_SIZING_FIXED((float)ScaleForCurrentDpi(LEVEL_BAR_WIDTH)),
              CLAY_SIZING_GROW(0, 0)},
             {},
             {},
             {},
             {}},
            ColorRefToClay(level_color),
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

      CLAY(CLAY_IDI("TextContainer", (uint32_t)i),
           {{{CLAY_SIZING_FIT({}, (float)max_text_w), {}},
             {static_cast<uint16_t>(ScaleForCurrentDpi(cfg->padding_left)),
              static_cast<uint16_t>(ScaleForCurrentDpi(cfg->padding_top)),
              static_cast<uint16_t>(ScaleForCurrentDpi(cfg->padding_right)),
              static_cast<uint16_t>(ScaleForCurrentDpi(cfg->padding_bottom))},
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
            {}}) {
         CLAY_TEXT(text_str, {{},
                              ColorRefToClay(cfg->text),
                              0,
                              (uint16_t)ScaleForCurrentDpi(cfg->font_size),
                              0,
                              0,
                              wrap,
                              {}});
      }
   }

   if (i < active_count - 1) {
      CLAY(
          CLAY_IDI("Divider", (uint32_t)i),
          {{{CLAY_SIZING_GROW(0, 0),
             CLAY_SIZING_FIXED((float)ScaleForCurrentDpi(cfg->divider_height))},
            {},
            {},
            {},
            {}},
           ColorRefToClay(cfg->divider),
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

void Snackbar::BuildLayout(const SnackbarConfig *cfg, bool fill_width) {
   Clay_Color background = ColorRefToClay(cfg->background);

   CLAY(CLAY_ID("SnackbarRoot"),
        {{
             {
                 fill_width ? CLAY_SIZING_GROW(0, 0) : CLAY_SIZING_FIT(0, 0),
                 {{{0, 0}}, CLAY__SIZING_TYPE_FIT},
             },
             {0, 0, 0, 0},
             (uint16_t)ScaleForCurrentDpi(GAP_BETWEEN_ITEMS),
             {},
             CLAY_TOP_TO_BOTTOM,

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
      for (int i = 0; i < active_count; i++)
         BuildItem(cfg, i, background);
   }
}

// ============================================================
// POSITION & RESIZE
// ============================================================

void Snackbar::UpdateWindow() {
   const SnackbarConfig *cfg = &ctx->config.snackbar;
   if (active_count == 0) {
      if (window_visible != 0) {
         ShowWindow(hwnd, SW_HIDE);
         window_visible = FALSE;
      }
      return;
   }

   const Monitor *mon =
       (target_monitor_ != nullptr) ? target_monitor_ : FindMonitor();

   RECT work = {.left = 0,
                .top = 0,
                .right = GetSystemMetrics(SM_CXSCREEN),
                .bottom = GetSystemMetrics(SM_CYSCREEN)};
   if (mon != nullptr)
      work = mon->GetWorkArea();

   int const w = total_width;
   int const h = total_height;

   int x;
   int y;
   switch (cfg->position) {
   case SNACKBAR_POSITION_BOTTOM_RIGHT:
      x = work.right - w - ScaleForCurrentDpi(cfg->margin_right);
      y = work.bottom - h - ScaleForCurrentDpi(cfg->margin_bottom);
      break;
   case SNACKBAR_POSITION_BOTTOM_LEFT:
      x = work.left + ScaleForCurrentDpi(cfg->margin_left);
      y = work.bottom - h - ScaleForCurrentDpi(cfg->margin_bottom);
      break;
   case SNACKBAR_POSITION_TOP_RIGHT:
      x = work.right - w - ScaleForCurrentDpi(cfg->margin_right);
      y = work.top + ScaleForCurrentDpi(cfg->margin_top);
      break;
   case SNACKBAR_POSITION_TOP_LEFT:
      x = work.left + ScaleForCurrentDpi(cfg->margin_left);
      y = work.top + ScaleForCurrentDpi(cfg->margin_top);
      break;
   default:
      x = work.right - w - ScaleForCurrentDpi(cfg->margin_right);
      y = work.bottom - h - ScaleForCurrentDpi(cfg->margin_bottom);
      break;
   }

   BFWMSetWindowZ(hwnd, BFWM_Z_TOPMOST);
   SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
   window_visible = TRUE;

   int const corner_radius = ScaleForCurrentDpi(cfg->corner_radius);
   if (corner_radius > 0) {
      HRGN result =
          CreateRoundRectRgn(0, 0, w + 1, h + 1, corner_radius, corner_radius);
      if (result != nullptr) {
         /* SetWindowRgn() transfers ownership of the region to the window
          * manager on success, so only delete the handle if it failed. */
         if (SetWindowRgn(hwnd, result, TRUE) == 0)
            DeleteObject(result);
      }
   } else {
      // Clear any stale window region from a previous corner_radius>0 call.
      // Without this, a growing window gets clipped by the old region.
      SetWindowRgn(hwnd, nullptr, TRUE);
   }

   int opacity = cfg->opacity;
   opacity = std::max(opacity, 0);
   opacity = std::min<int>(opacity, OPACITY_MAX);
   SetLayeredWindowAttributes(hwnd, 0, (BYTE)opacity, LWA_ALPHA);

   InvalidateRect(hwnd, nullptr, FALSE);
}

// ============================================================
// SNACKBAR LAYOUT HELPERS
// ============================================================

void Snackbar::DeriveDimensions(Clay_RenderCommandArray *cmds) {
   total_width = 0;
   total_height = 0;
   for (int32_t ci = 0; ci < cmds->length; ci++) {
      Clay_RenderCommand *cmd = &cmds->internalArray[ci];
      if (cmd == nullptr)
         continue;
      int const right = std::lround(cmd->boundingBox.x +
                                    cmd->boundingBox.width + ROUNDING_OFFSET);
      int const bottom = std::lround(cmd->boundingBox.y +
                                     cmd->boundingBox.height + ROUNDING_OFFSET);
      total_width = std::max(right, total_width);
      total_height = std::max(bottom, total_height);
   }
}

auto Snackbar::RemoveExpired(ULONGLONG now) -> BOOL {
   BOOL changed = FALSE;
   for (int i = active_count - 1; i >= 0; i--) {
      BOOL const expired = static_cast<BOOL>((active[i].hovered == 0) &&
                                             now >= active[i].dismiss_deadline);
      if ((active[i].dismissed != 0) || (expired != 0)) {
         for (int k = i; k < active_count - 1; k++)
            active[k] = active[k + 1];
         active_count--;
         changed = TRUE;
      }
   }
   return changed;
}

auto Snackbar::DrainQueue(const SnackbarConfig *cfg) -> BOOL {
   int const max_active =
       cfg->max_queue > 0 && cfg->max_queue < SNACKBAR_ACTIVE_MAX
           ? cfg->max_queue
           : SNACKBAR_ACTIVE_MAX;
   BOOL changed = FALSE;
   while (active_count < max_active && queue_count > 0) {
      active[active_count] = queue[queue_head];
      queue_head = (queue_head + 1) % SNACKBAR_QUEUE_MAX;
      queue_count--;
      active_count++;
      changed = TRUE;
   }
   return changed;
}

void Snackbar::RebuildLayout(const SnackbarConfig *cfg) {
   target_monitor_ = FindMonitor();
   target_dpi_ = (target_monitor_ != nullptr)
                     ? ctx->dpi->GetDpi(target_monitor_->GetHandle())
                     : GetDpiForSystem();
   UINT const dpi = target_dpi_;
   effective_max_width = EffectiveWidth(cfg, target_monitor_, dpi);

   Clay_Context *saved = Clay_GetCurrentContext();
   Clay_SetCurrentContext(clay_context);

   /* First pass: measure content width */
   Clay_Dimensions dimensions = {.width = (float)effective_max_width,
                                 .height = LAYOUT_HEIGHT};
   Clay_Vector2 position = {.x = 0, .y = 0};
   Clay_SetLayoutDimensions(dimensions);
   Clay_SetPointerState(position, FALSE);
   Clay_BeginLayout();
   BuildLayout(cfg, false);
   Clay_RenderCommandArray cmds = Clay_EndLayout(0);
   DeriveDimensions(&cmds);

   /* Clamp width to [min_width, effective_max_width] */
   int const content_width = total_width;
   int const min_width = DpiSystem::Scale(cfg->min_width, dpi);
   if (cfg->min_width > 0 && total_width < min_width)
      total_width = min_width;
   total_width = std::min(total_width, effective_max_width);

   /* If clamp widened the layout, rebuild with GROW root so dividers span
    * full width */
   dimensions = {.width = (float)total_width, .height = LAYOUT_HEIGHT};
   position = {.x = 0, .y = 0};

   if (total_width > content_width) {
      Clay_SetLayoutDimensions(dimensions);
      Clay_SetPointerState(position, FALSE);
      Clay_BeginLayout();
      BuildLayout(cfg, true);
      cmds = Clay_EndLayout(0);
      DeriveDimensions(&cmds);
   }

   Clay_SetCurrentContext(saved);

   /* Compute per-item rendered heights for hit-testing */
   if (active_count > 0) {
      int const gap_total =
          (active_count - 1) * DpiSystem::Scale(GAP_BETWEEN_ITEMS, dpi);
      int const per_item = (total_height - gap_total) / active_count;
      for (int i = 0; i < active_count; i++)
         active[i].rendered_height = per_item;
   }

   UpdateWindow();
}

// ============================================================
// STATIC UTILITIES
// ============================================================

auto Snackbar::LevelColor(SnackbarLogLevel level, BOOL forced,
                          const SnackbarLevelColors *colors) -> COLORREF {
   if (forced != 0)
      return colors->normal;
   switch (level) {
   case SNACKBAR_LOG_ERROR:
      return colors->error;
   case SNACKBAR_LOG_WARN:
      return colors->warn;
   case SNACKBAR_LOG_INFO:
      return colors->info;
   case SNACKBAR_LOG_DEBUG:
      return colors->debug;
   default:
      return colors->info;
   }
}

auto Snackbar::ColorRefToClay(COLORREF color_ref) -> Clay_Color {
   Clay_Color color = {.r = (float)GetRValue(color_ref),
                       .g = (float)GetGValue(color_ref),
                       .b = (float)GetBValue(color_ref),
                       .a = 1.0F};
   return color;
}

void Snackbar::SnackbarClayError(Clay_ErrorData error) {
   Debug("Clay[snackbar]: %.*s", error.errorText.length, error.errorText.chars);
}

void Snackbar::WCharToChar(const std::wstring *src, std::string *dst) {
   int const len = WideCharToMultiByte(
       CP_UTF8, 0, src->data(), (int)src->size(), nullptr, 0, nullptr, nullptr);
   if (len <= 0) {
      dst->clear();
      return;
   }
   dst->resize((size_t)len);
   WideCharToMultiByte(CP_UTF8, 0, src->data(), (int)src->size(), dst->data(),
                       len, nullptr, nullptr);
}
