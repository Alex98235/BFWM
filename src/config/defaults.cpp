/**
 * @file defaults.c
 * @brief Applies default configuration values at startup and on config reload.
 */

#include "defaults.h"
#include "../core/bfwm_def.h"
#include "../core/shared_state.h"
#include "../input/keystroke.h"
#include "action.h"
#include "keybinds.h"
#include "lua/parser.h"
#include <cstring>
#include <memory>
#include <minwindef.h>

enum {
   MAX_WORKSPACE_KEYBINDS = 9,
};

void BarConfigDefaults(BarConfig *cfg) {
   *cfg = BarConfig{};
   cfg->height = BFWM_DEFAULT_BAR_HEIGHT_PX;
   cfg->enabled = BFWM_DEFAULT_BAR_ENABLED;
   cfg->padding.left = BFWM_DEFAULT_BAR_PADDING_LEFT;
   cfg->padding.right = BFWM_DEFAULT_BAR_PADDING_RIGHT;
   cfg->border.color = BFWM_DEFAULT_BAR_BORDER_COLOR;
   cfg->border.width = BFWM_DEFAULT_BAR_BORDER_WIDTH;
   cfg->corner_radius = BFWM_DEFAULT_BAR_CORNER_RADIUS;
   cfg->font.size = BFWM_DEFAULT_BAR_FONT_SIZE;
   cfg->font.weight = BFWM_DEFAULT_BAR_FONT_WEIGHT;
   cfg->font.name = BFWM_DEFAULT_BAR_FONT_NAME;
   cfg->colors.background = BFWM_DEFAULT_BAR_BG;
   cfg->colors.text = BFWM_DEFAULT_BAR_TEXT;
   cfg->colors.active_workspace = BFWM_DEFAULT_BAR_ACTIVE_WS;
   cfg->colors.inactive_workspace = BFWM_DEFAULT_BAR_INACTIVE_WS;
   cfg->colors.tab_border = BFWM_DEFAULT_BAR_TAB_BORDER;
   cfg->indicator_count = 3;
   cfg->indicators[0].type = BAR_INDICATOR_WORKSPACES;
   cfg->indicators[0].align = BAR_ALIGN_LEFT;
   cfg->indicators[0].id = "workspaces";
   cfg->indicators[0].format = "{id}";
   cfg->indicators[0].position_bar = false;
   cfg->indicators[1].type = BAR_INDICATOR_TITLE;
   cfg->indicators[1].align = BAR_ALIGN_CENTER;
   cfg->indicators[1].id = "title";
   cfg->indicators[1].max_width = BFWM_DEFAULT_INDICATOR_MAX_WIDTH;
   cfg->indicators[2].type = BAR_INDICATOR_CLOCK;
   cfg->indicators[2].align = BAR_ALIGN_RIGHT;
   cfg->indicators[2].id = "clock";
   cfg->indicators[2].format = "%m/%d, %H:%M";
}

namespace {

inline void insert_bind(SharedState *shared, DWORD vkCode, BOOL alt, BOOL shift,
                        BOOL repeat, BFWMAction *action) {
   Keystroke keystroke;
   memset(&keystroke, 0, sizeof(keystroke));
   keystroke.key.vkCode = vkCode;
   keystroke.modifiers.alt = alt;
   keystroke.modifiers.shift = shift;
   keystroke.repeat = repeat;
   if (action != nullptr) {
      action->repeatable = repeat;
      KBDictInsert(shared->keybinds, keystroke,
                   std::unique_ptr<BFWMAction>(action));
   }
}
} // namespace

void ConfigLoadDefaultKeybinds(SharedState *shared) {
   insert_bind(shared, 'Q', TRUE, FALSE, FALSE, BFWMActionCreateKillActive());
   insert_bind(shared, 'F', TRUE, FALSE, FALSE, BFWMActionCreateFullscreen());
   insert_bind(shared, 'X', TRUE, FALSE, FALSE, BFWMActionCreateMinimize());
   insert_bind(shared, 'W', TRUE, FALSE, FALSE, BFWMActionCreateToggleFloat());

   /* Resize (vim-style HJKL) */
   insert_bind(
       shared, 'H', TRUE, FALSE, TRUE,
       BFWMActionCreateResizeWindow(DirLeft, BFWM_DEFAULT_RESIZE_PX_AMOUNT));
   insert_bind(
       shared, 'J', TRUE, FALSE, TRUE,
       BFWMActionCreateResizeWindow(DirDown, BFWM_DEFAULT_RESIZE_PX_AMOUNT));
   insert_bind(
       shared, 'K', TRUE, FALSE, TRUE,
       BFWMActionCreateResizeWindow(DirUp, BFWM_DEFAULT_RESIZE_PX_AMOUNT));
   insert_bind(
       shared, 'L', TRUE, FALSE, TRUE,
       BFWMActionCreateResizeWindow(DirRight, BFWM_DEFAULT_RESIZE_PX_AMOUNT));

   /* Move focus (arrows) */
   insert_bind(shared, VK_LEFT, TRUE, FALSE, TRUE,
               BFWMActionCreateFocus(DirLeft));
   insert_bind(shared, VK_DOWN, TRUE, FALSE, TRUE,
               BFWMActionCreateFocus(DirDown));
   insert_bind(shared, VK_UP, TRUE, FALSE, TRUE, BFWMActionCreateFocus(DirUp));
   insert_bind(shared, VK_RIGHT, TRUE, FALSE, TRUE,
               BFWMActionCreateFocus(DirRight));

   /* Move window (arrows) */
   insert_bind(shared, VK_LEFT, TRUE, TRUE, TRUE,
               BFWMActionCreateMoveWindow(DirLeft, 0));
   insert_bind(shared, VK_DOWN, TRUE, TRUE, TRUE,
               BFWMActionCreateMoveWindow(DirDown, 0));
   insert_bind(shared, VK_UP, TRUE, TRUE, TRUE,
               BFWMActionCreateMoveWindow(DirUp, 0));
   insert_bind(shared, VK_RIGHT, TRUE, TRUE, TRUE,
               BFWMActionCreateMoveWindow(DirRight, 0));

   /* Workspaces + move-to-workspace */
   for (int i = 1; i <= MAX_WORKSPACE_KEYBINDS; i++) {
      DWORD const virtual_key = (DWORD)'0' + i;
      insert_bind(shared, virtual_key, TRUE, FALSE, FALSE,
                  BFWMActionCreateWorkspace(i, DirNext));
      insert_bind(shared, virtual_key, TRUE, TRUE, FALSE,
                  BFWMActionCreateMoveToWorkspace(i));
   }

   /* Layout */
   insert_bind(shared, 'P', TRUE, FALSE, FALSE, BFWMActionCreateSwapSplit());
   insert_bind(shared, 'P', TRUE, TRUE, FALSE, BFWMActionCreateSplit());
   insert_bind(shared, 'C', TRUE, FALSE, FALSE, BFWMActionCreateToggleGaps());
   insert_bind(shared, 'D', TRUE, FALSE, FALSE,
               BFWMActionCreateCycleLayout(DirNext));
   insert_bind(shared, 'D', TRUE, TRUE, FALSE,
               BFWMActionCreateCycleLayout(DirPrev));

   /* System */
   insert_bind(shared, 'R', TRUE, TRUE, FALSE, BFWMActionCreateReloadConfig());
}

void ConfigLoadDefaults(SharedState *shared) {
   shared->config.gap_between = BFWM_DEFAULT_GAP_BETWEEN;
   shared->config.gap_edge = BFWM_DEFAULT_GAP_EDGE;
   shared->config.gaps_enabled = BFWM_DEFAULT_GAPS_ENABLED;
   shared->config.bar_height = BFWM_DEFAULT_BAR_HEIGHT;
   shared->config.border_color = BFWM_DEFAULT_BORDER_COLOR;
   shared->config.inactive_border = BFWM_DEFAULT_INACTIVE_BORDER;
   shared->config.border_width = BFWM_DEFAULT_BORDER_WIDTH;
   shared->config.border_radius = BFWM_DEFAULT_BORDER_RADIUS;
   shared->config.focus_follows_mouse = BFWM_DEFAULT_FOCUS_FOLLOWS_MOUSE;
   shared->config.mouse_follows_focus = BFWM_DEFAULT_MOUSE_FOLLOWS_FOCUS;
   shared->config.unlock_modifier = BFWM_DEFAULT_UNLOCK_MODIFIER;
   shared->config.unlock_move_modifier = BFWM_DEFAULT_UNLOCK_MOVE_MODIFIER;
   shared->config.default_layout = BFWM_DEFAULT_LAYOUT;
   shared->config.disabled_monitor_count = BFWM_DEFAULT_DISABLED_MONITOR_COUNT;

   BarConfigDefaults(&shared->config.bar_cfg);

   /* Snackbar */
   shared->config.snackbar.enabled = BFWM_DEFAULT_SNACKBAR_ENABLED;
   shared->config.snackbar.log_level = BFWM_DEFAULT_SNACKBAR_LOG_LEVEL;
   shared->config.snackbar.display_duration_ms =
       BFWM_DEFAULT_SNACKBAR_DISPLAY_DURATION;
   shared->config.snackbar.position = BFWM_DEFAULT_SNACKBAR_POSITION;

   shared->config.snackbar.margin_left = BFWM_DEFAULT_SNACKBAR_MARGIN_LEFT;
   shared->config.snackbar.margin_right = BFWM_DEFAULT_SNACKBAR_MARGIN_RIGHT;
   shared->config.snackbar.margin_top = BFWM_DEFAULT_SNACKBAR_MARGIN_TOP;
   shared->config.snackbar.margin_bottom = BFWM_DEFAULT_SNACKBAR_MARGIN_BOTTOM;

   shared->config.snackbar.min_width = BFWM_DEFAULT_SNACKBAR_MIN_WIDTH;
   shared->config.snackbar.max_width = BFWM_DEFAULT_SNACKBAR_MAX_WIDTH;

   shared->config.snackbar.padding_left = BFWM_DEFAULT_SNACKBAR_PADDING_LEFT;
   shared->config.snackbar.padding_right = BFWM_DEFAULT_SNACKBAR_PADDING_RIGHT;
   shared->config.snackbar.padding_top = BFWM_DEFAULT_SNACKBAR_PADDING_TOP;
   shared->config.snackbar.padding_bottom =
       BFWM_DEFAULT_SNACKBAR_PADDING_BOTTOM;

   shared->config.snackbar.background = BFWM_DEFAULT_SNACKBAR_BACKGROUND;
   shared->config.snackbar.text = BFWM_DEFAULT_SNACKBAR_TEXT;
   shared->config.snackbar.divider = BFWM_DEFAULT_SNACKBAR_DIVIDER;
   shared->config.snackbar.divider_height =
       BFWM_DEFAULT_SNACKBAR_DIVIDER_HEIGHT;

   shared->config.snackbar.font_size = BFWM_DEFAULT_SNACKBAR_FONT_SIZE;
   shared->config.snackbar.font_name = BFWM_DEFAULT_SNACKBAR_FONT_NAME;

   shared->config.snackbar.corner_radius = BFWM_DEFAULT_SNACKBAR_CORNER_RADIUS;
   shared->config.snackbar.opacity = BFWM_DEFAULT_SNACKBAR_OPACITY;
   shared->config.snackbar.close_on_click =
       BFWM_DEFAULT_SNACKBAR_CLOSE_ON_CLICK;
   shared->config.snackbar.pause_on_hover =
       BFWM_DEFAULT_SNACKBAR_PAUSE_ON_HOVER;
   shared->config.snackbar.click_to_expand =
       BFWM_DEFAULT_SNACKBAR_CLICK_TO_EXPAND;
   shared->config.snackbar.max_queue = BFWM_DEFAULT_SNACKBAR_MAX_QUEUE;
   shared->config.snackbar.monitor = BFWM_DEFAULT_SNACKBAR_MONITOR;

   shared->config.snackbar.colors.error = BFWM_DEFAULT_SNACKBAR_COLOR_ERROR;
   shared->config.snackbar.colors.warn = BFWM_DEFAULT_SNACKBAR_COLOR_WARN;
   shared->config.snackbar.colors.info = BFWM_DEFAULT_SNACKBAR_COLOR_INFO;
   shared->config.snackbar.colors.debug = BFWM_DEFAULT_SNACKBAR_COLOR_DEBUG;
   shared->config.snackbar.colors.normal = BFWM_DEFAULT_SNACKBAR_COLOR_NORMAL;

   /* Reset keybinds to defaults */
   KBDictClear(shared->keybinds);
   ConfigLoadDefaultKeybinds(shared);
}
