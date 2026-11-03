param([Parameter(Mandatory)][string]$ExePath, [Parameter(Mandatory)][string]$OutputPath)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path $ExePath).Path
New-Item -ItemType Directory -Force $OutputPath | Out-Null
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$references = @([Windows.Forms.Form].Assembly.Location, [Windows.Forms.CreateParams].Assembly.Location, [Drawing.Color].Assembly.Location, [ComponentModel.Component].Assembly.Location, 'System.Runtime.dll') | Select-Object -Unique
Add-Type -ReferencedAssemblies $references -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Windows.Forms;
public class PickerColorSource : Form {
 protected override bool ShowWithoutActivation { get { return true; } }
 protected override CreateParams CreateParams { get { var p=base.CreateParams; p.ExStyle |= 0x08000080; return p; } }
}
public class PickerTestNative {
 [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
 [StructLayout(LayoutKind.Sequential)] public struct GUI { public uint cbSize, flags; public IntPtr active, focus, capture, menu, move, caret; public RECT caretRect; }
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool ScreenToClient(IntPtr h, ref POINT p);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int height, uint flags);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetGUIThreadInfo(uint thread, ref GUI info);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint process);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
 [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr wp, StringBuilder text);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
 public static string ReadText(IntPtr h) { var s=new StringBuilder(256); SendMessageW(h, 13, (IntPtr)256, s); return s.ToString(); }
 public static IntPtr Position(int x,int y) { return (IntPtr)((y & 65535) << 16 | (x & 65535)); }
}
'@
$definitions = Get-Content 'Reader/platform/resource.h' -Raw
function Resource-Id($name) {
    $match = [regex]::Match($definitions, "(?m)^#define\s+" + [regex]::Escape($name) + "\s+(\d+)")
    if (!$match.Success) { throw "Missing ID: $name" }
    return [int]$match.Groups[1].Value
}
function Pause-Ui([int]$milliseconds = 100) { [Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds $milliseconds }
function Capture-Owner {
    $info = New-Object PickerTestNative+GUI
    $info.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($info)
    if (![PickerTestNative]::GetGUIThreadInfo($script:uiThread, [ref]$info)) { throw 'Unable to query Reader capture' }
    return $info.capture
}
function Point-Parameter([int]$x, [int]$y) {
    $point = New-Object PickerTestNative+POINT; $point.X = $x; $point.Y = $y
    [void][PickerTestNative]::ScreenToClient($script:dialog, [ref]$point)
    return [PickerTestNative]::Position($point.X, $point.Y)
}
function Move-Screen([int]$x, [int]$y) {
    if (![PickerTestNative]::SetCursorPos($x, $y)) { throw 'Desktop cursor is unavailable' }
    Pause-Ui
}
function Wait-Color($expected) {
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do { Pause-Ui; $actual = [PickerTestNative]::ReadText($script:rgb) } while ($actual -ne $expected -and [DateTime]::UtcNow -lt $deadline)
    if ($actual -ne $expected) { throw "Screen color mismatch: expected $expected, got $actual" }
}
function Begin-Pick {
    $rect = New-Object PickerTestNative+RECT
    [void][PickerTestNative]::GetWindowRect($script:picker, [ref]$rect)
    $script:pickX = [int](($rect.Left + $rect.Right) / 2)
    $script:pickY = [int](($rect.Top + $rect.Bottom) / 2)
    [void][PickerTestNative]::SetForegroundWindow($script:dialog)
    Move-Screen $script:pickX $script:pickY
    [void][PickerTestNative]::PostMessage($script:dialog, 0x201, [IntPtr]1, (Point-Parameter $script:pickX $script:pickY))
    $deadline = [DateTime]::UtcNow.AddSeconds(3)
    do { Pause-Ui } while ((Capture-Owner) -ne $script:dialog -and [DateTime]::UtcNow -lt $deadline)
    if ((Capture-Owner) -ne $script:dialog) { throw 'Eyedropper did not capture the mouse' }
}
function End-Pick([int]$x, [int]$y) {
    [void][PickerTestNative]::PostMessage($script:dialog, 0x202, [IntPtr]0, (Point-Parameter $x $y))
    Pause-Ui
    if ((Capture-Owner) -eq $script:dialog) { throw 'Eyedropper did not release capture' }
}
function Save-Window($window, $name) {
    $rect = New-Object PickerTestNative+RECT
    [void][PickerTestNative]::GetWindowRect($window, [ref]$rect)
    $bitmap = New-Object Drawing.Bitmap ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top)
    $graphics = [Drawing.Graphics]::FromImage($bitmap); $dc = $graphics.GetHdc()
    try { [void][PickerTestNative]::PrintWindow($window, $dc, 2) } finally { $graphics.ReleaseHdc($dc) }
    $bitmap.Save((Join-Path $OutputPath "$name.png"), [Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
}
function Open-Appearance {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do { Pause-Ui; $script:reader.Refresh() } while ($script:reader.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($script:reader.MainWindowHandle -eq 0) { throw 'Reader window not created' }
    [void][PickerTestNative]::PostMessage($script:reader.MainWindowHandle, 0x111, [IntPtr](Resource-Id 'IDM_DISPLAY'), [IntPtr]0)
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do { Pause-Ui; $script:dialog = [PickerTestNative]::FindWindow('#32770', '阅读外观与排版') } while ($script:dialog -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($script:dialog -eq 0) { throw 'Appearance dialog not created' }
    [void][PickerTestNative]::SetWindowPos($script:dialog, [IntPtr]0, 300, 180, 0, 0, 5)
    [uint32]$owner = 0
    $script:uiThread = [PickerTestNative]::GetWindowThreadProcessId($script:dialog, [ref]$owner)
    $script:picker = [PickerTestNative]::GetDlgItem($script:dialog, (Resource-Id 'IDC_STATIC_PICKER'))
    $script:rgb = [PickerTestNative]::GetDlgItem($script:dialog, (Resource-Id 'IDC_STATIC_RGB'))
    Pause-Ui
}
function Wait-Closed {
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ([PickerTestNative]::IsWindow($script:dialog) -and [DateTime]::UtcNow -lt $deadline) { Pause-Ui }
    if ([PickerTestNative]::IsWindow($script:dialog)) { throw 'Appearance dialog did not close' }
}
$script:reader = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru
$source = New-Object PickerColorSource
$log = New-Object 'Collections.Generic.List[string]'
try {
    Open-Appearance
    $original = [PickerTestNative]::ReadText($script:rgb)
    $source.FormBorderStyle = [Windows.Forms.FormBorderStyle]::None
    $source.StartPosition = [Windows.Forms.FormStartPosition]::Manual
    $source.Location = New-Object Drawing.Point 20,20
    $source.Size = New-Object Drawing.Size 180,120
    $source.TopMost = $true
    $source.BackColor = [Drawing.Color]::FromArgb(18,171,205)
    $source.Show(); $source.Refresh(); Pause-Ui 250

    # This target is outside Reader and above/left of the settings window:
    # captured mouse messages therefore have NEGATIVE client coordinates.
    Begin-Pick
    Move-Screen 80 80
    Wait-Color '#12ABCD'
    End-Pick 80 80
    $source.BackColor = [Drawing.Color]::FromArgb(226,125,96)
    $source.Refresh(); Pause-Ui 200
    Wait-Color '#12ABCD' # No continued sampling after release.
    $log.Add('PASS: drag sampled #12ABCD from an external PowerShell-owned window at negative client coordinates; release stopped sampling.')

    # A simple click also arms the picker, rather than ending it immediately.
    Begin-Pick
    [void][PickerTestNative]::PostMessage($script:dialog, 0x202, [IntPtr]0, (Point-Parameter $script:pickX $script:pickY))
    Pause-Ui
    if ((Capture-Owner) -ne $script:dialog) { throw 'Click-to-pick did not remain armed' }
    Move-Screen 80 80
    Wait-Color '#E27D60'
    [void][PickerTestNative]::PostMessage($script:dialog, 0x201, [IntPtr]1, (Point-Parameter 80 80))
    End-Pick 80 80
    $log.Add('PASS: click-to-arm and external click selection sampled #E27D60.')
    Save-Window $script:dialog 'color-picker-picked'
    Save-Window $source.Handle 'color-picker-external-source'

    $source.BackColor = [Drawing.Color]::FromArgb(141,92,246)
    $source.Refresh(); Pause-Ui
    foreach ($cancel in @(@(0x100,27,'Escape'), @(0x204,0,'right click'), @(0x1F,0,'cancel mode'), @(0x215,0,'capture loss'))) {
        Begin-Pick
        Move-Screen 80 80
        Wait-Color '#8D5CF6'
        [void][PickerTestNative]::PostMessage($script:dialog, [uint32]$cancel[0], [IntPtr]$cancel[1], [IntPtr]0)
        Wait-Color '#E27D60'
        if ((Capture-Owner) -eq $script:dialog) { throw "Capture not released after $($cancel[2])" }
        if (![PickerTestNative]::IsWindow($script:dialog)) { throw 'Cancelling the picker closed settings' }
        $log.Add("PASS: $($cancel[2]) restored the previous color and released capture.")
    }

    # Exercise an external target on the other side of the dialog too.
    $screen = [Windows.Forms.Screen]::PrimaryScreen.Bounds
    $source.Location = New-Object Drawing.Point ($screen.Right - 200),($screen.Bottom - 150)
    $source.Refresh(); Pause-Ui
    $x = $screen.Right - 140; $y = $screen.Bottom - 90
    Begin-Pick
    Move-Screen $x $y
    Wait-Color '#8D5CF6'
    End-Pick $x $y
    $log.Add('PASS: external lower/right screen target sampled #8D5CF6.')

    Begin-Pick
    Move-Screen $x $y
    [void][PickerTestNative]::PostMessage($script:dialog, 0x10, [IntPtr]0, [IntPtr]0)
    Wait-Closed
    if ((Capture-Owner) -eq $script:dialog) { throw 'Closing settings left capture active' }
    Open-Appearance
    Wait-Color $original
    $log.Add('PASS: closing settings while picking released capture and did not save cancelled colors.')

    # Explicit Save must persist the picked color across a process restart.
    Begin-Pick
    Move-Screen $x $y
    Wait-Color '#8D5CF6'
    End-Pick $x $y
    [void][PickerTestNative]::PostMessage($script:dialog, 0x111, [IntPtr]1, [IntPtr]0)
    Wait-Closed
    [void]$script:reader.CloseMainWindow()
    if (!$script:reader.WaitForExit(5000)) { throw 'Reader did not exit cleanly for persistence check' }
    $script:reader = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru
    Open-Appearance
    Wait-Color '#8D5CF6'
    $log.Add('PASS: saved screen color survived a clean Reader restart.')
    [void][PickerTestNative]::PostMessage($script:dialog, 0x111, [IntPtr]2, [IntPtr]0)
    Wait-Closed
    $log | Set-Content (Join-Path $OutputPath 'color-picker-tests.txt') -Encoding utf8
    $log | Write-Output
} finally {
    $source.Dispose()
    if (!$script:reader.HasExited) {
        [void]$script:reader.CloseMainWindow()
        if (!$script:reader.WaitForExit(5000)) { $script:reader.Kill(); $script:reader.WaitForExit() }
    }
}
