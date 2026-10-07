/**
 * @file keybinds.h
 * @brief Key-value dictionary mapping keystrokes to BFWM actions.
 *
 * Provides a mechanism to bind keyboard shortcuts to window manager actions.
 * Stores associations between Keystroke objects and lists of BFWMAction
 * objects.
 */

#ifndef BFWM_KEYBIND_DICTIONARY_H
#define BFWM_KEYBIND_DICTIONARY_H

#include "../input/keystroke.h"
#include "action.h"
#include <cstddef>
#include <memory>
#include <vector>

/**
 * @brief Associates a keystroke with a list of actions.
 */
class KeyValuePair {
 public:
   /// Keystroke definition (by value)
   Keystroke keystroke;
   /// Owned list of actions bound to the keystroke
   std::vector<std::unique_ptr<BFWMAction>> actions;
};

/**
 * @brief A dictionary mapping keystrokes to actions.
 */
class KeybindDictionary {
 public:
   /// Key-value entries
   std::vector<KeyValuePair> entries;
   /// Bumped whenever the dictionary is mutated
   size_t generation = 0;
};

/**
 * @brief Clear all entries from the dictionary.
 *
 * Destroys every action (via unique_ptr) and empties the entry list.
 * After calling this the dictionary is empty but still usable.
 *
 * @param dict The dictionary to clear
 */
void KBDictClear(KeybindDictionary &dict);

/**
 * @brief Insert a key-action binding into the dictionary.
 *
 * If the keystroke already exists, the action is appended to its action list.
 *
 * @param dict   The keybind dictionary
 * @param key    The keystroke to bind
 * @param action The action to associate with the keystroke (ownership taken)
 */
void KBDictInsert(KeybindDictionary &dict, const Keystroke &key,
                  std::unique_ptr<BFWMAction> action);

/**
 * @brief Look up actions bound to a keystroke.
 *
 * @param dict      The keybind dictionary
 * @param keystroke The keystroke to search for
 * @return std::vector<std::unique_ptr<BFWMAction>>* Pointer to the entry's
 *         action list, or nullptr if the keystroke is not found
 */
auto KBDFind(KeybindDictionary &dict, const Keystroke &keystroke)
    -> std::vector<std::unique_ptr<BFWMAction>> *;

#endif