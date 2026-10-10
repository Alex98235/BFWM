# bfwm-tidy

A clang-tidy / clang-format driver for this project, exposed two ways:

- as a **CLI** (`scripts/bfwm_tidy.py`), used by the pre-commit hook, and
- as a **stdio MCP server**, registered in `opencode.json`, so an agent can run
  analysis and apply fixes directly.

It supersedes the old `scripts/FixAndFormat.ps1`.

## Why it exists

Running clang-tidy over the whole tree serially takes tens of minutes, and
naively parallelising it **corrupts shared headers**: clang-tidy's `--fix` /
`--fix-notes` write source in place, so two translation units fixing the same
header race each other.

bfwm-tidy splits the work the way LLVM's own `run-clang-tidy` does:

1. **analyze** — runs every target translation unit in parallel, each exporting
   its fixes to its own YAML file. Analysis never writes source.
2. **fix** — applies every exported replacement **once, serially**, with
   `clang-apply-replacements` (which dedupes identical header fixes).
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
uv run scripts/bfwm_tidy.py analyze --diff

# Analyze the whole source tree instead.
uv run scripts/bfwm_tidy.py analyze --all

# Apply the last analyze session's fix-list (single serial pass).
uv run scripts/bfwm_tidy.py fix

# Format. --check is a dry run that only reports.
uv run scripts/bfwm_tidy.py format --diff --check
uv run scripts/bfwm_tidy.py format --all

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

With `--diff`, only **staged** files are in scope. A changed header has no
compile command of its own, so it is **mapped to the translation units that
include it** and those are analyzed — a header-only change is still covered.

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

This writes `.git/hooks/pre-commit`, which runs `analyze --diff` and
`format --diff --check` on the staged files. It is **fail-only — it never
writes** — and blocks the commit (exit 1) if either reports anything. Fix, then
re-stage and commit again:

```sh
uv run scripts/bfwm_tidy.py fix
uv run scripts/bfwm_tidy.py format
git add -p
```

The hook coexists with graphify's `post-commit` / `post-checkout` hooks.

## Workflow at a glance

```
analyze --diff   →  review the fix-list  →  fix  →  format  →  analyze --diff (confirm clean)
```

The fixes clang-tidy suggests can be wrong — `analyze` is always safe (it never
writes), so review before applying, and re-run `analyze` afterwards to confirm.

## Configuration

Formatting and checks come from the project files at the repo root:
`.clang-format` and `.clang-tidy`. bfwm-tidy does not add its own rules.
