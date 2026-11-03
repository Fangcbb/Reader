param([Parameter(Mandatory)][string]$ExePath, [Parameter(Mandatory)][string]$OutputPath)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path $ExePath).Path
$profilePath = Join-Path (Split-Path $exe) '.settings.json'
New-Item -ItemType Directory -Force $OutputPath | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class UiThemeNative {
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left,Top,Right,Bottom; }
 [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X,Y; }
 public delegate bool EnumCallback(IntPtr window, IntPtr data);
 [DllImport("user32.dll")] static extern bool EnumWindows(EnumCallback callback, IntPtr data);
 [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr window,uint kind);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,StringBuilder name,int size);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls,string title);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string cls,string title);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window,int id);
 [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window,uint msg,IntPtr wp,IntPtr lp);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")] static extern IntPtr SendText(IntPtr w,uint m,IntPtr wp,string text);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")] static extern IntPtr ReadText(IntPtr w,uint m,IntPtr wp,StringBuilder text);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window,out RECT r);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window,out RECT r);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr window,ref POINT p);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window,IntPtr dc,uint flags);
 [DllImport("user32.dll",EntryPoint="SendMessageW")] public static extern IntPtr Number(IntPtr w,uint m,IntPtr wp,IntPtr lp);
 public static string Text(IntPtr w) { var s=new StringBuilder(256); ReadText(w,13,(IntPtr)256,s);return s.ToString(); }
 public static void SetText(IntPtr w,string text) { SendText(w,12,IntPtr.Zero,text); }
 public static IntPtr OwnedDialog(IntPtr owner) { IntPtr result=IntPtr.Zero; EnumWindows((w,d)=>{ var cls=new StringBuilder(32);GetClassName(w,cls,32);if(GetWindow(w,4)==owner && cls.ToString()=="#32770"){result=w;return false;}return true;},IntPtr.Zero);return result; }
}
'@
$definitions = Get-Content 'Reader/platform/resource.h' -Raw
function Id($name) { return [int]([regex]::Match($definitions,"(?m)^#define\s+"+[regex]::Escape($name)+"\s+(\d+)").Groups[1].Value) }
function Command($window,$id) { [void][UiThemeNative]::PostMessage($window,0x111,[IntPtr]$id,[IntPtr]0); Start-Sleep -Milliseconds 200 }
function Wait-Closed($window) {
    $deadline=[DateTime]::UtcNow.AddSeconds(5)
    while ([UiThemeNative]::IsWindow($window) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
    if ([UiThemeNative]::IsWindow($window)) { throw 'Dialog did not close' }
}
function Open-Theme {
    Command $script:main (Id 'IDM_UI_THEME_SETTINGS')
    $deadline=[DateTime]::UtcNow.AddSeconds(5)
    do { Start-Sleep -Milliseconds 100; $window=[UiThemeNative]::FindWindow('#32770','软件界面主题与颜色') } while ($window -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($window -eq 0) { throw 'UI theme settings did not open' }
    return $window
}
function Choose-Color($window,$button,[int]$r,[int]$g,[int]$b) {
    Command $window $button
    $deadline=[DateTime]::UtcNow.AddSeconds(5)
    do { Start-Sleep -Milliseconds 100; $chooser=[UiThemeNative]::OwnedDialog($window) } while ($chooser -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($chooser -eq 0) { throw 'Native color chooser did not open' }
    foreach ($pair in @(@(706,$r),@(707,$g),@(708,$b))) {
        $edit=[UiThemeNative]::GetDlgItem($chooser,$pair[0]); if ($edit -eq 0) { throw 'RGB input not found' }
        [UiThemeNative]::SetText($edit,[string]$pair[1])
    }
    Command $chooser 1; Wait-Closed $chooser
    $expected = '#{0:X2}{1:X2}{2:X2}' -f $r,$g,$b
    $caption=[UiThemeNative]::Text([UiThemeNative]::GetDlgItem($window,$button))
    if ($caption -notlike "*$expected*") { throw "Custom UI color mismatch: $caption expected $expected" }
}
function Save-Capture($window,$name) {
    $rect=New-Object UiThemeNative+RECT; [void][UiThemeNative]::GetWindowRect($window,[ref]$rect)
    $bitmap=New-Object Drawing.Bitmap ($rect.Right-$rect.Left),($rect.Bottom-$rect.Top)
    $graphics=[Drawing.Graphics]::FromImage($bitmap);$dc=$graphics.GetHdc()
    try { [void][UiThemeNative]::PrintWindow($window,$dc,2) } finally { $graphics.ReleaseHdc($dc) }
    $path=Join-Path $OutputPath "$name.png";$bitmap.Save($path,[Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose();$bitmap.Dispose()
}
function Start-Reader {
    $script:reader=Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru
    $deadline=[DateTime]::UtcNow.AddSeconds(20)
    do { Start-Sleep -Milliseconds 100;$script:reader.Refresh() } while ($script:reader.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($script:reader.MainWindowHandle -eq 0) { throw 'Reader not started' }
    $script:main=$script:reader.MainWindowHandle
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    do { Start-Sleep -Milliseconds 100;$script:reader.Refresh() } while ($script:reader.MainWindowTitle -notlike '*第一章*' -and [DateTime]::UtcNow -lt $deadline)
    if ($script:reader.MainWindowTitle -notlike '*第一章*') { throw 'Stored EPUB reading position did not reopen' }
}
function Stop-Reader {
    [void][UiThemeNative]::PostMessage($script:main,0x111,[IntPtr]106,[IntPtr]0)
    if (!$script:reader.WaitForExit(5000)) { throw 'Reader did not exit cleanly' }
}
$baseline = Get-Content $profilePath -Raw | ConvertFrom-Json
$reading = @{}
foreach ($name in 'background','styles','layout') { $reading[$name]=$baseline.$name | ConvertTo-Json -Depth 30 -Compress }
$log=New-Object 'Collections.Generic.List[string]'
try {
    Start-Reader
    $client=New-Object UiThemeNative+RECT;[void][UiThemeNative]::GetClientRect($script:main,[ref]$client)
    $origin=New-Object UiThemeNative+POINT;[void][UiThemeNative]::ClientToScreen($script:main,[ref]$origin)
    $windowRect=New-Object UiThemeNative+RECT;[void][UiThemeNative]::GetWindowRect($script:main,[ref]$windowRect)
    # Saved compact layout is below the docking threshold, so this crop covers
    # reading content, not the separate shell or sidebar.
    $crop=@(($origin.X-$windowRect.Left+40),($origin.Y-$windowRect.Top+110),($client.Right-80),($client.Bottom-200))
    $crop | ConvertTo-Json | Set-Content (Join-Path $OutputPath 'ui-theme-reading-crop.json')
    Save-Capture $script:main 'ui-theme-light'
    $header=[UiThemeNative]::FindWindowEx($script:main,[IntPtr]0,'STATIC','Reader 工具栏')
    if ([UiThemeNative]::GetDlgItem($header,(Id 'IDM_UI_THEME')) -eq 0) { throw 'Right-hand theme icon missing' }
    Command $script:main (Id 'IDM_UI_THEME')
    Save-Capture $script:main 'ui-theme-dark'
    $log.Add('PASS: right-hand theme command switched the shell to dark mode.')

    $dialog=Open-Theme
    Choose-Color $dialog (Id 'IDC_UI_ACCENT') 200 90 50
    Command $dialog 2;Wait-Closed $dialog
    $dialog=Open-Theme
    $caption=[UiThemeNative]::Text([UiThemeNative]::GetDlgItem($dialog,(Id 'IDC_UI_ACCENT')))
    if ($caption -notlike '*#6556D5*') { throw 'Cancelled custom color leaked into saved UI theme' }
    Choose-Color $dialog (Id 'IDC_UI_ACCENT') 36 156 160
    Choose-Color $dialog (Id 'IDC_UI_LIGHT_BG') 233 241 245
    Choose-Color $dialog (Id 'IDC_UI_DARK_BG') 12 18 28
    Save-Capture $dialog 'ui-theme-settings-dark'
    Command $dialog 1;Wait-Closed $dialog
    Save-Capture $script:main 'ui-theme-dark-custom'
    Command $script:main (Id 'IDM_UI_THEME')
    Save-Capture $script:main 'ui-theme-light-custom'
    Command $script:main (Id 'IDM_UI_THEME')
    $log.Add('PASS: custom accent/light/dark colors saved; Cancel discarded unsaved changes.')
    Command $script:main (Id 'IDM_READING_PRESETS')
    $deadline=[DateTime]::UtcNow.AddSeconds(5)
    do { Start-Sleep -Milliseconds 100; $presets=[UiThemeNative]::FindWindow('#32770','阅读预设与场景') } while ($presets -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($presets -eq 0) { throw 'Reading presets did not open' }
    [UiThemeNative]::SetText([UiThemeNative]::GetDlgItem($presets,(Id 'IDC_PRESETS_NAME')),'论文排版')
    Command $presets (Id 'IDC_PRESETS_SAVE')
    $list=[UiThemeNative]::GetDlgItem($presets,(Id 'IDC_PRESETS_LIST'))
    if ([UiThemeNative]::Number($list,0x18B,[IntPtr]0,[IntPtr]0).ToInt32() -ne 1) { throw 'Reading preset was not saved' }
    Save-Capture $presets 'reading-presets'
    Command $presets 1; Wait-Closed $presets
    $log.Add('PASS: named reading preset saved and applied through the actual dialog.')
    Stop-Reader
    $savedConfiguration=Get-Content $profilePath -Raw|ConvertFrom-Json
    if (!$savedConfiguration.uiTheme.darkMode -or $savedConfiguration.uiTheme.accent -ne (36+156*256+160*65536) -or $savedConfiguration.uiTheme.darkBackground -ne (12+18*256+28*65536)) { throw 'UI theme configuration was not persisted' }
    foreach ($name in 'background','styles','layout') {
        if (($savedConfiguration.$name|ConvertTo-Json -Depth 30 -Compress) -ne $reading[$name]) { throw "UI theme changed reading $name" }
    }
    if ($savedConfiguration.readingPresets.Count -ne 1 -or $savedConfiguration.readingPresets[0].name -ne '论文排版') { throw 'Reading preset was not persisted' }
    $log.Add('PASS: reading background, text styles and layout are unchanged in the saved profile.')
    # Change the active reading style while keeping the stored preset intact.
    # Applying it must restore actual values, not merely close the dialog.
    $savedConfiguration.background.color = 1971210
    $savedConfiguration.styles.textFont.lfHeight = -22
    $savedConfiguration.styles.textColor = 15790320
    $savedConfiguration.layout.lineGap = 21
    $savedConfiguration | ConvertTo-Json -Depth 50 | Set-Content $profilePath -Encoding utf8 -Force
    Start-Reader
    Command $script:main (Id 'IDM_READING_PRESETS')
    $deadline=[DateTime]::UtcNow.AddSeconds(5)
    do { Start-Sleep -Milliseconds 100; $presets=[UiThemeNative]::FindWindow('#32770','阅读预设与场景') } while ($presets -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($presets -eq 0) { throw 'Persisted preset manager did not reopen' }
    $list=[UiThemeNative]::GetDlgItem($presets,(Id 'IDC_PRESETS_LIST'))
    [void][UiThemeNative]::Number($list,0x186,[IntPtr]0,[IntPtr]0)
    Command $presets ((Id 'IDC_PRESETS_LIST') -bor 65536)
    Command $presets 1; Wait-Closed $presets
    Start-Sleep -Milliseconds 300
    Save-Capture $script:main 'ui-theme-dark-restarted'
    $dialog=Open-Theme
    if ([UiThemeNative]::Text([UiThemeNative]::GetDlgItem($dialog,(Id 'IDC_UI_DARK_BG'))) -notlike '*#0C121C*') { throw 'Custom dark color did not survive restart' }
    Command $dialog 2;Wait-Closed $dialog
    $log.Add('PASS: dark mode and custom colors survived restart.')
    Stop-Reader
    $restoredReading=Get-Content $profilePath -Raw|ConvertFrom-Json
    foreach ($name in 'background','styles','layout') {
        if (($restoredReading.$name|ConvertTo-Json -Depth 30 -Compress) -ne $reading[$name]) { throw "Preset failed to restore changed reading $name" }
    }
    $log.Add('PASS: persisted preset restored changed font size, text color, background and line spacing after restart.')
    $log | Set-Content (Join-Path $OutputPath 'ui-theme-tests.txt') -Encoding utf8
    $log | Write-Output
} catch {
    Write-Host $_.ScriptStackTrace
    throw
} finally {
    if ($script:reader -and !$script:reader.HasExited) { [void][UiThemeNative]::PostMessage($script:main,0x111,[IntPtr]106,[IntPtr]0);if (!$script:reader.WaitForExit(5000)) { $script:reader.Kill() } }
}
python scripts/verify-ui-theme-captures.py $OutputPath
if ($LASTEXITCODE -ne 0) { throw 'Independent shell-theme pixel verification failed' }
