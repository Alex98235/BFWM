-- BFWM full example configuration.

local mod = "Alt"

-- ── Window manager ──────────────────────────────────────────────
BFWM.gap_between = 6
BFWM.gap_edge = 8
BFWM.border_color = "#ef9f76"
BFWM.inactive_border = "#51576d"
BFWM.border_width = 4
BFWM.border_radius = 8
BFWM.focus_follows_mouse = false
BFWM.mouse_follows_focus = false
BFWM.layout = "dwindle"

-- Disable monitors by display number (optional):
-- BFWM.disabled_monitors = { 3 }

-- ── Bar ─────────────────────────────────────────────────────────
Bar.enabled = true
Bar.height = 32
Bar.corner_radius = 1
Bar.margin = { top = 0, right = 0, bottom = 0, left = 0 }
Bar.padding = { top = 0, right = 0, bottom = 0, left = 0 }
Bar.border = { width = 1, color = "#ef9f76" }
Bar.font = { name = "Segoe UI", size = 18, weight = 800 }
Bar.colors = {
  background         = "#303446",
  text               = "#c6d0f5",
  active_workspace   = "#ef9f76",
  inactive_workspace = "#626880",
  tab_border         = "#232634"
}
Bar.indicators = {
  { type = "workspaces", align = "left", id = "workspaces", position_bar = false, format = "{id}" },
  { type = "title", align = "center", id = "title", max_width = 300 },
  {
    type = "clock",
    align = "right",
    id = "clock",
    format = "%m/%d, %H:%M"
  }
}

-- ── Snackbar ────────────────────────────────────────────────────
Snackbar.enabled = true
Snackbar.log_level = "info"
Snackbar.position = "top-right"
Snackbar.margin_left = 0
Snackbar.margin_right = 16
Snackbar.margin_top = 48
Snackbar.margin_bottom = 0
Snackbar.min_width = 300
Snackbar.max_width = 400
Snackbar.padding_left = 8
Snackbar.padding_right = 8
Snackbar.padding_top = 8
Snackbar.padding_bottom = 8
Snackbar.background = "#303446"
Snackbar.text = "#c6d0f5"
Snackbar.divider = "#f2d5cf"
Snackbar.divider_height = 1
Snackbar.font_size = 20
Snackbar.font_name = "Segoe UI"
Snackbar.corner_radius = 1
Snackbar.opacity = 200
Snackbar.close_on_click = true
Snackbar.pause_on_hover = true
Snackbar.click_to_expand = true
Snackbar.max_queue = 5
Snackbar.monitor = -1
Snackbar.display_duration_ms = 5000
Snackbar.colors = { error = "#e78284", warn = "#e5c890", info = "#8caaee", debug = "#737994", normal = "#ef9f76" }

-- ── Keybinds ────────────────────────────────────────────────────
BFWM.keybinds = {
  -- Window management
  { mod .. " Q", "KillActive", {} },
  { mod .. " X", "Minimize", {} },
  { mod .. " F", "Fullscreen", {} },
  { mod .. " W", "ToggleFloat", {} },

  -- Layout
  { mod .. " P", "SwapSplit", {} },
  { mod .. " Shift P", "ToggleSplit", {} },
  { mod .. " C", "ToggleGaps", {} },
  { mod .. " Space", "CycleLayout", { direction = "next" } },
  { mod .. " Shift Space", "CycleLayout", { direction = "prev" } },
  { mod .. " M", "SetLayout", { layout = "monocle" } },
  { mod .. " D", "SetLayout", { layout = "dwindle" } },

  -- Focus
  { mod .. " Left", "FocusWindow", { direction = "left" } },
  { mod .. " Down", "FocusWindow", { direction = "down" } },
  { mod .. " Up", "FocusWindow", { direction = "up" } },
  { mod .. " Right", "FocusWindow", { direction = "right" } },

  -- Move window
  { mod .. " Shift Left", "MoveWindow", { direction = "left", repeatable = true } },
  { mod .. " Shift Down", "MoveWindow", { direction = "down", repeatable = true } },
  { mod .. " Shift Up", "MoveWindow", { direction = "up", repeatable = true } },
  { mod .. " Shift Right", "MoveWindow", { direction = "right", repeatable = true } },

  -- Resize
  { mod .. " H", "ResizeWindow", { direction = "left", pixels = 50, repeatable = true } },
  { mod .. " J", "ResizeWindow", { direction = "down", pixels = 50, repeatable = true } },
  { mod .. " K", "ResizeWindow", { direction = "up", pixels = 50, repeatable = true } },
  { mod .. " L", "ResizeWindow", { direction = "right", pixels = 50, repeatable = true } },

  -- Workspaces
  { mod .. " 1", "Workspace", { index = 1 } },
  { mod .. " 2", "Workspace", { index = 2 } },
  { mod .. " 3", "Workspace", { index = 3 } },
  { mod .. " 4", "Workspace", { index = 4 } },
  { mod .. " 5", "Workspace", { index = 5 } },
  { mod .. " 6", "Workspace", { index = 6 } },
  { mod .. " 7", "Workspace", { index = 7 } },
  { mod .. " 8", "Workspace", { index = 8 } },
  { mod .. " 9", "Workspace", { index = 9 } },
  { mod .. " 0", "Workspace", { index = 10 } },

  -- Move window to workspace
  { mod .. " Shift 1", "MoveToWorkspace", { index = 1 } },
  { mod .. " Shift 2", "MoveToWorkspace", { index = 2 } },
  { mod .. " Shift 3", "MoveToWorkspace", { index = 3 } },
  { mod .. " Shift 4", "MoveToWorkspace", { index = 4 } },
  { mod .. " Shift 5", "MoveToWorkspace", { index = 5 } },
  { mod .. " Shift 6", "MoveToWorkspace", { index = 6 } },
  { mod .. " Shift 7", "MoveToWorkspace", { index = 7 } },
  { mod .. " Shift 8", "MoveToWorkspace", { index = 8 } },
  { mod .. " Shift 9", "MoveToWorkspace", { index = 9 } },
  { mod .. " Shift 0", "MoveToWorkspace", { index = 10 } },

  -- Move workspace to monitor
  { mod .. " Ctrl Left", "MoveWorkspaceToMonitor", { direction = "left" } },
  { mod .. " Ctrl Right", "MoveWorkspaceToMonitor", { direction = "right" } },
  { mod .. " Ctrl 1", "MoveWorkspaceToMonitor", { index = 1 } },
  { mod .. " Ctrl 2", "MoveWorkspaceToMonitor", { index = 2 } },

  -- Spawn / exec
  -- { mod .. " Return",      "Spawn",                  { command = "wt" } },
  -- { mod .. " Shift Return", "Exec",               { command = "cmd /c echo hi" } },

  -- Reload config
  { mod .. " Ctrl R", "ReloadConfig", {} },

  -- Custom Lua callback
  { mod .. " B", "Call", { lua = "my_custom_action" } }
}

-- ── Window rules ────────────────────────────────────────────────
BFWM.window_rules = {
  {
    match = {
      {
        title = { regex = "[Pp]icture" },
        class = { regex = "Chrome_WidgetWin_1" }
      }
    },
    action = "ignore"
  },
  {
    match = {
      { process = { equals = "notepad.exe" } }
    },
    action = { move_to_workspace = 2 },
    run_once = true
  }
}

-- ── Workspaces ──────────────────────────────────────────────────
BFWM.workspaces = { { "1", { layout = "dwindle" } }, { "2", {} } }

-- ── Custom Lua function (invoked via the "Call" action) ─────────
local function my_custom_action()
  BFWM.notify("Custom action triggered!")
end

-- ── Spotify-style custom indicator (commented; requires a media CLI) ─────
-- BFWM.exec_cache(argv, key[, ttl_ms]) runs a CLI with stdout captured and
-- returns the last cached output (or nil until the first run). It never blocks
-- the UI. `argv` is a plain table of arguments (no shell). Any of these CLIs
-- work if installed: WinKlang, spotifyctl, or a playerctl-for-Windows build.
--
-- Example using a generic "playerctl"-style CLI that prints JSON:
--   playerctl --player=spotify metadata --format '{{status}}|{{artist}}|{{title}}'
--
-- local function spotify_status(id)
--   local out = BFWM.exec_cache(
--     { "playerctl", "--player=spotify", "metadata", "--format",
--       "{{status}}|{{artist}}|{{title}}" },
--     "spotify", 1500)
--   if not out or out == "" then return nil end
--   -- Parse the delimited output (status|artist|title).
--   local status, artist, title = out:match("^([^|]*)|([^|]*)|(.*)$")
--   if not title then return nil end
--   if status ~= "Playing" then
--     return { icon = "", text = title ~= "" and title or "Paused" }
--   end
--   return {
--     text = artist ~= "" and (artist .. " - " .. title) or title,
--     icon = "",
--   }
-- end
--
-- local function spotify_click(id, button, mods)
--   if button ~= "left" then return end
--   BFWM.spawn({ "playerctl", "--player=spotify", "play-pause" })
-- end
--
-- local function spotify_scroll(id, dir)
--   if dir == "up" then
--     BFWM.spawn({ "playerctl", "--player=spotify", "volume", "0.05+" })
--   else
--     BFWM.spawn({ "playerctl", "--player=spotify", "volume", "0.05-" })
--   end
-- end
--
-- Then add to Bar.indicators:
--   { type = "custom", align = "right", id = "spotify",
--     output = "spotify_status", on_click = "spotify_click",
--     on_scroll = "spotify_scroll", poll_rate = 1500 }
