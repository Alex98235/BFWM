/**
 * @file focus_intent.cpp
 * @brief General focus-intent model: a Windows-supplied foreground never
 *        overrules the user's most recent explicit focus action.
 *
 * The intent is armed whenever we are about to focus a specific window
 * (ArmKnown) or spawn a process whose window has not yet appeared (ArmLearn).
 * While armed, a registered non-target EVENT_SYSTEM_FOREGROUND is dropped, so
 * a stray foreground in a spawn storm cannot activate the wrong workspace.
 * Unlike a one-shot guard, the intent persists until the planned target
 * actually lands (NoteLanded), real OS input occurs (the user yields), or the
 * deadline backstop fires (Expire).
 *
 * All methods run on the main thread (the context lock already wraps the
 * callers), so no internal locking is used.
 */

#include "focus_intent.h"

namespace {

/// Deadline backstop only — not the primary retirement mechanism.
constexpr ULONGLONG FOCUS_INTENT_BACKSTOP_MS = 2000;

/// GetLastInputInfo-based tick (32-bit, wraps ~49.7 days). Kept separate from
/// GetTickCount64 to avoid mixing time bases.
inline auto LastInputTick() -> DWORD {
   LASTINPUTINFO lii{};
   lii.cbSize = sizeof(lii);
   GetLastInputInfo(&lii);
   return lii.dwTime;
}

/// Wrap-safe "did real input happen after baseline?" check.
inline auto InputSince(DWORD baseline) -> BOOL {
   return static_cast<BOOL>(static_cast<INT32>(LastInputTick() - baseline) > 0);
}

} // namespace

void FocusIntent::ArmKnown(HWND target) {
   kind_ = FocusIntentKind::Known;
   target_ = target;
   baseline_input_ = LastInputTick();
   deadline_ = GetTickCount64() + FOCUS_INTENT_BACKSTOP_MS;
}

void FocusIntent::ArmLearn() {
   kind_ = FocusIntentKind::Learn;
   target_ = nullptr;
   baseline_input_ = LastInputTick();
   deadline_ = GetTickCount64() + FOCUS_INTENT_BACKSTOP_MS;
}

auto FocusIntent::ShouldSuppress(HWND hwnd, BOOL registered) -> BOOL {
   if (kind_ == FocusIntentKind::None)
      return FALSE;
   if (InputSince(baseline_input_) == TRUE) {
      // The user spoke — yield and stop suppressing.
      Clear();
      return FALSE;
   }
   if (registered == FALSE)
      return FALSE; // let a brand-new window register
   if ((kind_ == FocusIntentKind::Known) && (hwnd == target_))
      return FALSE; // our target — let it land
   return TRUE;     // registered non-target — drop
}

void FocusIntent::NoteLanded(HWND hwnd) {
   if ((kind_ == FocusIntentKind::Known) && (hwnd == target_))
      Clear();
}

void FocusIntent::Expire() {
   if ((kind_ != FocusIntentKind::None) && (GetTickCount64() > deadline_))
      Clear();
}

void FocusIntent::Clear() {
   kind_ = FocusIntentKind::None;
   target_ = nullptr;
}
