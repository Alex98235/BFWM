#include "keybinds.h"
#include "../input/keystroke.h"
#include "action.h"
#include <memory>
#include <utility>
#include <vector>

namespace {

/* Compare only the meaningful fields of a keystroke: vkCode + modifiers */
inline auto keystrokes_equal(const Keystroke &a, const Keystroke &b) -> bool {
   return a.key.vkCode == b.key.vkCode &&
          a.modifiers.ctrl == b.modifiers.ctrl &&
          a.modifiers.shift == b.modifiers.shift &&
          a.modifiers.alt == b.modifiers.alt &&
          a.modifiers.super == b.modifiers.super;
}
} // namespace

void KBDictClear(KeybindDictionary &dict) {
   dict.entries.clear();
   dict.generation++;
}

void KBDictInsert(KeybindDictionary &dict, const Keystroke &key,
                  std::unique_ptr<BFWMAction> action) {
   /* Check if key already exists — append to its action list */
   for (auto &entry : dict.entries) {
      if (keystrokes_equal(entry.keystroke, key)) {
         entry.actions.push_back(std::move(action));
         return;
      }
   }

   /* Key does not exist, create new entry */
   KeyValuePair kvp;
   kvp.keystroke = key;
   kvp.actions.push_back(std::move(action));
   dict.entries.push_back(std::move(kvp));
   dict.generation++;
}

auto KBDFind(KeybindDictionary &dict, const Keystroke &keystroke)
    -> std::vector<std::unique_ptr<BFWMAction>> * {
   for (auto &entry : dict.entries) {
      if (keystrokes_equal(entry.keystroke, keystroke))
         return &entry.actions;
   }
   return nullptr;
}
