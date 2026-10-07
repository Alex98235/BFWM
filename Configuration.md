# BFWM Configuration

## Location

BFWM looks for a `config.lua` file in these locations, in order:

1. `%APPDATA%\BFWM\config.lua` — per-user application data directory
2. `%USERPROFILE%\.config\BFWM\config.lua` — fallback XDG-style config directory
3. `config.lua` in the directory where you launched BFWM (the working directory)
4. `config.lua` one directory above the launch directory (useful when running from the build folder during development)

## BFWM.* (Window Manager)

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `gap_between` | integer | `6` | Pixel gap between tiled windows |
| `gap_edge` | integer | `6` | Pixel gap at screen edges |
| `border_color` | string | `"#00ff00"` | Focused window border color (hex `#RRGGBB`) |
| `inactive_border` | string | `"#222222"` | Unfocused window border color (hex `#RRGGBB`) |
| `border_width` | integer | `2` | Focused window border thickness in pixels |
| `border_radius` | integer | `8` | Focused window border corner radius in pixels. `0` makes corners square; corners are also always square when a window is maximized/snapped |
| `focus_follows_mouse` | boolean | `false` | Auto-focus the window under the cursor |
| `mouse_follows_focus` | boolean | `false` | Move cursor to the newly focused window |
| `unlock_window_resize` | string | — | Modifier key (`"LALT"`, `"LCTRL"`, etc.) to hold while resizing tiled windows with the mouse. Empty (default) means always unlocked |
| `unlock_window_move` | string | — | Modifier key to hold while drag-moving tiled windows with the mouse. Empty (default) means always unlocked |
| `layout` | string | `"dwindle"` | Default layout algorithm. One of `"dwindle"` or `"monocle"` |
| `keybinds` | array | — | Array of keybind entries (see [Keybinds](#keybinds) below) |
| `window_rules` | array | see [Ignored windows classes](#ignored-window-classes) | Array of window rule entries (see [BFWM.window_rules](#bfwmwindow_rules) for syntax) |
| `workspaces` | array | — | Per-workspace configuration (see [BFWM.workspaces](#bfwmworkspaces)) |
| `disabled_monitors` | array | — | Array of display numbers to ignore (e.g. `{ 3 }` to skip monitor 3) |

## BFWM.window_rules

Window rules let you automatically apply behaviours to windows as they are
created, based on their process name, window class, or title.

### Rule structure

```lua
BFWM.window_rules = {
  {
    match = {
      { <field> = { <operator> = "<pattern>" }, ... },
      ...
    },
    action = "<action>",
    run_once = true,   -- optional, defaults to true
  },
}
```

### Match entries

Each rule has a `match` array with one or more entries. Entries are evaluated
in order — the first entry that matches wins.

An entry can match against three window properties:

| Field | Description |
| --- | --- |
| `process` | The executable name (e.g. `"notepad.exe"`, `"chrome.exe"`) |
| `class`   | The window class name (e.g. `"Notepad"`, `"Chrome_WidgetWin_1"`) |
| `title`   | The window title bar text |

Each field uses an **operator** to describe how to compare:

| Operator | Matches when |
| --- | --- |
| `equals` | The value equals the pattern (case-insensitive) |
| `not_equals` | The value does **not** equal the pattern |
| `includes` | The value contains the pattern as a substring |
| `regex` | The value matches the pattern as a regular expression |
| `not_regex` | The value does **not** match the pattern |

A rule's `match` array can contain multiple entries. The rule fires when a window matches
**any** entry in the array.

Each entry specifies one or more field comparisons. For an entry to match,
**all** of its comparisons must succeed.

For example, a rule with two entries:

```lua
  { class = { equals = "Chrome_WidgetWin_1" } },   -- entry 1
  { process = { equals = "firefox.exe" } },        -- entry 2
```

Chrome matches entry 1, Firefox matches entry 2 — either triggers the rule.

An entry with multiple fields:

```lua
  { class = { equals = "Notepad" },
    title = { includes = " - Notepad" } }
```

requires BOTH the class AND the title to match.

### Actions

| Action | Effect |
| --- | --- |
| `"ignore"` | Do not manage this window at all — it behaves as an unmanaged overlay |
| `"set-floating"` | Start the window in floating state;  can toggle it back to tiled |
| `"force-floating"` | Always keep the window floating; tile keybinds (`ToggleFloat`, layout changes) have no effect |
| `"force-tiled"` | Always keep the window tiled; float keybinds have no effect |
| `{ move_to_workspace = N }` | Move the window to workspace `N` on creation |

The `move_to_workspace` action is a Lua table instead of a string:

```lua
action = { move_to_workspace = 3 },
```

### `run_once`

Defaults to `true`. When `true`, the rule is applied only to the first window
that matches it. Subsequent windows that match are ignored. Set to `false` to
apply the rule to every matching window that appears.

### Examples

```lua
BFWM.window_rules = {
  -- Send paint.net windows to workspace 3
  {
    match = {
      { process = { equals = "paintdotnet.exe" } },
    },
    action = { move_to_workspace = 3 },
    run_once = false,
  },

  -- Match by title regex (any window with "Picture" in the class "Chrome_*")
  {
    match = {
      { title = { regex = "[Pp]icture" },
        class = { regex = "Chrome_WidgetWin_1" } },
    },
    action = "ignore",
  },
}
```

### Ignored window classes

Certain Windows windows are hardcoded to be ignored by BFWM because they
misbehave or serve no purpose when managed as tiled windows:

| Class | Description |
| --- | --- |
| `Progman`, `WorkerW` | Desktop background |
| `Shell_TrayWnd`, `Shell_SecondaryTrayWnd` | Taskbar |
| `MultitaskingViewFrame` | Task View |
| `TaskManagerWindow` | Task Manager — resists positioning |
| `ApplicationFrameWindow` | UWP app container (Calculator, Settings, etc.) |
| `EdgeUiInputTopWndClass` | Edge/Chrome UI overlays |
| `BFWMMonitorHelper` | Internal hidden BFWM window |

These classes are defined in `src/window/filter.c`. User-defined `"ignore"`
rules via `BFWM.window_rules` are evaluated after this hardcoded list.

## BFWM.workspaces

Each entry in `BFWM.workspaces` configures a workspace by ID with optional
label, monitor assignment, and layout override:

```lua
BFWM.workspaces = {
  { "<workspace_id>", { label = "<label>", monitor = <N>, layout = "<layout>" } },
  ...
}
```

| Field | Type | Default | Description |
| --- | --- | --- | --- |
| `workspace_id` | string | — | Numeric workspace ID (e.g. `"1"`, `"2"`) |
| `label` | string | — | User-facing label shown in the bar (e.g. `"coding"`, `"media"`). Requires a `format` string on the bar indicator (see [Bar.indicators\[\]](#barindicators)) |
| `monitor` | integer | — | Display number to pin this workspace to. When set, the workspace is **immovable** — `MoveWorkspaceToMonitor` is blocked. Keyboard shortcuts and window moves redirect to the assigned monitor. If the monitor does not exist, a warning is logged at config load time. |
| `layout` | string | — | Layout algorithm for this workspace, overriding `BFWM.layout`. One of `"dwindle"` or `"monocle"` |

### Behavior

- When a workspace is pinned to a monitor (`monitor = N`), pressing its keybind
  from any monitor activates it on the assigned monitor (cross-monitor jump).
- Moving a window to a pinned workspace redirects the window to the assigned monitor.
- If the assigned monitor is disconnected, workspace activation falls back to the current
  monitor.

### Example

```lua
BFWM.workspaces = {
  { "1", { label = "coding", monitor = 1, layout = "dwindle" } },
  { "2", { label = "media",  monitor = 1 } },
  { "3", { label = "chat",   monitor = 2 } },
  { "4", { } }, -- valid configuration but does nothing
}
```

## Keybinds

Each entry in `BFWM.keybinds` is a Lua table:

```lua
{ "<key_combination>", "<ActionName>", { <options> } }
```

### Key format

`<key_combination>` is one or more modifier names followed by a key name,
separated by spaces:

| Modifier | Names |
| --- | --- |
| Alt | `Alt` |
| Ctrl | `Ctrl`, `Control` |
| Shift | `Shift` |
| Super | `Super`, `Win` |

Key names can be:

- A single letter: `A`–`Z`
- A digit: `0`–`9`
- A named key: `Left`, `Right`, `Up`, `Down`, `Return`/`Enter`, `Space`, `Tab`,
  `Escape`/`Esc`, `Backspace`, `F1`–`F24`

Examples: `"Alt Return"`, `"Alt Shift H"`, `"Alt Ctrl Left"`.

> **Note:** The default modifier key is `Alt`. This avoids conflicts with
> Windows system shortcuts (such as `Win+L` for lock screen). If you prefer to
> use `Super` (the Windows key) as your modifier, be aware that `Win+L` is
> handled by Winlogon below the low-level keyboard hook and will still lock
> the screen even when BFWM's hook blocks it. To fully suppress it, add
> the following registry key and restart:
>
> ```powershell
> New-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Policies\System" `
>   -Name "DisableLockWorkstation" -Value 1 -PropertyType DWORD -Force
> ```
>
> This removes the `Lock` option from `Win+L` (and from `Ctrl+Alt+Delete`).
> To lock your machine you can instead use `Win` → user menu → Lock, or set a
> custom keybind via AutoHotkey or similar. Revert by setting `DisableLockWorkstation` to `0`.

### Action reference

| Action | Options | Description |
| --- | --- | --- |
| `KillActive` | — | Close the focused window (`WM_CLOSE`) |
| `Fullscreen` | — | Toggle fullscreen on the focused window |
| `ToggleFloat` | — | Toggle floating / tiled on the focused window |
| `FocusWindow` | `direction` | Focus the adjacent window in `direction`: `"left"`, `"right"`, `"up"`, `"down"`, `"next"`, `"prev"`. Falls back to the neighbouring monitor's active workspace if no window exists in that direction |
| `MoveWindow` | `direction` | Move / swap the focused window in `direction`. Moves across monitors when at a screen edge |
| `ResizeWindow` | `direction`, `pixels` | Resize the focused window in `direction` by `pixels` (default `50`) |
| `SwapSplit` | — | Swap the two children of the parent container |
| `ToggleSplit` | — | Toggle split orientation (horizontal ↔ vertical) |
| `Workspace` | `index` | Switch to workspace `index` (`1`–`10`). JIT-creates the workspace if it doesn't exist on the current monitor |
| `MoveToWorkspace` | `index` | Move the focused window to workspace `index`. JIT-creates the workspace on the current monitor if it doesn't exist globally |
| `MoveWorkspaceToMonitor` | `index` **or** `direction` | Move the active workspace to a different monitor. Use `index` to target a display number, or `direction` (`"left"`, `"right"`, `"next"`, `"prev"`) for relative movement |
| `CycleLayout` | `direction` | Cycle through layout algorithms: `"next"` or `"prev"` |
| `SetLayout` | `layout` | Switch to a specific layout: `"dwindle"` or `"monocle"` |
| `ToggleGaps` | — | Toggle all gaps on / off |
| `Spawn` | `command` | Launch a GUI process (`command` is a string, e.g. `"code"`, `"C:\\Programs\\emacs.exe"`) |
| `Exec` | `command` | Run a shell command without creating a window |
| `Call` | `lua` | Call a Lua function by name (`lua` is a string, e.g. `"my_custom_action"`) |
| `ReloadConfig` | — | Re-parse `config.lua` — updates all settings, keybinds, window rules, and bar appearance |

### Common option

All actions accept an optional `repeatable = true` option. When set, the action
fires repeatedly while the key is held (keyboard auto-repeat). By default
actions fire only on the initial key press.

### Examples

```lua
BFWM.keybinds = {
  -- Kill focused window with Alt+Q
  { "Alt Q",               "KillActive",           {} },

  -- Focus right with Alt+L
  { "Alt L",               "FocusWindow",           { direction = "right" } },

  -- Move window right with Alt+Shift+L
  { "Alt Shift L",         "MoveWindow",            { direction = "right", repeatable = true } },

  -- Switch to workspace 3 with Alt+3
  { "Alt 3",               "Workspace",             { index = 3 } },

  -- Move focused window to workspace 4 with Alt+Shift+4
  { "Alt Shift 4",         "MoveToWorkspace",       { index = 4 } },

  -- Move active workspace to monitor 2 with Alt+Ctrl+2
  { "Alt Ctrl 2",          "MoveWorkspaceToMonitor", { index = 2 } },

  -- Move active workspace right with Alt+Ctrl+Right
  { "Alt Ctrl Right",      "MoveWorkspaceToMonitor", { direction = "right" } },

  -- Launch a process (command is passed directly to CreateProcess)
  { "Alt Return",          "Spawn",                 { command = "notepad" } },

  -- Reload config with Alt+Ctrl+R
  { "Alt Ctrl R",          "ReloadConfig",          {} },
}
```

## Bar.* (Status Bar)

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `enabled` | boolean | `true` | Show the per-monitor status bar |
| `height` | integer | `30` | Bar height in pixels |
| `corner_radius` | integer | `0` | Bar window corner rounding in pixels (`0` = sharp corners) |
| `margin` | table | `{ top = 0, right = 0, bottom = 0, left = 0 }` | Outer margin around the bar window. `margin.bottom` adds extra space below a non-floating bar |
| `margin.top` | integer | `0` | Pixels above the bar |
| `margin.right` | integer | `0` | Pixels to the right of the bar |
| `margin.bottom` | integer | `0` | Pixels below the bar (shrinks work area when non-floating) |
| `margin.left` | integer | `0` | Pixels to the left of the bar |
| `padding` | table | `{ left = 6, right = 6, top = 0, bottom = 0 }` | Inner padding inside the bar around indicator content |
| `padding.left` | integer | `6` | Left inner padding |
| `padding.right` | integer | `6` | Right inner padding |
| `padding.top` | integer | `0` | Top inner padding |
| `padding.bottom` | integer | `0` | Bottom inner padding |
| `border` | table | `{ width = 0, color = "#ffffff" }` | Bar border. `width = 0` disables the border |
| `border.width` | integer | `0` | Border thickness in pixels |
| `border.color` | string | `"#ffffff"` | Border color (hex `#RRGGBB`) |
| `font.name` | string | `"Segoe UI"` | Font face for bar text |
| `font.size` | integer | `11` | Font size in points |
| `font.weight` | integer | `400` | Font weight (`400` = normal, `700` = bold) |
| `colors.background` | string | `"#1a1a1a"` | Bar background color (hex `#RRGGBB`) |
| `colors.text` | string | `"#cccccc"` | Default text color |
| `colors.active_workspace` | string | `"#00aaff"` | Active workspace tab color |
| `colors.inactive_workspace` | string | `"#444444"` | Inactive workspace tab color |
| `colors.tab_border` | string | `"#333333"` | Tab border color |
| `indicators` | array | — | Array of indicator entries (see below) |

### Bar.indicators[]

Each entry in `Bar.indicators` is a Lua table:

```lua
{ type = "<indicator_type>", align = "<alignment>", ... }
```

Common options (available on all types):

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `type` | string | — | Indicator type (see per-type tables below) |
| `align` | string | `"left"` | Horizontal alignment: `"left"`, `"center"`, or `"right"` |
| `max_width` | integer | — | Maximum width in pixels |
| `size` | integer | — | Font size in points (overrides `Bar.font.size`) |
| `format` | string | `"{icon}"` (workspaces: `"{id}"`, clock: `"%H:%M"`) | Format string (see [Format specifiers](#format-specifiers)) |
| `color` | string | — | Text color (hex `#RRGGBB`). Overrides `Bar.colors.text` |

#### `"workspaces"`

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `show_position_bar` | boolean | `false` | Show a 3px scrollbar-style position indicator on workspace tabs |
| `format` | string | `"{id}"` | Format for workspace tab text (supports `{id}`, `{label}`) |

#### `"title"`

*(no type-specific options)*

#### `"clock"`

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `format` | string | `"%H:%M"` | `strftime`-style format string (see [strftime reference][strftime]) |

[strftime]: https://man.archlinux.org/man/strftime.3

#### `"volume"`

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `format` | string | `"{icon}"` | Format string (supports `{icon}`, `{percent}`, `{level}`) |
| `icons` | string[5] | — | Level icons, mapped 0%–100% |
| `icon_muted` | string | — | Icon shown when audio is muted |
| `color_muted` | string | — | Text color when muted |

#### `"network"`

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `icons` | string[5] | — | Signal strength icons, mapped 0%–100% |
| `icon_disconnected` | string | — | Icon when disconnected |
| `icon_ethernet` | string | — | Icon for ethernet |
| `format_ethernet` | string | `"{icon}"` | Format for ethernet (supports `{icon}`) |
| `format_wifi` | string | `"{icon}"` | Format for WiFi (supports `{icon}`, `{ssid}`) |
| `color_disconnected` | string | — | Text color when disconnected |

#### `"cpu"`

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `icons` | string[5] | — | Load icons, mapped 0%–100% |
| `poll_rate` | integer | `500` | Data-fetch interval in milliseconds |

#### `"memory"`

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `icons` | string[5] | — | Load icons, mapped 0%–100% |
| `poll_rate` | integer | `500` | Data-fetch interval in milliseconds |

### Format specifiers

Available placeholders for `format`, `format_wifi`, and `format_ethernet`.
The `"workspaces"` indicator type supports its own specifiers (see
[BFWM.workspaces](#bfwmworkspaces)).
The `"clock"` indicator uses `strftime`-style format strings (see above)
instead of these placeholders.

| Specifier | Description | Types |
| --- | --- | --- |
| `{icon}` | The selected icon for the current level/state | All icon-based types |
| `{ssid}` | The WiFi network name | Network |
| `{load}` / `{load:.Nf}` | CPU, memory, or volume load (0–100). Supports decimal format, e.g. `{load:.1f}` | CPU, Memory, Volume |
| `{percent}` / `{percent:.Nf}` | Same value as `{load}` (0–100) | Volume |
| `{level}` / `{level:.Nf}` | Raw volume level (0.0–1.0). Supports decimal format, e.g. `{level:.2f}` | Volume |
| `{total_gb}` / `{total_gb:.Nf}` | Total physical memory in GB | Memory |
| `{used_gb}` / `{used_gb:.Nf}` | Used physical memory in GB | Memory |
| `{avail_gb}` / `{avail_gb:.Nf}` | Available physical memory in GB | Memory |
| `{id}` | The workspace id | Workspaces |
| `{label}` | The workspace label | Workspaces |

Without a `.Nf` suffix, `{load}`, `{percent}`, and `{level}` round to an
integer and GB values show one decimal place.

Format-specifiers are entirely optional. `""` is a valid format.

## Snackbar.* (Notification Toast)

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `enabled` | boolean | `true` | Master toggle for the notification toast |
| `log_level` | string | `"warn"` | Minimum severity for programmatic notifications: `"none"`, `"error"`, `"warn"`, `"info"`, `"debug"`. (`--notify` and `BFWM.notify()` bypass this check and always show) |
| `display_duration_ms` | integer | `3000` | Milliseconds before a notification auto-dismisses |
| `position` | string | `"bottom-right"` | Screen corner: `"bottom-right"`, `"bottom-left"`, `"top-right"`, `"top-left"` |
| `margin_left` | integer | `0` | Margin from the monitor's left edge (pixels) |
| `margin_right` | integer | `16` | Margin from the monitor's right edge (pixels) |
| `margin_top` | integer | `0` | Margin from the monitor's top edge (pixels) |
| `margin_bottom` | integer | `48` | Margin from the monitor's bottom edge (pixels) |
| `min_width` | integer | `0` | Minimum overlay width (pixels, `0` = no minimum) |
| `max_width` | integer | `0` | Maximum overlay width (pixels, `0` = 30% of monitor width, min 300) |
| `padding_left` | integer | `8` | Left padding inside each notification (pixels) |
| `padding_right` | integer | `8` | Right padding inside each notification (pixels) |
| `padding_top` | integer | `8` | Top padding inside each notification (pixels) |
| `padding_bottom` | integer | `8` | Bottom padding inside each notification (pixels) |
| `background` | string | `"#1e1e1e"` | Overlay background color (hex `#RRGGBB`) |
| `text` | string | `"#cccccc"` | Notification text color (hex `#RRGGBB`) |
| `divider` | string | `"#555555"` | Divider color between stacked notifications (hex `#RRGGBB`) |
| `divider_height` | integer | `1` | Divider thickness (pixels) |
| `font_size` | integer | `14` | Notification text font size in points |
| `font_name` | string | `"Segoe UI"` | Font face for notification text |
| `corner_radius` | integer | `4` | Overlay window corner rounding (pixels, `0` = sharp) |
| `opacity` | integer | `230` | Overlay window opacity (`0`–`255`, `255` = fully opaque) |
| `close_on_click` | boolean | `true` | Click a notification to dismiss it (expanded notifications dismiss on second click when `click_to_expand` is enabled) |
| `pause_on_hover` | boolean | `true` | Hovering pauses the auto-dismiss timer; the timer resumes when the cursor leaves |
| `click_to_expand` | boolean | `false` | Long notifications are truncated with `"..."` on a single line; click to expand to the full wrapped text. Click again to dismiss (if `close_on_click` is enabled) |
| `max_queue` | integer | `5` | Maximum number of simultaneously visible notifications (`1`–`16`) |
| `monitor` | integer | `0` | Target monitor: `0` = primary, `-1` = focused workspace's monitor, `N` = display number |

### Snackbar.colors

Colors for the 4 px left-hand severity bar inside each notification:

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `error` | string | `"#ff4444"` | LOG_ERROR / LOG_FATAL |
| `warn` | string | `"#ffaa00"` | LOG_WARN |
| `info` | string | `"#00aaff"` | LOG_INFO (programmatic) |
| `debug` | string | `"#888888"` | LOG_DEBUG |
| `normal` | string | `"#ffffff"` | Default bar color (used by `--notify` / `BFWM.notify()`) |

### Triggering notifications

```lua
-- From the CLI (forwards to the running instance):
--   BFWM --notify "Your message here"

-- From config.lua:
BFWM.notify("Your message here")
```

Notifications triggered via `--notify` or `BFWM.notify()` use the **normal**
bar color and bypass the `log_level` threshold.

### Typo protection

All `BFWM.*`, `Bar.*`, and `Snackbar.*` keys are validated at config load
time. Unknown keys produce a Lua error with the exact option name, preventing
silent misconfiguration.
