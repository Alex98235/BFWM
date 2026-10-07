# build_installer.ps1
#
# Compiles the BFWM installer with Inno Setup 6+ (iscc).
# Requires the built executable (build\BFWM.exe) and the icon
# (icons\BFWM.ico, produced by scripts\build_icon.ps1).
# The version is read from the VERSION file at the repo root and passed to
# iscc as /DMyAppVersion=<version>.
#
# Usage:
#   scripts\build_installer.ps1
#   scripts\build_installer.ps1 -Iscc "C:\Program Files\Inno Setup 7\ISCC.exe"
#
# iscc is auto-detected from the standard install locations, then PATH.
# Output: installer\output\BFWM-Setup-<version>.exe

param(
    [string]$Iscc = ''
)

$ErrorActionPreference = 'Stop'

# Repo root: this script lives in scripts\, so the root is its parent.
$RepoRoot = Split-Path -Parent $PSScriptRoot

# 1. Verify prerequisites ------------------------------------------------------

$ExePath = Join-Path $RepoRoot 'build\BFWM.exe'
if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
    throw "Build output not found: $ExePath - run the CMake build first."
}

$IconPath = Join-Path $RepoRoot 'icons\BFWM.ico'
if (-not (Test-Path -LiteralPath $IconPath -PathType Leaf)) {
    throw "Icon not found: $IconPath - run scripts\build_icon.ps1 first."
}

# 2. Locate iscc ----------------------------------------------------------------

# Auto-detect: standard install locations first, then PATH. Override with -Iscc.
$IsccCandidates = @(
    'C:\Program Files\Inno Setup 7\ISCC.exe',
    'C:\Program Files (x86)\Inno Setup 7\ISCC.exe',
    'C:\Program Files\Inno Setup 6\ISCC.exe',
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
)

if ([string]::IsNullOrWhiteSpace($Iscc)) {
    $Iscc = $IsccCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}

if (-not [string]::IsNullOrWhiteSpace($Iscc) -and -not (Test-Path -LiteralPath $Iscc -PathType Leaf)) {
    # Not a file path - try resolving as a command on PATH.
    $cmd = Get-Command $Iscc -ErrorAction SilentlyContinue
    if ($cmd) { $Iscc = $cmd.Source }
}

if ([string]::IsNullOrWhiteSpace($Iscc) -or -not (Test-Path -LiteralPath $Iscc -PathType Leaf)) {
    throw "iscc not found. Install Inno Setup 6+ (winget install --id JRSoftware.InnoSetup.7) or pass -Iscc <path>."
}

# 3. Read the version from the VERSION file (single source of truth) ------------

$VersionPath = Join-Path $RepoRoot 'VERSION'
$Version = (Get-Content -LiteralPath $VersionPath -Raw).Trim()

# 4. Compile the installer ------------------------------------------------------

$IssPath = Join-Path $RepoRoot 'installer\BFWM.iss'
Write-Host "Compiling installer from $IssPath (version $Version)" -ForegroundColor Green
& $Iscc "/DMyAppVersion=$Version" $IssPath
if ($LASTEXITCODE -ne 0) {
    throw "iscc failed with exit code $LASTEXITCODE"
}

# 5. Report output ---------------------------------------------------------------

$OutDir = Join-Path $RepoRoot 'installer\output'
Write-Host "Installer written to $OutDir (BFWM-Setup-$Version.exe)" -ForegroundColor Green
