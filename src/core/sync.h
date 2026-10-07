/**
 * @file sync.h
 * @brief RAII synchronization primitives shared across the codebase.
 *
 * CriticalSection wraps a Win32 CRITICAL_SECTION with RAII lifetime;
 * ScopedLock holds any CRITICAL_SECTION (raw or wrapped) until scope exit,
 * releasing it even when the scope exits via an exception.
 */

#ifndef BFWM_SYNC_H
#define BFWM_SYNC_H

#include <windows.h>

/**
 * @brief RAII wrapper around a Win32 CRITICAL_SECTION.
 *
 * Initializes on construction, deletes on destruction. Non-copyable.
 */
class CriticalSection {
 public:
   CriticalSection() { InitializeCriticalSection(&cs_); }
   ~CriticalSection() { DeleteCriticalSection(&cs_); }
   CriticalSection(const CriticalSection &) = delete;
   auto operator=(const CriticalSection &) -> CriticalSection & = delete;

   void Enter() { EnterCriticalSection(&cs_); }
   void Leave() { LeaveCriticalSection(&cs_); }

   /// Lets ScopedLock (and other CRITICAL_SECTION consumers) use this wrapper
   /// directly.
   operator CRITICAL_SECTION &() { return cs_; }

 private:
   CRITICAL_SECTION cs_;
};

/**
 * @brief Scoped RAII guard that holds a CRITICAL_SECTION until scope exit.
 *
 * Releases the lock even when the scope exits via an exception (e.g. a
 * std::bad_alloc from vector growth), which a manual Enter/Leave pair would
 * leak. Accepts both a raw CRITICAL_SECTION and a CriticalSection wrapper.
 */
class ScopedLock {
 public:
   explicit ScopedLock(CRITICAL_SECTION &critical_section)
       : cs_(&critical_section) {
      EnterCriticalSection(cs_);
   }
   ~ScopedLock() { LeaveCriticalSection(cs_); }
   ScopedLock(const ScopedLock &) = delete;
   auto operator=(const ScopedLock &) -> ScopedLock & = delete;

 private:
   CRITICAL_SECTION *cs_;
};

#endif