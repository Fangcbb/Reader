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
 [StructLayout(LayoutKind.Sequential)] public struct TITLEBARINFOEX {
   public uint size; public RECT title;
   [MarshalAs(UnmanagedType.ByValArray,SizeConst=6)] public uint[] states;
   [MarshalAs(UnmanagedType.ByValArray,SizeConst=6)] public RECT[] buttons;
 }
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
 // Ask Windows for the actual accessible caption-button rectangles. Synthetic
 // WM_NCHITTEST messages may use different classic/DWM button metrics.
 public static void ClickButton(IntPtr w,int hit) {
   SetWindowPos(w,new IntPtr(-1),0,0,0,0,3);
   SetForegroundWindow(w);
   System.Threading.Thread.Sleep(250);
   var title=new TITLEBARINFOEX { states=new uint[6],buttons=new RECT[6] };
   title.size=(uint)Marshal.SizeOf(typeof(TITLEBARINFOEX));
   IntPtr memory=Marshal.AllocHGlobal((int)title.size);
   try {
     Marshal.StructureToPtr(title,memory,false);
     SendMessage(w,0x33F,IntPtr.Zero,memory);
     title=(TITLEBARINFOEX)Marshal.PtrToStructure(memory,typeof(TITLEBARINFOEX));
   } finally { Marshal.FreeHGlobal(memory); }
   int index=hit==20?5:hit==9?3:2;
   RECT button=title.buttons[index];
   if (button.Right<=button.Left || button.Bottom<=button.Top || (title.states[index]&0x8000)!=0)
     throw new Exception("Windows did not expose caption button "+hit);
   var point=new POINT { X=(button.Left+button.Right)/2,Y=(button.Top+button.Bottom)/2 };
   if (GetAncestor(WindowFromPoint(point),2)!=w) throw new Exception("Caption is covered by another window");
   if (!SetCursorPos(point.X,point.Y)) throw new Exception("Desktop cursor is unavailable");
   System.Threading.Thread.Sleep(100);
   Console.WriteLine("Physical caption click: hit="+hit+" x="+point.X+" y="+point.Y);
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
