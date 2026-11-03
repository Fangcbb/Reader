param([Parameter(Mandatory)][string]$ExePath, [Parameter(Mandatory)][string]$BookPath, [Parameter(Mandatory)][string]$OutputPath)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path $ExePath).Path
$book = (Resolve-Path $BookPath).Path
New-Item -ItemType Directory -Force $OutputPath | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class ReaderSmokeNative {
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
}
'@
function Save-Window($handle, $name) {
    $rect = New-Object ReaderSmokeNative+RECT
    if (![ReaderSmokeNative]::GetWindowRect($handle, [ref]$rect)) { return }
    $bitmap = New-Object Drawing.Bitmap ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $dc = $graphics.GetHdc()
    try { $printed = [ReaderSmokeNative]::PrintWindow($handle, $dc, 2) } finally { $graphics.ReleaseHdc($dc) }
    if ($printed) { $bitmap.Save((Join-Path $OutputPath "$name.png"), [Drawing.Imaging.ImageFormat]::Png) }
    $graphics.Dispose()
    $bitmap.Dispose()
}
$process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 250
        $process.Refresh()
        if ($process.HasExited) { throw "Reader exited during startup: $($process.ExitCode)" }
    } while ($process.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($process.MainWindowHandle -eq 0) { throw 'Reader main window was not created' }
    # Existing SingleApp protocol forwards the second process's filename via WM_COPYDATA.
    $opener = Start-Process -FilePath $exe -ArgumentList "`"$book`"" -WorkingDirectory (Split-Path $exe) -PassThru
    if (!$opener.WaitForExit(15000)) { $opener.Kill(); throw 'Open-book forwarding timed out' }
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 250
        $process.Refresh()
        if ($process.HasExited) { throw 'Reader exited while opening EPUB' }
    } while ($process.MainWindowTitle -notlike '*EPUB fixture*封面*' -and [DateTime]::UtcNow -lt $deadline)
    if ($process.MainWindowTitle -notlike '*EPUB fixture*封面*') { throw "EPUB did not become ready: $($process.MainWindowTitle)" }
    "EPUB opened: $($process.MainWindowTitle)" | Set-Content (Join-Path $OutputPath 'smoke.txt') -Encoding utf8
    Save-Window $process.MainWindowHandle 'epub-cover'
    [void][ReaderSmokeNative]::PostMessage($process.MainWindowHandle, 0x100, [IntPtr]0x27, [IntPtr]0)
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do { Start-Sleep -Milliseconds 250; $process.Refresh() } while ($process.MainWindowTitle -notlike '*第一章*' -and [DateTime]::UtcNow -lt $deadline)
    if ($process.MainWindowTitle -notlike '*第一章*') { throw "EPUB page turn failed: $($process.MainWindowTitle)" }
    "Page turned: $($process.MainWindowTitle)" | Add-Content (Join-Path $OutputPath 'smoke.txt') -Encoding utf8
    Save-Window $process.MainWindowHandle 'epub-text'
} finally {
    if (!$process.HasExited) {
        [void]$process.CloseMainWindow()
        if (!$process.WaitForExit(5000)) { $process.Kill(); $process.WaitForExit() }
    }
}
