param(
    [string]$ProjectDir = ""
)

$ErrorActionPreference = "Stop"

$Version = "2.32.10"
$ArchiveName = "SDL2-devel-$Version-VC.zip"
$DownloadUrl = "https://github.com/libsdl-org/SDL/releases/download/release-$Version/$ArchiveName"

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
    $ProjectDir = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}

$ProjectDir = (Resolve-Path $ProjectDir).Path
$SdlRoot = Join-Path $ProjectDir "externals\SDL2"
$SdlLib = Join-Path $SdlRoot "lib\x64\SDL2.lib"
$SdlDll = Join-Path $SdlRoot "lib\x64\SDL2.dll"

if ((Test-Path -LiteralPath $SdlLib) -and (Test-Path -LiteralPath $SdlDll)) {
    Write-Host "SDL2 is already restored."
    exit 0
}

$CacheDir = Join-Path $ProjectDir "externals\.cache"
$ArchivePath = Join-Path $CacheDir $ArchiveName
$ExtractDir = Join-Path $CacheDir "SDL2-$Version"
$ExtractedRoot = Join-Path $ExtractDir "SDL2-$Version"

New-Item -ItemType Directory -Force -Path $CacheDir | Out-Null

if (!(Test-Path -LiteralPath $ArchivePath)) {
    Write-Host "Downloading SDL2 $Version..."
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $DownloadUrl -OutFile $ArchivePath
}

if (Test-Path -LiteralPath $ExtractDir) {
    Remove-Item -LiteralPath $ExtractDir -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $ExtractDir | Out-Null
Expand-Archive -LiteralPath $ArchivePath -DestinationPath $ExtractDir -Force

if (!(Test-Path -LiteralPath $ExtractedRoot)) {
    throw "SDL2 archive layout was not recognized: $ExtractedRoot"
}

New-Item -ItemType Directory -Force -Path (Join-Path $SdlRoot "include") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $SdlRoot "lib\x64") | Out-Null

Copy-Item -LiteralPath (Join-Path $ExtractedRoot "include\*") -Destination (Join-Path $SdlRoot "include") -Recurse -Force
Copy-Item -LiteralPath (Join-Path $ExtractedRoot "lib\x64\SDL2.lib") -Destination $SdlLib -Force
Copy-Item -LiteralPath (Join-Path $ExtractedRoot "lib\x64\SDL2.dll") -Destination $SdlDll -Force

Write-Host "SDL2 $Version restored."
