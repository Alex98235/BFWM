/**
 * @file indicator_provider.h
 * @brief Indicator provider interface and registry.
 *
 * Decouples indicator data acquisition from Bar layout/rendering. Providers
 * fill an IndicatorRuntime; Bar handles composition and rendering.
 *
 * The registry is *total*: every BarIndicatorType resolves to a provider. The
 * Workspaces and Title providers are Bar-backed (they delegate to Bar's own
 * refresh/click/scroll handlers), which keeps the lookup uniform at all call
 * sites (refresh, hit-test, measure, click, scroll) — see Bar.
 */

#ifndef BFWM_INDICATOR_PROVIDER_H
#define BFWM_INDICATOR_PROVIDER_H

#include "../config/lua/parser.h"
#include <memory>
#include <string>
#include <unordered_map>
struct IndicatorRuntime;
struct IndicatorProvider;
class Bar;

/// Compose an indicator's text from its effective format dialect.
///
/// Uses `{name}` / `{name:.Nf}` substitution, with the special `{icon}` and
/// `{state}` tokens. When the effective format is empty, returns
/// `runtime.text` verbatim. `provider` supplies the fallback default format;
/// it may be null (no fallback).
auto IndicatorComposeFormat(const BarIndicatorConfig &cfg,
                            const IndicatorRuntime &runtime,
                            const IndicatorProvider *provider) -> std::string;

/**
 * @brief Abstract provider for one indicator type.
 *
 * Implementations are registered with IndicatorRegistry and invoked by Bar
 * during its refresh/compose/render pipeline and for hit-testing dispatch.
 */
struct IndicatorProvider {
   // Provider metadata (defaults + layout behaviour). Plain public descriptor
   // fields read by Bar and the registry; encapsulating them behind accessors
   // would add boilerplate without any invariant to protect.
   // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
   const char *name = nullptr;
   const char *default_format = nullptr;
   int default_poll_ms = 0;    // 0 => per-frame
   bool per_frame = false;     // true => refresh every frame
   bool is_collection = false; // true => renders multiple items (workspaces)
   // NOLINTEND(misc-non-private-member-variables-in-classes)

   /// Fill `runtime` with values for this indicator type.
   /// Called by Bar::RefreshIndicator when the poll window elapses.
   virtual void Refresh(const BarIndicatorConfig &cfg,
                        IndicatorRuntime &runtime, SharedState *shared,
                        MainContext *main) const = 0;

   /// Optional: handle click on this indicator (or collection item).
   /// Called by Bar::DispatchIndicatorClick after Lua overrides are ruled out.
   virtual void OnClick(const BarIndicatorConfig &cfg, int item,
                        SharedState *shared, MainContext *main) const {
      (void)cfg;
      (void)item;
      (void)shared;
      (void)main;
   }

   /// Optional: handle scroll on this indicator (or collection item).
   /// Called by Bar::DispatchIndicatorScroll after Lua overrides are ruled out.
   virtual void OnScroll(const BarIndicatorConfig &cfg, int delta,
                         SharedState *shared, MainContext *main) const {
      (void)cfg;
      (void)delta;
      (void)shared;
      (void)main;
   }

   virtual ~IndicatorProvider() = default;
};

/**
 * @brief Registry of indicator providers, one per BarIndicatorType.
 */
class IndicatorRegistry {
 public:
   IndicatorRegistry() = default;
   ~IndicatorRegistry() = default;

   IndicatorRegistry(const IndicatorRegistry &) = delete;
   auto operator=(const IndicatorRegistry &) -> IndicatorRegistry & = delete;

   /// Register a provider for a type. Takes ownership.
   void Register(BarIndicatorType type,
                 std::unique_ptr<IndicatorProvider> provider) {
      providers_[static_cast<size_t>(type)] = std::move(provider);
   }

   /// Get provider for a type, or nullptr if not registered.
   [[nodiscard]] auto Get(BarIndicatorType type) -> IndicatorProvider * {
      auto iter = providers_.find(static_cast<size_t>(type));
      return (iter != providers_.end()) ? iter->second.get() : nullptr;
   }

   /// Get provider for a type (const), or nullptr if not registered.
   [[nodiscard]] auto Get(BarIndicatorType type) const
       -> const IndicatorProvider * {
      auto iter = providers_.find(static_cast<size_t>(type));
      return (iter != providers_.end()) ? iter->second.get() : nullptr;
   }

   /// Create a registry populated with every built-in provider.
   ///
   /// `bar` is retained by the Bar-backed Workspaces/Title providers; it must
   /// outlive the registry. It may be null only for metadata-only inspection
   /// (the delegated handlers must not be invoked in that case).
   static auto CreateDefault(Bar *bar) -> std::unique_ptr<IndicatorRegistry>;

 private:
   std::unordered_map<size_t, std::unique_ptr<IndicatorProvider>> providers_;
};

#endif
