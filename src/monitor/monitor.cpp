/**
 * @file monitor.cpp
 * @brief Monitor detection, reconciliation, and workspace navigation.
 */

#include "monitor.h"
#include "../bar/bar.h"
#include "../config/lua/parser.h"
#include "../core/bfwm_context.h"
#include "../core/bfwm_def.h"
#include "../core/sync.h"
#include "../dpi/dpi.h"
#include "../logging/logger.h"
#include "../math/rect.h"
#include "../memory/safe.h"
#include "../transaction/transaction.h"
#include "../window/events/events.h"
#include "../window/fullscreen/detect.h"
#include "../workspace/workspace.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>
#include <cwchar>
#include <memory>
#include <minwindef.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <utility>
#include <windef.h>
#include <windows.h>
#include <winerror.h>
#include <wingdi.h>
#include <winnt.h>

enum {
   PINNED_WS_MAX = 16,
   ORIGIN_WS_MAX = 32,
};

Monitor::Monitor() = default;
Monitor::~Monitor() = default;

namespace {
// ============================================================
// INTERNAL HELPERS
// ============================================================

inline auto QueryDisplayNumber(HMONITOR hMonitor) -> UINT {
   MONITORINFOEXW monitor_info = {};
   monitor_info.cbSize = sizeof(monitor_info);
   if (GetMonitorInfoW(hMonitor, (LPMONITORINFO)&monitor_info) == 0)
      return 0;

   UINT32 path_count = 0;
   UINT32 mode_count = 0;
   if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count,
                                   &mode_count) != ERROR_SUCCESS)
      return 0;

   auto *paths = (DISPLAYCONFIG_PATH_INFO *)safe_malloc(
       path_count * sizeof(DISPLAYCONFIG_PATH_INFO),
       "Could not allocate display config paths");
   auto *modes = (DISPLAYCONFIG_MODE_INFO *)safe_malloc(
       mode_count * sizeof(DISPLAYCONFIG_MODE_INFO),
       "Could not allocate display config modes");

   if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths,
                          &mode_count, modes, nullptr) != ERROR_SUCCESS) {
      free(paths);
      free(modes);
      return 0;
   }

   UINT number = 0;
   for (UINT32 i = 0; i < path_count && number == 0; i++) {
      DISPLAYCONFIG_SOURCE_DEVICE_NAME source;
      ZeroMemory(&source, sizeof(source));
      source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
      source.header.size = sizeof(source);
      source.header.adapterId = paths[i].sourceInfo.adapterId;
      source.header.id = paths[i].sourceInfo.id;

      if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS)
         continue;

      size_t const len = wcslen(source.viewGdiDeviceName);
      if (wcsncmp(source.viewGdiDeviceName, monitor_info.szDevice, len) == 0 &&
          (monitor_info.szDevice[len] == L'\0' ||
           monitor_info.szDevice[len] == L'\\')) {
         number = i + 1;
      }
   }

   free(paths);
   free(modes);
   return number;
}

} // namespace

// ============================================================
// MONITOR REGISTRY
// ============================================================

auto SortMonitorByPosition(Monitor const *a, Monitor const *b) -> bool {
   if (a->GetRect().top != b->GetRect().top)
      return a->GetRect().top < b->GetRect().top;
   return a->GetRect().left < b->GetRect().left;
}

void RegisterMonitor(MonitorRegistry *reg, Monitor *monitor) {
   reg->Add(monitor);

   RECT const r = monitor->GetRect();
   DebugW(L"Monitor %u: x:%ld y:%ld w:%ld h:%ld", monitor->GetDisplayNumber(),
          r.left, r.top, r.right - r.left, r.bottom - r.top);
}

auto CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor,
                              LPRECT lprcMonitor, LPARAM dwData) -> BOOL {
   (void)hdcMonitor;

   auto *data = (MonitorEnumData *)dwData;
   if (data == nullptr)
      return FALSE;
   MonitorRegistry *const reg = data->reg;
   BFWMContext *const ctx = data->ctx;

   MONITORINFOEXW miex;
   miex.cbSize = sizeof(miex);
   if (GetMonitorInfoW(hMonitor, (LPMONITORINFO)&miex) == 0)
      return TRUE;

   auto *mon = new Monitor();

   mon->SetHandle(hMonitor);
   mon->SetRect(*lprcMonitor);
   mon->SetWorkArea(miex.rcWork);

   // Register the monitor's effective DPI in the DpiSystem registry (the
   // per-monitor DPI truth; rebuilt from scratch each reconcile).
   if (ctx != nullptr && ctx->dpi != nullptr) {
      UINT dpi_x = 0;
      UINT dpi_y = 0;
      if (GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y) == S_OK)
         ctx->dpi->RegisterMonitor(hMonitor, dpi_x);
   }

   mon->SetDisplayNumber(QueryDisplayNumber(hMonitor));

   // Populate a stable PnP device instance ID for identity matching
   // across display number renumbering.
   DISPLAY_DEVICEW display_device = {
       .cb = sizeof(display_device),
       .DeviceName = {},
       .DeviceString = {},
       .StateFlags = {},
       .DeviceID = {},
       .DeviceKey = {},

   };
   if (EnumDisplayDevicesW(miex.szDevice, 0, &display_device, 0) != 0)
      mon->SetUid(display_device.DeviceID);

   // Detect taskbar auto-hide: if enabled, use full monitor rect so
   // tiled windows extend edge-to-edge even when the taskbar is
   // normally occupying work area space.
   {
      HWND tray_bar = FindWindowW(L"Shell_TrayWnd", nullptr);
      if (tray_bar != nullptr) {
         APPBARDATA abd = {.cbSize = sizeof(abd),
                           .hWnd = tray_bar,
                           .uCallbackMessage = {},
                           .uEdge = {},
                           .rc = {},
                           .lParam = {}};
         UINT const state = (UINT)SHAppBarMessage(ABM_GETSTATE, &abd);
         if ((state & ABS_AUTOHIDE) != 0U) {
            RECT tb_rect;
            if (GetWindowRect(tray_bar, &tb_rect) != 0) {
               RECT inter;
               RECT const mon_rect = mon->GetRect();
               if (IntersectRect(&inter, &mon_rect, &tb_rect) != 0)
                  mon->SetWorkArea(mon_rect);
            }
         }
      }
   }

   RegisterMonitor(reg, mon);
   return TRUE;
}

// ============================================================
// WORKSPACE NAVIGATION (monitor-aware)
// ============================================================

auto FindNeighbouringWorkspace(struct BFWMContext *ctx,
                               BFWMDirection direction) -> Workspace * {
   if (ctx == nullptr)
      return nullptr;

   const auto &monitors = ctx->monitors->Monitors();
   size_t const mon_count = monitors.size();

   RECT src = {};
   Monitor *best = nullptr;
   int best_dist = INT_MAX;

   /* A floated/fullscreen focused window is not in the layout engine, so a
    * get_rect lookup would log "No node with window handle"; fall through to
    * the monitor work-area anchor like a failed lookup. Mirrors the
    * PlacementApply guard. */
   Window *focused_win = ctx->windows->FindByHwnd(ctx->focused_hwnd);
   bool const focused_managed = (focused_win != nullptr) &&
                                (IsWindowManagedByLayout(focused_win) != FALSE);

   if ((ctx->focused_workspace == nullptr) ||
       (ctx->focused_workspace->GetEngine() == nullptr) || !focused_managed ||
       !ctx->focused_workspace->GetEngine()->get_rect(ctx->focused_hwnd,
                                                      &src)) {
      Monitor *current_mon =
          FindMonitorByWorkspace(ctx, ctx->focused_workspace);
      if (current_mon != nullptr) {
         src = current_mon->GetWorkArea();
      } else if (mon_count > 0 && (monitors[0] != nullptr)) {
         src = monitors[0]->GetWorkArea();
      }
   }

   for (size_t m = 0; m < mon_count && (monitors[m] != nullptr); m++) {
      Monitor *mon = monitors[m];
      if (ctx->focused_workspace == mon->GetActiveWorkspace()) {
         if (direction == DirPrev) {
            return monitors[(m - 1 + mon_count) % mon_count]
                ->GetActiveWorkspace();
         }
         if (direction == DirNext)
            return monitors[(m + 1) % mon_count]->GetActiveWorkspace();
         continue;
      }

      int dist;
      if ((RectIsNeighbor(src, mon->GetWorkArea(), direction, &dist) == TRUE) &&
          dist < best_dist) {
         best_dist = dist;
         best = mon;
      }
   }

   return (best != nullptr) ? best->GetActiveWorkspace() : nullptr;
}

auto FindMonitorForHwnd(struct BFWMContext *ctx, HWND hwnd) -> Monitor * {
   Workspace *workspace = FindWorkspaceByHwnd(ctx, hwnd);
   if (workspace == nullptr)
      return nullptr;
   return FindMonitorByWorkspace(ctx, workspace);
}

auto FindMonitorByDisplayNumber(struct BFWMContext *ctx, UINT display_number)
    -> Monitor * {
   if (ctx == nullptr)
      return nullptr;
   for (auto *monitor : ctx->monitors->Monitors()) {
      if (monitor->GetDisplayNumber() == display_number)
         return monitor;
   }
   return nullptr;
}

namespace {

inline auto FindNextPrevMonitor(Monitor *const *monitors, size_t mon_count,
                                Monitor *src, BFWMDirection direction)
    -> Monitor * {
   if (direction == DirNext) {
      for (size_t i = 0; i < mon_count; i++) {
         if (monitors[i] == src)
            return monitors[(i + 1) % mon_count];
      }
      return monitors[0];
   }

   for (size_t i = 0; i < mon_count; i++) {
      if (monitors[i] == src)
         return monitors[(i - 1 + mon_count) % mon_count];
   }
   return monitors[mon_count - 1];
}

// ============================================================
// RECONCILE MONITORS — INTERNAL HELPERS
// ============================================================

inline auto EnumerateMonitors(struct BFWMContext *ctx, MonitorRegistry *reg)
    -> BOOL {
   reg->Reserve(BFC_INITIAL_MONITOR_CAP);

   MonitorEnumData const enum_data = {.reg = reg, .ctx = ctx};
   EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, (LPARAM)&enum_data);
   if (reg->Empty()) {
      Warn("ReconcileMonitors: no monitors found, keeping existing");
      return FALSE;
   }

   reg->SortByPosition();
   return TRUE;
}

inline void RemoveDisabledMonitors(struct BFWMContext *ctx,
                                   MonitorRegistry *reg) {
   size_t dst = 0;
   for (size_t src = 0; src < reg->Size(); src++) {
      Monitor *monitor = reg->At(src);
      if (monitor->IsDisabled(ctx) == TRUE) {
         InfoW(L"Monitor %u (uid %ls) is disabled, skipping",
               monitor->GetDisplayNumber(), monitor->GetUid().c_str());
         delete monitor;
      } else {
         reg->SetAt(dst++, monitor);
      }
   }
   reg->Resize(dst);
}

/* Match old monitors against the new enumeration by UID then HMONITOR.
 * Returns a std::vector<BOOL> where old_matched[i] is TRUE if
 * ctx->monitors->At(i) is still connected.  Geometry and display number are
 * updated for matches so existing workspaces keep their state. */

/* A DPI-only change (same physical rect) is invisible to TryMatchMonitor's
 * rect_changed check: re-converge the workspace cached scaled gap/border
 * values so windows don't keep the old-DPI layout until focus moves.  The DPI
 * registry was rebuilt from a fresh GetDpiForMonitor by MonitorEnumProc before
 * the matcher runs, so GetDpi on the adopted handle is the new value; the
 * pre-reconcile value comes from the UID-keyed old_dpis snapshot. */
inline void ReconcileMonitorDpi(
    struct BFWMContext *ctx, Monitor *mon,
    const std::vector<std::pair<std::wstring, UINT>> &old_dpis) {
   UINT const new_dpi =
       (ctx->dpi != nullptr) ? ctx->dpi->GetDpi(mon->GetHandle()) : 0;
   if (new_dpi == 0)
      return; // not registered — nothing to compare
   for (const auto &old : old_dpis) {
      if (old.first != mon->GetUid())
         continue;
      if (old.second != new_dpi) {
         InfoW(L"Monitor %u DPI changed: %u -> %u", mon->GetDisplayNumber(),
               old.second, new_dpi);
         for (size_t w = 0; w < mon->Workspaces().size(); w++) {
            mon->Workspaces()[w]->RecalculateRect(mon, ctx);
         }
      }
      break;
   }
}

inline void
TryMatchMonitor(struct BFWMContext *ctx, Monitor *old_mon,
                MonitorRegistry *new_reg,
                const std::vector<std::pair<std::wstring, UINT>> &old_dpis,
                std::vector<BOOL> &old_matched, size_t i) {
   for (size_t idx = 0; idx < new_reg->Size(); idx++) {
      Monitor *new_replacement = new_reg->At(idx);
      BOOL match = FALSE;
      if (!new_replacement->GetUid().empty() && !old_mon->GetUid().empty()) {
         match =
             static_cast<BOOL>(new_replacement->GetUid() == old_mon->GetUid());
      }
      if (match == FALSE) {
         match = static_cast<BOOL>(new_replacement->GetHandle() ==
                                   old_mon->GetHandle());
      }
      if (match == FALSE)
         continue;

      // A UID match can pair a reconnected monitor with a new handle; adopt
      // the current handle so the DPI registry (keyed by handle) stays
      // reachable for this monitor.
      old_mon->SetHandle(new_replacement->GetHandle());

      RECT const old_rect = old_mon->GetRect();
      RECT const new_rect = new_replacement->GetRect();
      RECT const old_work_area = old_mon->GetWorkArea();
      RECT const new_work_area = new_replacement->GetWorkArea();
      BOOL const rect_changed = static_cast<BOOL>(
          (EqualRect(&old_rect, &new_rect) == FALSE) ||
          (EqualRect(&old_work_area, &new_work_area) == FALSE));
      old_mon->SetRect(new_replacement->GetRect());
      old_mon->SetWorkArea(new_replacement->GetWorkArea());
      old_mon->SetDisplayNumber(new_replacement->GetDisplayNumber());

      if (rect_changed == TRUE) {
         InfoW(L"Monitor %u geometry changed: (%ld,%ld %ld,%ld)",
               old_mon->GetDisplayNumber(), old_mon->GetRect().left,
               old_mon->GetRect().top, old_mon->GetRect().right,
               old_mon->GetRect().bottom);
         for (size_t w = 0; w < old_mon->Workspaces().size(); w++) {
            old_mon->Workspaces()[w]->RecalculateRect(old_mon, ctx);
         }
      } else {
         ReconcileMonitorDpi(ctx, old_mon, old_dpis);
      }

      delete new_replacement;
      new_reg->SetAt(idx, old_mon);
      old_matched[i] = TRUE;
      break;
   }
}

inline auto MatchExistingMonitors(
    struct BFWMContext *ctx, MonitorRegistry *new_reg,
    const std::vector<std::pair<std::wstring, UINT>> &old_dpis)
    -> std::vector<BOOL> {
   std::vector<BOOL> old_matched(ctx->monitors->Size(), FALSE);

   for (size_t i = 0; i < ctx->monitors->Size(); i++) {
      Monitor *old_mon = ctx->monitors->At(i);
      if (old_mon->IsDisabled(ctx) == TRUE)
         continue;
      TryMatchMonitor(ctx, old_mon, new_reg, old_dpis, old_matched, i);
   }
   return old_matched;
}

/* Move unmatched monitors' workspaces to a fallback and destroy the
 * monitor itself.  A two-pass approach avoids thrashing the fallback's
 * active workspace during the move (pass 1) before activating the
 * correct one (pass 2). */
inline void HandleDisconnectedMonitors(struct BFWMContext *ctx,
                                       const std::vector<BOOL> &old_matched) {
   Monitor *fallback_mon = nullptr;
   for (size_t m = 0; m < ctx->monitors->Size(); m++) {
      if (old_matched[m] == TRUE) {
         fallback_mon = ctx->monitors->At(m);
         break;
      }
   }

   for (size_t i = 0; i < ctx->monitors->Size(); i++) {
      if (old_matched[i] == TRUE)
         continue;

      Monitor *old_mon = ctx->monitors->At(i);
      InfoW(L"Monitor %u disconnected at (%ld,%ld)",
            old_mon->GetDisplayNumber(), old_mon->GetRect().left,
            old_mon->GetRect().top);

      Workspace *active_ws = old_mon->GetActiveWorkspace();

      std::vector<Workspace *> dead_ws(old_mon->Workspaces().begin(),
                                       old_mon->Workspaces().end());
      size_t const dead_count = dead_ws.size();

      // Pass 1: move workspaces to fallback without affecting
      // the fallback's active state or cloaking.
      for (size_t w = 0; w < dead_count; w++) {
         Workspace *workspace = dead_ws[w];
         if (fallback_mon != nullptr) {
            workspace->SetOriginMonitorUid(old_mon->GetUid());
            workspace->MoveToMonitor(fallback_mon, ctx);
         } else {
            workspace->DestroyWindows(ctx);
            delete workspace;
         }
      }

      // Pass 2: activate only the previously-active workspace so
      // the fallback keeps the same active state.  Other workspaces
      // stay cloaked until the user switches to them.
      if ((fallback_mon != nullptr) && (active_ws != nullptr)) {
         WorkspaceActivateSimple(ctx, fallback_mon,
                                 fallback_mon->GetActiveWorkspace(), active_ws,
                                 true);
      }

      old_mon->ResetBar();
      delete old_mon;
   }
}

/* Remove entries whose monitors disconnected and fix up the
 * focused_workspace if it pointed to a now-dead monitor. */
inline void CompactMonitorArray(struct BFWMContext *ctx,
                                const std::vector<BOOL> &old_matched) {
   size_t keep = 0;
   for (size_t i = 0; i < ctx->monitors->Size(); i++) {
      if (old_matched[i] == TRUE)
         ctx->monitors->SetAt(keep++, ctx->monitors->At(i));
   }
   ctx->monitors->Resize(keep);
   // old_matched is a std::vector owned by ReconcileMonitors (automatic
   // scope cleanup).

   if (ctx->focused_workspace != nullptr) {
      Monitor *focused_monitor =
          FindMonitorByWorkspace(ctx, ctx->focused_workspace);
      if (focused_monitor == nullptr) {
         ctx->focused_workspace =
             !ctx->monitors->Empty()
                 ? ctx->monitors->At(0)->GetActiveWorkspace()
                 : nullptr;
         ctx->focused_hwnd = nullptr;
      }
   }
}

inline auto FindPinnedWorkspaces(struct BFWMContext *ctx, UINT display_number,
                                 Workspace **out, int max) -> int {
   int count = 0;
   for (size_t m = 0; m < ctx->monitors->Size() && count < max; m++) {
      Monitor *other = ctx->monitors->At(m);
      for (size_t w = 0; w < other->Workspaces().size() && count < max; w++) {
         Workspace *candidate = other->Workspaces()[w];
         const struct WorkspaceConfig *cfg =
             FindWorkspaceConfig(ctx, candidate->GetIdentifier());
         if ((cfg != nullptr) && cfg->assigned_monitor > 0 &&
             std::cmp_equal(cfg->assigned_monitor, display_number)) {
            out[count++] = candidate;
         }
      }
   }
   return count;
}

inline void SetupNewMonitors(struct BFWMContext *ctx,
                             MonitorRegistry *new_reg) {
   for (size_t idx = 0; idx < new_reg->Size(); idx++) {
      Monitor *mon = new_reg->At(idx);

      BOOL is_new = TRUE;
      for (size_t k = 0; k < ctx->monitors->Size(); k++) {
         if (ctx->monitors->At(k) == mon) {
            is_new = FALSE;
            break;
         }
      }
      if (is_new == FALSE)
         continue;

      InfoW(L"New monitor detected: display %u (%ld,%ld %ld,%ld)",
            mon->GetDisplayNumber(), mon->GetRect().left, mon->GetRect().top,
            mon->GetRect().right, mon->GetRect().bottom);

      mon->Init(ctx);

      if (mon->SetupNewWorkspaces(ctx) == FALSE) {
         Warn("ReconcileMonitors: could not create workspace for new monitor");
         delete mon;
         new_reg->SetAt(idx, nullptr);
         continue;
      }

      if (mon->GetBar() == nullptr || !mon->GetBar()->IsInitialized()) {
         mon->SetBar(std::make_unique<Bar>(mon, &ctx->config.bar_cfg, ctx));
         if (!mon->GetBar()->Init()) {
            /* keep object alive for retry */
         }
         BarUpdateRequest(ctx, BAR_UPDATE_RECREATE);
      }
   }
}

/* Ensure every monitor has at least one workspace and a valid
 * active_workspace pointer after the reconcile shuffle. */
inline void RestoreDonorMonitors(struct BFWMContext *ctx) {
   for (size_t i = 0; i < ctx->monitors->Size(); i++) {
      Monitor *other = ctx->monitors->At(i);
      if (other->Workspaces().empty()) {
         size_t const new_id = FindNextWorkspaceId(ctx);
         (void)other->CreateWorkspace(ctx, new_id);
         continue;
      }

      if (other->GetActiveWorkspace() == nullptr)
         continue;
      BOOL found = FALSE;
      for (size_t w = 0; w < other->Workspaces().size(); w++) {
         if (other->Workspaces()[w] == other->GetActiveWorkspace()) {
            found = TRUE;
            break;
         }
      }
      if (found == FALSE)
         other->SetActiveWorkspace(other->Workspaces()[0]);
   }
}

/* Cloak every workspace on each monitor, then uncloak only the active
 * workspace so that workspaces moved, created, or swapped during reconcile
 * get the correct visibility. */
inline void CommitMonitorVisibility(struct BFWMContext *ctx,
                                    MonitorRegistry *new_reg) {
   ctx->transaction.Begin();

   for (auto *monitor : new_reg->Monitors()) {
      Workspace *aws = monitor->GetActiveWorkspace();

      // Cloak all workspaces first
      for (auto *workspace : monitor->Workspaces()) {
         BOOL const is_active = (workspace == aws) ? TRUE : FALSE;
         for (const auto &window : workspace->Windows()) {
            // A genuinely fullscreen window in the active workspace must not
            // be transiently cloaked: the cloak → relayout → uncloak sequence
            // inside one commit can pull an exclusive-fullscreen window out
            // of exclusive mode. Non-active workspaces are still fully
            // cloaked so switching workspaces hides them as usual.
            if (is_active == TRUE) {
               FullscreenType const fullscreen_type =
                   DetectFullscreenWindow(window->GetHwnd());
               if ((fullscreen_type &
                    (FS_EXCLUSIVE_FULLSCREEN | FS_BORDERLESS_WINDOW)) != 0) {
                  continue;
               }
            }
            ctx->transaction.QueueCloak(window->GetHwnd(), TRUE);
         }
      }

      // Uncloak only the active workspace
      if (aws == nullptr)
         continue;

      for (const auto &window : aws->Windows()) {
         ctx->transaction.QueueCloak(window->GetHwnd(), FALSE);
      }
      ctx->transaction.QueueRelayout(aws);
   }

   BFWMTransactionCommit(ctx);
}

inline void FinalizeRegistrySwap(struct BFWMContext *ctx,
                                 MonitorRegistry *new_reg) {
   size_t write = 0;
   for (size_t j = 0; j < new_reg->Size(); j++) {
      if (new_reg->At(j) != nullptr)
         new_reg->SetAt(write++, new_reg->At(j));
   }
   new_reg->Resize(write);

   ctx->monitors->ReplaceWith(new_reg->TakeMonitors());
}

inline void RestoreFocusState(struct BFWMContext *ctx) {
   if (ctx->focused_hwnd != nullptr) {
      if (IsWindow(ctx->focused_hwnd) == TRUE) {
         Workspace *workspace = FindWorkspaceByHwnd(ctx, ctx->focused_hwnd);
         if ((workspace != nullptr) && workspace != ctx->focused_workspace)
            ctx->focused_workspace = workspace;
         return;
      }
      ctx->focused_hwnd = nullptr;
   }

   if ((ctx->focused_workspace != nullptr) &&
       !ctx->focused_workspace->IsEmpty()) {
      HWND first = nullptr;
      if (ctx->focused_workspace->GetEngine() != nullptr) {
         first =
             ctx->focused_workspace->GetEngine()->get_closest_window(nullptr);
      }
      if (first != nullptr) {
         FocusWindow(first, ctx);
         return;
      }
   }

   for (size_t m = 0; m < ctx->monitors->Size(); m++) {
      Monitor *mon = ctx->monitors->At(m);
      if ((mon == nullptr) || (mon->GetActiveWorkspace() == nullptr) ||
          mon->GetActiveWorkspace()->IsEmpty())
         continue;
      HWND first = nullptr;
      if (mon->GetActiveWorkspace()->GetEngine() != nullptr) {
         first = mon->GetActiveWorkspace()->GetEngine()->get_closest_window(
             nullptr);
      }
      if (first != nullptr) {
         ctx->focused_workspace = mon->GetActiveWorkspace();
         FocusWindow(first, ctx);
         return;
      }
   }
}

/* RAII guard: frees Monitor* objects still owned by a MonitorRegistry when
 * the registry is discarded (early-return paths in ReconcileMonitors). On the
 * success path FinalizeRegistrySwap transfers ownership to ctx->monitors and
 * the registry's vector is cleared, so nothing is double-freed. */
struct MonitorRegistryCleanup {
 private:
   MonitorRegistry &reg;

 public:
   explicit MonitorRegistryCleanup(MonitorRegistry &registry) : reg(registry) {}
   ~MonitorRegistryCleanup() {
      for (Monitor *mon : reg.Monitors()) {

         delete mon;
      }
      reg.Clear();
   }
};

} // namespace

// ============================================================
// MONITOR MEMBER FUNCTIONS
// ============================================================

void Monitor::Init(BFWMContext *ctx) {
   workspaces_.clear();
   active_workspace_ = nullptr;
   /* bar_height config is logical; store it scaled to this monitor's physical
    * pixels (the value reserved from the work area). */
   bar_height_ =
       (ctx->dpi != nullptr)
           ? ctx->dpi->ScaleForMonitor(handle_, ctx->config.bar_height)
           : ctx->config.bar_height;
   fullscreen_count_ = 0;
   bar_.reset();
}

auto Monitor::IsDisabled(BFWMContext *ctx) const -> BOOL {
   for (int d = 0; d < ctx->disabled_uid_count; d++) {
      if (uid_ == ctx->disabled_uids[d])
         return TRUE;
   }
   return FALSE;
}

auto Monitor::CreateWorkspace(BFWMContext *ctx, size_t identifier)
    -> Workspace * {
   std::wstring const name = L"workspace_" + std::to_wstring(identifier);
   auto *workspace = CreateWorkspaceForMonitor(ctx, identifier, &name, this);
   if (workspace == nullptr)
      return nullptr;
   workspace->ApplyConfig(ctx, identifier);
   workspace->SetOriginMonitorUid(uid_);
   workspaces_.push_back(workspace);
   active_workspace_ = workspace;
   return workspace;
}

/* For each newly-detected monitor, populate workspaces by priority:
 * 1. pinned (assigned_monitor in config),
 * 2. origin_monitor_uid match (reconnected monitor),
 * 3. fresh workspace. */
auto Monitor::TryPinnedWorkspaces(BFWMContext *ctx) -> BOOL {
   std::array<Workspace *, PINNED_WS_MAX> pinned_ws = {};
   int const pinned_count = FindPinnedWorkspaces(
       ctx, display_number_, pinned_ws.data(), PINNED_WS_MAX);
   if (pinned_count == 0)
      return FALSE;
   for (int p = 0; p < pinned_count; p++)
      pinned_ws[p]->MoveToMonitor(this, ctx);
   active_workspace_ = pinned_ws[0];
   return TRUE;
}

auto Monitor::TryOriginWorkspaces(BFWMContext *ctx) -> BOOL {
   std::array<Workspace *, ORIGIN_WS_MAX> origin_ws = {};
   int origin_count = 0;
   for (size_t m = 0; m < ctx->monitors->Size() && origin_count < ORIGIN_WS_MAX;
        m++) {
      Monitor *src = ctx->monitors->At(m);
      for (size_t w = 0;
           w < src->Workspaces().size() && origin_count < ORIGIN_WS_MAX; w++) {
         Workspace *candidate = src->Workspaces()[w];
         if (!candidate->GetOriginMonitorUid().empty() &&
             candidate->GetOriginMonitorUid() == uid_) {
            origin_ws[origin_count++] = candidate;
         }
      }
   }
   if (origin_count == 0)
      return FALSE;
   for (int p = 0; p < origin_count; p++)
      origin_ws[p]->MoveToMonitor(this, ctx);
   active_workspace_ = origin_ws[0];
   return TRUE;
}

auto Monitor::TryFreshWorkspace(BFWMContext *ctx) -> BOOL {
   size_t const new_id = FindUnassignedWorkspaceId(ctx, this);
   return static_cast<BOOL>(CreateWorkspace(ctx, new_id) != nullptr);
}

auto Monitor::SetupNewWorkspaces(BFWMContext *ctx) -> BOOL {
   return static_cast<BOOL>(TryPinnedWorkspaces(ctx) == TRUE ||
                            (TryOriginWorkspaces(ctx) == TRUE) ||
                            (TryFreshWorkspace(ctx) == TRUE));
}

void Monitor::SetBar(std::unique_ptr<Bar> b) { bar_ = std::move(b); }
void Monitor::ResetBar() { bar_.reset(); }

void MonitorRegistry::SortByPosition() {
   std::ranges::sort(monitors_, SortMonitorByPosition);
}

auto FindNeighbouringMonitor(struct BFWMContext *ctx, Monitor *src,
                             BFWMDirection direction) -> Monitor * {
   if ((ctx == nullptr) || (src == nullptr))
      return nullptr;

   const auto &monitors = ctx->monitors->Monitors();
   size_t const mon_count = monitors.size();
   if (mon_count < 2)
      return nullptr;

   if (direction == DirNext || direction == DirPrev)
      return FindNextPrevMonitor(monitors.data(), mon_count, src, direction);

   RECT const src_r = src->GetWorkArea();
   Monitor *best = nullptr;
   int best_dist = INT_MAX;

   for (size_t m = 0; m < mon_count; m++) {
      Monitor *mon = monitors[m];
      if (mon == src)
         continue;

      int dist;
      if ((RectIsNeighbor(src_r, mon->GetWorkArea(), direction, &dist) ==
           TRUE) &&
          dist < best_dist) {
         best_dist = dist;
         best = mon;
      }
   }

   return best;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void ReconcileMonitors(struct BFWMContext *ctx) {
   if (ctx == nullptr)
      return;

   ScopedLock const lock(ctx->lock);
   Info("ReconcileMonitors: re-enumerating displays");

   // Snapshot the pre-reconcile monitor handles: a topology change (add or
   // remove) must recreate the worker's per-monitor DPI watcher windows, but a
   // pure DPI change must not (the watchers survive and keep reporting).
   std::vector<HMONITOR> handles_before;
   handles_before.reserve(ctx->monitors->Size());
   for (Monitor *mon : ctx->monitors->Monitors()) {
      if (mon != nullptr)
         handles_before.push_back(mon->GetHandle());
   }

   // Snapshot the pre-reconcile DPI registry keyed by stable monitor identity
   // (PnP device ID): a runtime scaling change (e.g. Windows 150% → 100%) must
   // be detectable after the registry is rebuilt below, because a monitor's bar
   // otherwise keeps its startup geometry forever (BAR_UPDATE_DIRTY only
   // repaints; it does not recreate). Keying by uid (not handle) also catches a
   // disconnected-then-reconnected monitor that comes back with a new handle.
   std::vector<std::pair<std::wstring, UINT>> old_dpis;
   if (ctx->dpi != nullptr) {
      for (Monitor *mon : ctx->monitors->Monitors()) {
         if ((mon == nullptr) || mon->GetUid().empty())
            continue;
         UINT const dpi = ctx->dpi->GetDpi(mon->GetHandle());
         if (dpi == 0)
            continue; // not registered — nothing to compare
         old_dpis.emplace_back(mon->GetUid(), dpi);
      }
      // The DPI registry is rebuilt from scratch each reconcile.
      ctx->dpi->Clear();
   }

   MonitorRegistry new_reg;
   MonitorRegistryCleanup const new_reg_cleanup(new_reg);

   if (EnumerateMonitors(ctx, &new_reg) == FALSE) {
      return;
   }

   RemoveDisabledMonitors(ctx, &new_reg);
   if (new_reg.Empty()) {
      Warn("ReconcileMonitors: no enabled monitors found, keeping existing");
      return;
   }

   std::vector<BOOL> const old_matched =
       MatchExistingMonitors(ctx, &new_reg, old_dpis);

   HandleDisconnectedMonitors(ctx, old_matched);
   CompactMonitorArray(ctx, old_matched);
   SetupNewMonitors(ctx, &new_reg);
   RestoreDonorMonitors(ctx);
   CommitMonitorVisibility(ctx, &new_reg);
   FinalizeRegistrySwap(ctx, &new_reg);
   new_reg.Clear(); // ownership transferred to ctx->monitors
   RestoreFocusState(ctx);

   // Detect a runtime DPI change on any surviving monitor: enumeration above
   // re-registered every monitor from a fresh GetDpiForMonitor, so a value
   // differing from the pre-clear snapshot means the scaling changed. The main
   // loop owns the SetBarHeight + RecalculateAllWorkspaceRects + RECREATE
   // reaction (ctx->dpi_update); pure topology changes keep the DIRTY repaint.
   bool dpi_changed = false;
   if (ctx->dpi != nullptr) {
      for (Monitor *mon : ctx->monitors->Monitors()) {
         if ((mon == nullptr) || mon->GetUid().empty())
            continue;
         UINT const new_dpi = ctx->dpi->GetDpi(mon->GetHandle());
         if (new_dpi == 0)
            continue; // not registered — nothing to compare
         for (const auto &old : old_dpis) {
            if (old.first == mon->GetUid()) {
               if (old.second != new_dpi)
                  dpi_changed = true;
               break;
            }
         }
         if (dpi_changed)
            break;
      }
   }

   if (dpi_changed) {
      ctx->dpi_update = TRUE;
   } else {
      BarUpdateRequest(ctx, BAR_UPDATE_DIRTY);
   }

   // Monitor topology may have changed, so have the worker thread recreate its
   // hidden per-monitor DPI watcher windows. Gated on an actual topology change
   // (a pure DPI change must not churn the watchers) and on the worker thread
   // being alive (never post to thread 0).
   std::vector<HMONITOR> handles_after;
   handles_after.reserve(ctx->monitors->Size());
   for (Monitor *mon : ctx->monitors->Monitors()) {
      if (mon != nullptr)
         handles_after.push_back(mon->GetHandle());
   }
   bool const topology_changed = (handles_after != handles_before);
   if (topology_changed && ctx->worker_thread != 0) {
      PostThreadMessage(GetThreadId((HANDLE)ctx->worker_thread),
                        WM_APP_DPI_WATCHER_RECREATE, 0, 0);
   }

   Info("ReconcileMonitors: complete (%zu monitors)", ctx->monitors->Size());
}
