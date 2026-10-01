param([string]$Executable = (Join-Path (Split-Path $PSScriptRoot -Parent) 'dist\Interval.exe'))
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$artifactDir = Join-Path $projectRoot 'build\qa\activity'
New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null
$runId = [Guid]::NewGuid().ToString('N')
$historyPath = Join-Path $artifactDir "$runId.tsv"
$statusPath = Join-Path $artifactDir "$runId.json"
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ActivityQA {
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int left,top,right,bottom; }
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls,string title);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 public static void Command(IntPtr h,int id) { SendMessage(h,0x111,new IntPtr(id),IntPtr.Zero); }
}
'@
function Assert([bool]$condition,[string]$message) { if (!$condition) { throw $message }; Write-Output "PASS: $message" }
function Wait-For([scriptblock]$condition) {
    $deadline=[DateTime]::UtcNow.AddSeconds(10)
    do { if (& $condition) { return }; Start-Sleep -Milliseconds 100 } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Timed out waiting for activity state.'
}
function State { try { $s=Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json; if ($s.pid -eq $script:process.Id) { return $s } } catch {} }
function Records { @(Get-Content -LiteralPath $historyPath | Where-Object { $_ -match '^1\t' } | ForEach-Object { $parts=$_ -split "`t"; [PSCustomObject]@{ Time=[long]$parts[1]; Kind=$parts[2]; State=[int]$parts[3] } }) }
function Launch {
    $arguments='--test-mode --background --offline --test-seconds 3600 --history "'+$historyPath+'" --diagnostics "'+$statusPath+'"'
    $script:process=Start-Process -FilePath $Executable -ArgumentList $arguments -WindowStyle Hidden -PassThru
    Wait-For { $script:owner=[ActivityQA]::FindWindow('Interval.Owner','Interval.Background.Test'); $owner -ne [IntPtr]::Zero -and (State) }
}
function Quit {
    [ActivityQA]::Command($owner,304)
    Assert ($process.WaitForExit(5000)) 'Recorded exit closes the app promptly.'
}
function Capture([IntPtr]$window,[string]$name) {
    $rect=New-Object ActivityQA+RECT; [void][ActivityQA]::GetWindowRect($window,[ref]$rect)
    $bitmap=New-Object Drawing.Bitmap ($rect.right-$rect.left),($rect.bottom-$rect.top)
    $graphics=[Drawing.Graphics]::FromImage($bitmap); $dc=$graphics.GetHdc()
    try { Assert ([ActivityQA]::PrintWindow($window,$dc,0)) 'Dashboard renders into a native window.' }
    finally { $graphics.ReleaseHdc($dc); $graphics.Dispose() }
    try { $bitmap.Save((Join-Path $artifactDir $name),[Drawing.Imaging.ImageFormat]::Png) } finally { $bitmap.Dispose() }
}
if ([ActivityQA]::FindWindow('Interval.Owner','Interval.Background.Test') -ne [IntPtr]::Zero) { throw 'Close the existing test instance first.' }
try {
    Launch
    Assert ((Records)[0].Kind -eq 'app_start' -and !(State).historyFailed) 'Fresh launch persists its start in the isolated history file.'
    [ActivityQA]::Command($owner,400)
    Wait-For { $script:dashboard=[ActivityQA]::FindWindow('Interval.Dashboard','인터벌 · 사용 현황판'); [ActivityQA]::IsWindowVisible($dashboard) }
    Assert ((State).dashboardVisible) 'Dashboard opens from the resident app.'
    [ActivityQA]::Command($owner,301); Start-Sleep -Milliseconds 1100
    Assert ((State).activityState -eq 1) 'Manual break is recorded as resting.'
    [ActivityQA]::Command($owner,101)
    Assert (@(Records | Where-Object Kind -eq 'snooze').Count -eq 1) 'Five-minute snooze is distinguished from restart.'
    [ActivityQA]::Command($owner,303); Start-Sleep -Milliseconds 1100
    Assert ((State).activityState -eq 2) 'Pause is a separate activity state.'
    [void][ActivityQA]::SendMessage($owner,0x2B1,[IntPtr]7,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 1100
    Assert ((State).activityState -eq 3) 'Lock takes precedence over pause.'
    [void][ActivityQA]::SendMessage($owner,0x218,[IntPtr]4,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 1100
    Assert ((State).activityState -eq 4) 'Suspend takes precedence over lock.'
    $beforeWake=(State).runningMs
    [void][ActivityQA]::SendMessage($owner,0x218,[IntPtr]18,[IntPtr]::Zero)
    [void][ActivityQA]::SendMessage($owner,0x218,[IntPtr]7,[IntPtr]::Zero)
    Assert ((State).activityState -eq 3 -and (State).sleepingMs -ge 1000) 'Wake restores the still-locked state and excludes sleeping time.'
    Assert (@(Records | Where-Object Kind -eq 'wake').Count -eq 1) 'Duplicate Windows resume notifications do not duplicate wake events.'
    [void][ActivityQA]::SendMessage($owner,0x2B1,[IntPtr]8,[IntPtr]::Zero)
    Assert ((State).activityState -eq 2 -and (State).runningMs -eq $beforeWake) 'Unlock preserves pause and never credits hold time to running.'
    [ActivityQA]::Command($owner,303)
    Assert ((State).activityState -eq 0) 'Resume returns to running.'
    [ActivityQA]::Command($owner,302)
    Assert (@(Records | Where-Object Kind -eq 'restart').Count -eq 1) 'Restart is explicitly recorded.'
    [void][ActivityQA]::SendMessage($owner,0x11,[IntPtr]::Zero,[IntPtr]::Zero)
    [void][ActivityQA]::SendMessage($owner,0x16,[IntPtr]::Zero,[IntPtr]::Zero)
    Assert (!$process.HasExited -and @(Records | Where-Object Kind -eq 'shutdown').Count -eq 0) 'Cancelled shutdown does not produce a false shutdown or stop the app.'
    Capture $dashboard 'dashboard-light.png'
    [ActivityQA]::Command($owner,204); Capture $dashboard 'dashboard-dark.png'
    [ActivityQA]::Command($owner,401); [ActivityQA]::Command($owner,402); [ActivityQA]::Command($owner,403)
    [void][ActivityQA]::SendMessage($dashboard,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
    Assert (!$process.HasExited -and ![ActivityQA]::IsWindowVisible($dashboard)) 'Closing the dashboard keeps the app running.'
    Quit
    Assert ((Records)[-1].Kind -eq 'app_exit' -and (Records)[-1].State -eq 5) 'Graceful exit persists the offline boundary.'
    Start-Sleep -Milliseconds 1100
    Launch
    Assert (@(Records | Where-Object Kind -eq 'app_start').Count -eq 2 -and @(Records | Where-Object Kind -eq 'interrupted').Count -eq 0) 'Relaunch retains history without inventing a crash.'
    [ActivityQA]::Command($owner,302); Start-Sleep -Milliseconds 1100
    Stop-Process -Id $process.Id -Force; [void]$process.WaitForExit(5000)
    Start-Sleep -Milliseconds 1100
    Launch
    Assert (@(Records | Where-Object Kind -eq 'interrupted').Count -eq 1 -and (State).unknownMs -ge 2000) 'Forced termination recovers the unobserved gap as unknown time.'
    [void][ActivityQA]::SendMessage($owner,0x16,[IntPtr]1,[IntPtr]::Zero)
    Assert ($process.WaitForExit(5000)) 'Confirmed Windows session end flushes and exits.'
    Assert ((Records)[-1].Kind -eq 'shutdown') 'Windows shutdown remains distinct from ordinary app exit.'
    [IO.File]::AppendAllText($historyPath,"1`t1790870400000`ttorn")
    Launch
    Assert (@(Records | Where-Object Kind -eq 'app_start').Count -eq 4) 'A torn final record does not discard earlier history or prevent a new start.'
    Quit
    $historyPath=Join-Path $artifactDir "missing-$runId\history.tsv"
    Launch
    Assert ((State).historyFailed -and (State).activityState -eq 0) 'Unwritable history is reported while the timer remains usable.'
    Quit
    Write-Output "History artifacts: $artifactDir"
} finally {
    if ($process -and !$process.HasExited) { [ActivityQA]::Command($owner,304); [void]$process.WaitForExit(5000) }
}
