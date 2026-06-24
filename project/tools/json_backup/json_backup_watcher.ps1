param(
    [string[]]$Root = @("Resources/json"),
    [string]$BackupRoot = "Resources/.backup/json",
    [string]$Manifest = "Resources/.cache/json_backup_manifest.json",
    [string]$Report = "Resources/.cache/json_backup/latest_report.json",
    [switch]$Watch,
    [switch]$Once,
    [switch]$Force,
    [int]$Interval = 3,
    [int]$MaxVersionsPerFile = 20
)

$ErrorActionPreference = "Stop"

$projectRoot = (Get-Location).Path
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

function Get-FullPathSafe([string]$path) {
    if ([System.IO.Path]::IsPathRooted($path)) {
        return [System.IO.Path]::GetFullPath($path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $projectRoot $path))
}

function Assert-InProject([string]$path) {
    $full = Get-FullPathSafe $path
    $root = [System.IO.Path]::GetFullPath($projectRoot)
    if (-not $root.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
        $root += [System.IO.Path]::DirectorySeparatorChar
    }
    if (-not $full.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path escaped project root: $path"
    }
    return $full
}

function Convert-ToProjectPath([string]$path) {
    $full = [System.IO.Path]::GetFullPath($path)
    $root = [System.IO.Path]::GetFullPath($projectRoot)
    if (-not $root.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
        $root += [System.IO.Path]::DirectorySeparatorChar
    }
    if ($full.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $full.Substring($root.Length).Replace([char]92, [char]47)
    }
    return $full.Replace([char]92, [char]47)
}

function Write-JsonFile($path, $value) {
    $full = Assert-InProject $path
    $dir = Split-Path -Parent $full
    if (-not [string]::IsNullOrWhiteSpace($dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    $json = $value | ConvertTo-Json -Depth 12
    [System.IO.File]::WriteAllText($full, $json, $utf8NoBom)
}

function Read-Manifest {
    $full = Assert-InProject $Manifest
    if (-not (Test-Path -LiteralPath $full)) {
        return @{
            version = 1
            files = @{}
            backups = @()
        }
    }

    try {
        $loaded = Get-Content -LiteralPath $full -Raw -Encoding UTF8 | ConvertFrom-Json
        $files = @{}
        if ($loaded.files) {
            foreach ($property in $loaded.files.PSObject.Properties) {
                $files[$property.Name] = $property.Value
            }
        }
        $backups = @()
        if ($loaded.backups) {
            $backups = @($loaded.backups)
        }
        return @{
            version = 1
            files = $files
            backups = $backups
        }
    }
    catch {
        return @{
            version = 1
            files = @{}
            backups = @()
        }
    }
}

function Save-Manifest($state) {
    $filesObject = [ordered]@{}
    foreach ($key in ($state.files.Keys | Sort-Object)) {
        $filesObject[$key] = $state.files[$key]
    }
    Write-JsonFile $Manifest ([ordered]@{
        version = 1
        updatedAt = (Get-Date).ToUniversalTime().ToString("o")
        files = $filesObject
        backups = @($state.backups)
    })
}

function Test-ProcessAlive([int]$processId) {
    try {
        Get-Process -Id $processId -ErrorAction Stop | Out-Null
        return $true
    }
    catch {
        return $false
    }
}

function Get-LockPath {
    $reportFull = Assert-InProject $Report
    return Join-Path (Split-Path -Parent $reportFull) "json_backup_watcher.lock"
}

function Write-WatcherLock {
    $lockPath = Get-LockPath
    $lockDir = Split-Path -Parent $lockPath
    New-Item -ItemType Directory -Path $lockDir -Force | Out-Null
    $lock = [ordered]@{
        pid = $PID
        startedAt = (Get-Date).ToUniversalTime().ToString("o")
    }
    [System.IO.File]::WriteAllText($lockPath, ($lock | ConvertTo-Json -Depth 4), $utf8NoBom)
}

function Test-WatcherAlreadyRunning {
    $lockPath = Get-LockPath
    if (-not (Test-Path -LiteralPath $lockPath)) {
        return $false
    }

    try {
        $lock = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($lock.pid -and (Test-ProcessAlive ([int]$lock.pid))) {
            return $true
        }
    }
    catch {
    }
    return $false
}

function Get-JsonFiles {
    $files = @()
    foreach ($rootPath in $Root) {
        $fullRoot = Assert-InProject $rootPath
        if (-not (Test-Path -LiteralPath $fullRoot)) {
            continue
        }

        $files += Get-ChildItem -LiteralPath $fullRoot -Recurse -File -Filter "*.json" |
            Where-Object {
                $relative = Convert-ToProjectPath $_.FullName
                $lower = $relative.ToLowerInvariant()
                -not $lower.StartsWith("resources/.cache/") -and
                -not $lower.StartsWith("resources/.backup/")
            }
    }
    return $files
}

function Invoke-JsonBackupScan {
    $backupFullRoot = Assert-InProject $BackupRoot
    New-Item -ItemType Directory -Path $backupFullRoot -Force | Out-Null

    $state = Read-Manifest
    $now = Get-Date
    $stamp = $now.ToString("yyyyMMdd_HHmmss_fff")
    $backupStampRoot = Join-Path $backupFullRoot $stamp
    $backups = @()
    $errors = @()
    $scanned = 0
    $unchanged = 0

    foreach ($file in Get-JsonFiles) {
        $scanned++
        try {
            $source = Convert-ToProjectPath $file.FullName
            $key = $source.ToLowerInvariant()

            try {
                Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8 | ConvertFrom-Json | Out-Null
            }
            catch {
                $errors += [ordered]@{
                    path = $source
                    message = "Skipped invalid JSON."
                }
                continue
            }

            $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
            $size = [int64]$file.Length
            $previous = $state.files[$key]
            $reason = "changed"
            if (-not $previous) {
                $reason = "new"
            }

            if (-not $Force -and $previous -and $previous.hash -eq $hash -and [int64]$previous.size -eq $size) {
                $unchanged++
                continue
            }

            $backupPath = Join-Path $backupStampRoot $source
            New-Item -ItemType Directory -Path (Split-Path -Parent $backupPath) -Force | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $backupPath -Force

            $entry = [ordered]@{
                source = $source
                backup = (Convert-ToProjectPath $backupPath)
                reason = $reason
                size = $size
                hash = $hash
                backedUpAt = $now.ToUniversalTime().ToString("o")
            }
            $backups += $entry
            $state.backups += $entry
            $state.files[$key] = [ordered]@{
                path = $source
                hash = $hash
                size = $size
                lastWriteUtc = $file.LastWriteTimeUtc.ToString("o")
                latestBackup = $entry.backup
                updatedAt = $now.ToUniversalTime().ToString("o")
            }
        }
        catch {
            $errors += [ordered]@{
                path = (Convert-ToProjectPath $file.FullName)
                message = $_.Exception.Message
            }
        }
    }

    if ($MaxVersionsPerFile -gt 0 -and $state.backups.Count -gt 0) {
        $kept = @()
        $grouped = @{}
        foreach ($entry in @($state.backups | Sort-Object backedUpAt -Descending)) {
            $source = [string]$entry.source
            if (-not $grouped.ContainsKey($source)) {
                $grouped[$source] = 0
            }
            $grouped[$source]++
            if ($grouped[$source] -le $MaxVersionsPerFile) {
                $kept += $entry
            }
            else {
                try {
                    $oldBackup = Assert-InProject ([string]$entry.backup)
                    $backupRootChecked = Assert-InProject $BackupRoot
                    if ($oldBackup.StartsWith($backupRootChecked, [System.StringComparison]::OrdinalIgnoreCase) -and
                        (Test-Path -LiteralPath $oldBackup)) {
                        Remove-Item -LiteralPath $oldBackup -Force
                    }
                }
                catch {
                }
            }
        }
        $state.backups = @($kept | Sort-Object backedUpAt)
    }

    Save-Manifest $state

    $reportObject = [ordered]@{
        generatedAt = (Get-Date).ToUniversalTime().ToString("o")
        watch = [bool]$Watch
        roots = @($Root)
        backupRoot = $BackupRoot.Replace([char]92, [char]47)
        summary = [ordered]@{
            scanned = $scanned
            backedUp = $backups.Count
            unchanged = $unchanged
            errors = $errors.Count
        }
        backups = @($backups)
        errors = @($errors)
    }
    Write-JsonFile $Report $reportObject
    return $reportObject
}

if ($Watch) {
    if (Test-WatcherAlreadyRunning) {
        Write-JsonFile $Report ([ordered]@{
            generatedAt = (Get-Date).ToUniversalTime().ToString("o")
            watch = $true
            alreadyRunning = $true
            summary = [ordered]@{
                scanned = 0
                backedUp = 0
                unchanged = 0
                errors = 0
            }
            backups = @()
            errors = @()
        })
        exit 0
    }

    Write-WatcherLock
    while ($true) {
        try {
            Invoke-JsonBackupScan | Out-Null
        }
        catch {
            Write-JsonFile $Report ([ordered]@{
                generatedAt = (Get-Date).ToUniversalTime().ToString("o")
                watch = $true
                summary = [ordered]@{
                    scanned = 0
                    backedUp = 0
                    unchanged = 0
                    errors = 1
                }
                backups = @()
                errors = @([ordered]@{ path = ""; message = $_.Exception.Message })
            })
        }
        Start-Sleep -Seconds ([Math]::Max(1, $Interval))
    }
}

Invoke-JsonBackupScan | Out-Null
