param(
    [string]$LogPath = (Join-Path $env:TEMP 'cg2-scene-transition-runtime.log'),
    [int]$TimeoutSeconds = 600,
    [switch]$Debugger,
    [switch]$AttackLifecycle,
    [switch]$AttackEditor
)
$ErrorActionPreference = 'Stop'
$projectPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exePath = Join-Path (Split-Path $projectPath -Parent) 'generated/outputs/Development/DirectXGame.exe'
$configPath = Join-Path $projectPath 'Resources/json/user_config.json'
if (Get-Process DirectXGame -ErrorAction SilentlyContinue) {
    throw 'ゲームを終了してから実行してください。別のゲームプロセスは終了しません。'
}
$configExisted = Test-Path -LiteralPath $configPath
$configBytes = if ($configExisted) { [IO.File]::ReadAllBytes($configPath) } else { $null }
$previousLogPath = $env:CG2_SCENE_TRANSITION_TEST_LOG
$previousAttackMode = $env:CG2_ATTACK_LIFECYCLE_TEST
$previousEditorMode = $env:CG2_ATTACK_EDITOR_TEST
$attackPath = Join-Path $projectPath 'Resources/json/enemy_attack/fire_slime.json'
$attackBytes = if ($AttackLifecycle) { [IO.File]::ReadAllBytes($attackPath) } else { $null }
$gameProcess = $null
try {
    # テスト用環境変数は、このスクリプトから起動する子プロセスだけに渡します。
    $env:CG2_SCENE_TRANSITION_TEST_LOG = $LogPath
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
    $result = Get-Content -LiteralPath $LogPath -Raw
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
    if ($AttackLifecycle) { [IO.File]::WriteAllBytes($attackPath, $attackBytes) }
    # 最後に開いたシーンなど、検証のために変わった起動設定を元のバイト列へ戻します。
    if ($configExisted) { [IO.File]::WriteAllBytes($configPath, $configBytes) }
    elseif (Test-Path -LiteralPath $configPath) { Remove-Item -LiteralPath $configPath }
}
