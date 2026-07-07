# 元画像からDDSキャッシュを作成し、起動後の重いテクスチャ読み込みを次回以降軽くするツール。
param(
    [string[]]$Root = @("Resources"),
    [string]$Texconv = "Resources\tools\Texconv.exe",
    [string]$Manifest = "Resources\.cache\dds_cache_manifest.json",
    [string]$RequestQueue = "Resources\.cache\dds_cache_requests.jsonl",
    [string]$NotificationLog = "Resources\.cache\dds_cache_notifications.jsonl",
    [switch]$Watch,
    [int]$Interval = 5,
    [switch]$Force,
    [switch]$DryRun,
    [switch]$RequestOnly,
    [double]$MinRequestDurationMs = 2.0
)

$ErrorActionPreference = "Stop"
$SourceExtensions = @(".png", ".jpg", ".jpeg", ".tga", ".hdr")
$PreviewMarkers = @(
    "/generated/text/_preview_",
    "/generated/editor/text_preview/"
)
$LinearMarkers = @(
    "bakedshader",
    "_mask",
    "_noise",
    "normal",
    "_n",
    "nor",
    "arm",
    "orm",
    "rough",
    "metal",
    "ao"
)

# 絶対パスをプロジェクト相対へ戻し、manifestやログがPC固有パスに依存しないようにする。
function Convert-ToProjectPath([string]$Path) {
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $rootPath = [System.IO.Path]::GetFullPath((Get-Location).Path)
    if ($fullPath.StartsWith($rootPath, [System.StringComparison]::OrdinalIgnoreCase)) {
        $fullPath = $fullPath.Substring($rootPath.Length).TrimStart("\", "/")
    }
    return ($fullPath -replace "\\", "/")
}

# エディタの一時プレビュー画像はDDS化しても無駄なので、キャッシュ対象から除外する。
function Test-PreviewTexture([string]$Path) {
    $lower = "/" + (Convert-ToProjectPath $Path).ToLowerInvariant()
    foreach ($marker in $PreviewMarkers) {
        if ($lower.Contains($marker)) {
            return $true
        }
    }
    return $false
}

# 法線・マスク・ORM系をリニア扱いにし、sRGB圧縮で値が変わる事故を避ける。
function Test-LinearTexture([string]$Path) {
    $lower = (Convert-ToProjectPath $Path).ToLowerInvariant()
    foreach ($marker in $LinearMarkers) {
        if ($lower.Contains($marker)) {
            return $true
        }
    }
    return $false
}

# ブランチ切り替えで更新日時だけ変わった画像を再変換しないよう、内容ハッシュを取る。
function Get-SourceHash([System.IO.FileInfo]$File) {
    try {
        return (Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    catch {
        return ""
    }
}

# 画像用途に合わせてBC7/BC6HなどのDDS圧縮形式を決める。
function Get-DDSFormat([System.IO.FileInfo]$File, [object]$Request = $null) {
    if ($Request -and $Request.format) {
        return [string]$Request.format
    }
    if ($File.Extension.ToLowerInvariant() -eq ".hdr") {
        return "BC6H_UF16"
    }
    if (Test-LinearTexture $File.FullName) {
        return "BC7_UNORM"
    }
    return "BC7_UNORM_SRGB"
}

# ランタイム側が重いと判断したテクスチャ変換要求をJSONLから読み込む。
function Read-RequestMap {
    $map = @{}
    if (-not (Test-Path -LiteralPath $RequestQueue)) {
        return $map
    }

    Get-Content -LiteralPath $RequestQueue -ErrorAction SilentlyContinue | ForEach-Object {
        $line = $_.Trim()
        if ([string]::IsNullOrWhiteSpace($line)) {
            return
        }

        try {
            $request = $line | ConvertFrom-Json
            if (-not $request.source) {
                return
            }
            $source = Convert-ToProjectPath ([string]$request.source)
            $map[$source] = $request
        }
        catch {
        }
    }

    return $map
}

# 前回生成時のハッシュや形式を読み、タイムスタンプだけで再変換しないための基準にする。
function Read-ManifestMap {
    $map = @{}
    if (-not (Test-Path -LiteralPath $Manifest)) {
        return $map
    }

    try {
        $manifestData = Get-Content -LiteralPath $Manifest -Raw -Encoding UTF8 | ConvertFrom-Json
        foreach ($entry in @($manifestData.entries)) {
            if ($entry.source) {
                $map[[string]$entry.source] = $entry
            }
        }
    }
    catch {
    }

    return $map
}

# 1枚の元画像についてDDSの有無・鮮度・形式・ハッシュをまとめた判定レコードを作る。
function New-TextureEntry([System.IO.FileInfo]$File, [object]$Request = $null, [object]$ManifestEntry = $null) {
    $sourcePath = Convert-ToProjectPath $File.FullName
    $ddsPath = [System.IO.Path]::ChangeExtension($File.FullName, ".dds")
    $ddsExists = Test-Path -LiteralPath $ddsPath
    $ddsInfo = if ($ddsExists) { Get-Item -LiteralPath $ddsPath } else { $null }
    $format = Get-DDSFormat $File $Request
    $sourceHash = Get-SourceHash $File
    $status = "latest"
    $message = "DDS is up to date."

    if (-not $ddsExists) {
        $status = "missing"
        $message = "DDS is missing."
    }
    elseif ($File.LastWriteTimeUtc -gt $ddsInfo.LastWriteTimeUtc) {
        $manifestHash = if ($ManifestEntry -and $ManifestEntry.sourceHash) { ([string]$ManifestEntry.sourceHash).ToLowerInvariant() } else { "" }
        $manifestFormat = if ($ManifestEntry -and $ManifestEntry.format) { [string]$ManifestEntry.format } else { "" }
        $manifestDdsSize = if ($ManifestEntry -and $ManifestEntry.ddsSize) { [int64]$ManifestEntry.ddsSize } else { [int64]-1 }
        $sameSourceAsLastBuild = -not [string]::IsNullOrWhiteSpace($sourceHash) -and $sourceHash -eq $manifestHash
        $sameFormatAsLastBuild = $manifestFormat -eq $format
        $sameDdsFileAsLastBuild = $manifestDdsSize -lt 0 -or $manifestDdsSize -eq [int64]$ddsInfo.Length

        if ($sameSourceAsLastBuild -and $sameFormatAsLastBuild -and $sameDdsFileAsLastBuild) {
            $status = "latest"
            $message = "DDS is up to date by source hash."
        }
        else {
            $status = "outdated"
            $message = "Source is newer than DDS."
        }
    }

    $requestedDuration = 0.0
    if ($Request -and $Request.durationMs) {
        $requestedDuration = [double]$Request.durationMs
    }

    [pscustomobject]@{
        source = $sourcePath
        dds = Convert-ToProjectPath $ddsPath
        format = $format
        sourceHash = $sourceHash
        sourceSize = [int64]$File.Length
        ddsSize = if ($ddsInfo) { [int64]$ddsInfo.Length } else { [int64]0 }
        sourceWriteTimeUtc = $File.LastWriteTimeUtc.ToString("o")
        ddsWriteTimeUtc = if ($ddsInfo) { $ddsInfo.LastWriteTimeUtc.ToString("o") } else { "" }
        status = $status
        durationMs = 0.0
        requested = [bool]$Request
        requestedDurationMs = $requestedDuration
        message = $message
    }
}

# 変換候補を重複なく登録し、軽すぎる要求や一時プレビューをここで落とす。
function Add-TextureEntry([System.Collections.Generic.Dictionary[string, object]]$EntryMap, [System.IO.FileInfo]$File, [object]$Request = $null, [object]$ManifestEntry = $null) {
    if (-not $File.Exists) {
        return
    }
    if ($SourceExtensions -notcontains $File.Extension.ToLowerInvariant()) {
        return
    }
    if (Test-PreviewTexture $File.FullName) {
        return
    }
    if ($RequestOnly -and $Request) {
        $requestDuration = if ($Request.durationMs) { [double]$Request.durationMs } else { 0.0 }
        if ($requestDuration -lt $MinRequestDurationMs) {
            return
        }
    }

    $entry = New-TextureEntry $File $Request $ManifestEntry
    if ($EntryMap.ContainsKey($entry.source)) {
        if ($Request) {
            $EntryMap[$entry.source].requested = $true
            $EntryMap[$entry.source].requestedDurationMs = $entry.requestedDurationMs
            $EntryMap[$entry.source].format = $entry.format
        }
        return
    }
    $EntryMap.Add($entry.source, $entry)
}

# 通常スキャンとランタイム要求を統合し、missing/outdated/latestの順で処理しやすく並べる。
function Get-TextureEntries {
    $requests = Read-RequestMap
    $manifest = Read-ManifestMap
    $entryMap = [System.Collections.Generic.Dictionary[string, object]]::new()

    if (-not $RequestOnly) {
        foreach ($rootPath in $Root) {
            if (-not (Test-Path -LiteralPath $rootPath)) {
                Write-Warning "Root not found: $rootPath"
                continue
            }

            Get-ChildItem -LiteralPath $rootPath -Recurse -File | ForEach-Object {
                $source = Convert-ToProjectPath $_.FullName
                $request = if ($requests.ContainsKey($source)) { $requests[$source] } else { $null }
                $manifestEntry = if ($manifest.ContainsKey($source)) { $manifest[$source] } else { $null }
                Add-TextureEntry $entryMap $_ $request $manifestEntry
            }
        }
    }

    foreach ($source in $requests.Keys) {
        if ($entryMap.ContainsKey($source)) {
            continue
        }
        if (-not (Test-Path -LiteralPath $source)) {
            continue
        }
        $manifestEntry = if ($manifest.ContainsKey($source)) { $manifest[$source] } else { $null }
        Add-TextureEntry $entryMap (Get-Item -LiteralPath $source) $requests[$source] $manifestEntry
    }

    return $entryMap.Values | Sort-Object @{ Expression = {
        switch ($_.status) {
            "outdated" { 0 }
            "missing" { 1 }
            "latest" { 2 }
            default { 9 }
        }
    } }, @{ Expression = { if ($_.requested) { 0 } else { 1 } } }, source
}

# 次回の差分判定に使うため、各テクスチャのハッシュ・形式・DDSサイズを保存する。
function Save-Manifest($Entries) {
    $manifestPath = Split-Path -Parent $Manifest
    if ($manifestPath -and -not (Test-Path -LiteralPath $manifestPath)) {
        New-Item -ItemType Directory -Path $manifestPath | Out-Null
    }

    [pscustomobject]@{
        generatedAt = (Get-Date).ToString("o")
        entries = @($Entries)
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $Manifest -Encoding UTF8
}

# DDS生成完了をエディタやログから拾えるよう、軽量なJSONL通知として追記する。
function Write-Notification($Entry) {
    if ($DryRun) {
        return
    }

    $logPath = Split-Path -Parent $NotificationLog
    if ($logPath -and -not (Test-Path -LiteralPath $logPath)) {
        New-Item -ItemType Directory -Path $logPath | Out-Null
    }

    [pscustomobject]@{
        generatedAt = (Get-Date).ToString("o")
        source = $Entry.source
        dds = $Entry.dds
        format = $Entry.format
        durationMs = $Entry.durationMs
        requestedDurationMs = $Entry.requestedDurationMs
        status = $Entry.status
    } | ConvertTo-Json -Compress | Add-Content -LiteralPath $NotificationLog -Encoding UTF8
}

# texconvへ渡す引数を引用符で囲み、日本語パスや空白入りパスでも壊れにくくする。
function Quote-ProcessArgument([string]$Value) {
    return '"' + ($Value -replace '"', '\"') + '"'
}

# texconvを非表示で実行し、標準出力とエラーをまとめて呼び出し元へ返す。
function Invoke-TexconvHidden([string]$SourcePath, [string]$OutputDir, [string]$Format) {
    $resolvedTexconv = (Resolve-Path -LiteralPath $Texconv).Path
    $arguments = @(
        "-f", (Quote-ProcessArgument $Format),
        "-y",
        "-m", "0",
        "-o", (Quote-ProcessArgument $OutputDir),
        (Quote-ProcessArgument $SourcePath)
    ) -join " "

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $resolvedTexconv
    $psi.Arguments = $arguments
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true

    $process = [System.Diagnostics.Process]::Start($psi)
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()

    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Output = (($stdout + "`n" + $stderr).Trim())
    }
}

# 1件の変換を実行し、成功時はDDSサイズや所要時間をレコードへ反映する。
function Convert-Texture($Entry) {
    if ($DryRun) {
        $Entry.status = "dry-run"
        $Entry.message = "Dry run. No file was written."
        return $Entry
    }

    if (-not (Test-Path -LiteralPath $Texconv)) {
        $Entry.status = "error"
        $Entry.message = "Texconv.exe not found: $Texconv"
        return $Entry
    }

    $sourcePath = $Entry.source
    $outputDir = Split-Path -Parent $sourcePath
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $result = Invoke-TexconvHidden $sourcePath $outputDir $Entry.format
    $timer.Stop()

    $Entry.durationMs = [Math]::Round($timer.Elapsed.TotalMilliseconds, 3)
    if ($result.ExitCode -ne 0) {
        $Entry.status = "error"
        $Entry.message = $result.Output
        return $Entry
    }

    if (-not (Test-Path -LiteralPath $Entry.dds)) {
        $Entry.status = "error"
        $Entry.message = "Texconv finished, but DDS was not created."
        return $Entry
    }

    $ddsInfo = Get-Item -LiteralPath $Entry.dds
    $Entry.ddsSize = [int64]$ddsInfo.Length
    $Entry.status = "built"
    $Entry.message = "DDS built successfully."
    Write-Notification $Entry
    return $Entry
}

# スキャン結果の件数と容量をコンソールへ出し、処理前に状態を把握できるようにする。
function Write-Summary($Entries) {
    $latest = @($Entries | Where-Object { $_.status -eq "latest" }).Count
    $missing = @($Entries | Where-Object { $_.status -eq "missing" }).Count
    $outdated = @($Entries | Where-Object { $_.status -eq "outdated" }).Count
    $requested = @($Entries | Where-Object { $_.requested }).Count
    $sourceMb = (($Entries | Measure-Object -Property sourceSize -Sum).Sum / 1MB)
    $ddsMb = (($Entries | Measure-Object -Property ddsSize -Sum).Sum / 1MB)
    Write-Host ("scan={0} requested={1} latest={2} missing={3} outdated={4} source={5:N1}MB dds={6:N1}MB" -f @($Entries).Count, $requested, $latest, $missing, $outdated, $sourceMb, $ddsMb)
}

# DDSが無い/古い/強制指定された対象だけをtexconvへ流し、最新のものはスキップする。
function Invoke-DDSCacheBuild {
    $entries = @(Get-TextureEntries)
    Write-Summary $entries

    $targets = @($entries | Where-Object { $Force -or $_.status -eq "missing" -or $_.status -eq "outdated" })
    if ($targets.Count -eq 0) {
        Write-Host "DDS cache is already up to date."
        if (-not $DryRun) {
            Save-Manifest $entries
        }
        return $entries
    }

    Write-Host ("build targets={0} dryRun={1}" -f $targets.Count, [bool]$DryRun)
    $index = 0
    foreach ($entry in $targets) {
        $index++
        Write-Host ("[{0}/{1}] {2} -> {3}" -f $index, $targets.Count, $entry.source, $entry.format)
        $null = Convert-Texture $entry
        if ($entry.status -eq "error") {
            Write-Warning $entry.message
        }
        elseif ($entry.durationMs -gt 0.0) {
            Write-Host ("  done: {0:N1} ms" -f $entry.durationMs)
        }
    }

    if (-not $DryRun) {
        Save-Manifest $entries
    }
    return $entries
}

# 監視モードでは多重起動をMutexで防ぎ、要求キューや元画像の変化に応じてビルドする。
function Start-DDSCacheWatch {
    $createdNew = $false
    $mutex = [System.Threading.Mutex]::new($true, "Global\GE3_DDSCacheBuilder_Watcher", [ref]$createdNew)
    if (-not $createdNew) {
        Write-Host "DDS cache watcher is already running."
        $mutex.Dispose()
        return
    }

    Write-Host "DDS cache watcher started."
    Write-Host ("interval={0}s requestOnly={1} roots={2}" -f $Interval, [bool]$RequestOnly, ($Root -join ", "))
    $previousState = ""

    try {
        while ($true) {
            $entries = @(Get-TextureEntries)
            $state = ($entries | ForEach-Object { "$($_.source)|$($_.sourceWriteTimeUtc)|$($_.sourceSize)|$($_.status)|$($_.requested)|$($_.requestedDurationMs)" }) -join "`n"
            $hasDirty = @($entries | Where-Object { $_.status -eq "missing" -or $_.status -eq "outdated" }).Count -gt 0

            if ($hasDirty -or $state -ne $previousState) {
                $null = Invoke-DDSCacheBuild
                $previousState = $state
            }

            Start-Sleep -Seconds $Interval
        }
    }
    finally {
        $mutex.ReleaseMutex() | Out-Null
        $mutex.Dispose()
    }
}

# どこから起動されてもプロジェクトルート基準でResourcesやtoolsを解決する。
Set-Location (Resolve-Path (Join-Path $PSScriptRoot "..\.."))

if ($Watch) {
    Start-DDSCacheWatch
}
else {
    $null = Invoke-DDSCacheBuild
}
