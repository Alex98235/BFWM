# Developer toolchain

Everything you need to build, test, and work on BFWM, plus the optional tooling
around it. **This is the single source of truth for the development
environment** — when a new tool is added to the project, document it here.

New here? Start with [Building](#building), then wire up the optional tools you
want. Everything below assumes you are working from the repository root.

---

## Canonical environment: MSYS2 UCRT64 + Clang

BFWM is a C++20 Win32 application that targets **Windows 10**
(`WINVER`/`_WIN32_WINNT = 0x0A00`). The canonical build environment is the MSYS2
**UCRT64** environment using **Clang** — use it for both local development and
CI, so the binary you test is the binary that ships.

UCRT64 is the choice because:

- It uses the **Universal C Runtime** (`ucrtbase.dll`), which is in-box on
  Windows 10/11.
- **MINGW64 (msvcrt) is deprecated by MSYS2** — MSYS2's own guidance is "if
  unsure, use UCRT64".
- The two runtimes differ *observably*, not just in packaging: UTF-8 locale
  support, the C99 `printf` family, `errno` values/strings, and `FILE`/`stat`/
  `time` internals all differ. Object files and static libraries built for
  different CRTs must **never** be linked together.

Both UCRT64 and MINGW64 use Clang **with libstdc++**, so the C++ ABI itself
matches between them — the C runtime is the only axis that matters here. Do not
mix them anyway (see [Toolchain parity](#toolchain-parity-dev-vs-ci)).

> **CLANG64 is not a drop-in.** It is Clang with **libc++**, a different C++
> standard library and therefore a real ABI break. Don't switch to it without
> doing it on both ends deliberately.

---

## Required tools

| Tool | Purpose | Package (UCRT64) |
| --- | --- | --- |
| [MSYS2](https://www.msys2.org/) | Provides the whole toolchain | — |
| Clang / Clang++ | C11 + C++20 compiler | `mingw-w64-ucrt-x86_64-clang` |
| GNU binutils (`windres`, `ld`) | Resource compilation, linking | `mingw-w64-ucrt-x86_64-binutils` |
| make | Build driver (`MSYS Makefiles`) | `make` |
| CMake ≥ 3.15 | Build system (project minimum) | `cmake` |
| git | Version control, CBelt submodule | `git` |

Install (from an MSYS2 **UCRT64** shell):

```sh
pacman -S --needed \
  mingw-w64-ucrt-x86_64-clang \
  mingw-w64-ucrt-x86_64-binutils \
  make cmake git
```

Note: `cmake` and `make` are MSYS packages; `make` lives at
`/usr/bin/make.exe`, shared across all MSYS2 environments.

---

## Building

From the repository root, in an MSYS2 **UCRT64** shell:

```sh
cmake -S . -B build -G "MSYS Makefiles" \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/clang.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/clang++.exe \
  -DCMAKE_MAKE_PROGRAM=C:/msys64/usr/bin/make.exe

cmake --build build
```

This produces:

- `build/BFWM.exe` — the production (GUI) build.
- `build/debug/BFWM-debug.exe` — a console-subsystem debug build.
- `build/test/test_runner.exe` — the unit tests.

> **Generator note.** `-G "MSYS Makefiles"` is the generator used with a
> **Windows-native** `cmake`. If you use the MSYS2 `cmake` package instead, its
> generator is `-G "Unix Makefiles"` (what CI uses). Either is fine — the
> generator only affects the build driver, not the produced binary. The
> compiler and runtime are what must stay consistent (below). CMake will
> auto-detect `windres`; pin it with `-DCMAKE_RC_COMPILER=C:/msys64/ucrt64/bin/windres.exe`
> if it ever picks `llvm-rc` instead.

## Running

```sh
cmake --build build --target run        # production build, via CMake
./build/BFWM.exe                        # or directly

cmake --build build --target run-debug  # console debug build
```

Runtime options:

- `--no-bar` — disable the per-monitor bar.
- `--config <path>` — path to `config.lua` (default: resolved from the exe
  location, falling back to `../config.lua`).

## Testing

```sh
cmake --build build --target test-run   # via CMake
./build/test/test_runner.exe            # or directly
```

The test target depends on the `test/cbelt` **git submodule**, so first check
out a clone with `git submodule update --init --recursive`.

## Build options

| Option | Default | Effect |
| --- | --- | --- |
| `BFWM_PROFILE_BUILD` | `OFF` | Emits CodeView debug info + a PDB (via `lld`) for ETW profiling. See [PROFILING.md](PROFILING.md). |
| `BFWM_DEBUG_CRASHDUMP` | `OFF` | Compiles the `BFWM` target with the crash-dump VEH handler. |

Configure with `-D<option>=ON`.

---

## Vendored dependencies

Everything needed to build is in the tree; there is no package manager step for
BFWM's own dependencies.

| Dependency | Location | How it's provided |
| --- | --- | --- |
| Lua 5.4.7 | `external/lua-5.4.7/` | Vendored source, built as a static lib |
| Clay (UI layout) | `external/clay/` | Vendored, `add_subdirectory` |
| tiny-regex-c | `external/tiny-regex-c/` | Vendored source, static lib |
| CBelt (test framework) | `test/cbelt/` | **Git submodule** (`git submodule update --init`) |

---

## Optional tooling

None of these are required to build BFWM. Each has its own page.

| Tool | What it's for | Page |
| --- | --- | --- |
| **bfwm-tidy** | clang-tidy + clang-format driver, exposed as a CLI and an MCP server; backs the pre-commit hook. Requires [uv](https://docs.astral.sh/uv/). | [BFWM_TIDY.md](BFWM_TIDY.md) |
| **graphify** | Code knowledge graph (MCP + git hooks). Requires Python 3.10 and uv. | [GRAPHIFY.md](GRAPHIFY.md) |
| **PerfView** | ETW profiling of the `BFWM_PROFILE_BUILD` build. | [PROFILING.md](PROFILING.md) |
| **clangd** | Editor LSP. Configured by `.clangd`; reads the root `compile_commands.json`. | — |
| **clang-format / clang-tidy** | Formatting + static analysis. Config: `.clang-format`, `.clang-tidy`. Best driven through bfwm-tidy. | [BFWM_TIDY.md](BFWM_TIDY.md) |
| **Node.js ≥ 20** | Building the docs website (`website/`). | `website/README.md` |

### Editor / LSP

`CMakeLists.txt` sets `CMAKE_EXPORT_COMPILE_COMMANDS ON` and a build step copies
`compile_commands.json` to the repository root, so **clangd** finds it whether
you build in `build/`, `build-profile/`, or elsewhere. `.clangd` pins the target
triple and the `external/` include dirs. Start a build once so the database
exists.

---

## Toolchain parity (dev vs CI)

CI is supposed to ship the artifact you tested, so **pin what changes behavior
and let the rest float**:

**Must match**

- Environment / C runtime: **UCRT64** (`ucrt`) — dev and CI.
- Compiler: Clang, and the C++ stdlib flavor (libstdc++).
- Target: `x86_64`, `_WIN32_WINNT=0x0A00`.
- Behavioral flags/definitions: C++20, `-Wall -Wpedantic -Wextra`, etc.
- The generator, ideally, so makefile semantics don't drift.

**May differ safely**

- CMake version, absolute paths, build directory, host tool minor versions.

**Current state / follow-ups**

- CI (`.github/workflows/release.yml`) still builds with the deprecated
  **MINGW64** (`msystem: MSYS`, `mingw-w64-x86_64-*`). It should be migrated to
  **UCRT64** to match local development and the recommendation above.
- The robust fix is a pinned toolchain (`CMakePresets.json` and/or a toolchain
  file setting `CMAKE_C_COMPILER`, `CMAKE_CXX_COMPILER`, `CMAKE_RC_COMPILER`),
  so dev and CI consume identical settings and the CMake version stops
  mattering.
- `toolchain-msys2.cmake` hard-codes **gcc/g++** and overrides the compile/link
  rules; it contradicts the Clang build and its stated cause ("CMake 4.4.2
  broken compiler detection") is unverified. Re-test whether it is still needed
  before relying on it.

### Gotchas

- **Never mix UCRT64 and MINGW64** artifacts (objects, static libs, runtime
  DLLs). Reconfigure from scratch if you switch environments.
- A `build/` tree is environment-specific; delete it and reconfigure when
  changing toolchains.
- `build/` and the root `compile_commands.json` are git-ignored.
