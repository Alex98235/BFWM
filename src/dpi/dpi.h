/**
 * @file dpi.h
 * @brief Per-monitor DPI system — single owner of DPI truth, scaling math,
 *        and process awareness.
 */

#ifndef BFWM_DPI_H
#define BFWM_DPI_H

#include <vector>
#include <windows.h>

namespace {
const int DPI_HALF_DEFAULT_SCALING = 48;
const int DPI_DEFAULT_SCALING = 96;
const float DPI_DEFAULT_SCALING_F = 96.0F;
} // namespace

/**
 * @brief Per-monitor DPI system — single owner of DPI truth, scaling math,
 *        and process awareness.
 *
 * Owns the per-monitor DPI registry (populated from GetDpiForMonitor during
 * monitor enumeration), provides pure scaling math, and declares the process
 * DPI awareness context. Instance state is heap-owned by BFWMContext.
 */
class DpiSystem {
 public:
   /**
    * @brief Declare the process DPI awareness context (Per-Monitor V2).
    *
    * Call once, first thing in main(), before any window creation or GDI
    * use. Calls SetProcessDpiAwarenessContext(
    * DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2).
    *
    * @return FALSE on failure (the manifest still declares awareness)
    */
   static auto Init() -> BOOL;

   /**
    * @brief Query whether the process currently runs per-monitor aware.
    *
    * Wraps GetProcessDpiAwareness (the legacy enum cannot distinguish PMv1
    * from PMv2, so this is TRUE for either). Useful after a failed Init() to
    * distinguish "already aware via the manifest" from "not aware at all".
    *
    * @return TRUE when per-monitor aware, FALSE otherwise or on query failure
    */
   static auto IsPerMonitorAware() -> BOOL;

   /**
    * @brief Base (100%) DPI used as the scaling reference.
    *
    * @return 96
    */
   static constexpr auto BaseDpi() -> UINT { return DPI_DEFAULT_SCALING; }

   /**
    * @brief Scale a logical value to physical pixels for a DPI.
    *
    * Round-half-up: (logical * dpi + 48) / 96 for non-negative input;
    * sign-symmetric rounding for negatives. Never returns negative for
    * non-negative input.
    *
    * @param logical Logical (96-DPI) value
    * @param dpi     Target DPI
    * @return Scaled physical value
    */
   static auto Scale(int logical, UINT dpi) -> int;

   /**
    * @brief Scale a logical float value to physical pixels for a DPI.
    *
    * @param logical Logical (96-DPI) value
    * @param dpi     Target DPI
    * @return Scaled physical value
    */
   static auto ScaleF(float logical, UINT dpi) -> float;

   /**
    * @brief Scale all four fields of a logical RECT to physical pixels.
    *
    * @param logical Logical (96-DPI) rectangle
    * @param dpi     Target DPI
    * @return Scaled physical rectangle
    */
   static auto ScaleRect(RECT logical, UINT dpi) -> RECT;

   /**
    * @brief Inverse scale: physical pixels back to logical (96-DPI) values.
    *
    * Round-half-up: (physical * 96 + dpi/2) / dpi.
    *
    * @param physical Physical pixel value
    * @param dpi      Source DPI
    * @return Logical (96-DPI) value
    */
   static auto Unscale(int physical, UINT dpi) -> int;

   /**
    * @brief Upsert a monitor's DPI into the registry.
    *
    * Replaces the existing entry when the monitor is already registered.
    *
    * @param hmon Monitor handle
    * @param dpi  Effective DPI for the monitor
    */
   void RegisterMonitor(HMONITOR hmon, UINT dpi);

   /**
    * @brief Drop all registry entries (the registry is rebuilt from scratch
    *        each reconcile).
    */
   void Clear();

   /**
    * @brief Number of monitors currently registered.
    *
    * Registry introspection for the composition root: ReconcileMonitors uses
    * it (with MonitorAt) to snapshot the pre-reconcile DPIs so a runtime
    * scaling change can be detected after the registry is rebuilt.
    *
    * @return The number of registered monitor entries
    */
   [[nodiscard]] auto Count() const -> size_t;

   /**
    * @brief The monitor handle of the registry entry at @p index.
    *
    * Valid for `0 <= index < Count()`. Out-of-range reads return nullptr so
    * callers that bound their loop by Count() cannot read past the end.
    *
    * @param index Zero-based entry index
    * @return The registered monitor handle, or nullptr when out of range
    */
   [[nodiscard]] auto MonitorAt(size_t index) const -> HMONITOR;

   /**
    * @brief Look up a monitor's effective DPI.
    *
    * 0 on a registry miss — never a valid DPI (real effective DPIs are
    * >= 96), so a miss is distinguishable from a genuine 96-DPI monitor.
    *
    * @param hmon Monitor handle
    * @return The monitor's DPI, or 0 when unknown
    */
   [[nodiscard]] auto GetDpi(HMONITOR hmon) const -> UINT;

   /**
    * @brief Look up a monitor's scale factor (dpi / 96.0f).
    *
    * @param hmon Monitor handle
    * @return The monitor's scale factor, or 0.0f when unknown
    */
   [[nodiscard]] auto GetScale(HMONITOR hmon) const -> float;

   /**
    * @brief Scale a logical value for a specific monitor.
    *
    * @param hmon    Monitor handle
    * @param logical Logical (96-DPI) value
    * @return Scaled physical value
    */
   auto ScaleForMonitor(HMONITOR hmon, int logical) const -> int;

 private:
   /// Registry entry: monitor handle + effective DPI
   using Entry = struct {
      HMONITOR hmon;
      UINT dpi;
   };

   /// Per-monitor DPI registry (linear search; ≤16 monitors)
   std::vector<Entry> entries_;
};

#endif /* BFWM_DPI_H */
