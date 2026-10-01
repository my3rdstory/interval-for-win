param([switch]$Background)
$ErrorActionPreference = 'Stop'
$executable = Join-Path $PSScriptRoot 'Interval.exe'
if (!(Test-Path -LiteralPath $executable)) { $executable = Join-Path $PSScriptRoot 'dist\Interval.exe' }
if (!(Test-Path -LiteralPath $executable)) { throw 'Build Interval.exe before running start.ps1.' }
$executable = (Resolve-Path -LiteralPath $executable).Path
$arguments = if ($Background) { '--background' } else { '' }

# Obtain the Shell object hosted by the existing Explorer desktop. A newly created
# Shell.Application can launch from PowerShell itself and inherit a terminal job.
$shellBroker = New-Object -ComObject Shell.Application
$shellWindows = $null
try {
    $shellWindows = $shellBroker.Windows()
    $desktopLocation = 0
    $desktopRoot = 0
    $desktopHandle = 0
    $desktopWindow = $shellWindows.FindWindowSW([ref]$desktopLocation,[ref]$desktopRoot,8,[ref]$desktopHandle,1)
    if (!$desktopWindow -or !$desktopWindow.Document) { throw 'The Windows Explorer desktop is unavailable.' }
    $desktopShell = $desktopWindow.Document.Application
    $desktopShell.ShellExecute($executable,$arguments,(Split-Path $executable -Parent),'open',1)
} finally {
    if ($shellWindows) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shellWindows) }
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shellBroker)
}

$deadline = [DateTime]::UtcNow.AddSeconds(10)
do {
    $running = Get-CimInstance Win32_Process -Filter "Name='Interval.exe'" | Where-Object { $_.ExecutablePath -eq $executable } | Select-Object -First 1
    if ($running) { break }
    Start-Sleep -Milliseconds 100
} while ([DateTime]::UtcNow -lt $deadline)
if (!$running) { throw 'Interval did not start.' }
[PSCustomObject]@{ ProcessId=$running.ProcessId; Executable=$running.ExecutablePath; ParentProcessId=$running.ParentProcessId }
