param(
    [Parameter(Mandatory)][string]$ExePath,
    [Parameter(Mandatory)][string]$OutputPath,
    [switch]$ExpectBlocked
)
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path $ExePath).Path
# Isolate settings from other smoke checks and from the published baseline.
New-Item -ItemType Directory -Force $OutputPath | Out-Null
$exe = Join-Path (Resolve-Path $OutputPath).Path 'Reader.exe'
Copy-Item $source $exe -Force
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class CaptionNative {
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left,Top,Right,Bottom; }
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr w,out RECT r);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr w);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr w);
 [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr w);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr w,int command);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr w);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint flags,uint dx,uint dy,uint data,UIntPtr extra);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr w,uint message,IntPtr wp,IntPtr lp);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr SendMessage(IntPtr w,uint message,IntPtr wp,IntPtr lp);
 // Find a real non-client button by its Windows hit-test result, avoiding
 // assumptions about caption height, DPI or the active Windows visual style.
 public static void ClickButton(IntPtr w,int hit) {
   RECT r; if (!GetWindowRect(w,out r)) throw new Exception("Missing main window");
   for (int y=r.Top+8; y<Math.Min(r.Top+70,r.Bottom); y+=3)
     for (int x=r.Right-12; x>Math.Max(r.Right-220,r.Left); x-=3) {
       var point=new IntPtr(unchecked((int)(((uint)(ushort)y<<16)|(ushort)x)));
       if (SendMessage(w,0x84,IntPtr.Zero,point).ToInt32()!=hit) continue;
       SetForegroundWindow(w); SetCursorPos(x,y);
       mouse_event(2,0,0,0,UIntPtr.Zero);
       System.Threading.Thread.Sleep(80);
       mouse_event(4,0,0,0,UIntPtr.Zero);
       return;
     }
   throw new Exception("Caption hit target not found: "+hit);
 }
}
'@
$log = New-Object 'Collections.Generic.List[string]'
$reader = $null
function Start-Reader {
    $script:reader = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 100
        $script:reader.Refresh()
        if ($script:reader.HasExited) { throw 'Reader exited during startup' }
    } while ($script:reader.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($script:reader.MainWindowHandle -eq 0) { throw 'Reader window not created' }
    $script:main = $script:reader.MainWindowHandle
    Start-Sleep -Milliseconds 300
}
function Assert-Closed($reason) {
    if (!$script:reader.WaitForExit(5000)) { throw "$reason did not exit Reader" }
    if ($script:reader.ExitCode -ne 0) { throw "$reason crashed: $($script:reader.ExitCode)" }
    $log.Add("PASS: $reason exited cleanly.")
}
try {
    Start-Reader
    if ($ExpectBlocked) {
        [CaptionNative]::ClickButton($main,20)
        Start-Sleep -Milliseconds 600
        $reader.Refresh()
        if ($reader.HasExited -or ![CaptionNative]::IsWindowVisible($main)) { throw 'Published baseline did not reproduce blocked close click' }
        $log.Add('REPRODUCED: published preview.2 ignores a physical click on HTCLOSE while the main window remains visible.')
    } else {
        [CaptionNative]::ClickButton($main,9) # HTMAXBUTTON
        Start-Sleep -Milliseconds 400
        if (![CaptionNative]::IsZoomed($main)) { throw 'Physical maximize click was ignored' }
        [CaptionNative]::ClickButton($main,9)
        Start-Sleep -Milliseconds 400
        if ([CaptionNative]::IsZoomed($main)) { throw 'Physical restore click was ignored' }
        [CaptionNative]::ClickButton($main,8) # HTMINBUTTON
        Start-Sleep -Milliseconds 400
        if (![CaptionNative]::IsIconic($main)) { throw 'Physical minimize click was ignored' }
        [void][CaptionNative]::ShowWindow($main,9)
        Start-Sleep -Milliseconds 300
        $log.Add('PASS: physical maximize, restore and minimize caption clicks work.')
        [CaptionNative]::ClickButton($main,20) # HTCLOSE
        Assert-Closed 'Light UI close button'

        Start-Reader
        [void][CaptionNative]::PostMessage($main,0x111,[IntPtr]1311,[IntPtr]0)
        Start-Sleep -Milliseconds 300
        [CaptionNative]::ClickButton($main,20)
        Assert-Closed 'Dark UI close button'

        Start-Reader
        [void][CaptionNative]::PostMessage($main,0x112,[IntPtr]0xF060,[IntPtr]0)
        Assert-Closed 'System close command (Alt+F4 path)'

        # Explicit close-to-tray preference still hides instead of exiting;
        # Exit from the menu must remain available in that mode.
        $profile = Join-Path (Split-Path $exe) '.settings.json'
        $settings = Get-Content $profile -Raw | ConvertFrom-Json
        $settings.window.minToTray = $true
        $settings | ConvertTo-Json -Depth 100 | Set-Content $profile -Encoding utf8NoBOM
        Start-Reader
        [CaptionNative]::ClickButton($main,20)
        Start-Sleep -Milliseconds 500
        $reader.Refresh()
        if ($reader.HasExited -or [CaptionNative]::IsWindowVisible($main)) { throw 'Close-to-tray preference was not honored' }
        [void][CaptionNative]::PostMessage($main,0x111,[IntPtr]106,[IntPtr]0)
        Assert-Closed 'Exit command with close-to-tray enabled'
        $log.Add('PASS: close-to-tray hides the window; explicit Exit terminates the process.')
    }
} finally {
    if ($reader -and !$reader.HasExited) {
        [void][CaptionNative]::PostMessage($main,0x111,[IntPtr]106,[IntPtr]0)
        if (!$reader.WaitForExit(5000)) { $reader.Kill(); $reader.WaitForExit() }
    }
    $log | Set-Content (Join-Path $OutputPath 'caption-tests.txt') -Encoding utf8
    $log | Write-Output
    # Test-only settings must never be packaged with Reader.
    Remove-Item $exe -Force -ErrorAction SilentlyContinue
}
