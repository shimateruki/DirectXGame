[CmdletBinding()]
param(
    [switch]$NoPause,
    [switch]$CheckOnly
)

# Windows PowerShell 5.1 で日本語を正しく読むため、UTF-8 with BOM で保存する。
$ErrorActionPreference = 'Stop'
$exitCode = 0
$step = '開始'

try {
    Write-Host '--- 環境確認を開始します ---'

    # Step 1: Python 本体を起動できるか確認する。Windows の起動エイリアスだけでは成功としない。
    $step = 'Step 1: Python の確認'
    Write-Host $step
    $pythonPath = $null
    $pythonPrefix = @()
    $pythonFailures = @()
    foreach ($candidate in @(@{ Name = 'python'; Prefix = @() }, @{ Name = 'py'; Prefix = @('-3') })) {
        $commands = @(Get-Command -Name $candidate.Name -CommandType Application -All -ErrorAction SilentlyContinue)
        foreach ($command in $commands) {
            $candidatePath = $command.Source
            $candidatePrefix = @($candidate.Prefix)
            $versionOutput = $null
            $versionExitCode = $null
            try {
                # 外部プログラムの標準エラーも診断文として受け取り、終了コードを確認する。
                $oldErrorActionPreference = $ErrorActionPreference
                $ErrorActionPreference = 'Continue'
                $versionOutput = & $candidatePath @candidatePrefix --version 2>&1
                $versionExitCode = $LASTEXITCODE
            }
            catch {
                $versionOutput = $_.Exception.Message
            }
            finally {
                $ErrorActionPreference = $oldErrorActionPreference
            }

            # ErrorRecord の詳細表示をそのまま出すと長いため、本文だけを使う。
            $versionText = (@($versionOutput | ForEach-Object { $_.ToString().Trim() }) -join ' ').Trim()
            if ($versionExitCode -eq 0 -and $versionText -match '^Python\s+\d') {
                $pythonPath = $candidatePath
                $pythonPrefix = $candidatePrefix
                Write-Host "OK: $versionText ($candidatePath)"
                break
            }
            if ($versionExitCode -eq 9009 -and $candidatePath -match '\\Microsoft\\WindowsApps\\python\.exe$') {
                $pythonFailures += "python ($candidatePath): Windows の起動エイリアスが見つかりましたが、Python 本体は起動できません (終了コード 9009)"
                continue
            }
            if ($versionText.Length -gt 240) {
                $versionText = $versionText.Substring(0, 240) + '...'
            }
            $pythonFailures += "$($candidate.Name) ($candidatePath): 終了コード $versionExitCode / 出力: $versionText"
        }
        if ($null -ne $pythonPath) { break }
    }
    if ($null -eq $pythonPath) {
        if ($pythonFailures.Count -eq 0) {
            throw 'Python が見つかりません。Python 本体のインストールと PATH の設定を確認してください。'
        }
        throw "Python 本体を起動できません。$($pythonFailures -join ' | ')。Python 本体のインストールと PATH の設定を確認してください。"
    }

    # Step 2: Git も、表示内容だけでなく終了コードを確認する。
    $step = 'Step 2: Git の確認'
    Write-Host $step
    $gitCommand = Get-Command -Name 'git' -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -eq $gitCommand) {
        throw 'git コマンドが見つかりません。GitのインストールとPATHの設定を確認してください。'
    }
    $gitVersion = & $gitCommand.Source --version
    if ($LASTEXITCODE -ne 0) {
        throw "Git のバージョン確認に失敗しました (終了コード: $LASTEXITCODE)。"
    }
    $gitVersionText = ($gitVersion | Out-String).Trim()
    if ([string]::IsNullOrWhiteSpace($gitVersionText)) {
        throw 'Git のバージョンが表示されませんでした。'
    }
    Write-Host "OK: $gitVersionText"

    # Step 3: 一覧ファイルを確認し、実際にライブラリを読み込んでみる。
    $step = 'Step 3: 依存ライブラリの確認'
    Write-Host $step
    $requirementsPath = Join-Path -Path $PSScriptRoot -ChildPath 'requirements.txt'
    if (-not (Test-Path -LiteralPath $requirementsPath -PathType Leaf)) {
        throw "requirements.txt が見つかりません: $requirementsPath"
    }

    $importCode = 'import googleapiclient, google_auth_httplib2, google_auth_oauthlib'
    & $pythonPath @pythonPrefix -c $importCode
    $importExitCode = $LASTEXITCODE

    if ($importExitCode -eq 0) {
        Write-Host 'OK: 必要なライブラリを読み込めました。'
    }
    else {
        if ($CheckOnly) {
            throw "ライブラリを読み込めませんでした (終了コード: $importExitCode)。CheckOnly のため導入しません。"
        }

        $step = 'Step 3: 依存ライブラリの導入'
        Write-Host '読み込みに失敗したため、requirements.txt から導入します。'
        & $pythonPath @pythonPrefix -m pip --version
        if ($LASTEXITCODE -ne 0) {
            throw "pip を起動できませんでした (終了コード: $LASTEXITCODE)。"
        }

        & $pythonPath @pythonPrefix -m pip install -r $requirementsPath
        if ($LASTEXITCODE -ne 0) {
            throw "pip install に失敗しました (終了コード: $LASTEXITCODE)。"
        }

        # 「インストール完了」の表示前に、同じ Python で再度 import する。
        $step = 'Step 3: 導入結果の確認'
        & $pythonPath @pythonPrefix -c $importCode
        if ($LASTEXITCODE -ne 0) {
            throw "導入後もライブラリを読み込めません (終了コード: $LASTEXITCODE)。"
        }
        Write-Host 'OK: 導入後の読み込みに成功しました。'
    }

    Write-Host '--- 環境確認と準備が完了しました ---'
    if (-not $CheckOnly) {
        $step = 'Step 4: Gitからの取得と成果物の配置'
        . (Join-Path $PSScriptRoot 'Collect-GitSource.ps1')
        Invoke-PortfolioGitCollection -GitPath $gitCommand.Source -ConfigPath (Join-Path $PSScriptRoot 'PortfolioPipeline.config.json')
    }
}
catch {
    $exitCode = 1
    Write-Host "失敗 ($step): $($_.Exception.Message)" -ForegroundColor Red
}
finally {
    if (-not $NoPause) {
        Read-Host '結果を確認したら Enter キーを押してください' | Out-Null
    }
}

exit $exitCode
