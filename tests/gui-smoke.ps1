param([switch]$Live)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $projectRoot 'dist\Interval.exe'
$artifactDir = Join-Path $projectRoot 'build\qa'
New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class IntervalQA {
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int left, top, right, bottom; }
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] public struct LOGFONT {
  public int height,width,escapement,orientation,weight;
  public byte italic,underline,strikeout,charSet,outPrecision,clipPrecision,quality,pitchAndFamily;
  [MarshalAs(UnmanagedType.ByValTStr,SizeConst=32)] public string faceName;
 }
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls,string title);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern bool SetWindowText(IntPtr h,string text);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")] public static extern IntPtr SetTextMessage(IntPtr h,uint m,IntPtr w,string text);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")] public static extern IntPtr GetTextMessage(IntPtr h,uint m,IntPtr w,StringBuilder text);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out RECT rect);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 public delegate bool WindowCallback(IntPtr hwnd,IntPtr data);
 [DllImport("user32.dll")] public static extern bool EnumWindows(WindowCallback callback,IntPtr data);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd,StringBuilder name,int count);
 public static IntPtr ProcessWindow(int processId,string cls) {
  IntPtr found=IntPtr.Zero;
  EnumWindows((h,d)=>{ uint pid; GetWindowThreadProcessId(h,out pid); var name=new StringBuilder(128); GetClassName(h,name,name.Capacity); if(pid==processId&&name.ToString()==cls){found=h;return false;}return true; },IntPtr.Zero);
  return found;
 }
 [DllImport("gdi32.dll",CharSet=CharSet.Unicode)] public static extern int GetObject(IntPtr h,int count,out LOGFONT font);
 public static LOGFONT Font(IntPtr hwnd) {
  var handle=SendMessage(hwnd,0x31,IntPtr.Zero,IntPtr.Zero);
  LOGFONT font; GetObject(handle,Marshal.SizeOf(typeof(LOGFONT)),out font); return font;
 }
 public static void Click(IntPtr parent,int id) { SendMessage(parent,0x111,new IntPtr(id),GetDlgItem(parent,id)); }
 public static void Command(IntPtr owner,int id) { SendMessage(owner,0x111,new IntPtr(id),IntPtr.Zero); }
}
'@
function Wait-For([scriptblock]$Condition,[int]$Timeout=10000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($Timeout)
    do { if (& $Condition) { return }; Start-Sleep -Milliseconds 100 } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Timed out waiting for a UI state.'
}
function Assert([bool]$Condition,[string]$Message) { if (!$Condition) { throw $Message }; Write-Output "PASS: $Message" }
function Capture([IntPtr]$Window,[string]$Name) {
    $rect = New-Object IntervalQA+RECT
    [void][IntervalQA]::GetWindowRect($Window,[ref]$rect)
    $bitmap = New-Object Drawing.Bitmap ($rect.right-$rect.left),($rect.bottom-$rect.top)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $dc = $graphics.GetHdc()
    try { if (![IntervalQA]::PrintWindow($Window,$dc,0)) { throw 'Native window capture failed.' } }
    finally { $graphics.ReleaseHdc($dc); $graphics.Dispose() }
    $path = Join-Path $artifactDir $Name
    try { $bitmap.Save($path,[Drawing.Imaging.ImageFormat]::Png) } finally { $bitmap.Dispose() }
    Write-Output "Capture: $path"
}
$existing=[IntervalQA]::FindWindow('Interval.Owner','Interval.Background.Test')
if ($existing -ne [IntPtr]::Zero) { throw 'Close the existing Interval test instance before running the smoke test.' }
$statusPath = Join-Path $artifactDir 'status.json'
$arguments = '--test-mode --background --test-seconds 2 --diagnostics "'+$statusPath+'"'
if (!$Live) { $arguments += ' --offline' }
$process = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru
function State { try { $s=Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json; if ($s.pid -eq $process.Id) { $s } else { $null } } catch { $null } }
try {
    Wait-For { $script:owner=[IntervalQA]::FindWindow('Interval.Owner','Interval.Background.Test'); $owner -ne [IntPtr]::Zero }
    Wait-For { (State).breaking }
    Wait-For { $script:screen=[IntervalQA]::FindWindow('Interval.Screen','인터벌 · 잠시 쉬어가세요'); $screen -ne [IntPtr]::Zero -and [IntervalQA]::GetDlgItem($screen,101) -ne [IntPtr]::Zero }
    Assert ($screen -ne [IntPtr]::Zero) 'Automatic full-screen break appears at the deadline.'
    Assert ((State).intervalMinutes -eq 50) 'Default interval is 50 minutes.'
    if ($Live) { Wait-For { $s=State; $s.dollar -gt 0 -and $s.won -gt 0 -and $s.dollarPoints -gt 150 -and $s.wonPoints -gt 150 } 45000 }
    Capture $screen 'break-light.png'
    [IntervalQA]::Click($screen,102)
    Assert ((State).theme -eq 2) 'Break screen switches to dark mode.'
    Capture $screen 'break-dark.png'
    [IntervalQA]::Click($screen,100)
    Assert (!(State).breaking -and (State).remainingMs -ge 1500) 'Restart begins a fresh full interval (allowing native window animation time).'
    Start-Sleep -Milliseconds 400
    Assert (!(State).breaking) 'Break does not reappear prematurely.'
    Wait-For { (State).breaking } 5000
    Assert ((State).breaking) 'Restarted schedule produces its next break.'
    Wait-For { $script:screen=[IntervalQA]::FindWindow('Interval.Screen','인터벌 · 잠시 쉬어가세요'); $screen -ne [IntPtr]::Zero -and [IntervalQA]::GetDlgItem($screen,101) -ne [IntPtr]::Zero }
    [IntervalQA]::Click($screen,101)
    Assert (!(State).breaking -and (State).remainingMs -ge 299500) 'Snooze schedules five minutes (allowing native window animation time).'
    [void][IntervalQA]::SendMessage($owner,32771,[IntPtr]::Zero,[IntPtr]::Zero)
    $settings=[IntervalQA]::ProcessWindow($process.Id,'Interval.Settings')
    Assert ($settings -ne [IntPtr]::Zero) 'Settings opens from the resident app.'
    $edit=[IntervalQA]::GetDlgItem($settings,200)
    $font=[IntervalQA]::Font($edit)
    Assert ([Math]::Abs($font.height) -ge 26 -and $font.faceName -match '어그로|Aggro') 'Interval digits use the embedded Aggro font at a readable size.'
    [void][IntervalQA]::SetTextMessage($edit,12,[IntPtr]::Zero,'0'); [IntervalQA]::Click($settings,201)
    Assert ((State).intervalMinutes -eq 50) 'Invalid interval does not change the schedule.'
    [void][IntervalQA]::SetTextMessage($edit,12,[IntPtr]::Zero,'60')
    $editText=New-Object Text.StringBuilder 32; [void][IntervalQA]::GetTextMessage($edit,13,[IntPtr]32,$editText)
    Assert ($editText.ToString() -eq '60') 'Interval field receives the new number.'
    [IntervalQA]::Click($settings,201)
    Assert ((State).intervalMinutes -eq 60) 'A valid interval setting is applied.'
    [IntervalQA]::Command($owner,303)
    Assert ((State).paused -and (State).holding) 'Pausing holds the deadline.'
    Start-Sleep -Milliseconds 2300
    Assert (!(State).breaking) 'Paused app does not open a break at the old deadline.'
    [IntervalQA]::Click($settings,203); Capture $settings 'settings-light.png'
    [IntervalQA]::Click($settings,204); Capture $settings 'settings-dark.png'
    $duplicate=Start-Process -FilePath $exe -ArgumentList '--test-mode --background' -PassThru
    Assert ($duplicate.WaitForExit(4000)) 'Duplicate launch reuses the existing app.'
    [IntervalQA]::Click($settings,208)
    $process.Refresh()
    Write-Output ('Resident memory: working set {0:N1} MiB; private memory {1:N1} MiB; handles {2}' -f ($process.WorkingSet64/1MB),($process.PrivateMemorySize64/1MB),$process.HandleCount)
    [IntervalQA]::Command($owner,304)
    Assert ($process.WaitForExit(4000)) 'Tray exit cleans up the app promptly.'
} finally {
    if (!$process.HasExited) { [IntervalQA]::Command($owner,304); [void]$process.WaitForExit(4000) }
}
