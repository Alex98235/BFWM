#ifndef BFWM_FOCUS_INTENT_H
#define BFWM_FOCUS_INTENT_H
#include <cstdint>
#include <windows.h> // HWND, BOOL, ULONGLONG, LASTINPUTINFO, GetLastInputInfo

enum class FocusIntentKind : std::uint8_t { None = 0, Known, Learn };

class FocusIntent {
 public:
   // Arm for a specific successor we are about to focus.
   void ArmKnown(HWND target);
   // Arm for a spawn whose window has not registered/focused yet.
   void ArmLearn();
   // TRUE when a FOREGROUND event for hwnd must be dropped.
   // Lazily disarms (and returns FALSE) if real OS input happened since arming.
   auto ShouldSuppress(HWND hwnd, BOOL registered) -> BOOL;
   // Retire once our planned target actually became foreground.
   void NoteLanded(HWND hwnd);
   // Deadline backstop; call once per main-loop tick.
   void Expire();
   void Clear();
   [[nodiscard]] auto Kind() const -> FocusIntentKind { return kind_; }
   [[nodiscard]] auto Target() const -> HWND { return target_; }

 private:
   FocusIntentKind kind_ = FocusIntentKind::None;
   HWND target_ = nullptr;
   DWORD baseline_input_ = 0; // GetLastInputInfo dwTime at arm time
   ULONGLONG deadline_ = 0;   // GetTickCount64 backstop
};
#endif
