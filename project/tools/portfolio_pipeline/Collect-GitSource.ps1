<#
提出用に指定ブランチのソースだけを別の作業領域へ取得する内部処理。
コピー対象を設定の includePaths と許可拡張子へ絞り、既存の提出物は完成後に入れ替える。
#>
function Assert-PortfolioChildPath {
    param([string]$Parent, [string]$Child)
    $parentFull = [System.IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    $childFull = [System.IO.Path]::GetFullPath($Child)
    if (-not $childFull.StartsWith($parentFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "作業フォルダーの外を指すパスです: $childFull"
    }
    return $childFull
}

function Assert-PortfolioNormalDirectory {
    param([string]$Path)
    if (Test-Path -LiteralPath $Path) {
        $item = Get-Item -LiteralPath $Path -Force
        if (-not $item.PSIsContainer -or ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
            throw "通常のフォルダーではありません: $Path"
        }
    }
}

function Assert-PortfolioOwnedIntermediate {
    param([string]$Path)
    Assert-PortfolioNormalDirectory $Path
    $marker = Join-Path $Path '.portfolio-pipeline-owned'
    if (-not (Test-Path -LiteralPath $marker -PathType Leaf) -or
        (Get-Content -LiteralPath $marker -Raw -Encoding ASCII).Trim() -ne 'PortfolioPipeline') {
        throw "所有印のない Intermediate は削除しません: $Path"
    }
    $gitProcesses = @(Get-Process -Name git,git-remote-https,git-lfs -ErrorAction SilentlyContinue)
    if ($gitProcesses.Count -gt 0) {
        throw 'Gitプロセスが実行中のため Intermediate を削除しません。'
    }
    foreach ($item in @(Get-ChildItem -LiteralPath $Path -Recurse -Force)) {
        if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
            throw "リンクを含む Intermediate は削除しません: $($item.FullName)"
        }
        if ($item.Name -eq 'index.lock' -and $item.DirectoryName -match '[\\/]\.git$') {
            throw "Gitのロックが残っているため Intermediate を削除しません: $($item.FullName)"
        }
    }
}

function Invoke-PortfolioGitCollection {
    param([string]$GitPath, [string]$ConfigPath)

    $config = Get-Content -LiteralPath $ConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ([string]::IsNullOrWhiteSpace($config.repositoryUrl) -or
        [string]::IsNullOrWhiteSpace($config.branch) -or
        [string]::IsNullOrWhiteSpace($config.submissionFolderName) -or
        [string]::IsNullOrWhiteSpace($config.sourceRoot) -or
        @($config.includePaths).Count -eq 0) {
        throw 'PortfolioPipeline.config.json の必須項目が不足しています。'
    }
    if ($config.branch -notmatch '^[A-Za-z0-9._/-]+$' -or $config.branch.Contains('..')) {
        throw 'branch に使用できない文字が含まれています。'
    }
    if ($config.submissionFolderName -match '[\\/:*?"<>|]' -or
        $config.submissionFolderName -in @('.', '..')) {
        throw 'submissionFolderName は1つのフォルダー名にしてください。'
    }
    $relativePaths = @($config.sourceRoot) + @($config.includePaths)
    foreach ($relativePath in $relativePaths) {
        if ([string]::IsNullOrWhiteSpace($relativePath) -or
            [System.IO.Path]::IsPathRooted($relativePath) -or
            $relativePath -match '(^|[\\/])\.\.([\\/]|$)' -or
            $relativePath.Contains(':')) {
            throw "取得元の相対パスが不正です: $relativePath"
        }
    }

    $toolRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
    $intermediateDir = Assert-PortfolioChildPath $toolRoot (Join-Path $toolRoot 'Intermediate')
    $generatedDir = Assert-PortfolioChildPath $toolRoot (Join-Path $toolRoot 'Generated')
    $submissionDir = Assert-PortfolioChildPath $generatedDir (Join-Path $generatedDir $config.submissionFolderName)
    $outputDir = Assert-PortfolioChildPath $submissionDir (Join-Path $submissionDir '01 個人作品')
    $cloneDir = Assert-PortfolioChildPath $intermediateDir (Join-Path $intermediateDir '01_個人作品\Source')
    $preparedDir = Assert-PortfolioChildPath $intermediateDir (Join-Path $intermediateDir 'Prepared\01 個人作品')
    $backupDir = Assert-PortfolioChildPath $intermediateDir (Join-Path $intermediateDir 'PreviousOutput')
    $intermediateMarker = Join-Path $intermediateDir '.portfolio-pipeline-owned'
    $outputManifest = Join-Path $outputDir 'source-info.json'
    $logDir = Assert-PortfolioChildPath $toolRoot (Join-Path $toolRoot 'Logs')
    $runLog = [ordered]@{
        startedAt = (Get-Date).ToString('o')
        repositoryUrl = $config.repositoryUrl
        branch = $config.branch
        shallowClone = [bool]$config.shallowClone
        cloneExitCode = $null
        commit = $null
        sourceFileCount = 0
        outputPath = $outputDir
        outputExistedBefore = (Test-Path -LiteralPath $outputDir)
        outputExistsAfter = $false
        result = '失敗'
        reason = $null
    }
    $published = $false

    try {
        # 以前の失敗で残った作業場所だけを、所有印とパスの両方で確認して消す。
        if (Test-Path -LiteralPath $intermediateDir) {
            Assert-PortfolioOwnedIntermediate $intermediateDir
            Remove-Item -LiteralPath $intermediateDir -Recurse -Force -ErrorAction Stop
        }
        New-Item -ItemType Directory -Path (Split-Path -Parent $cloneDir) -Force -ErrorAction Stop | Out-Null
        Set-Content -LiteralPath $intermediateMarker -Value 'PortfolioPipeline' -Encoding ASCII

        Write-Host "Step 4: Git から作品を取得します ($($config.branch))"
        $cloneArguments = @('clone')
        if ($config.shallowClone) {
            $cloneArguments += @('--depth', '1', '--single-branch')
        }
        $cloneArguments += @('--branch', [string]$config.branch, [string]$config.repositoryUrl, $cloneDir)
        $oldPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            & $GitPath @cloneArguments 2>&1 | ForEach-Object { Write-Host $_.ToString() }
            $runLog.cloneExitCode = $LASTEXITCODE
        }
        finally {
            $ErrorActionPreference = $oldPreference
        }
        if ($runLog.cloneExitCode -ne 0) {
            throw "git clone が終了コード $($runLog.cloneExitCode) で失敗しました。"
        }
        if (-not (Test-Path -LiteralPath (Join-Path $cloneDir '.git') -PathType Container)) {
            throw '取得先に .git がなく、正常なクローンと確認できません。'
        }

        $oldOptionalLocks = $env:GIT_OPTIONAL_LOCKS
        $env:GIT_OPTIONAL_LOCKS = '0'
        try {
            $commit = (& $GitPath -C $cloneDir rev-parse HEAD).Trim()
            if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[0-9a-f]{40}$') {
                throw '取得したコミットIDを確認できません。'
            }
            $runLog.commit = $commit
        }
        finally {
            if ($null -eq $oldOptionalLocks) {
                Remove-Item Env:GIT_OPTIONAL_LOCKS -ErrorAction SilentlyContinue
            }
            else {
                $env:GIT_OPTIONAL_LOCKS = $oldOptionalLocks
            }
        }
        $sourceRoot = Assert-PortfolioChildPath $cloneDir (Join-Path $cloneDir $config.sourceRoot)
        if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) {
            throw "取得元フォルダーが見つかりません: $sourceRoot"
        }

        # ソースに関係する形式だけを、設定ファイルに列挙された場所からコピーする。
        $sourceDestination = Join-Path $preparedDir 'ソースコード'
        New-Item -ItemType Directory -Path $sourceDestination -Force -ErrorAction Stop | Out-Null
        $allowedExtensions = @('.c', '.cpp', '.h', '.hpp', '.inl', '.hlsl', '.hlsli', '.fx',
            '.fxh', '.json', '.sln', '.vcxproj', '.filters', '.rc', '.md', '.ps1', '.py',
            '.bat', '.cmd', '.props', '.targets', '.natvis')
        foreach ($includePath in @($config.includePaths)) {
            $selectedPath = Assert-PortfolioChildPath $sourceRoot (Join-Path $sourceRoot $includePath)
            if (-not (Test-Path -LiteralPath $selectedPath)) {
                throw "指定したソースが見つかりません: $includePath"
            }
            $selectedItem = Get-Item -LiteralPath $selectedPath -Force
            if ($selectedItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
                throw "リンク先をたどる取得は行いません: $includePath"
            }
            $files = if ($selectedItem.PSIsContainer) {
                @(Get-ChildItem -LiteralPath $selectedPath -Recurse -File -Force)
            }
            else { @($selectedItem) }
            foreach ($file in $files) {
                if ($file.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
                    throw "リンク先をたどる取得は行いません: $($file.FullName)"
                }
                if ($allowedExtensions -notcontains $file.Extension.ToLowerInvariant()) { continue }
                $relativeFile = $file.FullName.Substring($sourceRoot.Length).TrimStart('\', '/')
                $targetFile = Assert-PortfolioChildPath $sourceDestination (Join-Path $sourceDestination $relativeFile)
                New-Item -ItemType Directory -Path (Split-Path -Parent $targetFile) -Force -ErrorAction Stop | Out-Null
                Copy-Item -LiteralPath $file.FullName -Destination $targetFile -ErrorAction Stop
                $runLog.sourceFileCount++
            }
        }
        if ($runLog.sourceFileCount -eq 0) {
            throw '提出対象のソースファイルが1件もありません。'
        }

        $manifest = [ordered]@{
            generatedBy = 'PortfolioPipeline'
            generatedAt = (Get-Date).ToString('o')
            repositoryUrl = $config.repositoryUrl
            branch = $config.branch
            commit = $runLog.commit
            shallowClone = [bool]$config.shallowClone
            sourceFileCount = $runLog.sourceFileCount
            includePaths = @($config.includePaths)
        }
        [System.IO.File]::WriteAllText((Join-Path $preparedDir 'source-info.json'),
            ($manifest | ConvertTo-Json -Depth 5), (New-Object System.Text.UTF8Encoding($false)))

        # 前回の完成品を一時退避し、新しい完成品を置けたときだけ入れ替える。
        Assert-PortfolioNormalDirectory $generatedDir
        Assert-PortfolioNormalDirectory $submissionDir
        Assert-PortfolioNormalDirectory $outputDir
        New-Item -ItemType Directory -Path $submissionDir -Force -ErrorAction Stop | Out-Null
        if (Test-Path -LiteralPath $outputDir) {
            if (-not (Test-Path -LiteralPath $outputManifest -PathType Leaf)) {
                throw "所有印のない成果物は上書きしません: $outputDir"
            }
            $oldManifest = Get-Content -LiteralPath $outputManifest -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($oldManifest.generatedBy -ne 'PortfolioPipeline') {
                throw "このツールが作った成果物ではないため上書きしません: $outputDir"
            }
            Move-Item -LiteralPath $outputDir -Destination $backupDir -ErrorAction Stop
        }
        try {
            Move-Item -LiteralPath $preparedDir -Destination $outputDir -ErrorAction Stop
            $published = $true
        }
        catch {
            if (Test-Path -LiteralPath $backupDir) {
                Move-Item -LiteralPath $backupDir -Destination $outputDir -ErrorAction Stop
            }
            throw
        }
        $runLog.result = '成功'
        Write-Host "OK: $($runLog.sourceFileCount) ファイルを配置しました。"
        Write-Host "コミット: $($runLog.commit)"
        Write-Host "成果物: $outputDir"
    }
    catch {
        $runLog.reason = $_.Exception.Message
        throw
    }
    finally {
        $runLog.outputExistsAfter = Test-Path -LiteralPath $outputDir
        try {
            Assert-PortfolioNormalDirectory $logDir
            New-Item -ItemType Directory -Path $logDir -Force -ErrorAction Stop | Out-Null
            [System.IO.File]::WriteAllText((Join-Path $logDir 'last-run.json'),
                ($runLog | ConvertTo-Json -Depth 5), (New-Object System.Text.UTF8Encoding($false)))
        }
        catch {
            Write-Warning "実行記録を保存できませんでした: $($_.Exception.Message)"
        }
        if ($published) {
            try {
                if (Test-Path -LiteralPath $intermediateDir) {
                    Assert-PortfolioOwnedIntermediate $intermediateDir
                    Remove-Item -LiteralPath $intermediateDir -Recurse -Force -ErrorAction Stop
                }
            }
            catch {
                Write-Warning "中間ファイルを削除できませんでした: $($_.Exception.Message)"
            }
        }
    }
}
