/**
 * @file defaults.c
 * @brief Applies default configuration values at startup and on config reload.
 */

#include "defaults.h"
#include "../core/bfwm_context.h"
#include "../core/bfwm_def.h"
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
   cfg->indicators[0].show_position_bar = false;
   cfg->indicators[1].type = BAR_INDICATOR_TITLE;
   cfg->indicators[1].align = BAR_ALIGN_CENTER;
   cfg->indicators[1].max_width = BFWM_DEFAULT_INDICATOR_MAX_WIDTH;
   cfg->indicators[2].type = BAR_INDICATOR_CLOCK;
   cfg->indicators[2].align = BAR_ALIGN_RIGHT;
   cfg->indicators[2].format = "%m/%d, %H:%M";
}

namespace {

inline void insert_bind(BFWMContext *ctx, DWORD vkCode, BOOL alt, BOOL shift,
                        BOOL repeat, BFWMAction *action) {
   Keystroke keystroke;
   memset(&keystroke, 0, sizeof(keystroke));
   keystroke.key.vkCode = vkCode;
   keystroke.modifiers.alt = alt;
   keystroke.modifiers.shift = shift;
   keystroke.repeat = repeat;
   if (action != nullptr) {
      action->repeatable = repeat;
      KBDictInsert(ctx->keybinds, keystroke,
                   std::unique_ptr<BFWMAction>(action));
   }
}
} // namespace

void ConfigLoadDefaultKeybinds(BFWMContext *ctx) {
   insert_bind(ctx, 'Q', TRUE, FALSE, FALSE, BFWMActionCreateKillActive());
   insert_bind(ctx, 'F', TRUE, FALSE, FALSE, BFWMActionCreateFullscreen());
   insert_bind(ctx, 'X', TRUE, FALSE, FALSE, BFWMActionCreateMinimize());
   insert_bind(ctx, 'W', TRUE, FALSE, FALSE, BFWMActionCreateToggleFloat());

   /* Resize (vim-style HJKL) */
   insert_bind(
       ctx, 'H', TRUE, FALSE, TRUE,
       BFWMActionCreateResizeWindow(DirLeft, BFWM_DEFAULT_RESIZE_PX_AMOUNT));
   insert_bind(
       ctx, 'J', TRUE, FALSE, TRUE,
       BFWMActionCreateResizeWindow(DirDown, BFWM_DEFAULT_RESIZE_PX_AMOUNT));
   insert_bind(
       ctx, 'K', TRUE, FALSE, TRUE,
       BFWMActionCreateResizeWindow(DirUp, BFWM_DEFAULT_RESIZE_PX_AMOUNT));
   insert_bind(
       ctx, 'L', TRUE, FALSE, TRUE,
       BFWMActionCreateResizeWindow(DirRight, BFWM_DEFAULT_RESIZE_PX_AMOUNT));

   /* Move focus (arrows) */
   insert_bind(ctx, VK_LEFT, TRUE, FALSE, TRUE, BFWMActionCreateFocus(DirLeft));
   insert_bind(ctx, VK_DOWN, TRUE, FALSE, TRUE, BFWMActionCreateFocus(DirDown));
   insert_bind(ctx, VK_UP, TRUE, FALSE, TRUE, BFWMActionCreateFocus(DirUp));
   insert_bind(ctx, VK_RIGHT, TRUE, FALSE, TRUE,
               BFWMActionCreateFocus(DirRight));

   /* Move window (arrows) */
   insert_bind(ctx, VK_LEFT, TRUE, TRUE, TRUE,
               BFWMActionCreateMoveWindow(DirLeft, 0));
   insert_bind(ctx, VK_DOWN, TRUE, TRUE, TRUE,
               BFWMActionCreateMoveWindow(DirDown, 0));
   insert_bind(ctx, VK_UP, TRUE, TRUE, TRUE,
               BFWMActionCreateMoveWindow(DirUp, 0));
   insert_bind(ctx, VK_RIGHT, TRUE, TRUE, TRUE,
               BFWMActionCreateMoveWindow(DirRight, 0));

   /* Workspaces + move-to-workspace */
   for (int i = 1; i <= MAX_WORKSPACE_KEYBINDS; i++) {
      DWORD const virtual_key = (DWORD)'0' + i;
      insert_bind(ctx, virtual_key, TRUE, FALSE, FALSE,
                  BFWMActionCreateWorkspace(i, DirNext));
      insert_bind(ctx, virtual_key, TRUE, TRUE, FALSE,
                  BFWMActionCreateMoveToWorkspace(i));
   }

   /* Layout */
   insert_bind(ctx, 'P', TRUE, FALSE, FALSE, BFWMActionCreateSwapSplit());
   insert_bind(ctx, 'P', TRUE, TRUE, FALSE, BFWMActionCreateSplit());
   insert_bind(ctx, 'C', TRUE, FALSE, FALSE, BFWMActionCreateToggleGaps());
   insert_bind(ctx, 'D', TRUE, FALSE, FALSE,
               BFWMActionCreateCycleLayout(DirNext));
   insert_bind(ctx, 'D', TRUE, TRUE, FALSE,
               BFWMActionCreateCycleLayout(DirPrev));

   /* System */
   insert_bind(ctx, 'R', TRUE, TRUE, FALSE, BFWMActionCreateReloadConfig());
}

void ConfigLoadDefaults(BFWMContext *ctx) {
   ctx->config.gap_between = BFWM_DEFAULT_GAP_BETWEEN;
   ctx->config.gap_edge = BFWM_DEFAULT_GAP_EDGE;
   ctx->config.gaps_enabled = BFWM_DEFAULT_GAPS_ENABLED;
   ctx->config.bar_height = BFWM_DEFAULT_BAR_HEIGHT;
   ctx->config.border_color = BFWM_DEFAULT_BORDER_COLOR;
   ctx->config.inactive_border = BFWM_DEFAULT_INACTIVE_BORDER;
   ctx->config.border_width = BFWM_DEFAULT_BORDER_WIDTH;
   ctx->config.border_radius = BFWM_DEFAULT_BORDER_RADIUS;
   ctx->config.focus_follows_mouse = BFWM_DEFAULT_FOCUS_FOLLOWS_MOUSE;
   ctx->config.mouse_follows_focus = BFWM_DEFAULT_MOUSE_FOLLOWS_FOCUS;
   ctx->config.unlock_modifier = BFWM_DEFAULT_UNLOCK_MODIFIER;
   ctx->config.unlock_move_modifier = BFWM_DEFAULT_UNLOCK_MOVE_MODIFIER;
   ctx->config.default_layout = BFWM_DEFAULT_LAYOUT;
   ctx->config.disabled_monitor_count = BFWM_DEFAULT_DISABLED_MONITOR_COUNT;

   BarConfigDefaults(&ctx->config.bar_cfg);

   /* Snackbar */
   ctx->config.snackbar.enabled = BFWM_DEFAULT_SNACKBAR_ENABLED;
   ctx->config.snackbar.log_level = BFWM_DEFAULT_SNACKBAR_LOG_LEVEL;
   ctx->config.snackbar.display_duration_ms =
       BFWM_DEFAULT_SNACKBAR_DISPLAY_DURATION;
   ctx->config.snackbar.position = BFWM_DEFAULT_SNACKBAR_POSITION;

   ctx->config.snackbar.margin_left = BFWM_DEFAULT_SNACKBAR_MARGIN_LEFT;
   ctx->config.snackbar.margin_right = BFWM_DEFAULT_SNACKBAR_MARGIN_RIGHT;
   ctx->config.snackbar.margin_top = BFWM_DEFAULT_SNACKBAR_MARGIN_TOP;
   ctx->config.snackbar.margin_bottom = BFWM_DEFAULT_SNACKBAR_MARGIN_BOTTOM;

   ctx->config.snackbar.min_width = BFWM_DEFAULT_SNACKBAR_MIN_WIDTH;
   ctx->config.snackbar.max_width = BFWM_DEFAULT_SNACKBAR_MAX_WIDTH;

   ctx->config.snackbar.padding_left = BFWM_DEFAULT_SNACKBAR_PADDING_LEFT;
   ctx->config.snackbar.padding_right = BFWM_DEFAULT_SNACKBAR_PADDING_RIGHT;
   ctx->config.snackbar.padding_top = BFWM_DEFAULT_SNACKBAR_PADDING_TOP;
   ctx->config.snackbar.padding_bottom = BFWM_DEFAULT_SNACKBAR_PADDING_BOTTOM;

   ctx->config.snackbar.background = BFWM_DEFAULT_SNACKBAR_BACKGROUND;
   ctx->config.snackbar.text = BFWM_DEFAULT_SNACKBAR_TEXT;
   ctx->config.snackbar.divider = BFWM_DEFAULT_SNACKBAR_DIVIDER;
   ctx->config.snackbar.divider_height = BFWM_DEFAULT_SNACKBAR_DIVIDER_HEIGHT;

   ctx->config.snackbar.font_size = BFWM_DEFAULT_SNACKBAR_FONT_SIZE;
   ctx->config.snackbar.font_name = BFWM_DEFAULT_SNACKBAR_FONT_NAME;

   ctx->config.snackbar.corner_radius = BFWM_DEFAULT_SNACKBAR_CORNER_RADIUS;
   ctx->config.snackbar.opacity = BFWM_DEFAULT_SNACKBAR_OPACITY;
   ctx->config.snackbar.close_on_click = BFWM_DEFAULT_SNACKBAR_CLOSE_ON_CLICK;
   ctx->config.snackbar.pause_on_hover = BFWM_DEFAULT_SNACKBAR_PAUSE_ON_HOVER;
   ctx->config.snackbar.click_to_expand = BFWM_DEFAULT_SNACKBAR_CLICK_TO_EXPAND;
   ctx->config.snackbar.max_queue = BFWM_DEFAULT_SNACKBAR_MAX_QUEUE;
   ctx->config.snackbar.monitor = BFWM_DEFAULT_SNACKBAR_MONITOR;

   ctx->config.snackbar.colors.error = BFWM_DEFAULT_SNACKBAR_COLOR_ERROR;
   ctx->config.snackbar.colors.warn = BFWM_DEFAULT_SNACKBAR_COLOR_WARN;
   ctx->config.snackbar.colors.info = BFWM_DEFAULT_SNACKBAR_COLOR_INFO;
   ctx->config.snackbar.colors.debug = BFWM_DEFAULT_SNACKBAR_COLOR_DEBUG;
   ctx->config.snackbar.colors.normal = BFWM_DEFAULT_SNACKBAR_COLOR_NORMAL;

   /* Reset keybinds to defaults */
   KBDictClear(ctx->keybinds);
   ConfigLoadDefaultKeybinds(ctx);
}
