/**
 * @file gdi_raii.h
 * @brief RAII guards for Win32 GDI resources.
 *
 * Provides small scope-based guards that own GDI objects, device
 * contexts, and SelectObject state so resources are released
 * automatically on destruction.
 */

#ifndef BFWM_WIN_GDI_RAII_H
#define BFWM_WIN_GDI_RAII_H

#include <windows.h>

/**
 * @brief Owns a GDI object (HFONT, HBRUSH, HBITMAP, HPEN, ...).
 *
 * Calls DeleteObject on destruction. Non-copyable; movable.
 */
class GdiObject {
 public:
   /// Construct an empty guard (owns nothing).
   GdiObject() : handle_(nullptr) {}

   /// Take ownership of an existing GDI object.
   explicit GdiObject(HGDIOBJ handle) : handle_(handle) {}

   /// Release the owned object.
   ~GdiObject() {
      if (handle_ != nullptr)
         DeleteObject(handle_);
   }

   /// Non-copyable.
   GdiObject(const GdiObject &) = delete;
   auto operator=(const GdiObject &) -> GdiObject & = delete;

   /// Move constructor: transfers ownership.
   GdiObject(GdiObject &&other) noexcept : handle_(other.handle_) {
      other.handle_ = nullptr;
   }

   /// Move assignment: releases the current object, transfers ownership.
   auto operator=(GdiObject &&other) noexcept -> GdiObject & {
      if (this != &other) {
         if (handle_ != nullptr)
            DeleteObject(handle_);
         handle_ = other.handle_;
         other.handle_ = nullptr;
      }
      return *this;
   }

   /// The owned handle, or nullptr if empty.
   [[nodiscard]] auto get() const -> HGDIOBJ { return handle_; }

   /// Release ownership without deleting; returns the handle.
   auto release() -> HGDIOBJ {
      HGDIOBJ handle = handle_;
      handle_ = nullptr;
      return handle;
   }

 private:
   HGDIOBJ handle_;
};

/**
 * @brief Owns a memory device context created via CreateCompatibleDC.
 *
 * Calls DeleteDC on destruction. Non-copyable; movable.
 */
class DcGuard {
 public:
   /// Construct an empty guard (owns nothing).
   DcGuard() : dc_(nullptr) {}

   /// Take ownership of an existing device context.
   explicit DcGuard(HDC hdc) : dc_(hdc) {}

   /// Release the owned device context.
   ~DcGuard() {
      if (dc_ != nullptr)
         DeleteDC(dc_);
   }

   /// Non-copyable.
   DcGuard(const DcGuard &) = delete;
   auto operator=(const DcGuard &) -> DcGuard & = delete;

   /// Move constructor: transfers ownership.
   DcGuard(DcGuard &&other) noexcept : dc_(other.dc_) { other.dc_ = nullptr; }

   /// Move assignment: releases the current DC, transfers ownership.
   auto operator=(DcGuard &&other) noexcept -> DcGuard & {
      if (this != &other) {
         if (dc_ != nullptr)
            DeleteDC(dc_);
         dc_ = other.dc_;
         other.dc_ = nullptr;
      }
      return *this;
   }

   /// The owned device context, or nullptr if empty.
   [[nodiscard]] auto get() const -> HDC { return dc_; }

 private:
   HDC dc_;
};

/**
 * @brief Restores the previously selected GDI object on destruction.
 *
 * Selects new_obj into dc on construction and remembers the previous
 * object; the destructor selects the previous object back. Non-copyable.
 */
class SelectObjectGuard {
 public:
   /// Select new_obj into hdc, remembering the previously selected object.
   SelectObjectGuard(HDC hdc, HGDIOBJ new_obj)
       : dc_(hdc), old_obj_(SelectObject(hdc, new_obj)) {}

   /// Restore the previously selected object.
   ~SelectObjectGuard() {
      if (dc_ != nullptr)
         SelectObject(dc_, old_obj_);
   }

   /// Non-copyable.
   SelectObjectGuard(const SelectObjectGuard &) = delete;
   auto operator=(const SelectObjectGuard &) -> SelectObjectGuard & = delete;

 private:
   HDC dc_;
   HGDIOBJ old_obj_;
};

#endif
