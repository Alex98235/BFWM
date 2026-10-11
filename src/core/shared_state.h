/**
 * @file shared_state.h
 * @brief State readable by both the main thread and the input/event worker.
 *
 * SharedState holds the configuration-derived state that the worker thread
 * reads without holding the main context's lock:
 *
 *   - `config`         runtime configuration; the worker reads
 *                      `config.focus_follows_mouse`.
 *   - `keybinds`       keystroke dictionary; the worker calls KBDFind().
 *   - `main_thread_id` main thread id; the worker posts messages to it via
 *                      PostThreadMessage().
 *
 * Threading contract:
 *   - The main thread owns all writes. Writes happen during BFWMInit(),
 *     BFWMSetup(), and Lua config (re)loads — only ever on the main
 *     thread, before the worker may observe the new value.
 *   - The worker thread only reads. It must never mutate SharedState.
 *   - Reads are deliberately lock-free. MainContext::lock does NOT cover
 *     SharedState.
 *
 * This header is intentionally free of subsystem types (Window, Workspace,
 * Monitor, ...) so translation units that only need configuration do not pull
 * the full bfwm_context.h include chain.
 */

#ifndef BFWM_SHARED_STATE_H
#define BFWM_SHARED_STATE_H

#include "../config/keybinds.h"
#include "../config/lua/parser.h"
#include "../notification/snackbar.h"

#include <array>
#include <windows.h>

/**
 * @brief Configuration-derived state shared with the worker thread.
 *
 * See the file header for the threading contract.
 */
struct SharedState {
   /** @name Runtime configuration (populated by ConfigLoadDefaults /
    * config.lua)
    */
   /** @{ */
   struct {
      /// Pixel gap between tiled windows
      int gap_between = 0;
      /// Pixel gap at screen edges
      int gap_edge = 0;
      /// Runtime toggle for all gaps
      BOOL gaps_enabled = FALSE;
      /// Active window border color
      COLORREF border_color = 0;
      /// Inactive window border color
      COLORREF inactive_border = 0;
      /// Active border thickness in logical pixels
      int border_width = 0;
      /// Active border corner radius in logical pixels (0 = square)
      int border_radius = 0;
      /// Auto-focus on mouse hover
      BOOL focus_follows_mouse = FALSE;
      /// Move cursor to newly focused window
      BOOL mouse_follows_focus = FALSE;
      /// Reserved bar height in pixels
      int bar_height = 0;
      /// Bar appearance and indicator config
      BarConfig bar_cfg;
      /// VK code to unlock resize (0 = always unlocked)
      DWORD unlock_modifier = 0;
      /// VK code to unlock drag-move (0 = always unlocked)
      DWORD unlock_move_modifier = 0;
      /// Default layout algorithm for new workspaces
      LayoutType default_layout = DWINDLE;
      /// Per-workspace overrides declared via BFWM.workspaces
      std::array<WorkspaceConfig, 32> workspace_configs = {};
      int workspace_config_count = 0;
      /// Monitors disabled via config (display numbers)
      std::array<int, 16> disabled_monitors = {};
      int disabled_monitor_count = 0;
      SnackbarConfig snackbar;
   } config;
   /** @} */

   /** @name Keybind dictionary (worker reads via KBDFind) */
   /** @{ */
   KeybindDictionary keybinds;
   /** @} */

   /**
    * @brief Main thread id, for PostThreadMessage from the worker.
    *
    * Constant after BFWMInit(); read by the worker's hook callbacks.
    */
   DWORD main_thread_id = 0;
};

#endif
