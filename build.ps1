param([switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
& (Join-Path $projectRoot 'fetch-fonts.ps1')
& (Join-Path $projectRoot 'build-icon.ps1')
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio Build Tools with Desktop development with C++.' }
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsPath) { throw 'The Visual C++ x64 build tools are required.' }
$devCommand = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
$environmentLines = & $env:ComSpec /d /c "call `"$devCommand`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the Visual C++ environment.' }
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process') }
}
New-Item -ItemType Directory -Path (Join-Path $projectRoot 'build'),(Join-Path $projectRoot 'dist') -Force | Out-Null
Push-Location (Join-Path $projectRoot 'src')
try {
    & rc.exe /nologo /fo '../build/interval.res' 'interval.rc'
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
    & cl.exe /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /D_WIN32_WINNT=0x0A00 /Fo'../build/main.obj' 'main.cpp' '../build/interval.res' /link /SUBSYSTEM:WINDOWS /OUT:'../dist/Interval.exe' /DYNAMICBASE /NXCOMPAT /OPT:REF /OPT:ICF
    if ($LASTEXITCODE -ne 0) { throw 'App compilation failed.' }
    if (!$SkipTests) {
        & cl.exe /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Fo'../build/core-tests.obj' '../tests/core-tests.cpp' /link /OUT:'../build/core-tests.exe'
        if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed.' }
        & '../build/core-tests.exe'
        if ($LASTEXITCODE -ne 0) { throw 'Core verification failed.' }
    }
    Copy-Item -LiteralPath '../assets/SB_Aggro_Font_license.pdf' -Destination '../dist/SB_Aggro_Font_license.pdf' -Force
    Copy-Item -LiteralPath '../assets/interval.svg' -Destination '../dist/interval.svg' -Force
    Copy-Item -LiteralPath '../start.ps1' -Destination '../dist/start.ps1' -Force
    $releaseReadme = (Get-Content -LiteralPath '../README.md' -Raw -Encoding UTF8).Replace('(docs/screenshots/', '(https://raw.githubusercontent.com/my3rdstory/interval-for-win/main/docs/screenshots/').Replace('(assets/SB_Aggro_Font_license.pdf)', '(SB_Aggro_Font_license.pdf)').Replace('(assets/licenses/MSVC-STL-LICENSE.txt)', '(MSVC-STL-LICENSE.txt)').Replace('(assets/interval.svg)', '(interval.svg)')
    Set-Content -LiteralPath '../dist/README.md' -Value $releaseReadme -Encoding UTF8
    Copy-Item -LiteralPath '../LICENSE' -Destination '../dist/LICENSE' -Force
    Copy-Item -LiteralPath '../assets/licenses/MSVC-STL-LICENSE.txt' -Destination '../dist/MSVC-STL-LICENSE.txt' -Force
    $releaseNotices = (Get-Content -LiteralPath '../THIRD-PARTY-NOTICES.md' -Raw -Encoding UTF8).Replace('(assets/licenses/MSVC-STL-LICENSE.txt)', '(MSVC-STL-LICENSE.txt)')
    Set-Content -LiteralPath '../dist/THIRD-PARTY-NOTICES.md' -Value $releaseNotices -Encoding UTF8
    Compress-Archive -LiteralPath '../dist/Interval.exe','../dist/start.ps1','../dist/interval.svg','../dist/LICENSE','../dist/SB_Aggro_Font_license.pdf','../dist/MSVC-STL-LICENSE.txt','../dist/THIRD-PARTY-NOTICES.md','../dist/README.md' -DestinationPath '../dist/Interval-1.0.0-win-x64.zip' -Force
    Get-Item '../dist/Interval.exe' | Select-Object FullName,Length
} finally { Pop-Location }
