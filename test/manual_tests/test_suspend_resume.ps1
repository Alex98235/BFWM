Add-Type @'
using System;
using System.Runtime.InteropServices;
public class Win32 {
    [DllImport("user32.dll", SetLastError=true)]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
    [DllImport("user32.dll", SetLastError=true, CharSet=CharSet.Auto)]
    public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool PostThreadMessage(uint threadId, uint msg, UIntPtr wParam, IntPtr lParam);
}
'@

# Find BFWM's monitor helper window (class: BFWMMonitorHelper)
$hwnd = [Win32]::FindWindow("BFWMMonitorHelper", $null)
if ($hwnd -eq [IntPtr]::Zero) {
    Write-Host "BFWM does not appear to be running (BFWMMonitorHelper not found)"
    exit 1
}

$procId = 0
$tid = [Win32]::GetWindowThreadProcessId($hwnd, [ref]$procId)
Write-Host "Found BFWM (PID=$pid, main thread=0x$('{0:X}' -f $tid))"

Write-Host "`nSending WM_APP_SUSPEND (0x8003) — bar should freeze..."
[Win32]::PostThreadMessage($tid, 0x8003, [UIntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep 2

Write-Host "Sending WM_APP_RESUME (0x8004) — bar should repaint, COM re-initialized..."
[Win32]::PostThreadMessage($tid, 0x8004, [UIntPtr]::Zero, [IntPtr]::Zero)

Write-Host "`nDone. Check the bar for expected behavior."
