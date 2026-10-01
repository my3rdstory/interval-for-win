param()
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $projectRoot 'dist\Interval.exe'
$iconFile=Join-Path $projectRoot 'build\interval.ico'
$artifactDir=Join-Path $projectRoot 'build\qa'
New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null
$file=[IO.File]::OpenRead($iconFile)
$reader=New-Object IO.BinaryReader $file
try {
    if ($reader.ReadUInt16() -ne 0 -or $reader.ReadUInt16() -ne 1 -or $reader.ReadUInt16() -ne 9) { throw 'Invalid multi-resolution icon header.' }
    foreach ($size in @(16,20,24,32,40,48,64,128,256)) {
        $width=$reader.ReadByte(); $height=$reader.ReadByte()
        if ($width -eq 0) { $width=256 }; if ($height -eq 0) { $height=256 }
        if ($width -ne $size -or $height -ne $size) { throw 'Missing icon resolution.' }
        [void]$reader.ReadBytes(14)
    }
} finally { $reader.Dispose() }
Write-Output 'PASS: Windows icon includes all nine 16-256 px resolutions.'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class IntervalExeIconQA {
 [DllImport("shell32.dll",CharSet=CharSet.Unicode)] public static extern uint ExtractIconEx(string path,int index,[Out] IntPtr[] large,[Out] IntPtr[] small,uint count);
 [DllImport("user32.dll")] public static extern bool DestroyIcon(IntPtr icon);
}
'@
$large=[IntPtr[]]@([IntPtr]::Zero); $small=[IntPtr[]]@([IntPtr]::Zero)
try {
    $count=[IntervalExeIconQA]::ExtractIconEx($exe,0,$large,$small,1)
    if ($count -lt 1 -or $count -eq [uint32]::MaxValue -or $large[0] -eq [IntPtr]::Zero -or $small[0] -eq [IntPtr]::Zero) { throw 'Executable does not expose its embedded app icon.' }
    foreach ($entry in @(@($large[0],'app-icon-32.png'),@($small[0],'app-icon-16.png'))) {
        $bitmap=[Drawing.Icon]::FromHandle($entry[0]).ToBitmap()
        try { $bitmap.Save((Join-Path $artifactDir $entry[1]),[Drawing.Imaging.ImageFormat]::Png) } finally { $bitmap.Dispose() }
    }
    Write-Output 'PASS: Windows Shell extracts both small and large icons from Interval.exe.'
} finally {
    if ($large[0] -ne [IntPtr]::Zero) { [void][IntervalExeIconQA]::DestroyIcon($large[0]) }
    if ($small[0] -ne [IntPtr]::Zero) { [void][IntervalExeIconQA]::DestroyIcon($small[0]) }
}
if ((Get-Item -LiteralPath $exe).VersionInfo.FileDescription -ne '인터벌 · 휴식 알림') { throw 'Korean application description is missing.' }
Write-Output 'PASS: Executable description is correctly encoded in Korean.'
