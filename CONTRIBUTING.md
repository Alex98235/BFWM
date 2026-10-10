# Contributing

Thanks for helping with BFWM. This is a short orientation; the
[developer toolchain docs](doc-devtools/README.md) have the details.

## Setup

BFWM builds with the MSYS2 **UCRT64** environment and Clang. Install the
toolchain and build:

```sh
cmake -S . -B build -G "MSYS Makefiles" \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/clang.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/clang++.exe \
  -DCMAKE_MAKE_PROGRAM=C:/msys64/usr/bin/make.exe
cmake --build build
```

Then run the tests:

```sh
cmake --build build --target test-run
```

See [doc-devtools/README.md](doc-devtools/README.md) for prerequisites, build
options, vendored dependencies, and the full optional-tooling list (bfwm-tidy,
graphify, profiling, clangd).

## Before you commit

Install the formatting/lint pre-commit hook once:

```sh
uv run scripts/bfwm_tidy.py install-hook
```

It runs clang-tidy and clang-format over your staged files and blocks the
commit on findings. To fix them:

```sh
uv run scripts/bfwm_tidy.py analyze --diff   # see what's wrong
uv run scripts/bfwm_tidy.py fix              # apply clang-tidy fixes
uv run scripts/bfwm_tidy.py format           # apply clang-format
git add -p
```

The full story is in [doc-devtools/BFWM_TIDY.md](doc-devtools/BFWM_TIDY.md).

## Conventions

- **Commits** follow [Conventional Commits](https://www.conventionalcommits.org/)
  (`feat(scope): …`, `fix: …`, `refactor(bar): …`, `ci: …`, `build: …`).
- **Formatting** is enforced by `.clang-format`; don't hand-format.
- **User-facing docs** live in `website/`; **developer tooling docs** live in
  `doc-devtools/`.
