#include "cbelt.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <windows.h>

#include "../../src/window/window.h"

CBELT_GROUP("window_registry")

/* =========================================================================
 * Helpers
 * ========================================================================= */

namespace {

/* Unique, fake HWND per index — never a real window handle. */
inline auto test_hwnd(int i) -> HWND {
   return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x100 + i));
}
} // namespace

/* =========================================================================
 * Window construction / registry lifetime
 * ========================================================================= */

/* The old WindowDestroy(nullptr) safety test is obsolete: destruction is now
 * scope-based. Construction/destruction of an empty registry must be safe
 * (the registry owns its CRITICAL_SECTION). */
CBELT_TEST(empty_registry_construct_destroy) {
   WindowRegistry reg;
   cbelt_assert(reg.Windows().empty());
   return TEST_SUCCESS;
}

CBELT_TEST(window_create_and_free) {
   auto w = std::make_unique<Window>(reinterpret_cast<HWND>(0x1234), L"MyApp");
   cbelt_assert(w != nullptr);
   cbelt_assert(w->GetHwnd() == reinterpret_cast<HWND>(0x1234));
   cbelt_assert(w->GetTitle() == L"MyApp");
   return TEST_SUCCESS;
}

CBELT_TEST(window_create_empty_title) {
   auto w = std::make_unique<Window>(reinterpret_cast<HWND>(1), L"");
   cbelt_assert(w != nullptr);
   cbelt_assert(w->GetTitle().empty());
   return TEST_SUCCESS;
}

CBELT_TEST(window_create_unicode_title) {
   auto w = std::make_unique<Window>(reinterpret_cast<HWND>(100), L"Ünïcødé✓");
   cbelt_assert(w != nullptr);
   cbelt_assert(w->GetTitle() == L"Ünïcødé✓");
   return TEST_SUCCESS;
}

/* =========================================================================
 * Register / iteration order
 * ========================================================================= */

CBELT_TEST(register_one_window) {
   WindowRegistry reg;
   HWND hwnd = test_hwnd(1);

   Window *w = reg.Register(std::make_unique<Window>(hwnd, L"TestWindow"));

   cbelt_assert(w != nullptr);
   cbelt_assert(reg.Windows().size() == 1);
   cbelt_assert(reg.Windows()[0].get() == w);
   cbelt_assert(reg.FindByHwnd(hwnd) == w);
   return TEST_SUCCESS;
}

CBELT_TEST(register_multiple_windows) {
   WindowRegistry reg;

   std::array<Window *, 5> wins{};
   for (int i = 0; i < 5; i++) {
      wins[i] =
          reg.Register(std::make_unique<Window>(test_hwnd(i), L"TestWindow"));
   }

   cbelt_assert(reg.Windows().size() == 5);
   /* Order preserved: windows[0..4] in registration order. */
   for (int i = 0; i < 5; i++) {
      cbelt_assert(reg.Windows()[static_cast<size_t>(i)].get() == wins[i]);
      cbelt_assert(reg.FindByHwnd(test_hwnd(i)) == wins[i]);
   }
   return TEST_SUCCESS;
}

CBELT_TEST(get_window_by_index) {
   WindowRegistry reg;

   Window *win1 = reg.Register(std::make_unique<Window>(test_hwnd(1), L"W1"));
   Window *win2 = reg.Register(std::make_unique<Window>(test_hwnd(2), L"W2"));

   cbelt_assert(reg.Windows().size() == 2);
   cbelt_assert(reg.Windows()[0].get() == win1);
   cbelt_assert(reg.Windows()[1].get() == win2);
   return TEST_SUCCESS;
}

/* Register returns a pointer to a stable Window object: a vector reallocation
 * moves the unique_ptr wrappers, never the Window objects themselves. */
CBELT_TEST(register_pointer_stable_across_registers) {
   WindowRegistry reg;

   HWND hwnd0 = test_hwnd(0);
   Window *first = reg.Register(std::make_unique<Window>(hwnd0, L"First"));
   cbelt_assert(first != nullptr);

   /* Register enough windows to force several vector reallocations. */
   for (int i = 1; i < 16; i++) {
      reg.Register(std::make_unique<Window>(test_hwnd(i), L"TestWindow"));
   }

   cbelt_assert(reg.Windows().size() == 16);
   cbelt_assert(reg.Windows()[0].get() == first);
   cbelt_assert(first->GetHwnd() == hwnd0);
   cbelt_assert(first->GetTitle() == L"First");
   cbelt_assert(reg.FindByHwnd(hwnd0) == first);
   return TEST_SUCCESS;
}

/* =========================================================================
 * FindByHwnd
 * ========================================================================= */

CBELT_TEST(find_window_by_hwnd_found) {
   WindowRegistry reg;

   reg.Register(std::make_unique<Window>(test_hwnd(10), L"W1"));
   Window *win2 = reg.Register(std::make_unique<Window>(test_hwnd(20), L"W2"));
   reg.Register(std::make_unique<Window>(test_hwnd(30), L"W3"));

   Window *found = reg.FindByHwnd(test_hwnd(20));
   cbelt_assert(found != nullptr);
   cbelt_assert(found == win2);
   cbelt_assert(found->GetHwnd() == test_hwnd(20));
   return TEST_SUCCESS;
}

CBELT_TEST(find_window_by_hwnd_not_found) {
   WindowRegistry reg;

   Window *win =
       reg.Register(std::make_unique<Window>(test_hwnd(1), L"TestWindow"));
   cbelt_assert(win != nullptr);

   Window *found =
       reg.FindByHwnd(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(999)));
   cbelt_assert(found == nullptr);
   return TEST_SUCCESS;
}

CBELT_TEST(find_window_by_hwnd_empty_reg) {
   WindowRegistry reg;

   Window *found = reg.FindByHwnd(test_hwnd(1));
   cbelt_assert(found == nullptr);
   return TEST_SUCCESS;
}

CBELT_TEST(find_window_by_hwnd_unique_hwnds) {
   WindowRegistry reg;

   std::array<Window *, 4> wins{};
   for (int i = 0; i < 4; i++) {
      wins[i] =
          reg.Register(std::make_unique<Window>(test_hwnd(i), L"TestWindow"));
   }

   /* Each distinct hwnd resolves to exactly its own window. */
   for (int i = 0; i < 4; i++) {
      cbelt_assert(reg.FindByHwnd(test_hwnd(i)) == wins[i]);
      for (int j = 0; j < 4; j++) {
         if (i != j)
            cbelt_assert(reg.FindByHwnd(test_hwnd(j)) != wins[i]);
      }
   }
   return TEST_SUCCESS;
}

/* =========================================================================
 * Remove
 * ========================================================================= */

CBELT_TEST(remove_window_success) {
   WindowRegistry reg;
   HWND hwnd1 = test_hwnd(1);
   HWND hwnd2 = test_hwnd(2);
   HWND hwnd3 = test_hwnd(3);

   Window *win1 = reg.Register(std::make_unique<Window>(hwnd1, L"W1"));
   Window *win2 = reg.Register(std::make_unique<Window>(hwnd2, L"W2"));
   Window *win3 = reg.Register(std::make_unique<Window>(hwnd3, L"W3"));

   cbelt_assert(reg.Windows().size() == 3);

   /* Remove win2 (middle). win2 becomes dangling — do not dereference it. */
   bool result = reg.Remove(win2);
   cbelt_assert(result == true);
   cbelt_assert(reg.Windows().size() == 2);

   /* Remaining windows are contiguous: win1, win3. */
   cbelt_assert(reg.Windows()[0].get() == win1);
   cbelt_assert(reg.Windows()[1].get() == win3);

   /* Removed window is no longer findable. */
   cbelt_assert(reg.FindByHwnd(hwnd2) == nullptr);
   return TEST_SUCCESS;
}

CBELT_TEST(remove_first_window) {
   WindowRegistry reg;
   HWND hwnd1 = test_hwnd(1);
   HWND hwnd2 = test_hwnd(2);

   Window *win1 = reg.Register(std::make_unique<Window>(hwnd1, L"W1"));
   Window *win2 = reg.Register(std::make_unique<Window>(hwnd2, L"W2"));

   bool result = reg.Remove(win1);
   cbelt_assert(result == true);
   cbelt_assert(reg.Windows().size() == 1);
   cbelt_assert(reg.Windows()[0].get() == win2);
   cbelt_assert(reg.FindByHwnd(hwnd1) == nullptr);
   return TEST_SUCCESS;
}

/* Removing the last (only) window leaves the registry empty. */
CBELT_TEST(remove_last_window) {
   WindowRegistry reg;
   HWND hwnd1 = test_hwnd(1);

   Window *win1 = reg.Register(std::make_unique<Window>(hwnd1, L"W1"));
   cbelt_assert(reg.Windows().size() == 1);

   bool result = reg.Remove(win1);
   cbelt_assert(result == true);
   cbelt_assert(reg.Windows().empty());
   cbelt_assert(reg.FindByHwnd(hwnd1) == nullptr);
   return TEST_SUCCESS;
}

CBELT_TEST(remove_nonexistent_window) {
   WindowRegistry reg;
   WindowRegistry other_reg;
   HWND hwnd1 = test_hwnd(1);
   HWND hwnd2 = test_hwnd(2);

   Window *win1 = reg.Register(std::make_unique<Window>(hwnd1, L"W1"));
   Window *win2 = other_reg.Register(std::make_unique<Window>(hwnd2, L"W2"));
   cbelt_assert(win1 != nullptr);
   cbelt_assert(win2 != nullptr);

   /* win2 belongs to other_reg, not reg — removal must fail. */
   bool result = reg.Remove(win2);
   cbelt_assert(result == false);
   cbelt_assert(reg.Windows().size() == 1);

   /* nullptr is never a registered window. */
   bool result_null = reg.Remove(nullptr);
   cbelt_assert(result_null == false);
   cbelt_assert(reg.Windows().size() == 1);
   return TEST_SUCCESS;
}

CBELT_TEST(registry_empty_after_full_remove) {
   WindowRegistry reg;

   std::array<Window *, 3> wins{};
   for (int i = 0; i < 3; i++) {
      wins[i] =
          reg.Register(std::make_unique<Window>(test_hwnd(i), L"TestWindow"));
   }

   for (int i = 0; i < 3; i++) {
      cbelt_assert(reg.Remove(wins[i]) == true);
   }

   cbelt_assert(reg.Windows().empty());
   cbelt_assert(reg.FindByHwnd(test_hwnd(0)) == nullptr);
   cbelt_assert(reg.FindByHwnd(test_hwnd(1)) == nullptr);
   cbelt_assert(reg.FindByHwnd(test_hwnd(2)) == nullptr);
   return TEST_SUCCESS;
}

/* =========================================================================
 * Registry destructor
 * ========================================================================= */

/* The registry destructor must destroy any windows still owned by it (the
 * Window destructor is out-of-line in window.cpp). */
CBELT_TEST(registry_destructor_destroys_remaining_windows) {
   {
      WindowRegistry reg;
      reg.Register(std::make_unique<Window>(test_hwnd(1), L"A"));
      reg.Register(std::make_unique<Window>(test_hwnd(2), L"B"));
      cbelt_assert(reg.Windows().size() == 2);
   } /* reg goes out of scope with 2 live windows — safe */
   return TEST_SUCCESS;
}
