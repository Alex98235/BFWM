/**
 * @file system_status.h
 * @brief System status polling (volume, network, CPU, memory).
 *
 * Provides functions to poll various system metrics for display in
 * the status bar. These are rate-limited by the bar's poll timers.
 */

#ifndef BFWM_SYSTEM_STATUS_H
#define BFWM_SYSTEM_STATUS_H

#include <string>
#include <windows.h>

/** @brief Maximum SSID string length (including null terminator). */
enum { MAX_SSID_LEN = 64 };

/**
 * @brief System audio volume status.
 */
using VolumeStatus = struct {
   /// Volume level 0.0 to 1.0
   float level;
   /// Whether the master volume is muted
   BOOL muted;
   /// Whether audio information is available
   BOOL available;
};

/**
 * @brief Network connection type.
 */
using NetworkType = enum {
   /// No network connection
   NET_DISCONNECTED,
   /// Wired Ethernet connection
   NET_ETHERNET,
   /// Wireless (Wi-Fi) connection
   NET_WIFI,
};

/**
 * @brief Network connection status.
 */
using NetworkStatus = struct NetworkStatus {
   /// Connection type
   NetworkType type;
   /// Signal strength 0-100 (valid for NET_WIFI)
   int signal_percent;
   /// SSID (empty if not Wi-Fi)
   std::string ssid;
   /// Whether network information is available
   BOOL available;
};

/**
 * @brief CPU load status.
 */
using CpuStatus = struct {
   /// CPU utilisation 0.0 to 100.0
   float load;
   /// Whether CPU information is available
   BOOL available;
};

/**
 * @brief Memory usage status.
 */
using MemoryStatus = struct {
   /// Memory utilisation 0.0 to 100.0 (dwMemoryLoad)
   float load;
   /// Total physical memory in bytes
   DWORD64 total_bytes;
   /// Available physical memory in bytes
   DWORD64 available_bytes;
   /// Whether memory information is available
   BOOL available;
};

/**
 * @brief Initialise the system status subsystem.
 *
 * Opens audio endpoint, WLAN handle, and performance counters.
 */
void SystemStatusInit();

/**
 * @brief Shut down the system status subsystem and free resources.
 */
void SystemStatusShutdown();

/**
 * @brief Re-initialise audio endpoints (e.g. after resume).
 */
void SystemStatusReinitAudio();

/**
 * @brief Poll the current system volume level.
 * @return The current VolumeStatus
 */
auto SystemGetVolumeStatus() -> VolumeStatus;

/**
 * @brief Poll the current network status.
 * @return The current NetworkStatus
 */
auto SystemGetNetworkStatus() -> NetworkStatus;

/**
 * @brief Poll the current CPU utilisation.
 * @return The current CpuStatus
 */
auto SystemGetCpuStatus() -> CpuStatus;

/**
 * @brief Poll the current memory utilisation.
 * @return The current MemoryStatus
 */
auto SystemGetMemoryStatus() -> MemoryStatus;

#endif
