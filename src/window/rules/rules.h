/**
 * @file rules.h
 * @brief Window matching rules engine.
 *
 * Defines a rule system that matches windows against criteria
 * (process name, class, title) and applies actions such as
 * ignoring, floating, or moving to a specific workspace.
 */

#ifndef BFWM_WINDOW_RULE_H
#define BFWM_WINDOW_RULE_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>

struct BFWMContext;
class Window;

/**
 * @brief Comparison operator for a match criterion.
 */
using MatchOperator = enum {
   /// Exact string match
   MATCH_EQUALS,
   /// Case-insensitive substring match
   MATCH_INCLUDES,
   /// POSIX extended regular expression match
   MATCH_REGEX,
   /// Negated exact match
   MATCH_NOT_EQUALS,
   /// Negated regex match
   MATCH_NOT_REGEX,
};

/**
 * @brief A single match criterion (field + operator + pattern).
 */
using WindowMatchCriterion = struct WindowMatchCriterion {
   /// How to compare
   MatchOperator op;
   /// Pattern to match against
   std::string pattern;
};

/**
 * @brief A set of criteria for one rule entry.
 *
 * All specified criteria must match for the entry to apply (AND logic).
 */
using WindowMatchEntry = struct WindowMatchEntry {
   /// Process name criterion (or none)
   std::optional<WindowMatchCriterion> process_match;
   /// Window class criterion (or none)
   std::optional<WindowMatchCriterion> class_match;
   /// Window title criterion (or none)
   std::optional<WindowMatchCriterion> title_match;
};

/**
 * @brief Types of actions a rule can take.
 */
using WindowRuleActionType = enum {
   /// Ignore the window (don't manage)
   RULE_IGNORE,
   /// Mark window as floating
   RULE_SET_FLOATING,
   /// Assign window to a specific workspace
   RULE_MOVE_TO_WORKSPACE,
   /// Force window to be floating (override)
   RULE_FORCE_FLOATING,
   /// Force window to be tiled (override)
   RULE_FORCE_TILED,
};

/**
 * @brief The action to apply when a rule matches.
 */
using WindowRuleAction = struct WindowRuleAction {
   /// What action to take
   WindowRuleActionType type;
   /// Workspace index (for MOVE_TO_WORKSPACE)
   int workspace_index;
};

/**
 * @brief A complete window rule.
 */
using WindowRule = struct WindowRule {
   /// Match entries (OR'd together)
   std::vector<WindowMatchEntry> entries;
   /// Action to apply on match
   WindowRuleAction action;
   /// Only apply once per window
   BOOL run_once;
};

/**
 * @brief Result of applying rules to a window.
 */
using WindowRuleResult = enum {
   /// No rule matched, continue normal handling
   RULE_RESULT_CONTINUE,
   /// A rule matched and was applied
   RULE_RESULT_APPLIED,
};

/**
 * @brief Free a window rule and all its entries.
 * @param rule Rule to destroy
 */
void WindowRuleDestroy(WindowRule *rule);

/**
 * @brief Check if a rule matches a given window.
 * @param rule       The rule to test
 * @param process    Process name
 * @param class_name Window class name
 * @param title      Window title
 * @return true if any entry matches
 */
auto WindowRuleMatch(const WindowRule *rule, const char *process,
                     const char *class_name, const char *title) -> bool;

/**
 * @brief Get the process name for a window handle.
 * @param hwnd     Window handle
 * @param buf      Output buffer
 * @param buf_size Buffer size
 * @return Pointer to buf, or NULL on failure
 */
auto GetWindowProcessName(HWND hwnd, char *buf, size_t buf_size) -> const
    char *;

/**
 * @brief Apply all relevant rules to a window.
 *
 * Iterates the global rules list and applies the first matching
 * rule's action.
 *
 * @param ctx        The BFWM context
 * @param window     The window to apply rules to
 * @param class_name Window class name
 * @param title      Window title
 * @param process    Process name
 * @return RULE_RESULT_APPLIED if a rule matched, RULE_RESULT_CONTINUE otherwise
 */
auto WindowRulesApply(struct BFWMContext *ctx, Window *window,
                      const char *class_name, const char *title,
                      const char *process) -> WindowRuleResult;

#endif
