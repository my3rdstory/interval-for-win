param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$artifactDir = Join-Path $projectRoot 'build\qa'
New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class IntervalTaskbarQA {
 public delegate bool WindowCallback(IntPtr hwnd,IntPtr data);
 [DllImport("user32.dll")] public static extern bool EnumWindows(WindowCallback callback,IntPtr data);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd,StringBuilder name,int count);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd,StringBuilder name,int count);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls,string title);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")] public static extern IntPtr SetTextMessage(IntPtr h,uint m,IntPtr w,string text);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 public static IntPtr ProcessWindow(int processId,string cls) {
  IntPtr found=IntPtr.Zero;
  EnumWindows((h,d)=>{uint pid; GetWindowThreadProcessId(h,out pid);var name=new StringBuilder(128);GetClassName(h,name,name.Capacity);if(pid==processId&&name.ToString()==cls){found=h;return false;}return true;},IntPtr.Zero);
  return found;
 }
 public static string Title(IntPtr h) {var s=new StringBuilder(256);GetWindowText(h,s,s.Capacity);return s.ToString();}
 public static void Command(IntPtr h,int id) {SendMessage(h,0x111,new IntPtr(id),IntPtr.Zero);}
}
'@
function Assert([bool]$Condition,[string]$Message) {
    if (!$Condition) { throw $Message }; Write-Output "PASS: $Message"
}
function Wait-For([scriptblock]$Condition,[int]$Timeout=10000) {
    $deadline=[DateTime]::UtcNow.AddMilliseconds($Timeout)
    do { if (& $Condition) { return }; Start-Sleep -Milliseconds 100 } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Timed out waiting for taskbar state.'
}
function Capture-Icon([IntPtr]$Window,[string]$Name) {
    $handle=[IntervalTaskbarQA]::SendMessage($Window,0x7F,[IntPtr]1,[IntPtr]::Zero)
    Assert ($handle -ne [IntPtr]::Zero) 'Countdown window exposes an actual icon.'
    $bitmap=[Drawing.Icon]::FromHandle($handle).ToBitmap()
    try { $bitmap.Save((Join-Path $artifactDir $Name),[Drawing.Imaging.ImageFormat]::Png) }
    finally { $bitmap.Dispose() }
}
if ([IntervalTaskbarQA]::FindWindow('Interval.Owner','Interval.Background.Test') -ne [IntPtr]::Zero) { throw 'Close the existing test instance first.' }
$statusPath=Join-Path $artifactDir 'taskbar-status.json'
$exe=Join-Path $projectRoot 'dist\Interval.exe'
$arguments='--test-mode --background --offline --test-seconds 310 --diagnostics "'+$statusPath+'"'
$process=Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -WindowStyle Hidden
function State {
    try { $s=Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json; if ($s.pid -eq $process.Id) { $s } } catch { $null }
}
try {
    Wait-For { $script:owner=[IntervalTaskbarQA]::ProcessWindow($process.Id,'Interval.Owner'); $owner -ne [IntPtr]::Zero -and (State).taskbarVisible }
    $settings=[IntervalTaskbarQA]::ProcessWindow($process.Id,'Interval.Settings')
    Assert ([IntervalTaskbarQA]::IsWindowVisible($settings) -and [IntervalTaskbarQA]::IsIconic($settings)) 'Background launch keeps a minimized taskbar button without opening settings.'
    Assert ((State).taskbarMinutes -eq 6 -and !(State).taskbarWarning) 'Minute count rounds up and does not warn before five minutes.'
    Assert ([IntervalTaskbarQA]::Title($settings) -match '\d{2}:\d{2} 남음') 'Window title contains the precise countdown.'
    Wait-For { (State).taskbarProgressApplied }
    Assert ((State).taskbarProgressApplied) 'Explorer accepts native taskbar progress calls.'
    Capture-Icon $settings 'taskbar-6.png'
    [IntervalTaskbarQA]::Command($owner,209)
    Assert ((State).taskbarMode -eq 1 -and ![IntervalTaskbarQA]::IsWindowVisible($settings)) 'Five-minute-only mode hides the button before the boundary.'
    Wait-For { (State).taskbarWarning -and [IntervalTaskbarQA]::IsWindowVisible($settings) } 14000
    Assert ([IntervalTaskbarQA]::IsIconic($settings)) 'At five minutes only the minimized button appears.'
    # Other applications may legitimately change focus while this test waits.
    [uint32]$foregroundPid=0
    [void][IntervalTaskbarQA]::GetWindowThreadProcessId([IntervalTaskbarQA]::GetForegroundWindow(),[ref]$foregroundPid)
    Assert ($foregroundPid -ne $process.Id) 'The five-minute notification does not activate an Interval window.'
    Assert ((State).taskbarMinutes -eq 5 -and [IntervalTaskbarQA]::Title($settings) -match '곧 휴식') 'Last five minutes show both the number and warning text.'
    Capture-Icon $settings 'taskbar-5-warning.png'
    [IntervalTaskbarQA]::Command($owner,209)
    Assert ((State).taskbarMode -eq 2 -and ![IntervalTaskbarQA]::IsWindowVisible($settings)) 'Disabled mode removes the minimized taskbar button.'
    [IntervalTaskbarQA]::Command($owner,209)
    Wait-For { (State).taskbarMode -eq 0 -and [IntervalTaskbarQA]::IsWindowVisible($settings) }
    Assert ((State).taskbarMode -eq 0 -and [IntervalTaskbarQA]::IsWindowVisible($settings)) 'Always mode restores the taskbar button.'
    [IntervalTaskbarQA]::Command($owner,303)
    Wait-For { (State).paused }
    Assert ((State).paused -and !(State).taskbarWarning -and [IntervalTaskbarQA]::Title($settings) -match '일시 정지') 'Pause clears the imminent-break warning and updates the title.'
    Capture-Icon $settings 'taskbar-paused.png'
    $held=(State).remainingMs
    Start-Sleep -Milliseconds 1300
    Assert ((State).remainingMs -eq $held) 'Paused countdown remains unchanged.'
    [IntervalTaskbarQA]::Command($owner,303)
    Wait-For { $s=State; $null -ne $s -and !$s.paused -and $s.taskbarWarning }
    Assert (!(State).paused -and (State).taskbarWarning) 'Resume restores the five-minute warning.'
    [IntervalTaskbarQA]::Command($owner,302)
    Wait-For { (State).taskbarMinutes -eq 6 }
    Assert ((State).remainingMs -ge 309000 -and (State).taskbarMinutes -eq 6 -and !(State).taskbarWarning) 'Restart resets the number and warning using the fresh interval.'
    [IntervalTaskbarQA]::Command($owner,300)
    [void][IntervalTaskbarQA]::SendMessage($settings,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
    Assert ([IntervalTaskbarQA]::IsIconic($settings) -and [IntervalTaskbarQA]::IsWindowVisible($settings)) 'Closing settings keeps the running timer on the taskbar.'
    $process.Refresh()
    Write-Output ('Resident memory: working set {0:N1} MiB; private {1:N1} MiB; handles {2}' -f ($process.WorkingSet64/1MB),($process.PrivateMemorySize64/1MB),$process.HandleCount)
    [IntervalTaskbarQA]::Command($owner,304)
    Assert ($process.WaitForExit(4000)) 'Exit removes all application windows.'

    $arguments='--test-mode --background --offline --diagnostics "'+$statusPath+'"'
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -WindowStyle Hidden
    Wait-For { $script:owner=[IntervalTaskbarQA]::ProcessWindow($process.Id,'Interval.Owner'); $owner -ne [IntPtr]::Zero -and (State).taskbarMinutes -eq 50 }
    $settings=[IntervalTaskbarQA]::ProcessWindow($process.Id,'Interval.Settings')
    Capture-Icon $settings 'taskbar-50.png'
    Assert ((State).taskbarMinutes -eq 50) 'Default two-digit icon shows fifty minutes.'
    [IntervalTaskbarQA]::Command($owner,300)
    $edit=[IntervalTaskbarQA]::GetDlgItem($settings,200)
    [void][IntervalTaskbarQA]::SetTextMessage($edit,12,[IntPtr]::Zero,'240')
    [IntervalTaskbarQA]::Command($settings,201)
    Wait-For { (State).taskbarMinutes -eq 240 }
    Capture-Icon $settings 'taskbar-240.png'
    Assert ((State).intervalMinutes -eq 240 -and (State).taskbarMinutes -eq 240) 'Maximum three-digit interval reaches the taskbar icon.'
    [IntervalTaskbarQA]::Command($owner,304)
    Assert ($process.WaitForExit(4000)) 'Icon verification instance exits cleanly.'
} finally {
    if (!$process.HasExited) { [IntervalTaskbarQA]::Command($owner,304); [void]$process.WaitForExit(4000) }
}
