#include "cbelt.h"
#include <windows.h>

#include "../../src/input/keystroke.h"

CBELT_GROUP("keystroke")

/* =========================================================================
 * GetKeyNameW — named key codes
 * ========================================================================= */

CBELT_TEST(get_key_name_special_keys) {
   cbelt_assert(wcscmp(GetKeyNameW(VK_SPACE), L"Space") == 0);
   cbelt_assert(wcscmp(GetKeyNameW(VK_RETURN), L"Enter") == 0);
   cbelt_assert(wcscmp(GetKeyNameW(VK_TAB), L"Tab") == 0);
   cbelt_assert(wcscmp(GetKeyNameW(VK_ESCAPE), L"Escape") == 0);
   cbelt_assert(wcscmp(GetKeyNameW(VK_BACK), L"Backspace") == 0);
   cbelt_assert(wcscmp(GetKeyNameW(VK_LEFT), L"Left") == 0);
   cbelt_assert(wcscmp(GetKeyNameW(VK_RIGHT), L"Right") == 0);
   cbelt_assert(wcscmp(GetKeyNameW(VK_UP), L"Up") == 0);
   cbelt_assert(wcscmp(GetKeyNameW(VK_DOWN), L"Down") == 0);
   cbelt_assert(wcscmp(GetKeyNameW(VK_CONTROL), L"Ctrl") == 0);
   cbelt_assert(wcscmp(GetKeyNameW(VK_SHIFT), L"Shift") == 0);
   cbelt_assert(wcscmp(GetKeyNameW(VK_MENU), L"Alt") == 0);
   cbelt_assert(wcscmp(GetKeyNameW(VK_LWIN), L"Win") == 0);
   return TEST_SUCCESS;
}

/* =========================================================================
 * GetKeyNameW — printable characters
 * ========================================================================= */

CBELT_TEST(get_key_name_printable) {
   /* Space (0x20) through tilde (0x7E) should return the character itself */
   cbelt_assert(wcscmp(GetKeyNameW(0x30), L"0") == 0); /* '0' */
   cbelt_assert(wcscmp(GetKeyNameW(0x41), L"A") == 0); /* 'A' */
   cbelt_assert(wcscmp(GetKeyNameW(0x5A), L"Z") == 0); /* 'Z' */
   cbelt_assert(wcscmp(GetKeyNameW(0x61), L"a") == 0); /* 'a' */
   cbelt_assert(wcscmp(GetKeyNameW(0x7A), L"z") == 0); /* 'z' */
   cbelt_assert(wcscmp(GetKeyNameW(0x2E), L".") == 0); /* '.' */
   cbelt_assert(wcscmp(GetKeyNameW(0x2F), L"/") == 0); /* '/' */
   return TEST_SUCCESS;
}

/* =========================================================================
 * GetKeyNameW — unknown / out of range
 * ========================================================================= */

CBELT_TEST(get_key_name_unknown) {
   /* Below printable range */
   cbelt_assert(wcscmp(GetKeyNameW(0x1F), L"?") == 0);
   /* Just above printable range */
   cbelt_assert(wcscmp(GetKeyNameW(0x7F), L"?") == 0);
   /* Large value that isn't a named key */
   cbelt_assert(wcscmp(GetKeyNameW(0xFF), L"?") == 0);
   return TEST_SUCCESS;
}

/* =========================================================================
 * GetKeyNameW — boundary tests for the printable range
 * ========================================================================= */

CBELT_TEST(get_key_name_printable_boundaries) {
   /* VK_SPACE (0x20) is caught by the named case, returns "Space" */
   cbelt_assert(wcscmp(GetKeyNameW(VK_SPACE), L"Space") == 0);
   /* Lowest printable char not in named cases: '!' (0x21) */
   cbelt_assert(wcscmp(GetKeyNameW(0x21), L"!") == 0);
   /* Highest printable character */
   cbelt_assert(wcscmp(GetKeyNameW(0x7E), L"~") == 0);
   return TEST_SUCCESS;
}
