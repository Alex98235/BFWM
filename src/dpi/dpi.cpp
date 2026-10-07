/**
 * @file dpi.cpp
 * @brief Per-monitor DPI system implementation.
 */

#include "dpi.h"

#include <cassert>
#include <shellscalingapi.h>

auto DpiSystem::Init() -> BOOL {
   return SetProcessDpiAwarenessContext(
       DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

auto DpiSystem::IsPerMonitorAware() -> BOOL {
   PROCESS_DPI_AWARENESS awareness = PROCESS_DPI_UNAWARE;
   if (GetProcessDpiAwareness(nullptr, &awareness) != S_OK)
      return FALSE;
   return (awareness == PROCESS_PER_MONITOR_DPI_AWARE) ? TRUE : FALSE;
}

auto DpiSystem::Scale(int logical, UINT dpi) -> int {
   if (logical >= 0) {
      return ((logical * (int)dpi) + DPI_HALF_DEFAULT_SCALING) /
             DPI_DEFAULT_SCALING;
   }
   // Sign-symmetric rounding: round the magnitude half-up, then negate.
   return -((((-logical) * (int)dpi) + DPI_HALF_DEFAULT_SCALING) /
            DPI_DEFAULT_SCALING);
}

auto DpiSystem::ScaleF(float logical, UINT dpi) -> float {
   return logical * (float)dpi / DPI_DEFAULT_SCALING_F;
}

auto DpiSystem::ScaleRect(RECT logical, UINT dpi) -> RECT {
   RECT scaled = {
       .left = Scale(logical.left, dpi),
       .top = Scale(logical.top, dpi),
       .right = Scale(logical.right, dpi),
       .bottom = Scale(logical.bottom, dpi),
   };
   return scaled;
}

auto DpiSystem::Unscale(int physical, UINT dpi) -> int {
   return ((physical * DPI_DEFAULT_SCALING) + ((int)dpi / 2)) / (int)dpi;
}

void DpiSystem::RegisterMonitor(HMONITOR hmon, UINT dpi) {
   for (Entry &entry : entries_) {
      if (entry.hmon == hmon) {
         entry.dpi = dpi;
         return;
      }
   }
   entries_.push_back({.hmon = hmon, .dpi = dpi});
}

void DpiSystem::Clear() { entries_.clear(); }

auto DpiSystem::Count() const -> size_t { return entries_.size(); }

auto DpiSystem::MonitorAt(size_t index) const -> HMONITOR {
   if (index >= entries_.size())
      return nullptr;
   return entries_[index].hmon;
}

auto DpiSystem::GetDpi(HMONITOR hmon) const -> UINT {
   assert(hmon != nullptr);
   for (const Entry &entry : entries_) {
      if (entry.hmon == hmon)
         return entry.dpi;
   }
#ifdef DEBUG
   // A miss against a populated registry is a bug (stale handle or an
   // unregistered monitor). A miss against an empty registry is a benign
   // first-use (e.g. before the first reconcile) — suppress.
   assert(entries_.empty());
#endif
   return 0;
}

auto DpiSystem::GetScale(HMONITOR hmon) const -> float {
   return (float)GetDpi(hmon) / BaseDpi();
}

auto DpiSystem::ScaleForMonitor(HMONITOR hmon, int logical) const -> int {
   UINT const dpi = GetDpi(hmon);
   if (dpi == 0)
      return logical; // unknown monitor — identity (no scaling)
   return Scale(logical, dpi);
}
