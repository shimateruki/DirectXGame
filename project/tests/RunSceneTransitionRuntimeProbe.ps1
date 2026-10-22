param(
    [string]$LogPath = (Join-Path $env:TEMP 'cg2-scene-transition-runtime.log'),
    [int]$TimeoutSeconds = 600,
    [switch]$Debugger,
    [switch]$AttackLifecycle,
    [switch]$AttackEditor,
    [switch]$Cinematic,
    [switch]$FlipPair,
    [switch]$GimmickProperties,
    [switch]$GateEntry,
    [string]$ExecutablePath = ''
)
$ErrorActionPreference = 'Stop'
$projectPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exePath = Join-Path (Split-Path $projectPath -Parent) 'generated/outputs/Development/DirectXGame.exe'
if ($ExecutablePath) { $exePath = [IO.Path]::GetFullPath($ExecutablePath) }
$configPath = Join-Path $projectPath 'Resources/json/user_config.json'
# 別worktreeの同名EXEとは並行実行でき、同じ出力ファイルの二重起動だけを防ぎます。
if (Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($exePath)) -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and [IO.Path]::GetFullPath($_.Path) -eq $exePath }) {
    throw 'ゲームを終了してから実行してください。別のゲームプロセスは終了しません。'
}
$configExisted = Test-Path -LiteralPath $configPath
$configBytes = if ($configExisted) { [IO.File]::ReadAllBytes($configPath) } else { $null }
$previousLogPath = $env:CG2_SCENE_TRANSITION_TEST_LOG
$previousAttackMode = $env:CG2_ATTACK_LIFECYCLE_TEST
$previousEditorMode = $env:CG2_ATTACK_EDITOR_TEST
$previousCinematicMode = $env:CG2_CINEMATIC_TEST
$previousFlipMode = $env:CG2_FLIP_PAIR_TEST
$previousGimmickMode = $env:CG2_GIMMICK_PROPERTY_TEST
$previousGateMode = $env:CG2_GATE_ENTRY_TEST
$attackPath = Join-Path $projectPath 'Resources/json/enemy_attack/fire_slime.json'
$attackBytes = if ($AttackLifecycle) { [IO.File]::ReadAllBytes($attackPath) } else { $null }
$saveDirectory = Join-Path $projectPath 'Resources/json/save'
$flipSaveSnapshot = @{}
if ($FlipPair -or $GimmickProperties -or $GateEntry) {
    # 高速落下の検証に失敗して残機が減っても、既存のセーブを検証前へ戻します。
    Get-ChildItem -LiteralPath $saveDirectory -File -ErrorAction SilentlyContinue | ForEach-Object {
        $flipSaveSnapshot[$_.Name] = [IO.File]::ReadAllBytes($_.FullName)
    }
}
$gameProcess = $null
try {
    # テスト用環境変数は、このスクリプトから起動する子プロセスだけに渡します。
    $env:CG2_SCENE_TRANSITION_TEST_LOG = $LogPath
    if ($GateEntry) { $env:CG2_GATE_ENTRY_TEST = '1' }
    else { $env:CG2_GATE_ENTRY_TEST = $null }
    if ($GimmickProperties) { $env:CG2_GIMMICK_PROPERTY_TEST = '1' }
    else { $env:CG2_GIMMICK_PROPERTY_TEST = $null }
    if ($FlipPair) { $env:CG2_FLIP_PAIR_TEST = '1' }
    else { $env:CG2_FLIP_PAIR_TEST = $null }
    if ($Cinematic) { $env:CG2_CINEMATIC_TEST = '1' }
    else { $env:CG2_CINEMATIC_TEST = $null }
    if ($AttackEditor) { $env:CG2_ATTACK_EDITOR_TEST = '1' }
    else { $env:CG2_ATTACK_EDITOR_TEST = $null }
    if ($AttackLifecycle) { $env:CG2_ATTACK_LIFECYCLE_TEST = '1' }
    else { $env:CG2_ATTACK_LIFECYCLE_TEST = $null }
    [IO.File]::WriteAllText($LogPath, 'STARTING' + [Environment]::NewLine)
    if ($Debugger) {
        $debuggerPath = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
        $commandsPath = $LogPath + '.cdb.txt'
        [IO.File]::WriteAllText($commandsPath, "sxe -c `".ecxr; kv; q`" av`r`ng`r`n")
        $debuggerArguments = '-G -cf "' + $commandsPath + '" -logo "' + $LogPath + '.debugger.txt" "' + $exePath + '"'
        $gameProcess = Start-Process -FilePath $debuggerPath -ArgumentList $debuggerArguments -WorkingDirectory $projectPath -WindowStyle Hidden -PassThru -RedirectStandardOutput ($LogPath + '.stdout.txt') -RedirectStandardError ($LogPath + '.stderr.txt')
    } else {
        $gameProcess = Start-Process -FilePath $exePath -WorkingDirectory $projectPath -WindowStyle Hidden -PassThru
    }
    Write-Output "Runtime probe PID=$($gameProcess.Id) Log=$LogPath"
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while (-not $gameProcess.WaitForExit(1000)) {
        if ($timer.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
            # 自分がこの検証で起動したプロセスだけを終了します。
            Stop-Process -Id $gameProcess.Id -Force
            throw '実行確認が制限時間内に終了しませんでした。ログを確認してください。'
        }
    }
    [string]$result = Get-Content -LiteralPath $LogPath -Raw
    Write-Output $result
    Write-Output "ExitCode=$($gameProcess.ExitCode)"
    # PASSを書いた後のFinalizeで落ちた場合も、CDB自身の終了コード0で成功にしません。
    $debuggerFailed = $Debugger -and (Select-String -LiteralPath ($LogPath + '.debugger.txt') -Pattern 'Access violation - code|second chance' -Quiet)
    if ($gameProcess.ExitCode -ne 0 -or $result -notmatch '(?m)^PASS:' -or $debuggerFailed) {
        throw 'シーン遷移の実行確認に失敗しました。'
    }
}
finally {
    $env:CG2_SCENE_TRANSITION_TEST_LOG = $previousLogPath
    $env:CG2_ATTACK_LIFECYCLE_TEST = $previousAttackMode
    $env:CG2_ATTACK_EDITOR_TEST = $previousEditorMode
    $env:CG2_CINEMATIC_TEST = $previousCinematicMode
    $env:CG2_FLIP_PAIR_TEST = $previousFlipMode
    $env:CG2_GIMMICK_PROPERTY_TEST = $previousGimmickMode
    $env:CG2_GATE_ENTRY_TEST = $previousGateMode
    if ($AttackLifecycle) { [IO.File]::WriteAllBytes($attackPath, $attackBytes) }
    if ($FlipPair -or $GimmickProperties -or $GateEntry) {
        Get-ChildItem -LiteralPath $saveDirectory -File -ErrorAction SilentlyContinue | ForEach-Object {
            if (-not $flipSaveSnapshot.ContainsKey($_.Name)) { Remove-Item -LiteralPath $_.FullName }
        }
        foreach ($name in $flipSaveSnapshot.Keys) {
            [IO.File]::WriteAllBytes((Join-Path $saveDirectory $name), $flipSaveSnapshot[$name])
        }
    }
    # 最後に開いたシーンなど、検証のために変わった起動設定を元のバイト列へ戻します。
    if ($configExisted) { [IO.File]::WriteAllBytes($configPath, $configBytes) }
    elseif (Test-Path -LiteralPath $configPath) { Remove-Item -LiteralPath $configPath }
}
