/**
 * @file system_indicator_mapping.h
 * @brief Pure status -> IndicatorRuntime mappers for the built-in indicators.
 *
 * These functions hold the *exact* value-bag/state semantics the bar has always
 * rendered. They are deliberately free of Windows polling (the caller passes a
 * status snapshot) so the mapping can be unit-tested deterministically, and
 * they are the single source of truth shared by the indicator providers.
 */

#ifndef BFWM_SYSTEM_INDICATOR_MAPPING_H
#define BFWM_SYSTEM_INDICATOR_MAPPING_H

#include "../system/system_status.h"
#include "bar.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

namespace bar_indicator_mapping {

/// Add a numeric value to a runtime's value bag.
inline void AddNumericValue(IndicatorRuntime &runtime, const char *name,
                            double value, int precision = 0) {
   IndicatorValue entry;
   entry.name = name;
   entry.is_num = true;
   entry.num = value;
   entry.precision = precision;
   runtime.values.push_back(std::move(entry));
}

/// Add a string value to a runtime's value bag.
inline void AddStringValue(IndicatorRuntime &runtime, const char *name,
                           const std::string &value) {
   IndicatorValue entry;
   entry.name = name;
   entry.is_num = false;
   entry.str = value;
   runtime.values.push_back(std::move(entry));
}

/// Volume: `value` is level*100 (0 decimals); muted sets the "muted" state.
inline void ApplyVolumeStatus(const VolumeStatus &status,
                              IndicatorRuntime &runtime) {
   if (status.available == 0) {
      runtime.valid = false;
      return;
   }
   AddNumericValue(runtime, "value", static_cast<double>(status.level) * 100.0,
                   0);
   if (status.muted != 0) {
      runtime.state = "muted";
   }
   runtime.valid = true;
}

/// Network: disconnected/ethernet set a state and value 0; wifi uses signal %.
inline void ApplyNetworkStatus(const NetworkStatus &status,
                               IndicatorRuntime &runtime) {
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

/// CPU: `value` is the load percentage (0 decimals).
inline void ApplyCpuStatus(const CpuStatus &status, IndicatorRuntime &runtime) {
   if (status.available == 0) {
      runtime.valid = false;
      return;
   }
   AddNumericValue(runtime, "value", static_cast<double>(status.load), 0);
   runtime.valid = true;
}

/// Memory: value/used_gb/total_gb/avail_gb in gigabytes (1 decimal).
inline void ApplyMemoryStatus(const MemoryStatus &status,
                              IndicatorRuntime &runtime) {
   if (status.available == 0) {
      runtime.valid = false;
      return;
   }
   constexpr double kBytesPerGb = 1024.0 * 1024.0 * 1024.0;
   double const to_gb = 1.0 / kBytesPerGb;
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

/// Clock: format the given local time into `runtime.text`.
///
/// Empty format falls back to a zero-padded `HH:MM` (matching the historical
/// default, which did not go through strftime).
inline void FormatClock(const SYSTEMTIME &local_time, const std::string &format,
                        IndicatorRuntime &runtime) {
   constexpr int kYearBase = 1900;
   if (!format.empty()) {
      struct tm tm_time = {};
      tm_time.tm_sec = local_time.wSecond;
      tm_time.tm_min = local_time.wMinute;
      tm_time.tm_hour = local_time.wHour;
      tm_time.tm_mday = local_time.wDay;
      tm_time.tm_mon = local_time.wMonth - 1;
      tm_time.tm_year = local_time.wYear - kYearBase;
      tm_time.tm_wday = local_time.wDayOfWeek;
      tm_time.tm_isdst = -1;
      runtime.text.resize(64);
      strftime(runtime.text.data(), runtime.text.size(), format.c_str(),
               &tm_time);
      runtime.text.resize(std::strlen(runtime.text.c_str()));
   } else {
      runtime.text.resize(64);
      snprintf(runtime.text.data(), runtime.text.size(), "%02d:%02d",
               local_time.wHour, local_time.wMinute);
      runtime.text.resize(std::strlen(runtime.text.c_str()));
   }
   runtime.valid = true;
}

} // namespace bar_indicator_mapping

#endif
