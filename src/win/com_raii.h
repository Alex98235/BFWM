/**
 * @file com_raii.h
 * @brief RAII guard for COM apartment initialization on a thread.
 *
 * Provides a scope-based guard that initializes the COM apartment with
 * CoInitializeEx and balances it with CoUninitialize on destruction.
 */

#ifndef BFWM_WIN_COM_RAII_H
#define BFWM_WIN_COM_RAII_H

#include <objbase.h>
#include <winerror.h>

/**
 * @brief Owns a COM apartment initialization on the calling thread.
 *
 * Calls CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED) on construction
 * and CoUninitialize on destruction, but only when initialization actually
 * succeeded (a failed CoInitializeEx must not be paired with
 * CoUninitialize). Check @ref hr before relying on COM.
 */
class ComInitGuard {
 public:
   /// Initialize the COM apartment on this thread.
   ComInitGuard() { hr_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }

   /// Balance the initialization with CoUninitialize, if it succeeded.
   ~ComInitGuard() {
      if (SUCCEEDED(hr_))
         CoUninitialize();
   }

   /// Non-copyable.
   ComInitGuard(const ComInitGuard &) = delete;
   auto operator=(const ComInitGuard &) -> ComInitGuard & = delete;

   /// The result of the underlying CoInitializeEx call.
   [[nodiscard]] auto hr() const -> HRESULT { return hr_; }

 private:
   HRESULT hr_ = E_FAIL;
};

#endif
