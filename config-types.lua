---@class BFWMConfig
---@field gap_between integer
---@field gap_edge integer
---@field border_color string `#RRGGBB` hex
---@field inactive_border string
---@field border_width integer
---@field border_radius integer
---@field focus_follows_mouse boolean
---@field mouse_follows_focus boolean
---@field unlock_window_resize string e.g. `"LALT"`
---@field unlock_window_move string
---@field layout string `"dwindle"` or `"monocle"`
---@field disabled_monitors integer[]
---@field keybinds table[]
---@field window_rules table[]
---@field workspaces table[]
---@field notify fun(text: string)

---@class BarConfig
---@field height integer
---@field enabled boolean
---@field corner_radius? integer
---@field margin EdgeInsets
---@field padding EdgeInsets
---@field border BorderStyle
---@field font FontConfig
---@field colors BarColorConfig
---@field indicators BarIndicatorConfig[]

---@class BarColorConfig
---@field background string
---@field text string
---@field active_workspace string
---@field inactive_workspace string
---@field tab_border string

---@class FontConfig
---@field name string
---@field size integer
---@field weight? integer

---@class BorderStyle
---@field width? integer
---@field color string

---@class EdgeInsets
---@field top integer
---@field right integer
---@field bottom integer
---@field left integer

---@class BarIndicatorConfig
---@field type string `"workspaces"|"title"|"clock"|"volume"|"network"|"cpu"|"memory"|"custom"`
---@field align? string `"left"|"center"|"right"`
---@field id? string stable identifier for this indicator instance
---@field max_width? integer
---@field format? string unified format dialect: `{name}` / `{name:.Nf}` (clock uses strftime)
---@field color? string
---@field size? integer font size for this indicator
---@field poll_rate? integer ms between updates (0 = provider default)
---@field states? IndicatorStateRule[] conditional icon/color/format overrides
---@field position_bar? boolean workspaces: show the position indicator
---@field on_click? string global Lua function called on left click: on_click(id, button, mods)
---@field on_scroll? string global Lua function called on mouse wheel: on_scroll(id, dir)
---@field output? string custom: global Lua `output(id)` returning a string or table

---@class CustomOutput
---@field text? string literal text (kept when `format` is empty)
---@field state? string discrete state name (matched by `states`)
---@field icon? string glyph used by `{icon}`
---@field value? number primary numeric value (`{value}`)
---@field color? string `#rrggbb`
---@field values? table<string, number|string> extra named values

---@class IndicatorStateRule
---@field state? string discrete state name (volume `"muted"`, network `"disconnected"|"ethernet"`)
---@field at? number numeric threshold: applies when the primary value is >= at
---@field icon? string
---@field format? string
---@field color? string

---@class SnackbarConfig
---@field enabled boolean
---@field log_level string `"none"|"error"|"warn"|"info"|"debug"`
---@field display_duration_ms integer
---@field position string `"top-left"|"top-right"|"bottom-left"|"bottom-right"`
---@field margin_left integer
---@field margin_right integer
---@field margin_top integer
---@field margin_bottom integer
---@field padding_left integer
---@field padding_right integer
---@field padding_top integer
---@field padding_bottom integer
---@field min_width integer
---@field max_width integer
---@field background string
---@field text string
---@field divider string
---@field divider_height integer
---@field font_size integer
---@field font_name string
---@field corner_radius integer
---@field opacity integer 0–255
---@field close_on_click boolean
---@field pause_on_hover boolean
---@field click_to_expand boolean
---@field max_queue integer
---@field monitor integer
---@field colors SnackbarColorConfig

---@class SnackbarColorConfig
---@field error string
---@field warn string
---@field info string
---@field debug string
---@field normal string

---@type BFWMConfig
_G.BFWM = {}

---@type BarConfig
_G.Bar = {}

---@type SnackbarConfig
_G.Snackbar = {}
