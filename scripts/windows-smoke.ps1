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
 [StructLayout(LayoutKind.Sequential)] public struct GUITHREADINFO { public uint cbSize, flags; public IntPtr active, focus, capture, menu, moveSize, caret; public RECT caretRect; }
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint process);
 [DllImport("user32.dll")] public static extern bool GetGUIThreadInfo(uint thread, ref GUITHREADINFO info);
 [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string caption);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string caption);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hwnd, int id);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int w, int h, uint flags);
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
    $main = $process.MainWindowHandle
    Start-Sleep -Milliseconds 300
    Save-Window $main 'welcome'
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
    $definitions = Get-Content 'Reader/platform/resource.h' -Raw
    function Resource-Id($name) {
        $match = [regex]::Match($definitions, "(?m)^#define\s+" + [regex]::Escape($name) + "\s+(\d+)")
        if (!$match.Success) { throw "Missing resource ID: $name" }
        return [int]$match.Groups[1].Value
    }
    foreach ($dialog in @(@('IDM_DISPLAY', '阅读外观与排版', 'appearance'), @('IDM_CONFIG', '翻页与阅读行为', 'behavior'))) {
        if ($dialog[0] -eq 'IDM_DISPLAY') {
            [uint32]$owner = 0
            $thread = [ReaderSmokeNative]::GetWindowThreadProcessId($main, [ref]$owner)
            $info = New-Object ReaderSmokeNative+GUITHREADINFO
            $info.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($info)
            $found = $false
            for ($i = 0; $i -lt 12; $i++) {
                [void][ReaderSmokeNative]::PostMessage($main, 0x100, [IntPtr]9, [IntPtr]0)
                Start-Sleep -Milliseconds 80
                if (![ReaderSmokeNative]::GetGUIThreadInfo($thread, [ref]$info)) { throw 'Unable to inspect keyboard focus' }
                if ([ReaderSmokeNative]::GetDlgCtrlID($info.focus) -eq (Resource-Id 'IDM_DISPLAY')) { $found = $true; break }
            }
            if (!$found) { throw 'Tab navigation did not reach Appearance' }
            [void][ReaderSmokeNative]::PostMessage($info.focus, 0x100, [IntPtr]13, [IntPtr]0)
        } else {
            [void][ReaderSmokeNative]::PostMessage($main, 0x111, [IntPtr](Resource-Id $dialog[0]), [IntPtr]0)
        }
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        do { Start-Sleep -Milliseconds 200; $handle = [ReaderSmokeNative]::FindWindow('#32770', $dialog[1]) } while ($handle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
        if ($handle -eq 0) { throw "Settings dialog did not open: $($dialog[0])" }
        Start-Sleep -Milliseconds 250
        Save-Window $handle $dialog[2]
        [void][ReaderSmokeNative]::PostMessage($handle, 0x111, [IntPtr]2, [IntPtr]0)
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        while ([ReaderSmokeNative]::IsWindow($handle) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
        if ([ReaderSmokeNative]::IsWindow($handle)) { throw 'Settings Cancel did not close the dialog' }
    }
    $toolbar = [ReaderSmokeNative]::FindWindowEx($main, [IntPtr]0, 'STATIC', 'Reader 工具栏')
    $appearanceButton = [ReaderSmokeNative]::GetDlgItem($toolbar, (Resource-Id 'IDM_DISPLAY'))
    foreach ($reading in @(@(0x25, '*封面*'), @(0x27, '*第一章*'))) {
        [void][ReaderSmokeNative]::PostMessage($appearanceButton, 0x100, [IntPtr]$reading[0], [IntPtr]0)
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do { Start-Sleep -Milliseconds 100; $process.Refresh() } while ($process.MainWindowTitle -notlike $reading[1] -and [DateTime]::UtcNow -lt $deadline)
        if ($process.MainWindowTitle -notlike $reading[1]) { throw 'Reading arrows stopped working with toolbar focus' }
    }
    [void][ReaderSmokeNative]::SetWindowPos($main, [IntPtr]0, 80, 80, 480, 620, 4)
    Start-Sleep -Milliseconds 300
    Save-Window $main 'compact'
    [void][ReaderSmokeNative]::PostMessage($main, 0x111, [IntPtr](Resource-Id 'IDM_VIEW'), [IntPtr]0)
    Start-Sleep -Milliseconds 250
    Save-Window $main 'compact-contents'
    [void][ReaderSmokeNative]::PostMessage($main, 0x111, [IntPtr](Resource-Id 'IDM_VIEW'), [IntPtr]0)
    Start-Sleep -Milliseconds 150
    $header = [ReaderSmokeNative]::FindWindowEx($main, [IntPtr]0, 'STATIC', 'Reader 工具栏')
    if ($header -eq 0 -or ![ReaderSmokeNative]::IsWindowVisible($header)) { throw 'Modern toolbar is missing' }
    [void][ReaderSmokeNative]::PostMessage($main, 0x111, [IntPtr](Resource-Id 'IDM_UI_FOCUS'), [IntPtr]0)
    Start-Sleep -Milliseconds 300
    if ([ReaderSmokeNative]::IsWindowVisible($header)) { throw 'Toolbar remains visible in focus mode' }
    Save-Window $main 'focus'
    [void][ReaderSmokeNative]::PostMessage($main, 0x111, [IntPtr](Resource-Id 'IDM_UI_FOCUS'), [IntPtr]0)
    Start-Sleep -Milliseconds 300
    if (![ReaderSmokeNative]::IsWindowVisible($header)) { throw 'Toolbar did not return after focus mode' }
    Save-Window $main 'restored'
    python scripts/verify-ui-captures.py $OutputPath
    if ($LASTEXITCODE -ne 0) { throw 'UI capture verification failed' }
    'Modern dialogs, compact layout and focus-mode transitions passed' | Add-Content (Join-Path $OutputPath 'smoke.txt') -Encoding utf8
} finally {
    if (!$process.HasExited) {
        [void]$process.CloseMainWindow()
        if (!$process.WaitForExit(5000)) { $process.Kill(); $process.WaitForExit() }
    }
}
