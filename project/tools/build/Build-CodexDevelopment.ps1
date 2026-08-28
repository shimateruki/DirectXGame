[CmdletBinding()]
param(
    [switch]$PrimeOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$solutionPath = Join-Path $projectRoot 'DirectXGame.sln'
$trackingSource = [System.IO.Path]::GetFullPath((Join-Path $projectRoot '..\generated\obj\DirectXGame\DirectXGame\Development\DirectXGame.tlog'))
$trackingCache = Join-Path $projectRoot 'output\codex-msbuild-cache\DirectXGame\Development\DirectXGame.tlog'

$msBuildCandidates = @(
    'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe',
    'C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\MSBuild.exe'
)
$msBuild = $msBuildCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $msBuild) {
    throw 'Visual Studio 18 の MSBuild.exe が見つかりません。'
}

function Copy-TrackingState {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,

        [Parameter(Mandatory = $true)]
        [string]$Destination
    )

    if (-not (Test-Path -LiteralPath $Source)) {
        return $false
    }

    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    & robocopy.exe $Source $Destination * /E /COPY:DAT /DCOPY:DAT /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    $robocopyExitCode = $LASTEXITCODE
    if ($robocopyExitCode -gt 7) {
        throw "MSBuild追跡情報のコピーに失敗しました。Robocopy exit code: $robocopyExitCode"
    }

    return $true
}

if ($PrimeOnly) {
    if (-not (Copy-TrackingState -Source $trackingSource -Destination $trackingCache)) {
        throw '退避元のMSBuild追跡情報がありません。先にDevelopmentビルドを完了してください。'
    }

    Write-Host "増分ビルド追跡情報を退避しました: $trackingCache"
    exit 0
}

if (Copy-TrackingState -Source $trackingCache -Destination $trackingSource) {
    Write-Host '前回の増分ビルド追跡情報を復元しました。'
}

$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
Push-Location $projectRoot
try {
    & $msBuild $solutionPath `
        /p:Configuration=Development `
        /p:Platform=x64 `
        /p:MultiProcessorCompilation=false `
        /m:1 `
        /v:minimal `
        /nr:false
    $buildExitCode = $LASTEXITCODE
}
finally {
    Pop-Location
    $stopwatch.Stop()
}

if ($buildExitCode -ne 0) {
    throw "Developmentビルドに失敗しました。MSBuild exit code: $buildExitCode"
}

if (-not (Copy-TrackingState -Source $trackingSource -Destination $trackingCache)) {
    throw 'ビルド後のMSBuild追跡情報を退避できませんでした。'
}

Write-Host ('Developmentビルド完了: {0:N2} 秒' -f $stopwatch.Elapsed.TotalSeconds)
