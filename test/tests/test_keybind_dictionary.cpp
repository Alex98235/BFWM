#include "cbelt.h"
#include <cstring>
#include <memory>
#include <variant>
#include <windows.h>

#include "../../src/config/keybinds.h"
#include "../../src/config/action.h"

CBELT_GROUP("keybind_dictionary")

/* =========================================================================
 * Helper: create a keystroke with specific vkCode and modifiers
 * ========================================================================= */

namespace {

inline auto make_keystroke(DWORD vkCode, BOOL ctrl, BOOL shift, BOOL alt,
                           BOOL super) -> Keystroke {
   Keystroke ks = {};
   ks.key.vkCode = vkCode;
   ks.key.flags = 0;
   ks.key.scanCode = 0;
   ks.key.dwExtraInfo = 0;
   ks.modifiers.ctrl = ctrl;
   ks.modifiers.shift = shift;
   ks.modifiers.alt = alt;
   ks.modifiers.super = super;
   return ks;
}
} // namespace

/* =========================================================================
 * KBDFind — empty / not found
 * ========================================================================= */

CBELT_TEST(find_in_empty_dict) {
   KeybindDictionary dict;

   Keystroke ks = make_keystroke('A', FALSE, FALSE, FALSE, FALSE);
   auto *result = KBDFind(dict, ks);
   cbelt_assert(result == nullptr);

   return TEST_SUCCESS;
}

CBELT_TEST(find_nonexistent_key) {
   KeybindDictionary dict;

   Keystroke ks_a = make_keystroke('A', FALSE, FALSE, FALSE, FALSE);
   Keystroke ks_b = make_keystroke('B', FALSE, FALSE, FALSE, FALSE);

   BFWMAction *action = BFWMActionCreateSpawn("test.exe");
   KBDictInsert(dict, ks_a, std::unique_ptr<BFWMAction>(action));

   /* Searching for 'B' should return NULL */
   auto *result = KBDFind(dict, ks_b);
   cbelt_assert(result == nullptr);

   return TEST_SUCCESS;
}

/* =========================================================================
 * KBDictInsert / KBDFind — basic insert and lookup
 * ========================================================================= */

CBELT_TEST(insert_and_find_one) {
   KeybindDictionary dict;

   Keystroke ks = make_keystroke('A', FALSE, FALSE, FALSE, FALSE);
   BFWMAction *action = BFWMActionCreateSpawn("notepad.exe");
   KBDictInsert(dict, ks, std::unique_ptr<BFWMAction>(action));

   /* Look up by same keystroke */
   Keystroke lookup_key = ks;

   auto *found = KBDFind(dict, lookup_key);
   cbelt_assert(found != nullptr);
   cbelt_assert((*found)[0] != nullptr);
   cbelt_assert((*found)[0]->type == ActionSpawn);
   cbelt_assert(std::get<ActionArgsSpawn>((*found)[0]->args).command ==
                "notepad.exe");

   return TEST_SUCCESS;
}

CBELT_TEST(insert_two_different_keys) {
   KeybindDictionary dict;

   Keystroke ks_a = make_keystroke('A', FALSE, FALSE, FALSE, FALSE);
   Keystroke ks_b =
       make_keystroke('B', TRUE, FALSE, FALSE, FALSE); /* Ctrl+B */

   BFWMAction *act1 = BFWMActionCreateSpawn("app1.exe");
   BFWMAction *act2 = BFWMActionCreateSpawn("app2.exe");
   KBDictInsert(dict, ks_a, std::unique_ptr<BFWMAction>(act1));
   KBDictInsert(dict, ks_b, std::unique_ptr<BFWMAction>(act2));

   Keystroke lookup_a;
   memset(&lookup_a, 0, sizeof(lookup_a));
   lookup_a.key.vkCode = 'A';
   lookup_a.modifiers.ctrl = FALSE;

   Keystroke lookup_b;
   memset(&lookup_b, 0, sizeof(lookup_b));
   lookup_b.key.vkCode = 'B';
   lookup_b.modifiers.ctrl = TRUE;

   auto *found_a = KBDFind(dict, lookup_a);
   auto *found_b = KBDFind(dict, lookup_b);
   cbelt_assert(found_a != nullptr);
   cbelt_assert(found_b != nullptr);
   cbelt_assert((*found_a)[0]->type == ActionSpawn);
   cbelt_assert((*found_b)[0]->type == ActionSpawn);

   cbelt_assert(std::get<ActionArgsSpawn>((*found_a)[0]->args).command ==
                "app1.exe");
   cbelt_assert(std::get<ActionArgsSpawn>((*found_b)[0]->args).command ==
                "app2.exe");

   return TEST_SUCCESS;
}

/* =========================================================================
 * Multiple actions per keystroke
 * ========================================================================= */

CBELT_TEST(multiple_actions_per_key) {
   KeybindDictionary dict;

   Keystroke ks =
       make_keystroke(VK_SPACE, TRUE, FALSE, FALSE, FALSE); /* Ctrl+Space */
   BFWMAction *act1 = BFWMActionCreateSpawn("cmd.exe");
   BFWMAction *act2 = BFWMActionCreateFocus(DirNext);
   KBDictInsert(dict, ks, std::unique_ptr<BFWMAction>(act1));
   KBDictInsert(dict, ks, std::unique_ptr<BFWMAction>(act2)); /* same keystroke — should append */

   Keystroke lookup;
   memset(&lookup, 0, sizeof(lookup));
   lookup.key.vkCode = VK_SPACE;
   lookup.modifiers.ctrl = TRUE;

   auto *found = KBDFind(dict, lookup);
   cbelt_assert(found != nullptr);
   cbelt_assert((*found)[0] != nullptr);
   cbelt_assert((*found)[1] != nullptr);
   cbelt_assert((*found)[0]->type == ActionSpawn);
   cbelt_assert((*found)[1]->type == ActionFocus);
   /* Only 2 actions inserted */
   cbelt_assert(found->size() == 2);

   return TEST_SUCCESS;
}

CBELT_TEST(three_actions_per_key) {
   KeybindDictionary dict;

   Keystroke ks =
       make_keystroke('X', TRUE, TRUE, FALSE, FALSE); /* Ctrl+Shift+X */

   BFWMAction *a1 = BFWMActionCreateKillActive();
   BFWMAction *a2 = BFWMActionCreateSplit();
   BFWMAction *a3 = BFWMActionCreateFullscreen();
   KBDictInsert(dict, ks, std::unique_ptr<BFWMAction>(a1));
   KBDictInsert(dict, ks, std::unique_ptr<BFWMAction>(a2));
   KBDictInsert(dict, ks, std::unique_ptr<BFWMAction>(a3));

   Keystroke lookup;
   memset(&lookup, 0, sizeof(lookup));
   lookup.key.vkCode = 'X';
   lookup.modifiers.ctrl = TRUE;
   lookup.modifiers.shift = TRUE;

   auto *found = KBDFind(dict, lookup);
   cbelt_assert(found != nullptr);
   cbelt_assert((*found)[0] != nullptr);
   cbelt_assert((*found)[1] != nullptr);
   cbelt_assert((*found)[2] != nullptr);
   cbelt_assert((*found)[0]->type == ActionKillActive);
   cbelt_assert((*found)[1]->type == ActionSplit);
   cbelt_assert((*found)[2]->type == ActionFullscreen);
   cbelt_assert(found->size() == 3);

   return TEST_SUCCESS;
}

/* =========================================================================
 * Modifier matching — exact modifier comparison
 * ========================================================================= */

CBELT_TEST(modifier_mismatch_returns_null) {
   KeybindDictionary dict;

   /* Insert Ctrl+A */
   Keystroke ks = make_keystroke('A', TRUE, FALSE, FALSE, FALSE);
   BFWMAction *action = BFWMActionCreateSpawn("test.exe");
   KBDictInsert(dict, ks, std::unique_ptr<BFWMAction>(action));

   /* Look up 'A' without Ctrl — should NOT match */
   Keystroke lookup;
   memset(&lookup, 0, sizeof(lookup));
   lookup.key.vkCode = 'A';
   lookup.modifiers.ctrl = FALSE;
   lookup.modifiers.shift = FALSE;
   lookup.modifiers.alt = FALSE;
   lookup.modifiers.super = FALSE;

   auto *found = KBDFind(dict, lookup);
   cbelt_assert(found == nullptr);

   return TEST_SUCCESS;
}

CBELT_TEST(shift_modifier_distinguishes_keys) {
   KeybindDictionary dict;

   Keystroke ks_a = make_keystroke('A', FALSE, FALSE, FALSE, FALSE);
   Keystroke ks_shift_a = make_keystroke('A', FALSE, TRUE, FALSE, FALSE);

   BFWMAction *act1 = BFWMActionCreateSpawn("plain_a.exe");
   BFWMAction *act2 = BFWMActionCreateSpawn("shift_a.exe");
   KBDictInsert(dict, ks_a, std::unique_ptr<BFWMAction>(act1));
   KBDictInsert(dict, ks_shift_a, std::unique_ptr<BFWMAction>(act2));

   Keystroke lookup_plain;
   memset(&lookup_plain, 0, sizeof(lookup_plain));
   lookup_plain.key.vkCode = 'A';

   Keystroke lookup_shift;
   memset(&lookup_shift, 0, sizeof(lookup_shift));
   lookup_shift.key.vkCode = 'A';
   lookup_shift.modifiers.shift = TRUE;

   auto *found_plain = KBDFind(dict, lookup_plain);
   auto *found_shift = KBDFind(dict, lookup_shift);
   cbelt_assert(found_plain != nullptr);
   cbelt_assert(found_shift != nullptr);
   cbelt_assert(found_plain != found_shift);

   cbelt_assert(std::get<ActionArgsSpawn>((*found_plain)[0]->args).command ==
                "plain_a.exe");
   cbelt_assert(std::get<ActionArgsSpawn>((*found_shift)[0]->args).command ==
                "shift_a.exe");

   return TEST_SUCCESS;
}

/* =========================================================================
 * Insert causes capacity expansion
 * ========================================================================= */

CBELT_TEST(insert_triggers_expansion) {
   KeybindDictionary dict;

   /* Insert 3 different keys — the vector grows automatically */
   for (int i = 0; i < 3; i++) {
      Keystroke ks = make_keystroke('A' + i, FALSE, FALSE, FALSE, FALSE);
      BFWMAction *act = BFWMActionCreateSpawn("test.exe");
      KBDictInsert(dict, ks, std::unique_ptr<BFWMAction>(act));
   }

   cbelt_assert(dict.entries.size() == 3);

   /* All keys should still be findable */
   for (int i = 0; i < 3; i++) {
      Keystroke lookup;
      memset(&lookup, 0, sizeof(lookup));
      lookup.key.vkCode = 'A' + i;
      auto *found = KBDFind(dict, lookup);
      cbelt_assert(found != nullptr);
   }

   Keystroke lookup_d;
   memset(&lookup_d, 0, sizeof(lookup_d));
   lookup_d.key.vkCode = 'D';
   auto *found_d = KBDFind(dict, lookup_d);
   cbelt_assert(found_d == nullptr);

   return TEST_SUCCESS;
}

/* =========================================================================
 * Insert same key twice adds to action list (not duplicate entry)
 * ========================================================================= */

CBELT_TEST(insert_same_key_twice_single_entry) {
   KeybindDictionary dict;

   Keystroke ks = make_keystroke('Q', FALSE, FALSE, FALSE, FALSE);
   BFWMAction *act1 = BFWMActionCreateKillActive();
   BFWMAction *act2 = BFWMActionCreateFullscreen();
   KBDictInsert(dict, ks, std::unique_ptr<BFWMAction>(act1));
   KBDictInsert(dict, ks, std::unique_ptr<BFWMAction>(act2)); /* same key — should append */

   /* Should be exactly ONE entry in the dictionary */
   cbelt_assert(dict.entries.size() == 1);

   /* But with TWO actions in the action list */
   Keystroke lookup;
   memset(&lookup, 0, sizeof(lookup));
   lookup.key.vkCode = 'Q';
   auto *found = KBDFind(dict, lookup);
   cbelt_assert(found != nullptr);
   cbelt_assert((*found)[0] != nullptr);
   cbelt_assert((*found)[1] != nullptr);
   cbelt_assert((*found)[0]->type == ActionKillActive);
   cbelt_assert((*found)[1]->type == ActionFullscreen);

   return TEST_SUCCESS;
}
