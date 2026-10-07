# clang-tidy applies fixes, then clang-format formats afterward
# Note: clang-tidy's fixes could be wrong, check after they are applied
#
# Usage:
#   scripts\FixAndFormat.ps1          # full pass over src\ (all .cpp/.h)
#   scripts\FixAndFormat.ps1 -Diff    # quick pass over files changed vs HEAD
#
# -Diff also picks up new (untracked) files under src\. New .cpp files that
# are not yet in the build get clang-formatted but skipped by clang-tidy,
# since clang-tidy needs a compile_commands.json entry to analyze them.

param(
    [switch]$Diff
)

$BuildPath = (Resolve-Path build\).Path

# clang-tidy cannot consume the MSYS Makefiles compile database as-is: it
# contains POSIX-style /C/... paths and @response files (includes_*.rsp).
# Materialize a clang-tidy-friendly copy with native paths and expanded
# response files, then point -p at it.
$TidyDbPath = Join-Path $BuildPath '.fixandformat'
New-Item -ItemType Directory -Force -Path $TidyDbPath | Out-Null
$db = Get-Content (Join-Path $BuildPath 'compile_commands.json') -Raw | ConvertFrom-Json
foreach ($entry in $db) {
    if (-not $entry.command) { continue }
    $cmd = $entry.command
    $cmd = [regex]::Replace($cmd, '(?<=^|[\s"''])/([A-Za-z])/', { param($m) $m.Groups[1].Value + ':/' })
    $cmd = [regex]::Replace($cmd, '@([^\s]+\.rsp)', {
            param($m)
            (Get-Content (Join-Path $entry.directory $m.Groups[1].Value) -Raw).Trim() + ' '
        })
    $entry.command = $cmd
}
$db | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $TidyDbPath 'compile_commands.json') -Encoding utf8

# Pick the files to process: everything under src\ unless -Diff is given,
# in which case only files changed vs HEAD plus new untracked files.
if ($Diff) {
    $ChangedRel = @(git diff --name-only HEAD -- src)
    $ChangedRel += @(git ls-files --others --exclude-standard -- src)
    $ChangedRel = $ChangedRel |
    Where-Object { $_ -match '\.(cpp|h)$' -and (Test-Path $_) } |
    Sort-Object -Unique

    if ($ChangedRel.Count -eq 0) {
        Write-Host "No changed files under src\. Nothing to do." -ForegroundColor DarkYellow
        exit 0
    }

    $TidyTargets = $ChangedRel |
    Where-Object { $_ -like '*.cpp' } |
    Where-Object {
        # Only files clang-tidy can analyze (present in the compile db).
        $rel = $_ -replace '\\', '/'
        @($db | Where-Object { ($_.file -replace '\\', '/') -like "*/$rel" }).Count -gt 0
    } |
    ForEach-Object { (Resolve-Path $_).Path }

    $FormatTargets = $ChangedRel | ForEach-Object { (Resolve-Path $_).Path }
    Write-Host "Diff mode: $($ChangedRel.Count) changed file(s) - $($TidyTargets.Count) clang-tidy target(s)" -ForegroundColor Cyan
}
else {
    $TidyTargets = Get-ChildItem -Path src\ -Recurse -Include *.cpp | ForEach-Object FullName
    $FormatTargets = Get-ChildItem -Path src\ -Recurse -Include *.cpp, *.h | ForEach-Object FullName
}

# Run clang-tidy. Note: must be serial, NOT parallel. Because -header-filter
# also targets headers, a parallel run makes several clang-tidy processes apply
# fixes to the same shared header concurrently, which corrupts it (e.g.
# `-> bool -> bool`, `[[nod->booliscard]]`). One process at a time is slower
# but safe.
Write-Host "Running clang-tidy..." -ForegroundColor Green

# clang-tidy's output is mostly bookkeeping noise: suppressed-warning counts,
# -header-filter advice, and the "N warnings treated as errors" tally (the
# warnings themselves already print as `error:` lines, so the tally is
# redundant). Drop those, keep real diagnostics plus their source context and
# any applied fixes. `2>&1` captures stderr, where clang-tidy writes.
#
# Headers get re-diagnosed once per translation unit that includes them, so
# identical diagnostics are deduplicated: first occurrence prints (with its
# source context), later repeats are dropped entirely.
$TidyNoise = '^(\d+(,\d+)* (warnings|errors) generated\.|Suppressed \d+(,\d+)* warnings \(|Use -header-filter=|\d+(,\d+)* warnings? treated as errors?\.?)'
# Capture path, line, column and message separately so the dedup key can be
# built from a normalized path: clang-tidy spells the same header's path
# differently per TU (e.g. .../wm/../core/../window/window.h vs
# .../workspace/../window/window.h), which would otherwise defeat dedup.
$DiagHeader = '^(.*?\.(?:cpp|h)):(\d+):(\d+): (error|warning|note|remark): (.*)$'
$SeenDiag = @{}
$NumTargets = $TidyTargets.Length
$NumProcessed = 1
foreach ($file in $TidyTargets) {
    Write-Host "[$NumProcessed/$NumTargets] " -NoNewLine
    Write-Host "$file" -ForegroundColor Cyan
    $NumProcessed++
    $inBlock = $false
    & clang-tidy -p $TidyDbPath --fix-notes -header-filter='.*[/\\]BFWM[/\\](src|include)[/\\].*' $file 2>&1 |
    ForEach-Object { "$_" } |
    ForEach-Object {
        $line = $_
        # Count noise -> drop.
        if ($line -match $TidyNoise) { return }
        # Genuine per-run summary / hard failures -> always show.
        if ($line -match 'clang-tidy applied \d+ of \d+ suggested fixes\.|^Found compiler errors|^error:') {
            Write-Host "      $line"
            return
        }
        # Diagnostic header: dedupe across TUs by normalized path.
        if ($line -match $DiagHeader) {
            $normPath = [System.IO.Path]::GetFullPath($matches[1])
            $key = "$normPath`:$($matches[2]):$($matches[3]): $($matches[5])"
            if (-not $SeenDiag.ContainsKey($key)) {
                $SeenDiag[$key] = $true
                $inBlock = $true
                Write-Host "      $line"
            }
            else {
                $inBlock = $false
            }
            return
        }
        # Source-context / caret lines belong to the current diagnostic.
        if ($inBlock) { Write-Host "      $line" }
    }
}

# Format in parallel (each file touched by exactly one process - safe)
Write-Host "Running clang-format..." -ForegroundColor Green
$FormatTargets | ForEach-Object -Parallel {
    clang-format -i $_
} -ThrottleLimit 8

Write-Host "Done!" -ForegroundColor Green
Write-Host "Remember to double-check output" -ForegroundColor DarkYellow
