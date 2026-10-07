#include "cloaking.h"
#include "../logging/logger.h"

#include <initguid.h>
#include <objbase.h>
#include <servprov.h>
#include <windows.h>
#include <winerror.h>

// The ImmersiveShell / ApplicationView COM types used here are undocumented
// and are absent from the MinGW SDK headers, so we model their vtable layout
// ourselves below.  The documented IServiceProvider interface, by contrast, is
// used directly as the native C++ abstract class from <servprov.h>.

// CLSID_ImmersiveShell
// {C2F03A33-21F5-47FA-B4BB-156362A2F239}
DEFINE_GUID(CLSID_ImmersiveShell, 0xC2F03A33, 0x21F5, 0x47FA, 0xB4, 0xBB, 0x15,
            0x63, 0x62, 0xA2, 0xF2, 0x39);

// IID_IApplicationViewCollection (Windows 10 1803+)
// {1841C6D7-4F9D-42C0-AF41-8747538F10E5}
DEFINE_GUID(IID_IApplicationViewCollection_Modern, 0x1841C6D7, 0x4F9D, 0x42C0,
            0xAF, 0x41, 0x87, 0x47, 0x53, 0x8F, 0x10, 0xE5);

// IID_IApplicationViewCollection (Windows 10 pre-1803)
// {2C08ADF0-A386-4B35-9250-0FE183476FCC}
DEFINE_GUID(IID_IApplicationViewCollection_Legacy, 0x2C08ADF0, 0xA386, 0x4B35,
            0x92, 0x50, 0x0F, 0xE1, 0x83, 0x47, 0x6F, 0xCC);

namespace {

// --- Undocumented ApplicationView interfaces (minimal vtable layouts) ---
//
// Every COM interface shares the first three IUnknown vtable slots:
//   0: QueryInterface   1: AddRef   2: Release
// The slots we do not use are kept as placeholders so the methods we do need
// land at their real vtable offsets.

struct IApplicationView;
struct IApplicationViewCollection;

// GetViewForHwnd lives at vtable slot 6 (3 IUnknown + 3 custom).
struct IApplicationViewCollectionVtbl {
   // IUnknown (slots 0-2)
   HRESULT(STDMETHODCALLTYPE *QueryInterface)(IApplicationViewCollection *This,
                                              REFIID riid, void **ppvObject);
   ULONG(STDMETHODCALLTYPE *AddRef)(IApplicationViewCollection *This);
   ULONG(STDMETHODCALLTYPE *Release)(IApplicationViewCollection *This);
   // undocumented custom methods (slots 3-5)
   HRESULT(STDMETHODCALLTYPE *Reserved3)(IApplicationViewCollection *This);
   HRESULT(STDMETHODCALLTYPE *Reserved4)(IApplicationViewCollection *This);
   HRESULT(STDMETHODCALLTYPE *Reserved5)(IApplicationViewCollection *This);
   // slot 6
   HRESULT(STDMETHODCALLTYPE *GetViewForHwnd)(IApplicationViewCollection *This,
                                              HWND hwnd,
                                              IApplicationView **ppView);
};
struct IApplicationViewCollection {
   const IApplicationViewCollectionVtbl *lpVtbl;
};

// SetCloak lives at vtable slot 12 (3 IUnknown + 3 IInspectable + 6 custom).
struct IApplicationViewVtbl {
   // IUnknown (slots 0-2)
   HRESULT(STDMETHODCALLTYPE *QueryInterface)(IApplicationView *This,
                                              REFIID riid, void **ppvObject);
   ULONG(STDMETHODCALLTYPE *AddRef)(IApplicationView *This);
   ULONG(STDMETHODCALLTYPE *Release)(IApplicationView *This);
   // IInspectable (slots 3-5)
   HRESULT(STDMETHODCALLTYPE *GetIids)(IApplicationView *This);
   HRESULT(STDMETHODCALLTYPE *GetRuntimeClassName)(IApplicationView *This);
   HRESULT(STDMETHODCALLTYPE *GetTrustLevel)(IApplicationView *This);
   // undocumented custom methods (slots 6-11)
   HRESULT(STDMETHODCALLTYPE *Reserved6)(IApplicationView *This);
   HRESULT(STDMETHODCALLTYPE *Reserved7)(IApplicationView *This);
   HRESULT(STDMETHODCALLTYPE *Reserved8)(IApplicationView *This);
   HRESULT(STDMETHODCALLTYPE *Reserved9)(IApplicationView *This);
   HRESULT(STDMETHODCALLTYPE *Reserved10)(IApplicationView *This);
   HRESULT(STDMETHODCALLTYPE *Reserved11)(IApplicationView *This);
   // slot 12
   HRESULT(STDMETHODCALLTYPE *SetCloak)(IApplicationView *This, DWORD arg1,
                                        DWORD arg2);
};
struct IApplicationView {
   const IApplicationViewVtbl *lpVtbl;
};

// Minimal RAII COM pointer for SDK interfaces (released via the virtual
// IUnknown::Release member).  Copyless.
template <typename T> class ComPtr {
 public:
   ComPtr() noexcept = default;
   ~ComPtr() { Release(); }

   ComPtr(const ComPtr &) = delete;
   auto operator=(const ComPtr &) -> ComPtr & = delete;

   [[nodiscard]] auto Get() const noexcept -> T * { return ptr_; }
   auto operator->() const noexcept -> T * { return ptr_; }
   explicit operator bool() const noexcept { return ptr_ != nullptr; }
   auto GetAddressOf() noexcept -> T ** { return &ptr_; }

   void Release() noexcept {
      if (ptr_) {
         ptr_->Release();
         ptr_ = nullptr;
      }
   }

 private:
   T *ptr_ = nullptr;
};

// RAII COM pointer for the custom vtable-struct interfaces above, whose
// Release is reached through lpVtbl.
template <typename T> class ComVtblPtr {
 public:
   ComVtblPtr() noexcept = default;
   ~ComVtblPtr() { Release(); }

   ComVtblPtr(const ComVtblPtr &) = delete;
   auto operator=(const ComVtblPtr &) -> ComVtblPtr & = delete;

   [[nodiscard]] auto Get() const noexcept -> T * { return ptr_; }
   auto operator->() const noexcept -> T * { return ptr_; }
   explicit operator bool() const noexcept { return ptr_ != nullptr; }
   auto GetAddressOf() noexcept -> T ** { return &ptr_; }

   void Release() noexcept {
      if (ptr_) {
         ptr_->lpVtbl->Release(ptr_);
         ptr_ = nullptr;
      }
   }

 private:
   T *ptr_ = nullptr;
};

} // namespace

auto SetWindowCloakState(HWND hwnd, BOOL cloaked) -> HRESULT {
   if (IsWindow(hwnd) == FALSE) {
      Debug("SetWindowCloakState: invalid HWND %p", hwnd);
      return E_INVALIDARG;
   }

   ComPtr<IServiceProvider> service_provider;
   HRESULT result = CoCreateInstance(
       CLSID_ImmersiveShell, nullptr,
       CLSCTX_LOCAL_SERVER | CLSCTX_INPROC_SERVER, IID_IServiceProvider,
       reinterpret_cast<void **>(service_provider.GetAddressOf()));
   if (FAILED(result) || !service_provider) {
      Error("SetWindowCloakState: CoCreateInstance(ImmersiveShell) failed "
            "(result=0x%08lx)",
            (unsigned long)result);
      return result;
   }

   // IApplicationViewCollection is NOT available through direct
   // IUnknown::QueryInterface on the ImmersiveShell object.  It is exposed
   // via IServiceProvider::QueryService, using the same GUID as both the
   // service identifier (SID) and the interface identifier (IID).
   ComVtblPtr<IApplicationViewCollection> collection;

   // Try modern IID first, fall back to legacy
   for (int pass = 0; pass < 2; pass++) {
      const GUID *iid = (pass == 0) ? &IID_IApplicationViewCollection_Modern
                                    : &IID_IApplicationViewCollection_Legacy;
      result = service_provider->QueryService(
          *iid, *iid, reinterpret_cast<void **>(collection.GetAddressOf()));
      if (SUCCEEDED(result) && collection)
         break;
   }
   if (FAILED(result) || !collection) {
      Error("SetWindowCloakState: QueryService(IApplicationViewCollection) "
            "failed (result=0x%08lx)",
            (unsigned long)result);
      return result;
   }

   ComVtblPtr<IApplicationView> view;
   result = collection->lpVtbl->GetViewForHwnd(collection.Get(), hwnd,
                                               view.GetAddressOf());
   if (FAILED(result) || !view)
      return result;

   // The window may have been destroyed mid-call (e.g. during a sleep/wake
   // storm). Re-validate before touching the view so we don't operate on a
   // HWND that no longer maps to a live window.
   if (IsWindow(hwnd) == FALSE) {
      Debug("SetWindowCloakState: HWND %p died during COM setup", hwnd);
      return E_INVALIDARG;
   }

   // SetCloak(1, 2) = cloak, SetCloak(1, 0) = uncloak
   result = view->lpVtbl->SetCloak(view.Get(), (DWORD)1,
                                   (cloaked == TRUE) ? (DWORD)2 : (DWORD)0);
   if (FAILED(result)) {
      Warn("SetWindowCloakState: SetCloak(%p, %d) failed (result=0x%08lx)",
           hwnd, cloaked, (unsigned long)result);
      return result;
   }

   return S_OK;
}
