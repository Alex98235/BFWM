#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = ["mcp>=2"]
# ///
"""bfwm-tidy - clang-tidy / clang-format driver for BFWM.

Why this exists
---------------
Running clang-tidy serially over the whole tree takes tens of minutes, and
naively parallelising it corrupts shared headers: clang-tidy's `--fix` /
`--fix-notes` write source in place, so two translation units fixing the same
header race each other.

This tool splits the work the way LLVM's own `run-clang-tidy` does:

  * analyze - runs every target translation unit in parallel, each with
    ``--export-fixes`` into its own file. Analysis never writes source.
  * fix     - applies the *source-file* replacements (``src/``, ``include/``)
    once, serially, with ``clang-apply-replacements`` (dedupes identical header
    fixes). Test files are never auto-fixed.
  * format  - runs clang-format, one process per file, in parallel.

Scoping
-------
``--diff`` limits work to what is staged. A changed *header* has no compile
command of its own, so it is mapped to the translation units that include it
and those are analyzed instead - a header-only change is still covered.

The same engine is exposed as a CLI and as a stdio MCP server:

    uv run scripts/bfwm_tidy.py <command> ...
    uv run scripts/bfwm_tidy.py mcp
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
import uuid
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import NoReturn

# --------------------------------------------------------------------------
# Paths and constants
# --------------------------------------------------------------------------

REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO_ROOT / "build"
WORK_DIR = BUILD_DIR / ".bfwm-tidy"
DB_SRC = BUILD_DIR / "compile_commands.json"
DB_CACHE = WORK_DIR / "compile_commands.json"
SESSIONS_DIR = WORK_DIR / "sessions"
CURRENT_SESSION = WORK_DIR / "current-session"

SOURCE_DIRS = tuple(
    d
    for d in (REPO_ROOT / "src", REPO_ROOT / "include", REPO_ROOT / "test" / "tests")
    if d.is_dir()
)
HEADER_FILTER = r".*[/\\]BFWM[/\\](src|include|test[/\\]tests)[/\\].*"
# Only source files are auto-fixed. Tests are analyzed and formatted but never
# auto-fixed: clang-tidy rewrites tokens inside CBelt's function-like macros.
FIXABLE_DIRS = tuple(d for d in (REPO_ROOT / "src", REPO_ROOT / "include") if d.is_dir())
SOURCE_SUFFIXES = (".cpp", ".h", ".hpp")
DEFAULT_JOBS = max(1, min(8, os.cpu_count() or 4))

_SRC_SUFFIX = SOURCE_SUFFIXES


def _die(message: str) -> NoReturn:
    print(f"bfwm-tidy: {message}", file=sys.stderr)
    raise SystemExit(2)


# --------------------------------------------------------------------------
# Tool discovery
# --------------------------------------------------------------------------


def _sibling(name: str) -> str | None:
    """Find a tool sitting next to clang-tidy (run-clang-tidy has no .exe)."""
    tidy = shutil.which("clang-tidy")
    if not tidy:
        return None
    for candidate in (Path(tidy).parent / name, Path(tidy).parent / f"{name}.exe"):
        if candidate.exists():
            return str(candidate)
    return None


def clang_tidy() -> str:
    return shutil.which("clang-tidy") or _sibling("clang-tidy") or _die("clang-tidy not found")


def clang_format() -> str:
    return shutil.which("clang-format") or _sibling("clang-format") or _die("clang-format not found")


def run_clang_tidy() -> str:
    found = shutil.which("run-clang-tidy") or _sibling("run-clang-tidy")
    return found or _die("run-clang-tidy not found (ships with LLVM)")


def clang_apply_replacements() -> str:
    found = shutil.which("clang-apply-replacements") or _sibling("clang-apply-replacements")
    return found or _die("clang-apply-replacements not found")


# --------------------------------------------------------------------------
# Compile database
# --------------------------------------------------------------------------

_DRIVE_RE = re.compile(r'(^|[\s"\'])/([A-Za-z])/')
_RSP_RE = re.compile(r"@([^\s]+\.rsp)")


def _transform_command(command: str, directory: str) -> str:
    """Normalize a compile_commands.json command entry for clang-tidy.

    Converts POSIX drive paths (/C/...) to native (C:/...) and expands
    response files, whose contents are already native.
    """
    command = _DRIVE_RE.sub(r"\1\2:/", command)

    def expand(match: re.Match[str]) -> str:
        rsp = Path(directory) / match.group(1)
        try:
            return rsp.read_text(encoding="utf-8", errors="replace").strip() + " "
        except OSError:
            return match.group(0)

    return _RSP_RE.sub(expand, command)


def ensure_db(force: bool = False) -> Path:
    """Return a clang-tidy friendly compile database, regenerating if stale."""
    if not DB_SRC.exists():
        _die(f"missing {DB_SRC} - run `bfwm-tidy build` first")
    fresh = DB_CACHE.exists() and DB_CACHE.stat().st_mtime >= DB_SRC.stat().st_mtime
    if fresh and not force:
        return DB_CACHE

    entries = json.loads(DB_SRC.read_text(encoding="utf-8-sig"))
    for entry in entries:
        if entry.get("command"):
            entry["command"] = _transform_command(entry["command"], entry.get("directory", ""))

    WORK_DIR.mkdir(parents=True, exist_ok=True)
    DB_CACHE.write_text(json.dumps(entries, indent=2), encoding="utf-8")
    return DB_CACHE


def _db_entries() -> list[dict]:
    return json.loads(ensure_db().read_text(encoding="utf-8"))


def _db_tus() -> set[Path]:
    return {Path(e["file"]).resolve() for e in _db_entries() if e.get("file")}


# --------------------------------------------------------------------------
# Git + path helpers
# --------------------------------------------------------------------------


def _git(*args: str) -> str:
    proc = subprocess.run(["git", *args], cwd=REPO_ROOT, capture_output=True, text=True)
    return proc.stdout


def changed_files(staged: bool) -> list[str]:
    if staged:
        out = _git("diff", "--cached", "--name-only", "--diff-filter=ACM")
    else:
        out = _git("diff", "--name-only", "--diff-filter=ACM", "HEAD")
        out += _git("ls-files", "--others", "--exclude-standard")
    return sorted({line.strip() for line in out.splitlines() if line.strip()})


def _abs(rel: str) -> Path:
    path = Path(rel)
    return (path if path.is_absolute() else REPO_ROOT / path).resolve()


def _in_source_tree(path: Path) -> bool:
    return any(path == d or d in path.parents for d in SOURCE_DIRS)


def _is_fixable(path: str) -> bool:
    """True when a path may be auto-fixed (under src/ or include/)."""
    try:
        resolved = Path(path).resolve()
    except OSError:
        return False
    return any(resolved == d or d in resolved.parents for d in FIXABLE_DIRS)


def _iter_source_files():
    for directory in SOURCE_DIRS:
        for path in directory.rglob("*"):
            if path.suffix in _SRC_SUFFIX:
                yield path.resolve()


# --------------------------------------------------------------------------
# Include graph (for changed-header -> translation unit mapping)
# --------------------------------------------------------------------------

_INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)


def _build_include_graph() -> dict[Path, set[Path]]:
    """Reverse include map: a file -> the files that include it."""
    included_by: dict[Path, set[Path]] = {}
    for source in _iter_source_files():
        try:
            text = source.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for inc in _INCLUDE_RE.findall(text):
            dep = (source.parent / inc)
            if not dep.exists():
                for root in (*SOURCE_DIRS, REPO_ROOT):
                    if (root / inc).exists():
                        dep = root / inc
                        break
            if dep.exists():
                included_by.setdefault(dep.resolve(), set()).add(source)
    return included_by


def _tus_for_headers(headers: set[Path], included_by: dict[Path, set[Path]]) -> set[Path]:
    seen: set[Path] = set()
    stack = list(headers)
    tus: set[Path] = set()
    while stack:
        current = stack.pop()
        for parent in included_by.get(current, ()):
            if parent in seen:
                continue
            seen.add(parent)
            if parent.suffix == ".cpp":
                tus.add(parent)
            else:
                stack.append(parent)
    return tus


# --------------------------------------------------------------------------
# Target resolution
# --------------------------------------------------------------------------


def resolve_targets(
    diff: bool, staged: bool = True, explicit: list[str] | None = None
) -> dict[str, list[str]]:
    """Return the tidy and format target sets (absolute paths)."""
    tus_in_db = _db_tus()

    if explicit:
        changed = [_abs(f) for f in explicit]
    elif diff:
        changed = [_abs(f) for f in changed_files(staged)]
    else:
        all_tidy = sorted(str(p) for p in tus_in_db if _in_source_tree(p))
        all_fmt = sorted(str(p) for p in _iter_source_files())
        return {"tidy": all_tidy, "format": all_fmt}

    tidy: set[Path] = set()
    fmt: set[Path] = set()
    headers: set[Path] = set()

    for path in changed:
        if not _in_source_tree(path) or path.suffix not in SOURCE_SUFFIXES:
            continue
        fmt.add(path)
        if path.suffix == ".cpp" and path in tus_in_db:
            tidy.add(path)
        elif path.suffix in (".h", ".hpp"):
            headers.add(path)

    if headers:
        included_by = _build_include_graph()
        for tu in _tus_for_headers(headers, included_by):
            if tu in tus_in_db and _in_source_tree(tu):
                tidy.add(tu)

    return {"tidy": sorted(str(p) for p in tidy), "format": sorted(str(p) for p in fmt)}


# --------------------------------------------------------------------------
# Diagnostics parsing
# --------------------------------------------------------------------------

_DIAG_RE = re.compile(
    r"^\s*(?P<file>.*?\.(?:cpp|h|hpp)):(?P<line>\d+):(?P<col>\d+):"
    r"\s+(?P<sev>error|warning|note|remark):\s+(?P<msg>.*?)\s*$"
)
_CHECK_RE = re.compile(r"\[([\w.,=\-]+)\]\s*$")


def _parse_diagnostics(text: str) -> list[dict]:
    out: list[dict] = []
    seen: set[tuple] = set()
    for line in text.splitlines():
        match = _DIAG_RE.match(line)
        if not match:
            continue
        # Notes are source context for a preceding diagnostic, not findings.
        if match.group("sev") == "note":
            continue
        message = match.group("msg")
        check = ""
        check_match = _CHECK_RE.search(message)
        if check_match:
            check = check_match.group(1)
            message = message[: check_match.start()].rstrip()
        key = (
            os.path.normcase(os.path.normpath(match.group("file"))),
            match.group("line"),
            match.group("col"),
            message,
        )
        if key in seen:
            continue
        seen.add(key)
        out.append(
            {
                "file": match.group("file"),
                "line": int(match.group("line")),
                "column": int(match.group("col")),
                "severity": match.group("sev"),
                "check": check,
                "message": message,
            }
        )
    return out


_FP_RE = re.compile(r"FilePath:\s*(.+)")


def _yaml_paths(yaml_file: Path) -> list[str]:
    text = yaml_file.read_text(encoding="utf-8", errors="replace")
    return [raw.strip().strip("'\"") for raw in _FP_RE.findall(text)]


def _yaml_fix_files(fixes_dir: Path) -> list[str]:
    files: set[str] = set()
    for yaml_file in fixes_dir.glob("*.yaml"):
        files.update(_yaml_paths(yaml_file))
    return sorted(files)


# --------------------------------------------------------------------------
# Core operations
# --------------------------------------------------------------------------


def analyze(targets: list[str], jobs: int = DEFAULT_JOBS) -> dict:
    """Run clang-tidy in parallel, exporting fixes. Never writes source."""
    if not targets:
        return {
            "session": None,
            "targets": 0,
            "diagnostics": [],
            "fix_list": {"session": None, "files": [], "tu_yamls": 0},
            "clean": True,
            "returncode": 0,
        }

    db = ensure_db()
    wanted = {str(Path(t).resolve()) for t in targets}

    session_id = time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:6]
    session = SESSIONS_DIR / session_id
    fixes_dir = session / "fixes"
    db_dir = session / "db"
    fixes_dir.mkdir(parents=True, exist_ok=True)
    db_dir.mkdir(parents=True, exist_ok=True)

    entries = [e for e in json.loads(db.read_text(encoding="utf-8")) if str(Path(e["file"]).resolve()) in wanted]
    (db_dir / "compile_commands.json").write_text(json.dumps(entries, indent=2), encoding="utf-8")

    cmd = [
        sys.executable,
        run_clang_tidy(),
        "-p",
        str(db_dir),
        f"-export-fixes={fixes_dir}{os.sep}",
        f"-header-filter={HEADER_FILTER}",
        "-j",
        str(jobs),
    ]
    proc = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True)
    output = (proc.stdout or "") + (proc.stderr or "")
    diagnostics = _parse_diagnostics(output)

    yamls = list(fixes_dir.glob("*.yaml"))
    fix_files = _yaml_fix_files(fixes_dir)

    WORK_DIR.mkdir(parents=True, exist_ok=True)
    CURRENT_SESSION.write_text(session_id, encoding="utf-8")

    return {
        "session": session_id,
        "targets": len(targets),
        "diagnostics": diagnostics,
        "fix_list": {"session": session_id, "files": fix_files, "tu_yamls": len(yamls)},
        "clean": not diagnostics,
        "returncode": proc.returncode,
    }


def current_session() -> str | None:
    if CURRENT_SESSION.exists():
        value = CURRENT_SESSION.read_text(encoding="utf-8").strip()
        return value or None
    return None


def apply_fixes(session_id: str | None = None) -> dict:
    """Apply an analyze session's fix-list once, serially.

    Only replacements targeting source files (``src/``, ``include/``) are
    applied. Test files are analyzed but never auto-fixed: clang-tidy rewrites
    tokens inside CBelt's function-like macros and breaks the build. Fix test
    findings by hand (or NOLINT them).
    """
    session_id = session_id or current_session()
    if not session_id:
        _die("no session to fix - run `analyze` first")
    session = SESSIONS_DIR / session_id
    fixes_dir = session / "fixes"
    if not fixes_dir.is_dir():
        _die(f"session {session_id} has no fixes directory")

    # Stage only the source-file replacements; drop anything touching tests.
    staging = session / "fixes-src"
    shutil.rmtree(staging, ignore_errors=True)
    staging.mkdir(parents=True, exist_ok=True)
    applied = 0
    skipped = 0
    for yaml_file in fixes_dir.glob("*.yaml"):
        paths = _yaml_paths(yaml_file)
        if not paths:
            continue
        if all(_is_fixable(p) for p in paths):
            shutil.copy2(yaml_file, staging / yaml_file.name)
            applied += 1
        else:
            skipped += 1

    if applied == 0:
        return {
            "session": session_id,
            "returncode": 0,
            "applied": 0,
            "skipped": skipped,
            "output": "no source-file fixes to apply",
        }

    proc = subprocess.run(
        [clang_apply_replacements(), "-ignore-insert-conflict", str(staging)],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    return {
        "session": session_id,
        "returncode": proc.returncode,
        "applied": applied,
        "skipped": skipped,
        "output": ((proc.stdout or "") + (proc.stderr or "")).strip(),
    }


def format_files(targets: list[str], check: bool = False, jobs: int = DEFAULT_JOBS) -> dict:
    """Run clang-format in parallel; check mode is a dry run that never writes."""
    if not targets:
        return {"changed": [], "ok": True, "count": 0, "mode": "check" if check else "write"}

    exe = clang_format()

    def needs_format(path: str) -> tuple[str, int]:
        proc = subprocess.run(
            [exe, "--dry-run", "-Werror", path],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
        )
        return path, proc.returncode

    changed: list[str] = []
    with ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        for path, code in pool.map(needs_format, targets):
            if code != 0:
                changed.append(path)

    if not check and changed:
        with ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
            list(
                pool.map(
                    lambda path: subprocess.run(
                        [exe, "-i", path],
                        cwd=REPO_ROOT,
                        capture_output=True,
                        text=True,
                    ),
                    changed,
                )
            )

    return {
        "changed": changed,
        "ok": not changed,
        "count": len(targets),
        "mode": "check" if check else "write",
    }


def do_build() -> dict:
    proc = subprocess.run(
        ["cmake", "--build", "build"], cwd=REPO_ROOT, capture_output=True, text=True
    )
    tail = "\n".join(((proc.stdout or "") + (proc.stderr or "")).splitlines()[-15:])
    if proc.returncode == 0:
        ensure_db(force=True)
    return {"returncode": proc.returncode, "tail": tail}


def affected(paths: list[str]) -> dict:
    resolved = [_abs(p) for p in paths]
    changed_headers = {p for p in resolved if p.suffix in (".h", ".hpp")}
    direct_tus = {p for p in resolved if p.suffix == ".cpp"}
    tus = set(direct_tus)
    if changed_headers:
        tus |= _tus_for_headers(changed_headers, _build_include_graph())
    return {
        "headers": sorted(str(p) for p in changed_headers),
        "tus": sorted(str(p) for p in tus),
    }


def _tool_version(exe: str, *args: str) -> str | None:
    try:
        proc = subprocess.run([exe, *args], capture_output=True, text=True)
        first = ((proc.stdout or "") + (proc.stderr or "")).strip().splitlines()
        return first[0] if first else None
    except OSError:
        return None


def status() -> dict:
    stale = None
    if DB_SRC.exists() and DB_CACHE.exists():
        stale = DB_CACHE.stat().st_mtime < DB_SRC.stat().st_mtime
    return {
        "repo": str(REPO_ROOT),
        "compile_db": str(DB_SRC) if DB_SRC.exists() else None,
        "db_cached": DB_CACHE.exists(),
        "db_stale": stale,
        "clang_tidy": _tool_version(clang_tidy(), "--version"),
        "clang_format": _tool_version(clang_format(), "--version"),
        "current_session": current_session(),
        "source_dirs": [str(d) for d in SOURCE_DIRS],
    }


# --------------------------------------------------------------------------
# Pre-commit hook
# --------------------------------------------------------------------------


def run_hook() -> int:
    targets = resolve_targets(diff=True, staged=True)
    if not targets["tidy"] and not targets["format"]:
        return 0

    result = analyze(targets["tidy"], jobs=DEFAULT_JOBS)
    fmt = format_files(targets["format"], check=True)

    problems = bool(result["diagnostics"]) or not fmt["ok"]
    if not problems:
        return 0

    print("", file=sys.stderr)
    print("bfwm-tidy: commit blocked", file=sys.stderr)
    if result["diagnostics"]:
        print(f"  clang-tidy: {len(result['diagnostics'])} finding(s)", file=sys.stderr)
        for diag in result["diagnostics"][:20]:
            location = f"{diag['file']}:{diag['line']}:{diag['column']}"
            check = f" [{diag['check']}]" if diag["check"] else ""
            print(f"    {location}: {diag['message']}{check}", file=sys.stderr)
    if not fmt["ok"]:
        print(f"  clang-format: {len(fmt['changed'])} file(s) need formatting", file=sys.stderr)
        for path in fmt["changed"][:20]:
            print(f"    {path}", file=sys.stderr)
    print("", file=sys.stderr)
    print("  review then apply fixes:  uv run scripts/bfwm_tidy.py fix", file=sys.stderr)
    print("  apply formatting:         uv run scripts/bfwm_tidy.py format", file=sys.stderr)
    print("  re-stage and commit again.", file=sys.stderr)
    return 1


_HOOK_TEMPLATE = """#!/bin/sh
# Installed by: uv run scripts/bfwm_tidy.py install-hook
# Blocks a commit on clang-tidy findings or unformatted staged sources.
root=$(git rev-parse --show-toplevel 2>/dev/null) || exit 0
cd "$root" || exit 0
exec uv run scripts/bfwm_tidy.py hook
"""


def install_hook() -> dict:
    hooks_dir = REPO_ROOT / ".git" / "hooks"
    if not hooks_dir.is_dir():
        _die("no .git/hooks directory - is this a git checkout?")
    hook_path = hooks_dir / "pre-commit"
    hook_path.write_text(_HOOK_TEMPLATE, encoding="utf-8", newline="\n")
    try:
        hook_path.chmod(0o755)
    except OSError:
        pass
    return {"installed": str(hook_path)}


# --------------------------------------------------------------------------
# MCP server
# --------------------------------------------------------------------------


def build_mcp_server():
    from mcp.server.mcpserver import MCPServer

    server = MCPServer("bfwm-tidy")

    @server.tool()
    def analyze_targets(diff: bool = True, staged: bool = True, files: list[str] | None = None) -> dict:
        """Run clang-tidy over targets without modifying any source.

        diff=true (default) limits to staged files and maps changed headers to
        the translation units that include them. Returns diagnostics plus a
        fix-list for `apply_fixes`. `clean` is true when nothing was reported.
        """
        targets = resolve_targets(diff=diff, staged=staged, explicit=files)
        return analyze(targets["tidy"])

    @server.tool()
    def apply_fixes_tool(session: str | None = None) -> dict:
        """Apply an analyze session's source-file fixes, once and serially.

        Only `src/` and `include/` files are fixed; test files are never
        auto-fixed (clang-tidy's fixes break CBelt's macros).
        """
        return apply_fixes(session)

    @server.tool()
    def format_sources(diff: bool = True, staged: bool = True, check: bool = True) -> dict:
        """Run clang-format. check=true is a dry run that reports files needing
        formatting; check=false rewrites them in place."""
        targets = resolve_targets(diff=diff, staged=staged)
        return format_files(targets["format"], check=check)

    @server.tool()
    def build() -> dict:
        """Run an incremental cmake build and refresh the compile database."""
        return do_build()

    @server.tool()
    def affected_files(paths: list[str]) -> dict:
        """Map changed files to the translation units and headers they affect."""
        return affected(paths)

    @server.tool()
    def status_tool() -> dict:
        """Report compile-database freshness, tool versions and session state."""
        return status()

    return server


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------


def _emit(payload: dict) -> int:
    print(json.dumps(payload, indent=2))
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="bfwm-tidy", description=(__doc__ or "").splitlines()[0])
    parser.add_argument("--jobs", type=int, default=DEFAULT_JOBS, help="parallel jobs (default: %(default)s)")
    sub = parser.add_subparsers(dest="command", required=True)

    p_analyze = sub.add_parser("analyze", help="run clang-tidy, export fixes (no writes)")
    p_analyze.add_argument("--diff", action="store_true", help="only staged files (with header->TU mapping)")
    p_analyze.add_argument("--all", action="store_true", help="entire source tree")
    p_analyze.add_argument("--fail-on-findings", action="store_true", help="exit 1 if anything was reported")

    p_fix = sub.add_parser(
        "fix", help="apply the last analyze session's source fixes (tests are never auto-fixed)"
    )
    p_fix.add_argument("--session", default=None)

    p_format = sub.add_parser("format", help="run clang-format")
    p_format.add_argument("--diff", action="store_true", help="only staged files")
    p_format.add_argument("--all", action="store_true", help="entire source tree")
    p_format.add_argument("--check", action="store_true", help="dry run (report, do not write)")

    sub.add_parser("build", help="incremental cmake build + refresh compile db")
    sub.add_parser("status", help="report db freshness, tool versions, session state")
    sub.add_parser("hook", help="pre-commit entry point")
    sub.add_parser("install-hook", help="install the pre-commit hook")
    sub.add_parser("mcp", help="run the stdio MCP server")

    p_affected = sub.add_parser("affected", help="map files to affected translation units")
    p_affected.add_argument("paths", nargs="+")

    args = parser.parse_args(argv)

    if args.command == "mcp":
        build_mcp_server().run()
        return 0
    if args.command == "hook":
        return run_hook()
    if args.command == "install-hook":
        return _emit(install_hook())

    if args.command == "analyze":
        all_files = not args.diff
        targets = resolve_targets(diff=not all_files, staged=True)
        result = analyze(targets["tidy"], jobs=args.jobs)
        _emit(result)
        if args.fail_on_findings and not result["clean"]:
            return 1
        return 0

    if args.command == "fix":
        return _emit(apply_fixes(args.session))

    if args.command == "format":
        all_files = not args.diff
        targets = resolve_targets(diff=not all_files, staged=True)
        result = format_files(targets["format"], check=args.check, jobs=args.jobs)
        _emit(result)
        return 0 if result["ok"] else 1

    if args.command == "build":
        return _emit(do_build())

    if args.command == "status":
        return _emit(status())

    if args.command == "affected":
        return _emit(affected(args.paths))

    parser.error(f"unknown command {args.command}")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
