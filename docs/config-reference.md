# BFWM Config Reference

BFWM is configured with a single Lua file, `config.lua`. The file is
**read-only from the app's perspective**: BFWM parses it at startup and on
reload, but never writes back to it. All settings are optional — every key has
a built-in default, so a missing or empty config file simply runs the window
manager with defaults.

---

## Table of contents

- [Config file location](#config-file-location)
- [Command-line overrides](#command-line-overrides)
- [Lua features](#lua-features)
- [Global tables](#global-tables)
- [`BFWM` settings](#bfwm-settings)
- [`BFWM.keybinds`](#bfwmkeybinds)
- [`BFWM.window_rules`](#bfwmwindow_rules)
- [`BFWM.workspaces`](#bfwmworkspaces)
- [`Bar` settings](#bar-settings)
- [`Bar.indicators`](#barindicators)
- [`Snackbar` settings](#snackbar-settings)
- [Minimal example config](#minimal-example-config)
- [Full example config](#full-example-config)

---

## Config file location

At startup BFWM looks for `config.lua` in the following order and uses the
**first file found**:

1. `%APPDATA%\BFWM\config.lua`
2. `%USERPROFILE%\.config\BFWM\config.lua`

These are the recommended, installer-friendly places to put your config. If no
file is found anywhere, BFWM runs entirely on built-in defaults.

The config is never modified by BFWM. To change settings, edit the file
and reload it (see [Reloading](#reloading) below).

---

## Command-line overrides

```
BFWM [--config <path>] [--no-bar] [--notify <text>]
```

| Flag | Description |
| --- | --- |
| `--config <path>` | Load the config from `<path>` instead of the search order above. The path is used verbatim. |
| `--no-bar` | Disable the bar entirely (equivalent to `Bar.enabled = false`), overriding the config. |
| `--notify <text>` | Send a snackbar notification to an already-running instance and exit. If BFWM is not running, prints an error and exits. |

---

## Lua features

The config file is executed by a full Lua 5.x interpreter with the standard
libraries opened (`luaL_openlibs`), so you get the complete Lua language plus:

- **`require`** and all standard libraries (`string`, `table`, `math`, `os`,
  `io`, `print`, …).
- **Local variables and expressions** — config values are ordinary Lua
  expressions, so you can build keybind strings, reuse colors, or compute
  values:

  ```lua
  local mod = "Alt"
  local accent = "#ef9f76"
  Bar.colors.active_workspace = accent
  BFWM.keybinds = {
    { mod .. " Q", "KillActive", {} },
  }
  ```

- **`BFWM.notify(text)`** — a function registered before the config runs.
  Calling it from anywhere in `config.lua` (or from a custom function) shows a
  snackbar notification immediately:

  ```lua
  BFWM.notify("Config loaded")
  ```

- **User-defined functions** — define your own Lua functions in the config and
  bind them to keys with the `Call` action (see
  [Keybind actions](#keybind-actions)):

  ```lua
  local function my_custom_action()
    BFWM.notify("Custom action triggered!")
  end

  BFWM.keybinds = {
    { "Alt C", "Call", { lua = "my_custom_action" } },
  }
  ```

  The `Call` action first looks up a global function by name; if that fails it
  tries to run the string as inline Lua code.

### Typo protection

The `BFWM`, `Bar`, and `Snackbar` tables are metatable-backed. Assigning an
unknown key raises a Lua error at load time (and shows a snackbar warning), so
typos are caught immediately instead of silently ignored. The same applies to
`Bar.colors`, `Bar.font`, `Bar.margin`, `Bar.padding`, and `Bar.border`.

---

## Global tables

The config is organized into three global tables:

| Table | Purpose |
| --- | --- |
| `BFWM` | Window-manager settings, keybinds, window rules, workspaces |
| `Bar` | Top bar appearance and indicators |
| `Snackbar` | On-screen notification toasts |

---

## `BFWM` settings

### `BFWM.gap_between`

- **Type:** integer (pixels)
- **Default:** `6`
- **Controls:** The gap between tiled windows.

```lua
BFWM.gap_between = 6
```

### `BFWM.gap_edge`

- **Type:** integer (pixels)
- **Default:** `8`
- **Controls:** The gap between tiled windows and the screen edges.

```lua
BFWM.gap_edge = 8
```

### `BFWM.border_color`

- **Type:** color string (`#RRGGBB`)
- **Default:** `"#ef9f76"`
- **Controls:** The border color of the focused (active) window.

```lua
BFWM.border_color = "#ef9f76"
```

### `BFWM.inactive_border`

- **Type:** color string (`#RRGGBB`)
- **Default:** `"#51576d"`
- **Controls:** The border color of unfocused windows.

```lua
BFWM.inactive_border = "#51576d"
```

### `BFWM.border_width`

- **Type:** integer (pixels)
- **Default:** `4`
- **Controls:** Thickness of the window border.

```lua
BFWM.border_width = 4
```

### `BFWM.border_radius`

- **Type:** integer (pixels)
- **Default:** `8`
- **Controls:** Corner radius of the window border. `0` = square corners.

```lua
BFWM.border_radius = 8
```

### `BFWM.focus_follows_mouse`

- **Type:** boolean
- **Default:** `false`
- **Controls:** When `true`, hovering the mouse over a window focuses it
  automatically.

```lua
BFWM.focus_follows_mouse = false
```

### `BFWM.mouse_follows_focus`

- **Type:** boolean
- **Default:** `false`
- **Controls:** When `true`, the cursor is moved to the newly focused window.

```lua
BFWM.mouse_follows_focus = false
```

### ~~`BFWM.unlock_window_resize`~~
>
> WIP: may or may not work as intended, do not use

- **Type:** string (modifier name)
- **Default:** none (resize is always unlocked)
- **Allowed values:** `LALT`, `RALT`, `ALT`, `LCTRL`, `RCTRL`, `CTRL`,
  `CONTROL`, `LSHIFT`, `RSHIFT`, `SHIFT`
- **Controls:** When set, the named modifier must be held to resize windows
  with the mouse. When unset, resize is always available.

```lua
BFWM.unlock_window_resize = "LALT"  -- hold LAlt + drag to resize
```

### ~~`BFWM.unlock_window_move`~~
>
> WIP: dead feature, do not use

- **Type:** string (modifier name)
- **Default:** none (move is always unlocked)
- **Allowed values:** same as `unlock_window_resize`
- **Controls:** When set, the named modifier must be held to drag-move windows
  with the mouse.

```lua
BFWM.unlock_window_move = "LALT"
```

### `BFWM.layout`

- **Type:** string
- **Default:** `"dwindle"`
- **Allowed values:** `"dwindle"`, `"monocle"`
- **Controls:** The default layout algorithm used for new workspaces.

```lua
BFWM.layout = "dwindle"
```

### `BFWM.disabled_monitors`

- **Type:** array of integers (display numbers)
- **Default:** `{}` (no monitors disabled)
- **Controls:** Display numbers to disable (e.g. HDMI audio passthrough
  devices). Up to 16 entries are honored.

```lua
BFWM.disabled_monitors = { 3 }
```

### `BFWM.keybinds`

- **Type:** array of keybind entries — see
  [`BFWM.keybinds`](#bfwmkeybinds).
- **Default:** built-in keybinds (see below).
- **Controls:** Replaces the entire default keybind set. If the table is
  present but empty, all keybinds are cleared.

### `BFWM.window_rules`

- **Type:** array of rule tables — see
  [`BFWM.window_rules`](#bfwmwindow_rules).
- **Default:** none.

### `BFWM.workspaces`

- **Type:** array of workspace tables — see
  [`BFWM.workspaces`](#bfwmworkspaces).
- **Default:** none (workspaces are created automatically).

### `BFWM.notify`

- **Type:** function (not a setting)
- **Usage:** `BFWM.notify("text")` — shows a snackbar notification. Always
  bypasses the snackbar log-level filter.

---

## `BFWM.keybinds`

`BFWM.keybinds` is an array of entries. Each entry is a table with three
elements:

```lua
{ "<key combination>", "<ActionName>", { <options> } }
```

| Element | Type | Description |
| --- | --- | --- |
| 1 | string | Key combination, e.g. `"Alt Shift H"` |
| 2 | string | Action name, e.g. `"MoveWindow"` |
| 3 | table | Options for the action (may be empty `{}`) |

If the `keybinds` table is present, it **replaces** the default keybinds
entirely. If it is missing, the built-in defaults remain active.

### Key combinations

A key combination is a space-separated list of modifiers followed by one key
name:

- **Modifiers:** `Super` (or `Win`), `Ctrl` (or `Control`), `Shift`, `Alt`.

> Using the `Super`/`Win` key is generally discourages as it can interfere with a lot of native Windows keybinds, e.g. `Win + L` locks the screen.

- **Key names:** single letters `A`–`Z`, digits `0`–`9`, `F1`–`F24`,
  `Space`, `Return` (or `Enter`), `Tab`, `Escape` (or `Esc`), `Backspace`,
  `Left`, `Right`, `Up`, `Down`.

```lua
{ "Alt Shift H", "MoveWindow", { direction = "left" } }
{ "Ctrl Alt T", "Spawn", { command = "wt" } }
```

### Keybind actions

| Action | Options | Description |
| --- | --- | --- |
| `MoveWindow` | `direction` | Move the focused window in a direction. |
| `Spawn` | `command` (**required**) | Launch a GUI process. |
| `Exec` | `command` (**required**) | Run a shell command without spawning a window. |
| `KillActive` | — | Close the focused window. |
| `Minimize` | — | Minimize the focused window. |
| `ToggleFloat` | — | Toggle floating for the focused window. |
| `Fullscreen` | — | Toggle fullscreen for the focused window. |
| `FocusWindow` | `direction` | Focus the window in a direction. |
| `Workspace` | `index` | Switch to the workspace with the given index. |
| `MoveToWorkspace` | `index` | Move the focused window to a workspace. |
| `ToggleSplit` | — | Toggle the split direction of the focused window's container. |
| `SwapSplit` | — | Swap the two children of the focused window's container. |
| `SetLayout` | `layout` (**required**) | Set the layout by name (`"dwindle"`, `"monocle"`). |
| `CycleLayout` | `direction` | Cycle to the next/previous layout. |
| `ResizeWindow` | `direction`, `pixels` | Resize the focused window. `pixels` defaults to `50`. |
| `Call` | `lua` (**required**) | Call a user-defined Lua function (or run inline Lua). |
| `ToggleGaps` | — | Toggle all gaps on/off. |
| `ReloadConfig` | — | Re-parse `config.lua` (settings, keybinds, rules). |
| `MoveWorkspaceToMonitor` | `index`, `direction` | Move the active workspace to a monitor. `index` defaults to `-1` (use `direction`). |

**`direction` values:** `"left"`, `"right"`, `"up"`, `"down"`, `"next"`,
`"prev"` (case-insensitive).

**Common option — `repeatable`:** every action accepts a `repeatable` boolean
option. When `true`, the action fires on keyboard auto-repeat while the key is
held.

```lua
{ "Alt Shift H", "MoveWindow", { direction = "left", repeatable = true } }
{ "Alt Return", "Spawn", { command = "wt" } }
{ "Alt C", "Call", { lua = "my_custom_action" } }
```

### Default keybinds

If `BFWM.keybinds` is omitted, these defaults apply:

| Key | Action |
| --- | --- |
| `Alt Q` | KillActive |
| `Alt F` | Fullscreen |
| `Alt X` | Minimize |
| `Alt W` | ToggleFloat |
| `Alt H/J/K/L` | FocusWindow left/down/up/right |
| `Alt Shift H/J/K/L` | MoveWindow left/down/up/right (repeatable) |
| `Alt 1`–`Alt 9` | Workspace 1–9 |
| `Alt Shift 1`–`Alt Shift 9` | MoveToWorkspace 1–9 |
| `Alt P` | SwapSplit |
| `Alt Shift P` | ToggleSplit |
| `Alt C` | ToggleGaps |
| `Alt D` | CycleLayout next |
| `Alt Shift D` | CycleLayout prev |
| `Alt Shift R` | ReloadConfig |

---

## `BFWM.window_rules`

`BFWM.window_rules` is an array of rule tables. Each rule has:

```lua
{
  match = {
    { process = { <op> = "<pattern>" },
      class   = { <op> = "<pattern>" },
      title   = { <op> = "<pattern>" } },
    -- ...more entries (OR'd together)
  },
  action = "<action>",          -- or { move_to_workspace = N }
  run_once = true,              -- optional, defaults to true
}
```

### Match criteria

Each match entry may specify `process`, `class`, and/or `title` criteria.
All criteria within one entry must match (AND logic); multiple entries are
OR'd together. A rule with no valid criteria is skipped with a warning.

Each criterion is a single-key table mapping an operator to a pattern string:

| Operator | Meaning |
| --- | --- |
| `equals` | Exact string match |
| `includes` | Case-insensitive substring match |
| `regex` | POSIX extended regular expression match |
| `not_equals` | Negated exact match |
| `not_regex` | Negated regex match |

### Actions

| Action | Meaning |
| --- | --- |
| `"ignore"` | Don't manage the window at all |
| `"set-floating"` | Mark the window as floating |
| `"force-floating"` | Force the window to be floating |
| `"force-tiled"` | Force the window to be tiled |
| `{ move_to_workspace = N }` | Assign the window to workspace `N` |

### `run_once`

- **Type:** boolean
- **Default:** `true`
- **Controls:** When `true`, the rule is applied only once per window.

```lua
BFWM.window_rules = {
  {
    match = {
      { title = { regex = "[Pp]icture" },
        class = { regex = "Chrome_WidgetWin_1" } },
    },
    action = "ignore",
  },
  {
    match = {
      { process = { equals = "notepad.exe" } },
    },
    action = { move_to_workspace = 2 },
  },
}
```

---

## `BFWM.workspaces`

`BFWM.workspaces` is an array of workspace definitions. Each entry is a
table whose first element is the workspace ID (a string, parsed as an
integer) and whose optional second element is an options table:

```lua
BFWM.workspaces = {
  { "1", { label = "coding", monitor = 1, layout = "dwindle" } },
  { "2", { label = "media",  monitor = 1 } },
  { "3", { label = "chat",   monitor = 2 } },
  { "4", {} },
}
```

### Workspace options

| Option | Type | Default | Description |
| --- | --- | --- | --- |
| `label` | string | (none) | User-facing label shown in the bar. |
| `monitor` | integer | `0` (unassigned) | Display number to pin the workspace to. `0` = unassigned. A warning is shown if the monitor doesn't exist. |
| `layout` | string | (default layout) | Layout for this workspace: `"dwindle"` or `"monocle"`. |

Duplicate workspace IDs are rejected (first definition wins). Labels can be
referenced from bar indicators via format strings (see
[`Bar.indicators`](#barindicators)).

---

## `Bar` settings

### `Bar.enabled`

- **Type:** boolean
- **Default:** `true`
- **Controls:** Master toggle for the bar. Also overridable with the
  `--no-bar` CLI flag.

```lua
Bar.enabled = true
```

### `Bar.height`

- **Type:** integer (pixels)
- **Default:** `32`
- **Controls:** Height of the bar.

```lua
Bar.height = 32
```

### `Bar.corner_radius`

- **Type:** integer (pixels)
- **Default:** `1`
- **Controls:** Corner radius of the bar window. `0` = sharp corners.

```lua
Bar.corner_radius = 1
```

### `Bar.margin`

- **Type:** table `{ top, right, bottom, left }` (integers, pixels)
- **Default:** all `0`
- **Controls:** Margin between the bar and the screen edges.

```lua
Bar.margin = { top = 0, right = 0, bottom = 0, left = 0 }
```

### `Bar.padding`

- **Type:** table `{ top, right, bottom, left }` (integers, pixels)
- **Default:** all `0`
- **Controls:** Inner padding inside the bar.

```lua
Bar.padding = { top = 0, right = 0, bottom = 0, left = 0 }
```

### `Bar.border`

- **Type:** table `{ width, color }`
- **Default:** `width = 1`, `color = "#ef9f76"`
- **Controls:** Border around the bar. `width = 0` disables it.

```lua
Bar.border = { width = 1, color = "#ef9f76" }
```

### `Bar.font`

- **Type:** table `{ size, name, weight }`
- **Default:** `size = 18`, `name = "Segoe UI"`, `weight = 800`
  (`FW_BOLD`)
- **Controls:** Font used for bar text. `weight` is a GDI font weight
  (e.g. `400` normal, `700` bold).

```lua
Bar.font = { name = "Segoe UI", size = 18, weight = 800 }
```

### `Bar.colors`

- **Type:** table of color strings (`#RRGGBB`)
- **Default:**

  ```lua
  {
    background         = "#303446",
    text               = "#c6d0f5",
    active_workspace   = "#ef9f76",
    inactive_workspace = "#626880",
    tab_border         = "#232634",
  }
  ```

- **Controls:** Bar color scheme.

| Key | Controls |
| --- | --- |
| `background` | Bar background color |
| `text` | Default text color |
| `active_workspace` | Active workspace indicator color |
| `inactive_workspace` | Inactive workspace indicator color |
| `tab_border` | Tab border color |

```lua
Bar.colors = {
  background         = "#303446",
  text               = "#c6d0f5",
  active_workspace   = "#ef9f76",
  inactive_workspace = "#626880",
  tab_border         = "#232634",
}
```

### `Bar.indicators`

- **Type:** array of indicator tables — see
  [`Bar.indicators`](#barindicators).
- **Default:** 7 built-in indicators (Catppuccin Mocha theme):

  ```lua
  Bar.indicators = {
    { type = "workspaces", align = "left", show_position_bar = false },
    { type = "title",      align = "center", max_width = 300 },
    { type = "network",    align = "right",
      icons = { "󰤯", "󰤟", "󰤢", "󰤥", "󰤨" },
      icon_disconnected = "󰤮",
      icon_ethernet = "󰀂",
      format_wifi = "{icon} {ssid}",
      format_ethernet = "{icon}",
      size = 22,
      color_disconnected = "#e78284" },
    { type = "volume",     align = "right",
      icons = { "󰕿", "󰖀", "󰕾" },
      icon_muted = "󰝟",
      format = "{percent}% {icon}" },
    { type = "cpu",        align = "right",
      format = "{icon} {load}%",
      icons = { "▁", "▂", "▃", "▄", "▅" } },
    { type = "memory",     align = "right",
      format = "{used_gb}/{total_gb}GB" },
    { type = "clock",      align = "right",
      format = "%m/%d, %H:%M" },
  }
  ```

---

## `Bar.indicators`

`Bar.indicators` is an array of indicator definitions, drawn left-to-right in
order. Up to **8** indicators are supported.

```lua
Bar.indicators = {
  { type = "workspaces", align = "left", show_position_bar = false },
  { type = "title",      align = "center", max_width = 300 },
  { type = "network",    align = "right",
    icons = { "󰤯", "󰤟", "󰤢", "󰤥", "󰤨" },
    icon_disconnected = "󰤮",
    icon_ethernet = "󰀂",
    format_wifi = "{icon} {ssid}",
    format_ethernet = "{icon}",
    size = 22,
    color_disconnected = "#e78284" },
  { type = "volume",     align = "right",
    icons = { "󰕿", "󰖀", "󰕾" },
    icon_muted = "󰝟",
    format = "{percent}% {icon}" },
  { type = "cpu",        align = "right",
    format = "{icon} {load}%",
    icons = { "▁", "▂", "▃", "▄", "▅" } },
  { type = "memory",     align = "right",
    format = "{used_gb}/{total_gb}GB" },
  { type = "clock",      align = "right",
    format = "%m/%d, %H:%M" },
}
```

### Indicator options

| Option | Type | Default | Description |
| --- | --- | --- | --- |
| `type` | string | `"workspaces"` | Indicator type (see below). |
| `align` | string | `"left"` | `"left"`, `"center"`, or `"right"`. |
| `max_width` | integer | `0` | Maximum width in pixels. |
| `format` | string | `""` | Format string. For `workspaces`, supports `{id}` and `{label}` placeholders. |
| `show_position_bar` | boolean | `false` | Show a position bar (workspaces indicator). |
| `size` | integer | `0` (bar default) | Font size for this indicator. |
| `poll_rate` | integer | `0` (bar default, 500 ms) | Poll interval in milliseconds for dynamic indicators. |
| `icons` | array of strings | `{}` | Per-state icon strings (up to 5, e.g. volume states). |
| `icon_muted` | string | `""` | Icon shown for the muted state (volume). |
| `icon_disconnected` | string | `""` | Icon shown for the disconnected state (network). |
| `icon_ethernet` | string | `""` | Icon shown for the ethernet state (network). |
| `format_ethernet` | string | `""` | Format string for the ethernet state (network). |
| `format_wifi` | string | `""` | Format string for the wifi state (network). |
| `color` | color string | bar default text | Text color for this indicator. |
| `color_disconnected` | color string | bar default | Color for the disconnected state (network). |
| `color_muted` | color string | bar default | Color for the muted state (volume). |

### Indicator types

| Type | Description |
| --- | --- |
| `"workspaces"` | Workspace list. Supports `format` with `{id}` and `{label}` placeholders, e.g. `format = "{id}:{label}"`. |
| `"title"` | Title of the focused window. |
| `"clock"` | Current time. |
| `"volume"` | System volume. Supports `icons`, `icon_muted`, `color_muted`. |
| `"network"` | Network status. Supports `icon_disconnected`, `icon_ethernet`, `format_ethernet`, `format_wifi`, `color_disconnected`. |
| `"cpu"` | CPU usage. |
| `"memory"` | Memory usage. |

Unknown indicator types, aligns, and keys are logged as warnings and skipped.

---

## `Snackbar` settings

The snackbar shows on-screen notification toasts in a configurable screen
corner.

### `Snackbar.enabled`

- **Type:** boolean
- **Default:** `true`
- **Controls:** Master toggle for the snackbar.

```lua
Snackbar.enabled = true
```

### `Snackbar.log_level`

- **Type:** string
- **Default:** `"info"`
- **Allowed values:** `"none"`, `"error"`, `"warn"`, `"info"`, `"debug"`
- **Controls:** Minimum severity for programmatic messages. Notifications from
  `--notify` and `BFWM.notify()` always bypass this filter.

```lua
Snackbar.log_level = "info"
```

### `Snackbar.position`

- **Type:** string
- **Default:** `"top-right"`
- **Allowed values:** `"bottom-right"`, `"bottom-left"`, `"top-right"`,
  `"top-left"`
- **Controls:** Screen corner where notifications appear.

```lua
Snackbar.position = "top-right"
```

### `Snackbar.margin_left` / `margin_right` / `margin_top` / `margin_bottom`

- **Type:** integer (pixels)
- **Defaults:** `0`, `16`, `48`, `0`
- **Controls:** Distance from the monitor work-area edges.

```lua
Snackbar.margin_right = 16
Snackbar.margin_top = 48
```

### `Snackbar.min_width` / `max_width`

- **Type:** integer (pixels)
- **Defaults:** `300`, `400`
- **Controls:** Minimum overlay width (`0` = no minimum) and maximum width
  (`0` = 30% of monitor width; `>0` = explicit pixels). If `min_width` exceeds
  `max_width`, `min_width` is capped to `max_width`.

```lua
Snackbar.max_width = 400
```

### `Snackbar.padding_left` / `padding_right` / `padding_top` / `padding_bottom`

- **Type:** integer (pixels)
- **Defaults:** all `8`
- **Controls:** Inner padding inside each notification.

```lua
Snackbar.padding_left = 8
Snackbar.padding_right = 8
Snackbar.padding_top = 8
Snackbar.padding_bottom = 8
```

### `Snackbar.background` / `text` / `divider`

- **Type:** color strings (`#RRGGBB`)
- **Defaults:** `"#303446"`, `"#c6d0f5"`, `"#f2d5cf"`
- **Controls:** Overlay background, notification text, and the divider color
  between stacked notifications.

```lua
Snackbar.background = "#303446"
Snackbar.text = "#c6d0f5"
Snackbar.divider = "#f2d5cf"
```

### `Snackbar.divider_height`

- **Type:** integer (pixels)
- **Default:** `1`
- **Controls:** Divider thickness between stacked notifications.

```lua
Snackbar.divider_height = 1
```

### `Snackbar.font_size` / `font_name`

- **Type:** integer / string
- **Defaults:** `20`, `"Segoe UI"`
- **Controls:** Notification font.

```lua
Snackbar.font_size = 20
Snackbar.font_name = "Segoe UI"
```

### `Snackbar.corner_radius`

- **Type:** integer (pixels)
- **Default:** `1`
- **Controls:** Window corner rounding. `0` = sharp.

```lua
Snackbar.corner_radius = 1
```

### `Snackbar.opacity`

- **Type:** integer
- **Default:** `200`
- **Allowed values:** `0`–`255` (clamped)
- **Controls:** Window opacity.

```lua
Snackbar.opacity = 200
```

### `Snackbar.close_on_click`

- **Type:** boolean
- **Default:** `true`
- **Controls:** Click a notification to dismiss it.

```lua
Snackbar.close_on_click = true
```

### `Snackbar.pause_on_hover`

- **Type:** boolean
- **Default:** `true`
- **Controls:** Hovering pauses the auto-dismiss timer.

```lua
Snackbar.pause_on_hover = true
```

### `Snackbar.click_to_expand`

- **Type:** boolean
- **Default:** `true`
- **Controls:** Click a collapsed notification to expand it.

```lua
Snackbar.click_to_expand = true
```

### `Snackbar.max_queue`

- **Type:** integer
- **Default:** `5`
- **Allowed values:** `1`–`16` (clamped)
- **Controls:** Maximum number of visible notifications.

```lua
Snackbar.max_queue = 5
```

### `Snackbar.monitor`

- **Type:** integer
- **Default:** `-1`
- **Allowed values:** `0` = primary monitor, `-1` = focused monitor, `N` =
  display number `N`
- **Controls:** Which monitor shows the notifications.

```lua
Snackbar.monitor = -1
```

### `Snackbar.display_duration_ms`

- **Type:** integer (milliseconds)
- **Default:** `5000`
- **Controls:** How long a notification stays visible before auto-dismiss.

```lua
Snackbar.display_duration_ms = 5000
```

### `Snackbar.colors`

- **Type:** table of color strings (`#RRGGBB`)
- **Default:**

  ```lua
  {
    error  = "#e78284",
    warn   = "#e5c890",
    info   = "#8caaee",
    debug  = "#737994",
    normal = "#ef9f76",
  }
  ```

- **Controls:** Per-severity colors for the left-hand level bar.

| Key | Severity |
| --- | --- |
| `error` | Errors and fatal messages |
| `warn` | Warnings |
| `info` | Informational (programmatic) |
| `debug` | Debug messages |
| `normal` | Default bar color (`--notify` / `BFWM.notify()`) |

```lua
Snackbar.colors = {
  error  = "#e78284",
  warn   = "#e5c890",
  info   = "#8caaee",
  debug  = "#737994",
  normal = "#ef9f76",
}
```

---

## Reloading

The config can be reloaded at runtime without restarting BFWM:

- Press the default `Alt Shift R` keybind (or any key bound to
  `ReloadConfig`).
- On reload, settings, keybinds, window rules, workspace labels/assignments,
  and bar/snackbar config are re-parsed. Settings removed from the file revert
  to their defaults.

---

## Minimal example config

```lua
-- Minimal BFWM config: everything else uses built-in defaults.

BFWM.gap_between = 6
BFWM.gap_edge = 8
BFWM.border_color = "#ef9f76"
BFWM.inactive_border = "#51576d"
BFWM.border_width = 4
BFWM.border_radius = 8
BFWM.focus_follows_mouse = false
BFWM.mouse_follows_focus = false
BFWM.layout = "dwindle"

Bar.enabled = true
Bar.height = 32
```

---

## Full example config

```lua
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
  tab_border         = "#232634",
}
Bar.indicators = {
  { type = "workspaces", align = "left", show_position_bar = false },
  { type = "title",      align = "center", max_width = 300 },
  { type = "network",    align = "right",
    icons = { "󰤯", "󰤟", "󰤢", "󰤥", "󰤨" },
    icon_disconnected = "󰤮",
    icon_ethernet = "󰀂",
    format_wifi = "{icon} {ssid}",
    format_ethernet = "{icon}",
    size = 22,
    color_disconnected = "#e78284" },
  { type = "volume",     align = "right",
    icons = { "󰕿", "󰖀", "󰕾" },
    icon_muted = "󰝟",
    format = "{percent}% {icon}" },
  { type = "cpu",        align = "right",
    format = "{icon} {load}%",
    icons = { "▁", "▂", "▃", "▄", "▅" } },
  { type = "memory",     align = "right",
    format = "{used_gb}/{total_gb}GB" },
  { type = "clock",      align = "right",
    format = "%m/%d, %H:%M" },
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
Snackbar.colors = {
  error  = "#e78284",
  warn   = "#e5c890",
  info   = "#8caaee",
  debug  = "#737994",
  normal = "#ef9f76",
}

-- ── Keybinds ────────────────────────────────────────────────────
BFWM.keybinds = {
  -- Window management
  { mod .. " Q",           "KillActive",             {} },
  { mod .. " X",           "Minimize",               {} },
  { mod .. " F",           "Fullscreen",             {} },
  { mod .. " W",           "ToggleFloat",            {} },

  -- Layout
  { mod .. " P",           "SwapSplit",              {} },
  { mod .. " Shift P",     "ToggleSplit",            {} },
  { mod .. " C",           "ToggleGaps",             {} },
  { mod .. " Space",       "CycleLayout",            { direction = "next" } },
  { mod .. " Shift Space", "CycleLayout",            { direction = "prev" } },
  { mod .. " M",           "SetLayout",              { layout = "monocle" } },
  { mod .. " D",           "SetLayout",              { layout = "dwindle" } },

  -- Focus (vim-style HJKL)
  { mod .. " H",           "FocusWindow",            { direction = "left" } },
  { mod .. " J",           "FocusWindow",            { direction = "down" } },
  { mod .. " K",           "FocusWindow",            { direction = "up" } },
  { mod .. " L",           "FocusWindow",            { direction = "right" } },

  -- Move window
  { mod .. " Shift H",     "MoveWindow",             { direction = "left",  repeatable = true } },
  { mod .. " Shift J",     "MoveWindow",             { direction = "down",  repeatable = true } },
  { mod .. " Shift K",     "MoveWindow",             { direction = "up",    repeatable = true } },
  { mod .. " Shift L",     "MoveWindow",             { direction = "right", repeatable = true } },

  -- Resize
  { mod .. " Shift Left",  "ResizeWindow",           { direction = "left",  pixels = 50, repeatable = true } },
  { mod .. " Shift Down",  "ResizeWindow",           { direction = "down",  pixels = 50, repeatable = true } },
  { mod .. " Shift Up",    "ResizeWindow",           { direction = "up",    pixels = 50, repeatable = true } },
  { mod .. " Shift Right", "ResizeWindow",           { direction = "right", pixels = 50, repeatable = true } },

  -- Workspaces
  { mod .. " 1",           "Workspace",              { index = 1 } },
  { mod .. " 2",           "Workspace",              { index = 2 } },
  { mod .. " 3",           "Workspace",              { index = 3 } },
  { mod .. " 4",           "Workspace",              { index = 4 } },
  { mod .. " 5",           "Workspace",              { index = 5 } },
  { mod .. " 6",           "Workspace",              { index = 6 } },
  { mod .. " 7",           "Workspace",              { index = 7 } },
  { mod .. " 8",           "Workspace",              { index = 8 } },
  { mod .. " 9",           "Workspace",              { index = 9 } },
  { mod .. " 0",           "Workspace",              { index = 10 } },

  -- Move window to workspace
  { mod .. " Shift 1",     "MoveToWorkspace",        { index = 1 } },
  { mod .. " Shift 2",     "MoveToWorkspace",        { index = 2 } },
  { mod .. " Shift 3",     "MoveToWorkspace",        { index = 3 } },
  { mod .. " Shift 4",     "MoveToWorkspace",        { index = 4 } },
  { mod .. " Shift 5",     "MoveToWorkspace",        { index = 5 } },
  { mod .. " Shift 6",     "MoveToWorkspace",        { index = 6 } },
  { mod .. " Shift 7",     "MoveToWorkspace",        { index = 7 } },
  { mod .. " Shift 8",     "MoveToWorkspace",        { index = 8 } },
  { mod .. " Shift 9",     "MoveToWorkspace",        { index = 9 } },
  { mod .. " Shift 0",     "MoveToWorkspace",        { index = 10 } },

  -- Move workspace to monitor
  { mod .. " Ctrl Left",   "MoveWorkspaceToMonitor", { direction = "left" } },
  { mod .. " Ctrl Right",  "MoveWorkspaceToMonitor", { direction = "right" } },
  { mod .. " Ctrl 1",      "MoveWorkspaceToMonitor", { index = 1 } },
  { mod .. " Ctrl 2",      "MoveWorkspaceToMonitor", { index = 2 } },

  -- Spawn / exec
  -- { mod .. " Return",      "Spawn",                  { command = "wt" } },
  -- { mod .. " Shift Return", "Exec",               { command = "cmd /c echo hi" } },

  -- Reload config
  { mod .. " Ctrl R",      "ReloadConfig",           {} },

  -- Custom Lua callback
  { mod .. " B",           "Call",                   { lua = "my_custom_action" } },
}

-- ── Window rules ────────────────────────────────────────────────
BFWM.window_rules = {
  {
    match = {
      { title = { regex = "[Pp]icture" },
        class = { regex = "Chrome_WidgetWin_1" } },
    },
    action = "ignore",
  },
  {
    match = {
      { process = { equals = "notepad.exe" } },
    },
    action = { move_to_workspace = 2 },
    run_once = true,
  },
}

-- ── Workspaces ──────────────────────────────────────────────────
BFWM.workspaces = {
  { "1", { layout = "dwindle" } },
  { "2", { } },
}

-- ── Custom Lua function (invoked via the "Call" action) ─────────
local function my_custom_action()
  BFWM.notify("Custom action triggered!")
end
```
