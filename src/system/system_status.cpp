#include "system_status.h"

#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <netlistmgr.h>
#include <windows.h>
#include <winerror.h>
#include <wlanapi.h>

#include <algorithm>
#include <cstring>

// MinGW's SDK headers *declare* the COM interface GUIDs
// (IID_IMMDeviceEnumerator, IID_IAudioEndpointVolume,
// IID_IMMNotificationClient, IID_INetworkListManager, CLSID_NetworkListManager)
// but do not define them, and CLSID_MMDeviceEnumerator is missing entirely.
// Provide the definitions here via <initguid.h>.
//
// The interfaces themselves are the native C++ abstract classes from the SDK
// headers (this file is compiled as C++ with no CINTERFACE), so we call methods
// directly and derive the notification sink from IMMNotificationClient instead
// of hand-rolling vtable boilerplate.
#include <initguid.h>
#include <wlantypes.h>
DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D,
            0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
DEFINE_GUID(IID_IMMDeviceEnumerator, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46,
            0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
DEFINE_GUID(IID_IAudioEndpointVolume, 0x5CDF2C82, 0x841E, 0x4546, 0x97, 0x22,
            0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A);
DEFINE_GUID(IID_IMMNotificationClient, 0x7991EEC9, 0x7E89, 0x4D85, 0x83, 0x90,
            0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0);
DEFINE_GUID(CLSID_NetworkListManager, 0xDCB00C01, 0x570F, 0x4A9B, 0x8D, 0x69,
            0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B);
DEFINE_GUID(IID_INetworkListManager, 0xDCB00000, 0x570F, 0x4A9B, 0x8D, 0x69,
            0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B);

namespace {

// Flips when the default audio endpoint (or endpoint state) changes.
volatile bool g_audio_dirty = false;

// Minimal RAII COM pointer.  Holds at most one reference; copyless.
template <typename T> class ComPtr {
 public:
   ComPtr() noexcept = default;
   ~ComPtr() { Reset(); }

   ComPtr(const ComPtr &) = delete;
   auto operator=(const ComPtr &) -> ComPtr & = delete;

   [[nodiscard]] auto Get() const noexcept -> T * { return ptr_; }
   auto operator->() const noexcept -> T * { return ptr_; }
   explicit operator bool() const noexcept { return ptr_ != nullptr; }
   auto GetAddressOf() noexcept -> T ** { return &ptr_; }

   void Reset() noexcept {
      if (ptr_) {
         ptr_->Release();
         ptr_ = nullptr;
      }
   }

 private:
   T *ptr_ = nullptr;
};

// RAII wrapper around a WLAN client handle (WlanCloseHandle on destruction).
class WlanHandle {
 public:
   WlanHandle() noexcept = default;
   ~WlanHandle() { Close(); }

   WlanHandle(const WlanHandle &) = delete;
   auto operator=(const WlanHandle &) -> WlanHandle & = delete;

   [[nodiscard]] auto Get() const noexcept -> HANDLE { return handle_; }
   auto GetAddressOf() noexcept -> HANDLE * { return &handle_; }
   explicit operator bool() const noexcept { return handle_ != nullptr; }

   void Close() noexcept {
      if (handle_ != nullptr) {
         WlanCloseHandle(handle_, nullptr);
         handle_ = nullptr;
      }
   }

 private:
   HANDLE handle_ = nullptr;
};

// Audio device notification sink (IMMNotificationClient).
class AudioNotificationClient final : public IMMNotificationClient {
 public:
   AudioNotificationClient() noexcept = default;

   // IUnknown
   // NOLINTBEGIN
   HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                            void **ppv) noexcept override {
      if (IsEqualIID(iid, IID_IUnknown) ||
          IsEqualIID(iid, IID_IMMNotificationClient)) {
         *ppv = this;
         return S_OK;
      }
      *ppv = nullptr;
      return E_NOINTERFACE;
   }
   ULONG STDMETHODCALLTYPE AddRef() noexcept override {
      ++refs_;
      return refs_;
   }
   ULONG STDMETHODCALLTYPE Release() noexcept override { return --refs_; }

   // IMMNotificationClient
   HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(
       LPCWSTR pwstrDeviceId, DWORD dwNewState) noexcept override {
      (void)pwstrDeviceId;
      (void)dwNewState;
      return S_OK;
   }
   HRESULT STDMETHODCALLTYPE
   OnDeviceAdded(LPCWSTR pwstrDeviceId) noexcept override {
      (void)pwstrDeviceId;
      return S_OK;
   }
   HRESULT STDMETHODCALLTYPE
   OnDeviceRemoved(LPCWSTR pwstrDeviceId) noexcept override {
      (void)pwstrDeviceId;
      return S_OK;
   }
   HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(
       EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId) noexcept override {
      (void)flow;
      (void)role;
      (void)pwstrDeviceId;
      g_audio_dirty = true;
      return S_OK;
   }
   HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(
       LPCWSTR pwstrDeviceId, const PROPERTYKEY key) noexcept override {
      (void)pwstrDeviceId;
      (void)key;
      return S_OK;
   }
   // NOLINTEND

 private:
   ULONG refs_ = 1;
};

// ---- Volume state ----
ComPtr<IMMDeviceEnumerator> g_vol_enum;
ComPtr<IAudioEndpointVolume> g_vol;
AudioNotificationClient g_audio_notif;

// ---- Network state ----
ComPtr<INetworkListManager> g_net_mgr;
WlanHandle g_wlan_handle;

constexpr int kBit32Shift = 32;

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void SystemStatusInit() {
   HRESULT result = CoCreateInstance(
       CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, IID_IMMDeviceEnumerator,
       reinterpret_cast<void **>(g_vol_enum.GetAddressOf()));
   if (SUCCEEDED(result) && g_vol_enum) {
      g_vol_enum->RegisterEndpointNotificationCallback(&g_audio_notif);

      ComPtr<IMMDevice> device;
      result = g_vol_enum->GetDefaultAudioEndpoint(eRender, eMultimedia,
                                                   device.GetAddressOf());
      if (SUCCEEDED(result) && device) {
         device->Activate(IID_IAudioEndpointVolume, CLSCTX_ALL, nullptr,
                          reinterpret_cast<void **>(g_vol.GetAddressOf()));
      }
   } else {
      g_vol_enum.Reset();
   }

   result = CoCreateInstance(
       CLSID_NetworkListManager, nullptr, CLSCTX_ALL, IID_INetworkListManager,
       reinterpret_cast<void **>(g_net_mgr.GetAddressOf()));
   if (FAILED(result))
      g_net_mgr.Reset();

   DWORD wlan_ver = 0;
   if (WlanOpenHandle(2, nullptr, &wlan_ver, g_wlan_handle.GetAddressOf()) !=
       ERROR_SUCCESS)
      g_wlan_handle.Close();
}

void SystemStatusShutdown() {
   g_vol.Reset();
   if (g_vol_enum) {
      g_vol_enum->UnregisterEndpointNotificationCallback(&g_audio_notif);
      g_vol_enum.Reset();
   }
   g_net_mgr.Reset();
   g_wlan_handle.Close();
}

void SystemStatusReinitAudio() {
   g_vol.Reset();
   if (g_vol_enum) {
      g_vol_enum->UnregisterEndpointNotificationCallback(&g_audio_notif);
      g_vol_enum.Reset();
   }
   HRESULT result = CoCreateInstance(
       CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, IID_IMMDeviceEnumerator,
       reinterpret_cast<void **>(g_vol_enum.GetAddressOf()));
   if (SUCCEEDED(result) && g_vol_enum) {
      g_vol_enum->RegisterEndpointNotificationCallback(&g_audio_notif);

      ComPtr<IMMDevice> device;
      result = g_vol_enum->GetDefaultAudioEndpoint(eRender, eMultimedia,
                                                   device.GetAddressOf());
      if (SUCCEEDED(result) && device) {
         device->Activate(IID_IAudioEndpointVolume, CLSCTX_ALL, nullptr,
                          reinterpret_cast<void **>(g_vol.GetAddressOf()));
      }
   } else {
      g_vol_enum.Reset();
   }
   g_audio_dirty = false;
}

auto SystemGetVolumeStatus() -> VolumeStatus {
   VolumeStatus volume_status = {};
   if (g_audio_dirty)
      SystemStatusReinitAudio();
   if (!g_vol)
      return volume_status;
   volume_status.available = TRUE;
   g_vol->GetMasterVolumeLevelScalar(&volume_status.level);
   g_vol->GetMute(&volume_status.muted);
   return volume_status;
}

auto SystemGetNetworkStatus() -> NetworkStatus {
   NetworkStatus network_status = {};

   // --- Check overall connectivity via INetworkListManager ---
   BOOL connected = FALSE;
   if (g_net_mgr) {
      VARIANT_BOOL variant_bool = VARIANT_FALSE;
      g_net_mgr->IsConnectedToInternet(&variant_bool);
      connected = static_cast<BOOL>(variant_bool == VARIANT_TRUE);
   }

   if (connected == 0) {
      network_status.type = NET_DISCONNECTED;
      network_status.available = TRUE;
      return network_status;
   }

   // --- Check WiFi via WlanApi ---
   if (g_wlan_handle) {
      PWLAN_INTERFACE_INFO_LIST iflist = nullptr;
      if (WlanEnumInterfaces(g_wlan_handle.Get(), nullptr, &iflist) ==
              ERROR_SUCCESS &&
          (iflist != nullptr) && iflist->dwNumberOfItems > 0) {
         for (DWORD i = 0; i < iflist->dwNumberOfItems; i++) {
            WLAN_INTERFACE_INFO *info = &iflist->InterfaceInfo[i];
            if (info->isState == wlan_interface_state_connected) {
               // Connected to a WiFi network — get SSID and signal
               PWLAN_CONNECTION_ATTRIBUTES conn = nullptr;
               DWORD conn_sz = sizeof(WLAN_CONNECTION_ATTRIBUTES);
               WLAN_INTF_OPCODE const operation =
                   wlan_intf_opcode_current_connection;
               if (WlanQueryInterface(g_wlan_handle.Get(), &info->InterfaceGuid,
                                      operation, nullptr, &conn_sz,
                                      reinterpret_cast<PVOID *>(&conn),
                                      nullptr) == ERROR_SUCCESS &&
                   (conn != nullptr)) {
                  // Copy SSID
                  DOT11_SSID ssid = conn->wlanAssociationAttributes.dot11Ssid;
                  int const ssid_len = std::min(
                      static_cast<int>(ssid.uSSIDLength), MAX_SSID_LEN - 1);
                  network_status.ssid.assign(
                      reinterpret_cast<const char *>(ssid.ucSSID),
                      static_cast<size_t>(ssid_len));

                  // Signal quality 0-100
                  network_status.signal_percent = static_cast<int>(
                      conn->wlanAssociationAttributes.wlanSignalQuality);
                  WlanFreeMemory(conn);
               }
               network_status.type = NET_WIFI;
               network_status.available = TRUE;
               WlanFreeMemory(iflist);
               return network_status;
            }
         }
      }
      if (iflist != nullptr)
         WlanFreeMemory(iflist);
   }

   // Connected but no WiFi — assume ethernet
   network_status.type = NET_ETHERNET;
   network_status.available = TRUE;
   return network_status;
}

auto SystemGetCpuStatus() -> CpuStatus {
   CpuStatus cpu_status = {};
   /* Previous-tick CPU counters persist across calls (function-local
    * statics: single-user of the delta math below). */
   static ULONGLONG prev_idle = 0;
   static ULONGLONG prev_kernel = 0;
   static ULONGLONG prev_user = 0;
   static bool prev_valid = false;
   FILETIME idle;
   FILETIME kernel;
   FILETIME user;

   if (GetSystemTimes(&idle, &kernel, &user) == 0)
      return cpu_status;

   ULONGLONG const idle_t =
       ((ULONGLONG)idle.dwHighDateTime << kBit32Shift) | idle.dwLowDateTime;
   ULONGLONG const kernel_t =
       ((ULONGLONG)kernel.dwHighDateTime << kBit32Shift) | kernel.dwLowDateTime;
   ULONGLONG const user_t =
       ((ULONGLONG)user.dwHighDateTime << kBit32Shift) | user.dwLowDateTime;

   if (!prev_valid) {
      prev_idle = idle_t;
      prev_kernel = kernel_t;
      prev_user = user_t;
      prev_valid = true;
      cpu_status.available = TRUE;
      cpu_status.load = 0.0F;
      return cpu_status;
   }

   ULONGLONG const idle_diff = idle_t - prev_idle;
   ULONGLONG const kernel_diff = kernel_t - prev_kernel;
   ULONGLONG const user_diff = user_t - prev_user;
   ULONGLONG const total_diff = kernel_diff + user_diff;

   prev_idle = idle_t;
   prev_kernel = kernel_t;
   prev_user = user_t;

   if (total_diff > 0) {
      auto busy = (double)(total_diff - idle_diff);
      cpu_status.load = (float)(busy * 100.0 / (double)total_diff);
      cpu_status.load = std::max(cpu_status.load, 0.0F);
      cpu_status.load = std::min(cpu_status.load, 100.0F);
   }

   cpu_status.available = TRUE;
   return cpu_status;
}

auto SystemGetMemoryStatus() -> MemoryStatus {
   MemoryStatus mem_status = {};
   MEMORYSTATUSEX msx;
   msx.dwLength = sizeof(msx);

   if (GlobalMemoryStatusEx(&msx) == 0)
      return mem_status;

   mem_status.load = (float)msx.dwMemoryLoad;
   mem_status.total_bytes = msx.ullTotalPhys;
   mem_status.available_bytes = msx.ullAvailPhys;
   mem_status.available = TRUE;
   return mem_status;
}
