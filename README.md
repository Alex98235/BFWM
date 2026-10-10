<p align="center">
  <img src="icons/bfwm_256.png" alt="BFWM" width="96" />
</p>

<h1 align="center">BFWM</h1>

<p align="center">
  A dynamic tiling window manager for Windows, inspired by
  <a href="https://github.com/glzr-io/glazewm">GlazeWM</a> and
  <a href="https://hyprland.org/">Hyprland</a>, fully configured via Lua.
</p>

## Features

- **Three layouts**: `dwindle` (binary-tree tiling), `monocle` (full-screen
  stacking), and `master` (stacked master/stack columns).
- **Multi-monitor**: independent workspaces on each display.
- **Lua configuration**: hot-reloadable, with full key validation.
- **Built-in status bar**: workspaces, clock, volume, network, CPU, and memory
  indicators.
- **Notification toast**: themable overlay for events and errors, callable from
  the CLI or Lua.
- **Window rules**: automatic float, ignore, or workspace assignment on window
  creation.
- **Sleep/wake recovery**: remembers state and restores your workspaces after
  the system wakes.
- **Monitor hotplugging**: workspaces are reconciled when a monitor is plugged
  in or out.

## Installation

Download the installer from the [releases page](https://github.com/Alex98235/BFWM/releases).
The file is named `BFWM-Setup-<version>.exe`.

## Building from Source

Prerequisites (MSYS2 **UCRT64** shell):

- CMake 3.15+
- MSYS2 **UCRT64** clang / clang++ (C11 + C++20)
- `make` and the `MSYS Makefiles` generator

```sh
git clone https://github.com/Alex98235/BFWM.git
cd BFWM
cmake -S . -B build -G "MSYS Makefiles" \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/clang.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/clang++.exe \
  -DCMAKE_MAKE_PROGRAM=C:/msys64/usr/bin/make.exe
cmake --build build
```

The output binary is `build/BFWM.exe`. See
[`doc-devtools/README.md`](doc-devtools/README.md) for the full toolchain
setup, build options, and vendored dependencies.

### Running

```sh
cmake --build build --target run   # via CMake
./build/BFWM.exe                   # or directly
```

Runtime options:

- `--no-bar`: disable the per-monitor bar.
- `--config <path>`: path to `config.lua` (default: resolved from the exe
  location, falling back to `../config.lua`).

### Testing

```sh
cmake --build build --target test-run   # via CMake
./build/test/test_runner.exe            # or directly
```

The test target depends on the `test/cbelt` submodule — clone with
`git submodule update --init --recursive`.

## Documentation

Full user documentation lives at <https://alex98235.github.io/BFWM/>
(source in [`website/`](website/)).

## Development

Contributor setup, the toolchain, and optional tooling (bfwm-tidy, graphify,
profiling, clangd) are documented in
[`doc-devtools/`](doc-devtools/README.md). See also
[CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](LICENSE) © 2026 Alexander Lindgren
