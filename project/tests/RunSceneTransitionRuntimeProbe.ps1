param(
    [string]$LogPath = (Join-Path $env:TEMP 'cg2-scene-transition-runtime.log'),
    [int]$TimeoutSeconds = 600,
    [switch]$Debugger,
    [switch]$AttackLifecycle,
    [switch]$AttackEditor,
    [switch]$Cinematic,
    [switch]$FlipPair,
    [switch]$GimmickProperties,
    [switch]$EditorEvents,
    [switch]$AttackPreviewRegistry,
    [switch]$PreviewComposition,
    [switch]$StatusPreview,
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
$previousEditorEvents = $env:CG2_EDITOR_EVENT_TEST
$previousPreviewRegistry = $env:CG2_ATTACK_PREVIEW_REGISTRY_TEST
$previousComposition = $env:CG2_PREVIEW_COMPOSITION_TEST
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
if ($FlipPair -or $GimmickProperties -or $GateEntry -or $EditorEvents -or $AttackPreviewRegistry -or $PreviewComposition -or $StatusPreview -or $AttackEditor) {
    # 高速落下の検証に失敗して残機が減っても、既存のセーブを検証前へ戻します。
    Get-ChildItem -LiteralPath $saveDirectory -File -ErrorAction SilentlyContinue | ForEach-Object {
        $flipSaveSnapshot[$_.Name] = [IO.File]::ReadAllBytes($_.FullName)
    }
}
# 起動時の既存カタログ同期も、検証後に元の内容へ戻します。
$presetPath = Join-Path $projectPath 'Resources/json/preset/presets.json'
$presetBytes = [IO.File]::ReadAllBytes($presetPath)
$statusPath = Join-Path $projectPath 'Resources/json/gameplay/status_settings.json'
$statusBytes = [IO.File]::ReadAllBytes($statusPath)
$previousStatusPreview = $env:CG2_STATUS_PREVIEW_TEST
$gameProcess = $null
try {
    $env:CG2_STATUS_PREVIEW_TEST = if ($StatusPreview) { '1' } else { $null }
    # テスト用環境変数は、このスクリプトから起動する子プロセスだけに渡します。
    $env:CG2_SCENE_TRANSITION_TEST_LOG = $LogPath
    if ($PreviewComposition) { $env:CG2_PREVIEW_COMPOSITION_TEST = '1' }
    else { $env:CG2_PREVIEW_COMPOSITION_TEST = $null }
    if ($AttackPreviewRegistry) { $env:CG2_ATTACK_PREVIEW_REGISTRY_TEST = '1' }
    else { $env:CG2_ATTACK_PREVIEW_REGISTRY_TEST = $null }
    if ($EditorEvents) { $env:CG2_EDITOR_EVENT_TEST = '1' }
    else { $env:CG2_EDITOR_EVENT_TEST = $null }
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
        [IO.File]::WriteAllText($commandsPath, "sxe -c `".ecxr; kv; q`" av`r`nsxe -c `".ecxr; kv; q`" 80000003`r`ng`r`n")
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
    $debuggerFailed = $false
    if ($Debugger) {
        # CDBが起動直後に止まる既定のブレークは除き、実行開始後の例外を検査します。
        $debuggerOutput = Get-Content -LiteralPath ($LogPath + '.debugger.txt') -Raw
        $runOutput = ($debuggerOutput -split '(?m)^\d+:\d+> g\r?\n', 2)[-1]
        $debuggerFailed = $runOutput -match 'Access violation - code|second chance|Heap block at|Break instruction exception - code'
    }
    $expectedPass = if ($StatusPreview) { 'PASS: integrated character status' }
        elseif ($PreviewComposition) { 'PASS: registered preview spawns and copy abilities' }
        elseif ($AttackEditor) { 'PASS: editor auto-play, pause, frame-step' }
        else { 'PASS:' }
    $passed = $result -match ('(?m)^' + [regex]::Escape($expectedPass))
    if ($gameProcess.ExitCode -ne 0 -or -not $passed -or $debuggerFailed) {
        throw 'シーン遷移の実行確認に失敗しました。'
    }
}
finally {
    $env:CG2_STATUS_PREVIEW_TEST = $previousStatusPreview
    [IO.File]::WriteAllBytes($statusPath, $statusBytes)
    $env:CG2_SCENE_TRANSITION_TEST_LOG = $previousLogPath
    $env:CG2_EDITOR_EVENT_TEST = $previousEditorEvents
    $env:CG2_ATTACK_PREVIEW_REGISTRY_TEST = $previousPreviewRegistry
    $env:CG2_PREVIEW_COMPOSITION_TEST = $previousComposition
    [IO.File]::WriteAllBytes($presetPath, $presetBytes)
    $env:CG2_ATTACK_LIFECYCLE_TEST = $previousAttackMode
    $env:CG2_ATTACK_EDITOR_TEST = $previousEditorMode
    $env:CG2_CINEMATIC_TEST = $previousCinematicMode
    $env:CG2_FLIP_PAIR_TEST = $previousFlipMode
    $env:CG2_GIMMICK_PROPERTY_TEST = $previousGimmickMode
    $env:CG2_GATE_ENTRY_TEST = $previousGateMode
    if ($AttackLifecycle) { [IO.File]::WriteAllBytes($attackPath, $attackBytes) }
    if ($FlipPair -or $GimmickProperties -or $GateEntry -or $EditorEvents -or $AttackPreviewRegistry -or $PreviewComposition -or $StatusPreview -or $AttackEditor) {
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
