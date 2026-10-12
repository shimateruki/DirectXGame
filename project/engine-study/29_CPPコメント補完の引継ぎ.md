# C++ コメント補完の引継ぎ

記録日: 2026-10-12

## この区切りの状態

`game` と `engine` の `.cpp` を対象に、実装の前提、状態の寿命、保存とプレビューの違い、失敗時の扱いが読み取りにくい関数から日本語コメントを追加した。今回の変更はコメントと、ごく一部の一行関数の整形に限り、処理分岐や保存形式の変更はしていない。

**全ファイルの点検は未完了。** 「全関数の内部に必要な説明がある」状態だとはまだ言えない。以前の全クラス点検記録である [16_全クラスコメント再点検.md](16_全クラスコメント再点検.md) は型の説明を中心とした記録であり、今回の関数内コメントの完了証明には使わない。

## この区切りで重点的に確認した箇所

| 範囲 | 追加した説明の中心 |
| --- | --- |
| `game/system`、`game/actor`、`game/scene` | チュートリアルの復元、弾と命中演出の寿命、ギミックの再生状態、敵攻撃の形態と演出の順序 |
| `engine/utility/SystemDebug/editor/windows/AnimationWorkbench.cpp` | キー補間、モデル切替、一時姿勢とJSONの関係 |
| `engine/utility/SystemDebug/editor/windows/EnemyAttackPreviewWindow*.cpp` | プレビューObjectの寿命、シーク、Undo単位、配置座標 |
| `engine/utility/SystemDebug/editor/core/AssetDatabase*.cpp` | GUIDとパス、`.meta`、退避と移動の巻き戻し、永続索引 |
| `engine/utility/SystemDebug/editor/core/DebugEditor*.cpp` | Sceneの選択、Prefab編集、保存確認、クラッシュ復元、配置 |
| `engine/utility/SystemDebug/editor/windows/replay/ReplayDebugger*.cpp` | Scene世代の照合、巻き戻し時の一時Object、履歴の保存 |
| `engine/graphics`、`engine/system` | LOD、特殊材質、光源、PostEffect、ローディング画面、破片エフェクト |
| `engine/utility/SystemDebug/editor` の各ツール | Scene配置集計、検索、撮影、VFX、Animator Controller、Sprite編集、保存前プレビュー |

コメントはコードを言い換えるだけにならないよう、実装を読んで理由・前提・所有先を優先した。たとえば `EditorQuickSearch::CollectAssets` は、モデル以外のAssetを「開く」のではなく「パスをコピーする」実装なので、その動作に合わせて説明を修正した。

## 次回の優先対象

以下は **2026-10-12 の簡易検出で、関数内にコメントが見つからなかった候補**。左の件数は「10行以上の本文を持つ候補 / 全候補」であり、正確な未説明件数ではない。短いアクセサーや空のスタブも含む。

| 優先 | ファイル | 検出候補 | 読むべき点 |
| --- | --- | ---: | --- |
| 1 | `engine/core/editor/GameEditorController.Workspace.cpp` | 4 / 4 | ワークスペース切替とレイアウト復元の順序 |
| 1 | `engine/graphics/2d/Sprite.cpp` | 4 / 13 | 親子座標、Replay状態、描画前提 |
| 1 | `engine/graphics/3d/camera/Camera.cpp` | 4 / 14 | カメラ上書き、Replay、角度単位 |
| 1 | `engine/graphics/3d/material/MaterialInstance.cpp` | 4 / 7 | 継承した材質の解決、保存と適用 |
| 1 | `engine/graphics/effect/DecalSystem.cpp` | 4 / 6 | 表面法線、深度オフセット、Scene所有権 |
| 1 | `engine/graphics/effect/EffectObject3d.cpp` | 4 / 5 | 手続きメッシュの頂点・法線・UV |
| 1 | `engine/graphics/particle/ParticleSystem.cpp` | 4 / 8 | 粒子寿命と描画バッファ |
| 1 | `engine/graphics/particle/VFXSequencer.cpp` | 4 / 18 | Event発火、再生時間、停止後の状態 |
| 1 | `engine/graphics/postprocess/Fade.cpp` | 4 / 15 | フェード完了とシーン遷移の契約 |
| 1 | `engine/system/input/InputManager.cpp` | 4 / 6 | 入力デバイス、押下エッジ、接続切替 |
| 2 | `engine/utility/SystemDebug/editor/core/EditorToolSettings.cpp` | 4 / 15 | ツール設定の永続化と既定値 |
| 2 | `engine/utility/SystemDebug/editor/core/PresetEditor.Catalog.cpp` | 4 / 19 | 既存Presetとの紐付け、カテゴリ |
| 2 | `engine/utility/SystemDebug/editor/vfx/TrailEmitterEditor.cpp` | 4 / 6 | 保存値と試用値の違い |
| 2 | `engine/utility/SystemDebug/editor/windows/ProjectWindow.cpp` | 4 / 6 | Assetの選択と配置 |
| 2 | `engine/utility/SystemDebug/editor/windows/SceneSavePreview.cpp` | 4 / 8 | 差分生成と保存前確認 |
| 2 | `engine/utility/SystemDebug/editor/core/EditorPropertyRegistry.cpp` | 3 / 18 | Property適用先と検証 |
| 2 | `engine/utility/SystemDebug/editor/windows/AssetReferenceExplorer.cpp` | 3 / 14 | 参照探索と未解決候補 |

上記以外にも、エンジン側の **79ファイル・443メソッド候補、そのうち10行以上が119件** と検出された。これは関数名に `Class::Method` があり、本文に `//` も `/*` もない場合だけを数える粗い調査である。**自由関数、ラムダ、複雑な定義、ヘッダー内実装、コメントはあるが内容が不足する関数は拾えていない。** 逆に短い自明な関数も候補へ入る。件数をゼロにすることだけを目的に定型コメントを追加しない。

`game` 側の同じ形式のクラスメソッド候補はゼロになったが、これも `game` 全体の完了証明ではない。自由関数とヘッダー内実装を再確認する。`tools/TextPngTool/TextPngTool.cpp` と `tests/*.cpp` も今回の再点検対象に含めていない。`externals` は外部ライブラリなので編集対象から外す。

## 次回の進め方

1. 上表の優先1から、本文と呼び出し元を読み、必要な説明を関数内へ追加する。特に単位、所有権、失敗時、Scene切替時の前提を確認する。
2. 簡易検出に頼り切らず、自由関数とヘッダー内実装、既存コメントの正確さを点検する。`game` 側も同じ基準で見直す。
3. 長い関数は入口だけで済ませず、状態が変わる分岐や復元処理に必要な説明を置く。短いアクセサーも未点検のままにせず、仕様上の前提を簡潔に記して可読性を保つ。
4. コードを変更した区切りごとに Development x64 をビルドする。ReleaseやDebugを試していない場合は、その事実を記録する。
5. UTF-8として読めるかと、文字化けの置換文字 `�` がないかを確認する。シェーダーを触る場合、そのコメントは英語にする。

## この区切りの検証と制限

- `DirectXGame.sln` の **Development x64** ビルド成功。`DirectXGame.exe` の生成を確認した。既存の `/LTCG` の性能通知は出たが、エラーではない。
- `engine` と `game` の `.cpp` **316ファイル**をUTF-8としてデコードし、失敗は0。置換文字 `�` の検索でも該当なし。
- **実際のゲーム画面・各Editor操作は今回は起動確認していない。** コメント追加のみのためビルドと文字コードを今回の確認範囲とした。
- Gitの追加・コミット・プッシュはしていない。既存の作業差分は保持する。
