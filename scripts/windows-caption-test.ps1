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
Remove-Item (Join-Path (Split-Path $exe) '.settings.json') -Force -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class CaptionNative {
 [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X,Y; }
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left,Top,Right,Bottom; }
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr w,out RECT r);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr w);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr w);
 [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr w);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr w,int command);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr w);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
 [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr w,uint flags);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr w,IntPtr after,int x,int y,int width,int height,uint flags);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls,string title);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr w,int id);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr w,IntPtr dc,uint flags);
 [DllImport("user32.dll")] public static extern void mouse_event(uint flags,uint dx,uint dy,uint data,UIntPtr extra);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr w,uint message,IntPtr wp,IntPtr lp);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr SendMessage(IntPtr w,uint message,IntPtr wp,IntPtr lp);
 // Find a real non-client button by its Windows hit-test result, avoiding
 // assumptions about caption height, DPI or the active Windows visual style.
 public static void ClickButton(IntPtr w,int hit) {
   SetWindowPos(w,new IntPtr(-1),0,0,0,0,3);
   SetForegroundWindow(w);
   System.Threading.Thread.Sleep(400);
   RECT r; if (!GetWindowRect(w,out r)) throw new Exception("Missing main window");
   int left=int.MaxValue,top=int.MaxValue,right=int.MinValue,bottom=int.MinValue;
   for (int y=r.Top+8; y<Math.Min(r.Top+70,r.Bottom); y+=3)
     for (int x=r.Right-12; x>Math.Max(r.Right-220,r.Left); x-=3) {
       var point=new IntPtr(unchecked((int)(((uint)(ushort)y<<16)|(ushort)x)));
       if (SendMessage(w,0x84,IntPtr.Zero,point).ToInt32()!=hit) continue;
       left=Math.Min(left,x);right=Math.Max(right,x);top=Math.Min(top,y);bottom=Math.Max(bottom,y);
     }
   if (left==int.MaxValue) throw new Exception("Caption hit target not found: "+hit);
   SetCursorPos((left+right)/2,(top+bottom)/2);
   System.Threading.Thread.Sleep(200);
   var p=new POINT { X=(left+right)/2,Y=(top+bottom)/2 };
   if (GetAncestor(WindowFromPoint(p),2)!=w) throw new Exception("Caption is covered by another window");
   var actual=SendMessage(w,0x84,IntPtr.Zero,new IntPtr(unchecked((int)(((uint)(ushort)p.Y<<16)|(ushort)p.X)))).ToInt32();
   if (actual!=hit) throw new Exception("Caption moved during click: expected="+hit+" actual="+actual);
   Console.WriteLine("Physical caption click: hit="+hit+" x="+p.X+" y="+p.Y);
   mouse_event(2,0,0,0,UIntPtr.Zero);
   System.Threading.Thread.Sleep(100);
   mouse_event(4,0,0,0,UIntPtr.Zero);
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
function Wait-Prompt {
    $deadline=[DateTime]::UtcNow.AddSeconds(5)
    do { Start-Sleep -Milliseconds 100; $dialog=[CaptionNative]::FindWindow('#32770','关闭 Reader') } while ($dialog -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($dialog -eq 0) { throw 'Close choice dialog did not open' }
    return $dialog
}
function Choose($dialog,[bool]$tray,[bool]$remember) {
    $id=if ($tray) {1231} else {1230}
    [void][CaptionNative]::PostMessage([CaptionNative]::GetDlgItem($dialog,$id),0xF5,[IntPtr]0,[IntPtr]0)
    if ($remember) { [void][CaptionNative]::PostMessage([CaptionNative]::GetDlgItem($dialog,1232),0xF5,[IntPtr]0,[IntPtr]0) }
    [void][CaptionNative]::PostMessage($dialog,0x111,[IntPtr]1,[IntPtr]0)
}
function Exit-Reader {
    [void][CaptionNative]::PostMessage($script:main,0x111,[IntPtr]106,[IntPtr]0)
    Assert-Closed 'Explicit Exit command'
}
function Assert-Hidden {
    Start-Sleep -Milliseconds 500
    $script:reader.Refresh()
    if ($script:reader.HasExited -or [CaptionNative]::IsWindowVisible($script:main)) { throw 'Tray choice failed to hide the running Reader window' }
    if ([CaptionNative]::FindWindow('#32770','关闭 Reader') -ne 0) { throw 'Remembered tray choice unexpectedly prompted again' }
}
function Save-Prompt($dialog,$name) {
    $rect=New-Object CaptionNative+RECT
    [void][CaptionNative]::GetWindowRect($dialog,[ref]$rect)
    $bitmap=New-Object Drawing.Bitmap ($rect.Right-$rect.Left),($rect.Bottom-$rect.Top)
    $graphics=[Drawing.Graphics]::FromImage($bitmap);$dc=$graphics.GetHdc()
    try { [void][CaptionNative]::PrintWindow($dialog,$dc,2) } finally { $graphics.ReleaseHdc($dc) }
    $bitmap.Save((Join-Path $OutputPath "$name.png"),[Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose();$bitmap.Dispose()
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
        [CaptionNative]::ClickButton($main,9)
        Start-Sleep -Milliseconds 600
        if (![CaptionNative]::IsZoomed($main)) { throw 'Physical maximize click was ignored' }
        [CaptionNative]::ClickButton($main,9)
        Start-Sleep -Milliseconds 500
        if ([CaptionNative]::IsZoomed($main)) { throw 'Physical restore click was ignored' }
        Exit-Reader
        Start-Reader
        [CaptionNative]::ClickButton($main,8)
        $deadline=[DateTime]::UtcNow.AddSeconds(3)
        while (![CaptionNative]::IsIconic($main) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
        if (![CaptionNative]::IsIconic($main)) { throw 'Physical minimize click was ignored' }
        [void][CaptionNative]::ShowWindow($main,9)
        Start-Sleep -Milliseconds 300
        $log.Add('PASS: physical maximize, restore and minimize caption clicks work.')

        [CaptionNative]::ClickButton($main,20)
        $dialog=Wait-Prompt
        Save-Prompt $dialog 'close-choice-light'
        # Closing the prompt itself is Cancel, never an implicit exit.
        [void][CaptionNative]::PostMessage($dialog,0x10,[IntPtr]0,[IntPtr]0)
        Start-Sleep -Milliseconds 300
        $reader.Refresh()
        if ($reader.HasExited -or ![CaptionNative]::IsWindowVisible($main)) { throw 'Cancel unexpectedly closed Reader' }
        [CaptionNative]::ClickButton($main,20)
        Choose (Wait-Prompt) $false $false
        Assert-Closed 'Exit choice without remembering'

        Start-Reader
        [void][CaptionNative]::PostMessage($main,0x111,[IntPtr]1311,[IntPtr]0)
        Start-Sleep -Milliseconds 300
        [CaptionNative]::ClickButton($main,20)
        $dialog=Wait-Prompt
        Save-Prompt $dialog 'close-choice-dark'
        Choose $dialog $true $false
        Assert-Hidden
        Exit-Reader
        $profile=Join-Path (Split-Path $exe) '.settings.json'
        $settings=Get-Content $profile -Raw|ConvertFrom-Json
        if (!$settings.window.askOnClose) { throw 'One-off tray choice was incorrectly remembered' }
        $log.Add('PASS: Cancel keeps Reader open; one-off exit/tray choices still prompt after restart.')

        Start-Reader
        [CaptionNative]::ClickButton($main,20)
        Choose (Wait-Prompt) $false $true
        Assert-Closed 'Remembered exit choice'
        Start-Reader
        [CaptionNative]::ClickButton($main,20)
        Assert-Closed 'Remembered exit after restart without a prompt'
        $log.Add('PASS: remembered exit survives restart and bypasses the prompt.')

        # Re-enable asking through the real settings dialog, not by editing JSON.
        Start-Reader
        [void][CaptionNative]::PostMessage($main,0x111,[IntPtr]32790,[IntPtr]0)
        $deadline=[DateTime]::UtcNow.AddSeconds(5)
        do { Start-Sleep -Milliseconds 100; $basic=[CaptionNative]::FindWindow('#32770','翻页与阅读行为') } while ($basic -eq 0 -and [DateTime]::UtcNow -lt $deadline)
        if ($basic -eq 0) { throw 'Reading behavior settings did not open' }
        [void][CaptionNative]::PostMessage([CaptionNative]::GetDlgItem($basic,1233),0xF5,[IntPtr]0,[IntPtr]0)
        [void][CaptionNative]::PostMessage($basic,0x111,[IntPtr]1,[IntPtr]0)
        Start-Sleep -Milliseconds 300
        [CaptionNative]::ClickButton($main,20)
        Choose (Wait-Prompt) $true $true
        Assert-Hidden
        Exit-Reader
        Start-Reader
        [CaptionNative]::ClickButton($main,20)
        Assert-Hidden
        Exit-Reader
        $settings=Get-Content $profile -Raw|ConvertFrom-Json
        if ($settings.window.askOnClose -or !$settings.window.minToTray) { throw 'Remembered tray preference did not persist' }
        $log.Add('PASS: settings re-enable asking; remembered tray survives restart; explicit Exit remains available.')

        # Upgrade from an older profile with no askOnClose must prompt by default.
        $settings.window.PSObject.Properties.Remove('askOnClose')
        $settings|ConvertTo-Json -Depth 100|Set-Content $profile -Encoding utf8NoBOM
        Start-Reader
        [void][CaptionNative]::PostMessage($main,0x112,[IntPtr]0xF060,[IntPtr]0)
        $dialog=Wait-Prompt
        [void][CaptionNative]::PostMessage($dialog,0x111,[IntPtr]2,[IntPtr]0)
        Start-Sleep -Milliseconds 200
        Exit-Reader
        $log.Add('PASS: old profiles default to asking; system close command uses the same prompt.')
    }
} catch {
    if ($reader -and !$reader.HasExited) {
        Save-Prompt $main 'caption-failure'
        Write-Host "Failure state: visible=$([CaptionNative]::IsWindowVisible($main)) iconic=$([CaptionNative]::IsIconic($main)) zoomed=$([CaptionNative]::IsZoomed($main))"
    }
    throw
} finally {
    if ($reader -and !$reader.HasExited) {
        $dialog=[CaptionNative]::FindWindow('#32770','关闭 Reader')
        if ($dialog -ne 0) { [void][CaptionNative]::PostMessage($dialog,0x111,[IntPtr]2,[IntPtr]0); Start-Sleep -Milliseconds 150 }
        [void][CaptionNative]::PostMessage($main,0x111,[IntPtr]106,[IntPtr]0)
        if (!$reader.WaitForExit(5000)) { $reader.Kill(); $reader.WaitForExit() }
    }
    $log|Set-Content (Join-Path $OutputPath 'caption-tests.txt') -Encoding utf8
    $log|Write-Output
    Remove-Item $exe -Force -ErrorAction SilentlyContinue
}
