# ProfileWorkload.ps1 - reproducible key-spam workload for profiling BFWM.
#
# Sends synthetic keystrokes that exercise the WM's hot paths: focus switching
# (the border-overlay stress case), layout cycles, gap toggles, and config
# reloads. Designed to be safe on a live desktop: it never kills windows and
# never moves them.
#
# Usage:
#   powershell -File ProfileWorkload.ps1 [-DurationSec 60] [-BaseDelayMs 120] [-WindowChurn 0]
#
# NOTE: UIPI blocks BFWM's medium-integrity keyboard hook from seeing
# input destined for HIGHER-integrity windows (e.g. an elevated console). Run
# this from a normal, non-elevated shell - or ensure the elevated capture
# console is minimized (ProfileCapture.ps1 does this automatically) - so the
# synthetic keys reach a normal window and BFWM's hook sees them.

param(
    [int]$DurationSec = 60,
    [int]$BaseDelayMs = 120,
    [int]$WindowChurn = 0
)

$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class NativeKey {
    [DllImport("user32.dll")]
    public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);

    public const uint KEYEVENTF_KEYUP = 0x0002;

    // VK codes used by the workload
    public const byte VK_MENU   = 0x12; // Alt
    public const byte VK_SHIFT  = 0x10;
    public const byte VK_H      = 0x48;
    public const byte VK_J      = 0x4A;
    public const byte VK_K      = 0x4B;
    public const byte VK_L      = 0x4C;
    public const byte VK_D      = 0x44;
    public const byte VK_C      = 0x43;
    public const byte VK_R      = 0x52;
    public const byte VK_P      = 0x50;

    public static void Tap(byte vk) {
        keybd_event(vk, 0, 0, UIntPtr.Zero);
        keybd_event(vk, 0, KEYEVENTF_KEYUP, UIntPtr.Zero);
    }

    public static void Chord(byte[] mods, byte vk) {
        foreach (byte m in mods) keybd_event(m, 0, 0, UIntPtr.Zero);
        keybd_event(vk, 0, 0, UIntPtr.Zero);
        keybd_event(vk, 0, KEYEVENTF_KEYUP, UIntPtr.Zero);
        foreach (byte m in mods) keybd_event(m, 0, KEYEVENTF_KEYUP, UIntPtr.Zero);
    }
}
'@

$alt      = @([NativeKey]::VK_MENU)
$altShift = @([NativeKey]::VK_MENU, [NativeKey]::VK_SHIFT)
$focusKeys = @([NativeKey]::VK_H, [NativeKey]::VK_J, [NativeKey]::VK_K, [NativeKey]::VK_L)

$rng = [System.Random]::new()
$deadline = (Get-Date).AddSeconds($DurationSec)

# Optional churn: spawn a few notepads so the WM registers windows mid-capture.
$churnPids = @()
for ($i = 0; $i -lt $WindowChurn; $i++) {
    $p = Start-Process notepad -PassThru
    $churnPids += $p.Id
}

$ops = @{
    focus  = 0
    layout = 0
    gaps   = 0
    reload = 0
    split  = 0
}

while ((Get-Date) -lt $deadline) {
    $roll = $rng.Next(100)
    if ($roll -lt 70) {
        # Focus switch in a random direction - the border overlay hot path.
        $key = $focusKeys[$rng.Next($focusKeys.Count)]
        [NativeKey]::Chord($alt, $key)
        $ops.focus++
    } elseif ($roll -lt 80) {
        # Cycle layout (Alt+D) / previous layout (Alt+Shift+D)
        if ($rng.Next(2) -eq 0) { [NativeKey]::Chord($altShift, [NativeKey]::VK_D) }
        else { [NativeKey]::Chord($alt, [NativeKey]::VK_D) }
        $ops.layout++
    } elseif ($roll -lt 90) {
        # Toggle gaps
        [NativeKey]::Chord($alt, [NativeKey]::VK_C)
        $ops.gaps++
    } elseif ($roll -lt 95) {
        # Reload config (Alt+Shift+R)
        [NativeKey]::Chord($altShift, [NativeKey]::VK_R)
        $ops.reload++
    } else {
        # Swap/split (Alt+P / Alt+Shift+P)
        if ($rng.Next(2) -eq 0) { [NativeKey]::Chord($altShift, [NativeKey]::VK_P) }
        else { [NativeKey]::Chord($alt, [NativeKey]::VK_P) }
        $ops.split++
    }

    Start-Sleep -Milliseconds ([int]($BaseDelayMs * (0.5 + $rng.NextDouble())))
}

foreach ($pid2 in $churnPids) { Stop-Process -Id $pid2 -Force -ErrorAction SilentlyContinue }

Write-Host ("Workload done: focus={0} layout={1} gaps={2} reload={3} split={4} (dur={5}s)" -f `
    $ops.focus, $ops.layout, $ops.gaps, $ops.reload, $ops.split, $DurationSec)
