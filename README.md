# BFWM

A tiling window manager for Windows.

## Documentation

Full documentation lives at <https://alex98235.github.io/BFWM/> (source in
[`website/`](website/)).

## Prerequisites

- CMake 3.20+
- C11 compiler (MSYS2 clang)
- C++20 compiler (MSYS2 clang++)

## Building

```sh
cd <project-root>
# create build/ if it doesn't exist
cmake -S . -B build -G "MSYS Makefiles" -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/clang.exe -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/clang++.exe -DCMAKE_MAKE_PROGRAM=C:/msys64/usr/bin/make.exe
# build
cmake --build build
```

## Running

```sh
cmake --build build --target run   # via CMake
./build/BFWM.exe               # or directly
```

### Options

- `--no-bar`: disable the per-monitor bar
- `--config <path>`: path to `config.lua` (default: resolved from exe location, fallback to `../config.lua`)

## Running Tests

```sh
cmake --build build --target test-run # via CMake
./build/test_runner.exe               # or directly
```
