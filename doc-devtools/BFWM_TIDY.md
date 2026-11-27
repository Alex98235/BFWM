# bfwm-tidy

A clang-tidy / clang-format driver for this project, exposed two ways:

- as a **CLI** (`scripts/bfwm_tidy.py`), used by the pre-commit hook, and
- as a **stdio MCP server**, registered in `opencode.json`, so an agent can run
  analysis and apply fixes directly.

## Why it exists

Running clang-tidy over the whole tree serially takes tens of minutes, and
naively parallelising it **corrupts shared headers**: clang-tidy's `--fix` /
`--fix-notes` write source in place, so two translation units fixing the same
header race each other.

bfwm-tidy splits the work the way LLVM's own `run-clang-tidy` does:

1. **analyze** — runs every target translation unit in parallel, each exporting
   its fixes to its own YAML file. Analysis never writes source.
2. **fix** — applies the **source-file** replacements (`src/`, `include/`)
   **once, serially**, with `clang-apply-replacements` (which dedupes identical
   header fixes). Test files are **never auto-fixed** — clang-tidy's fixes
   break CBelt's macros; fix or `NOLINT` those by hand.
3. **format** — runs clang-format, one process per file, in parallel.

A full run over the tree drops from ~20 minutes (serial) to a few minutes.

## Requirements

- [uv](https://docs.astral.sh/uv/) — the script is a
  [PEP 723](https://packaging.python.org/en/latest/specifications/inline-script-metadata/)
  script; `uv run` provisions Python and the `mcp` dependency automatically.
- clang-tidy, run-clang-tidy, clang-apply-replacements, clang-format on `PATH`
  (they ship with the MSYS2 UCRT64 clang; see [README.md](README.md)).
- A configured `build/` — the tool reads `build/compile_commands.json`. Run a
  build first (or use the `build` command).

## Commands

Run from the repository root. Add `--jobs N` before the subcommand to change
parallelism (default: 8).

```sh
# Analyze the staged changes (parallel, no writes). Prints diagnostics + a fix-list.
uv run scripts/bfwm_tidy.py analyze

# Analyze the whole source tree instead.
uv run scripts/bfwm_tidy.py analyze --all

# Apply the last analyze session's source fixes (single serial pass).
# Refuses if HEAD moved or the session no longer matches what is staged.
uv run scripts/bfwm_tidy.py fix

# Confirm applying a whole-tree (analyze --all) session.
uv run scripts/bfwm_tidy.py fix --all

# Format. Dry run by default: reports files, exits non-zero if any would change.
uv run scripts/bfwm_tidy.py format
uv run scripts/bfwm_tidy.py format --all

# Format in place (the only way to write).
uv run scripts/bfwm_tidy.py format --write
uv run scripts/bfwm_tidy.py format --all --write

# Incremental cmake build + refresh the compile database.
uv run scripts/bfwm_tidy.py build

# Map changed files to the translation units / headers they affect.
uv run scripts/bfwm_tidy.py affected src/bar/bar.h

# Show database freshness, tool versions and the current session.
uv run scripts/bfwm_tidy.py status
```

`analyze` returns `clean: true` when nothing was reported; pass
`--fail-on-findings` to exit non-zero when it isn't. Because `.clang-tidy` sets
`WarningsAsErrors: "*"`, any finding counts as a failure.

### Scoping

`analyze` and `format` are **staged-only by default**. `analyze` never writes
source, and `format` is a **dry run** unless you pass `--write`; it exits
non-zero when a dry run finds files that would change. `--all` widens either
command to the whole source tree.

With the default scope a changed header has no compile command of its own, so
it is **mapped to the translation units that include it** and those are
analyzed — a header-only change is still covered.

`analyze` and `format` cover `src/` + `test/tests/` (CBelt's `test/cbelt` is
excluded). `fix`, however, only ever writes **source files** (`src/`,
`include/`); test findings are never auto-applied.

`--check` is still accepted by `format` as a deprecated no-op (a dry run is
the default), so old invocations cannot become destructive. `--diff` has been
removed.

### Session safety

Each `analyze` records its scope (`staged` or `all`), the file list and the
current `HEAD` in `build/.bfwm-tidy/sessions/<id>/session.json`. `fix` refuses
to apply:

- a session produced at a different `HEAD` — re-run `analyze` at the current
  `HEAD`;
- an `analyze --all` session unless `fix --all` confirms it;
- a staged-scope fix that would modify a file which is no longer staged.

Before writing, `fix` prints the exact files it will modify.

## MCP server

`opencode.json` registers the server:

```json
{
  "mcp": {
    "bfwm-tidy": {
      "type": "local",
      "command": ["uv", "run", "scripts/bfwm_tidy.py", "mcp"],
      "enabled": true
    }
  }
}
```

It exposes six tools: `analyze_targets`, `apply_fixes_tool`, `format_sources`,
`build`, `affected_files`, `status_tool`. **Restart opencode** after changing
the config — it is loaded once at startup.

## Pre-commit hook

Install it once:

```sh
uv run scripts/bfwm_tidy.py install-hook
```

This writes `.git/hooks/pre-commit`, which runs `analyze` (staged, hunk-filtered)
and the `format` check on the staged files. It is **fail-only — it never
writes source** — and blocks the commit (exit 1) if either reports anything.
Fix, then re-stage and commit again:

```sh
uv run scripts/bfwm_tidy.py fix
uv run scripts/bfwm_tidy.py format --write
git add -p
```

The hook coexists with graphify's `post-commit` / `post-checkout` hooks.

## Workflow at a glance

```
analyze  →  review the fix-list  →  fix  →  format --write  →  analyze (confirm clean)
```

The fixes clang-tidy suggests can be wrong — `analyze` is always safe (it never
writes), so review before applying, and re-run `analyze` afterwards to confirm.

## Configuration

Formatting and checks come from the project files at the repo root:
`.clang-format` and `.clang-tidy`. bfwm-tidy does not add its own rules.
