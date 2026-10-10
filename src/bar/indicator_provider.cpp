/**
 * @file indicator_provider.cpp
 * @brief Built-in indicator provider implementations.
 */

#include "indicator_provider.h"

#include "../config/lua/parser.h"
#include "../core/bfwm_context.h"
#include "../system/system_status.h"
#include "bar.h"
#include "system_indicator_mapping.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>

// ===========================================================================
// Bar-backed providers (Workspaces / Title)
//
// These delegate straight back into Bar: their refresh, click and scroll
// behaviour is Bar's own, and Bar owns the per-monitor layout state they
// render. Keeping them in the registry makes type lookup total so every
// dispatch path (refresh, hit-test, measure, click, scroll) resolves them
// exactly as it did before the provider split.
// ===========================================================================

struct WorkspacesIndicatorProvider final : IndicatorProvider {
   explicit WorkspacesIndicatorProvider(Bar *bar) : bar_(bar) {
      name = "workspaces";
      default_format = "{id}";
      default_poll_ms = 0;
      per_frame = true;
      is_collection = true;
   }

   void Refresh(const BarIndicatorConfig &cfg, IndicatorRuntime &runtime,
                BFWMContext * /*ctx*/) const override {
      bar_->RefreshWorkspaces(cfg, runtime);
   }

   void OnClick(const BarIndicatorConfig &cfg, int item,
                BFWMContext * /*ctx*/) const override {
      bar_->DefaultWorkspacesClick(cfg, item);
   }

   void OnScroll(const BarIndicatorConfig &cfg, int delta,
                 BFWMContext * /*ctx*/) const override {
      bar_->DefaultWorkspacesScroll(cfg, delta);
   }

 private:
   Bar *bar_;
};

struct TitleIndicatorProvider final : IndicatorProvider {
   explicit TitleIndicatorProvider(Bar *bar) : bar_(bar) {
      name = "title";
      default_format = "";
      default_poll_ms = 0;
      per_frame = true;
      is_collection = false;
   }

   void Refresh(const BarIndicatorConfig &cfg, IndicatorRuntime &runtime,
                BFWMContext * /*ctx*/) const override {
      bar_->RefreshTitle(cfg, runtime);
   }

 private:
   Bar *bar_;
};

// ===========================================================================
// Data providers (Clock / Volume / Network / CPU / Memory / Custom)
// ===========================================================================

namespace {

constexpr int kDefaultPollMs = 500;
constexpr int kCustomPollMs = 1000;

/// Clock: per-frame; produces strftime text directly.
class ClockProvider final : public IndicatorProvider {
 public:
   ClockProvider() {
      name = "clock";
      default_format = "%H:%M";
      default_poll_ms = 0;
      per_frame = true;
      is_collection = false;
   }

   void Refresh(const BarIndicatorConfig &cfg, IndicatorRuntime &runtime,
                BFWMContext * /*ctx*/) const override {
      SYSTEMTIME now;
      GetLocalTime(&now);
      bar_indicator_mapping::FormatClock(now, cfg.format, runtime);
   }
};

/// Volume: polled.
class VolumeProvider final : public IndicatorProvider {
 public:
   VolumeProvider() {
      name = "volume";
      default_format = "{icon}{value:.0f}%";
      default_poll_ms = kDefaultPollMs;
      per_frame = false;
      is_collection = false;
   }

   void Refresh(const BarIndicatorConfig & /*cfg*/, IndicatorRuntime &runtime,
                BFWMContext * /*ctx*/) const override {
      bar_indicator_mapping::ApplyVolumeStatus(SystemGetVolumeStatus(),
                                               runtime);
   }
};

/// Network: polled.
class NetworkProvider final : public IndicatorProvider {
 public:
   NetworkProvider() {
      name = "network";
      default_format = "{icon}{value:.0f}%";
      default_poll_ms = kDefaultPollMs;
      per_frame = false;
      is_collection = false;
   }

   void Refresh(const BarIndicatorConfig & /*cfg*/, IndicatorRuntime &runtime,
                BFWMContext * /*ctx*/) const override {
      bar_indicator_mapping::ApplyNetworkStatus(SystemGetNetworkStatus(),
                                                runtime);
   }
};

/// CPU: polled.
class CpuProvider final : public IndicatorProvider {
 public:
   CpuProvider() {
      name = "cpu";
      default_format = "{icon}{value:.0f}%";
      default_poll_ms = kDefaultPollMs;
      per_frame = false;
      is_collection = false;
   }

   void Refresh(const BarIndicatorConfig & /*cfg*/, IndicatorRuntime &runtime,
                BFWMContext * /*ctx*/) const override {
      bar_indicator_mapping::ApplyCpuStatus(SystemGetCpuStatus(), runtime);
   }
};

/// Memory: polled.
class MemoryProvider final : public IndicatorProvider {
 public:
   MemoryProvider() {
      name = "memory";
      default_format = "{icon}{value:.1f}G";
      default_poll_ms = kDefaultPollMs;
      per_frame = false;
      is_collection = false;
   }

   void Refresh(const BarIndicatorConfig & /*cfg*/, IndicatorRuntime &runtime,
                BFWMContext * /*ctx*/) const override {
      bar_indicator_mapping::ApplyMemoryStatus(SystemGetMemoryStatus(),
                                               runtime);
   }
};

/// Custom: polled; calls into the Lua output callback.
class CustomProvider final : public IndicatorProvider {
 public:
   CustomProvider() {
      name = "custom";
      default_format = "";
      default_poll_ms = kCustomPollMs;
      per_frame = false;
      is_collection = false;
   }

   void Refresh(const BarIndicatorConfig &cfg, IndicatorRuntime &runtime,
                BFWMContext *ctx) const override {
      if (ctx == nullptr) {
         runtime.valid = false;
         return;
      }
      LuaIndicatorOutput out;
      bool const success = LuaConfigCallOutput(
          &ctx->lua, ctx, cfg.output.c_str(), cfg.id.c_str(), &out);
      if (!success || !out.valid) {
         runtime.valid = false;
         return;
      }
      ApplyIndicatorOutput(out, runtime);
   }
};

} // namespace

// ===========================================================================
// Registry factory
// ===========================================================================

auto IndicatorRegistry::CreateDefault(Bar *bar)
    -> std::unique_ptr<IndicatorRegistry> {
   auto registry = std::make_unique<IndicatorRegistry>();

   registry->Register(BAR_INDICATOR_WORKSPACES,
                      std::make_unique<WorkspacesIndicatorProvider>(bar));
   registry->Register(BAR_INDICATOR_TITLE,
                      std::make_unique<TitleIndicatorProvider>(bar));
   registry->Register(BAR_INDICATOR_CLOCK, std::make_unique<ClockProvider>());
   registry->Register(BAR_INDICATOR_VOLUME, std::make_unique<VolumeProvider>());
   registry->Register(BAR_INDICATOR_NETWORK,
                      std::make_unique<NetworkProvider>());
   registry->Register(BAR_INDICATOR_CPU, std::make_unique<CpuProvider>());
   registry->Register(BAR_INDICATOR_MEMORY, std::make_unique<MemoryProvider>());
   registry->Register(BAR_INDICATOR_CUSTOM, std::make_unique<CustomProvider>());

   return registry;
}
