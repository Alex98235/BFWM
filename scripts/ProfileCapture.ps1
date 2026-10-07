# ProfileCapture.ps1 - elevated PerfView capture of a scripted BFWM workload.
#
# Replaces the old wpr/wpaexporter pipeline with a single PerfView command.
# PerfView is a self-contained exe (no .wprp XML, no profile files, no headless
# exporter quirks) and its GUI does all analysis: CPU flame graphs, thread-time
# wait analysis, automatic symbol resolution from the PDB next to the exe.
#
# Usage (from a normal shell):
#   powershell -File ProfileCapture.ps1
#
# Prompts for: capture name [idle], duration in seconds [60], window churn [0].
# Self-elevates (kernel ETW needs admin), starts PerfView in the background,
# minimizes the elevated console so the synthetic workload keys reach BFWM
# (UIPI: a low-level keyboard hook at medium integrity cannot see keys destined
# for a higher-integrity window - with the console minimized, focus returns to a
# normal window and the hook sees everything), runs the workload, waits for
# PerfView's MaxCollectSec auto-stop, restores the console, and reports.
#
# Output: <captures>\<name>.etl.zip  (PerfView zips + merges by default)
# Analysis: PerfView.exe open <file>  -> CPU Stacks / Thread Time views.

param(
    [string]$Name = "",
    [int]$DurationSec = 0,
    [int]$WindowChurn = -1
)

$ErrorActionPreference = 'Stop'

$CaptureDir = "C:\Users\alind\Programming\BFWM\profiling\captures"
$PerfView = "C:\Users\alind\AppData\Local\PerfView\PerfView.exe"

# -- Interactive prompts (skipped when values come from params/env) ------------
function Get-Value {
    param([string]$Label, [string]$Default)
    $envVal = $env:BFWM_PROFILE_VALUE
    if ($envVal -ne $null -and $envVal -ne "") { return $envVal }
    $input = Read-Host "$Label [$Default]"
    if ($input -eq "") { return $Default }
    return $input
}

$Name = Get-Value "Capture name" "idle"
$DurationSec = [int](Get-Value "Duration (seconds)" "60")
$WindowChurn = [int](Get-Value "Window churn (notepads)" "0")

# -- Self-elevate --------------------------------------------------------------
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "Not elevated - relaunching as Administrator..."
    # Pass values via env vars: immune to argument-quoting mangling.
    $env:BFWM_PROFILE_NAME = $Name
    $env:BFWM_PROFILE_DURATION = "$DurationSec"
    $env:BFWM_PROFILE_CHURN = "$WindowChurn"
    $p = Start-Process powershell -Verb RunAs -ArgumentList "-NoExit -NoProfile -File `"$PSCommandPath`"" -PassThru
    if (-not $p) { throw "Elevation cancelled or failed." }
    exit
}

# Elevated path: values arrived via env vars (or params).
if ($env:BFWM_PROFILE_NAME)    { $Name = $env:BFWM_PROFILE_NAME }
if ($env:BFWM_PROFILE_DURATION) { $DurationSec = [int]$env:BFWM_PROFILE_DURATION }
if ($env:BFWM_PROFILE_CHURN)   { $WindowChurn = [int]$env:BFWM_PROFILE_CHURN }

# -- Console minimize/restore (UIPI fix) --------------------------------------
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Win32NativeConsole {
    [DllImport("kernel32.dll")]
    public static extern IntPtr GetConsoleWindow();
    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    public const int SW_MINIMIZE = 6;
    public const int SW_RESTORE = 9;
}
'@
$consoleHwnd = [Win32NativeConsole]::GetConsoleWindow()
function Minimize-Console { if ($consoleHwnd -ne [IntPtr]::Zero) { [Win32NativeConsole]::ShowWindow($consoleHwnd, [Win32NativeConsole]::SW_MINIMIZE) | Out-Null } }
function Restore-Console  { if ($consoleHwnd -ne [IntPtr]::Zero) { [Win32NativeConsole]::ShowWindow($consoleHwnd, [Win32NativeConsole]::SW_RESTORE) | Out-Null } }

# -- Preflight -----------------------------------------------------------------
if (-not (Test-Path $PerfView)) { throw "PerfView not found at $PerfView" }
if (-not (Get-Process BFWM -ErrorAction SilentlyContinue)) {
    Write-Warning "BFWM.exe is not running - the trace will show no BFWM activity."
}
New-Item -ItemType Directory -Force -Path $CaptureDir | Out-Null
$etlBase = Join-Path $CaptureDir $Name
$logFile = "$etlBase.perfview.log"
Get-ChildItem "$etlBase*" -ErrorAction SilentlyContinue | Remove-Item -Force

# -- Capture -------------------------------------------------------------------
Write-Host "Starting PerfView capture (${DurationSec}s + 10s rundown margin)..."
$proc = Start-Process -FilePath $PerfView -PassThru -WindowStyle Hidden -ArgumentList @(
    # /ThreadTime alone expands to Default | ContextSwitch | Dispatcher
    # (CPU sampling + CSwitch + ReadyThread + ImageLoad/Process/Thread) -
    # everything the flame graph and wait analysis need. Do NOT add
    # /KernelEvents:Default here; it can clobber ThreadTime's CSwitch/Dispatcher.
    "/NoGui", "/NoView", "/ThreadTime",
    "/BufferSizeMB:1024", "/MaxCollectSec:$($DurationSec + 10)",
    "/LogFile:$logFile",
    "collect", $etlBase
)

try {
    Start-Sleep -Seconds 2
    Write-Host "Console minimized - workload keys now reach BFWM."
    Minimize-Console

    Write-Host "Running workload..."
    $workload = Join-Path $PSScriptRoot "ProfileWorkload.ps1"
    & powershell -NoProfile -File $workload -DurationSec $DurationSec -WindowChurn $WindowChurn
} finally {
    Write-Host "Waiting for PerfView to finish (MaxCollectSec + merge)..."
    Wait-Process -Id $proc.Id -ErrorAction SilentlyContinue
    Restore-Console
}

# -- Report --------------------------------------------------------------------
$out = Get-ChildItem "$etlBase*" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notlike "*.log" } | Select-Object -First 1
if ($out) {
    $sizeMb = [math]::Round($out.Length / 1MB, 1)
    Write-Host "Capture saved: $($out.FullName) ($sizeMb MB)"
    Write-Host "Analyze: & `"$PerfView`" open `"$($out.FullName)`""
    Write-Host "  -> CPU Stacks view = flame graph; Thread Time view = wait analysis."
    Write-Host "  -> Symbols auto-resolve from build-profile\BFWM.pdb (next to the exe)."
} else {
    Write-Warning "No output file found. PerfView log: $logFile"
    if (Test-Path $logFile) { Get-Content $logFile -Tail 30 }
}
