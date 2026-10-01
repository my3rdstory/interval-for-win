param()
$ErrorActionPreference = 'Stop'
$assetDir = Join-Path $PSScriptRoot 'assets'
$sourceUrl = 'https://www.sandbox.co.kr/uploads/sbx-content/ci/SBX_AGGRO_FONT_2026.zip'
$files = @(
    @{ Name='SB_Aggro_L.ttf'; Entry='SBX_AGGRO_FONT_2026/Windows_TTF/SB_Aggro_L.ttf'; Hash='8329057DF354F5367CAF1BEC6E88A132F088D63B6F61E4DFF56AA0B27FAD549A' },
    @{ Name='SB_Aggro_M.ttf'; Entry='SBX_AGGRO_FONT_2026/Windows_TTF/SB_Aggro_M.ttf'; Hash='2F458E0D47D2AFB9D6B75887E1D8A58C153757A0AD703BD386A4629D658402DA' },
    @{ Name='SB_Aggro_Font_license.pdf'; Entry='SBX_AGGRO_FONT_2026/SB_Aggro_Font_license.pdf'; Hash='81205967CC4A132F6BFAA2A34C88A3698354F46B91C7D32E707663FA0FA462A0' }
)
$missing = $false
foreach ($file in $files) {
    $path = Join-Path $assetDir $file.Name
    if (!(Test-Path -LiteralPath $path)) { $missing = $true; continue }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.Hash) {
        throw "Existing asset differs from the verified original: $path. It has not been changed."
    }
}
if (!$missing) { Write-Output 'Font assets are ready (SHA-256 verified).'; return }
New-Item -ItemType Directory -Path $assetDir -Force | Out-Null
$fontTempDir = Join-Path ([IO.Path]::GetTempPath()) ('interval-fonts-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fontTempDir | Out-Null
$archivePath = Join-Path $fontTempDir 'official.zip'
$zip = $null
try {
    Write-Output 'Downloading the original Aggro font package from SANDBOX...'
    Invoke-WebRequest -Uri $sourceUrl -OutFile $archivePath -UseBasicParsing -TimeoutSec 60
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($archivePath)
    foreach ($file in $files) {
        $entry = $zip.GetEntry($file.Entry)
        if (!$entry) { throw "The official archive is missing $($file.Name)." }
        # Extract only named assets, never arbitrary archive paths.
        $temporaryPath = Join-Path $fontTempDir $file.Name
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$temporaryPath,$false)
        if ((Get-FileHash -LiteralPath $temporaryPath -Algorithm SHA256).Hash -ne $file.Hash) {
            throw 'The official package changed. Review its contents and license before updating the pinned hashes.'
        }
    }
    foreach ($file in $files) {
        $destination = Join-Path $assetDir $file.Name
        if (!(Test-Path -LiteralPath $destination)) {
            Copy-Item -LiteralPath (Join-Path $fontTempDir $file.Name) -Destination $destination
        }
    }
    Write-Output 'Font assets downloaded and SHA-256 verified.'
} finally {
    if ($zip) { $zip.Dispose() }
    # Exact files in the unique temporary directory created above. No recursive deletion.
    foreach ($name in @('official.zip','SB_Aggro_L.ttf','SB_Aggro_M.ttf','SB_Aggro_Font_license.pdf')) {
        $temporaryPath = Join-Path $fontTempDir $name
        if (Test-Path -LiteralPath $temporaryPath) { Remove-Item -LiteralPath $temporaryPath -Force }
    }
    Remove-Item -LiteralPath $fontTempDir
}
