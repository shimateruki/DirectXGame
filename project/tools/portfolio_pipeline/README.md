# ポートフォリオ用ソース収集ツール

`PortfolioPipeline.ps1` は、環境確認後にCG2のGitリポジトリからソースを取得し、提出用フォルダーに整理します。現在のPCで編集途中のファイルではなく、設定したリモートブランチに公開済みのコミットを取得します。

## 実行すること

1. `python --version` を実行します。失敗した場合は `py -3 --version` も試します。使えたPythonと `git --version` の終了コードを確認します。
2. スクリプトと同じフォルダーに `requirements.txt` があるか確認します。
3. 必要なPythonライブラリを実際に読み込みます。読み込めない場合は、通常実行では `python -m pip install -r requirements.txt` で導入します。
4. 導入後、同じPythonで再度読み込みを確認します。失敗した場合は理由と失敗終了コードを表示します。
5. `PortfolioPipeline.config.json` で指定したリポジトリとブランチを `Intermediate` にクローンします。Gitの終了コードとコミットIDを確認します。
6. 設定された場所からC++、シェーダー、JSON、Visual Studioプロジェクト定義などのソース形式だけを集め、`Generated/<提出名>/01 個人作品/ソースコード` に配置します。

`requirements.txt` は「必要なPythonライブラリの一覧」です。`pip` はそのライブラリを導入する道具です。導入には通常インターネット接続が必要で、実行中の `python` が使う環境に変更が加わります。

## 取得元と提出名

`PortfolioPipeline.config.json` の `repositoryUrl` は現在のCG2リポジトリ、`branch` は `kozin` に設定しています。`shallowClone: true` は最新状態だけを取得する指定です。過去のコミットも調べる必要があるときは `false` にします。`includePaths` は、クローンしたリポジトリの `project` 以下からコピーする場所です。

`submissionFolderName` の「LE3X_99 シメイ 作品」は授業資料の仮名です。**提出前に自分の学籍番号と名前に直してください。** フォルダー名だけを変更してもGitの取得元は変わりません。実行ファイル、モデル、画像、外部ライブラリはソース中心の提出物には含めません。そのため、この成果物だけでゲームをビルドすることは目的としていません。

途中作業は `Intermediate`、完成品は `Generated`、直近の実行記録は `Logs/last-run.json` に置きます。これらはこのフォルダーの `.gitignore` でGitの追跡対象から外します。既存の成果物は新しい取得とコピーが成功するまで維持します。失敗時には `last-run.json` の `cloneExitCode`、`reason`、`outputExistedBefore`、`outputExistsAfter` を確認してください。

## 使い方

エクスプローラーから使うときは、`Run-PortfolioPipeline.cmd` をダブルクリックします。結果が表示されたまま止まり、キーを押すと閉じます。このランチャーは起動したPowerShellプロセスにだけ `RemoteSigned` を指定します。PCに保存された実行ポリシーは変更しません。

Windows PowerShell 5.1 でこのフォルダーを開き、次を実行します。

```powershell
powershell.exe -NoProfile -ExecutionPolicy RemoteSigned -File .\PortfolioPipeline.ps1
```

このPCは `LocalMachine = AllSigned` なので、`.ps1` を右クリックして「PowerShell で実行」すると、スクリプトが始まる前に拒否され、ウィンドウがすぐ閉じます。上のランチャーかコマンドを使用してください。

ライブラリのインストールやGit取得を行わず、環境の確認だけを行う場合:

```powershell
powershell.exe -NoProfile -ExecutionPolicy RemoteSigned -File .\PortfolioPipeline.ps1 -CheckOnly
```

別のスクリプトから呼ぶ場合は `-NoPause` を付けるとEnterキー待ちを省けます。成功時の終了コードは `0`、失敗時は `1` です。

```powershell
powershell.exe -NoProfile -ExecutionPolicy RemoteSigned -File .\PortfolioPipeline.ps1 -NoPause -CheckOnly
$LASTEXITCODE
```

最初は `C:\Users\mgapj\AppData\Local\Microsoft\WindowsApps\python.exe` が終了コード `9009` を返し、Python本体を起動できませんでした。2026年9月29日にPython install managerとPython 3.14.7を導入し、課題スクリプトによるライブラリの自動導入まで確認しました。

別のPCでPython本体が未導入なら、[Python公式のWindows向け手順](https://docs.python.org/3/using/windows.html)に従ってPython install managerとPythonランタイムを導入してください。導入後は新しいPowerShellを開き、`py -3 --version` または `python --version` でバージョンが表示されることを確認してから、このランチャーを実行します。既にPython本体を導入済みなら、起動先のエイリアス設定と `PATH` を確認してください。`PATH` は、Windowsがコマンドを探すフォルダーの一覧です。Python自体のインストールは、この課題スクリプトの対象外です。

## 安全な再実行

`Intermediate` が残っている場合は、ツールの所有印を確認してから作り直します。所有印のないフォルダーや、別のプログラムが作った成果物は削除・上書きせずに停止します。クローン失敗時は既存の `Generated` を維持します。成功時の `source-info.json` に取得元、ブランチ、コミットID、ファイル数を記録します。

この段階ではZIP作成とGoogle Driveへのアップロードは行いません。
