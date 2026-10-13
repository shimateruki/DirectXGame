# C++コメント再点検で見つかった構成課題

調査開始日: 2026-10-13

コメントと空行の補完中に見つかった、説明を増やすだけでは解消しない構成上の問題を記録する。**この作業では挙動を変える修正は行わない。** 行番号は2026-10-13の作業時点であり、確認は静的なコード読解による。

## 優先して直す候補

### 1. ギミック選択UIの型名と表示ラベルが対応していない【高・実害候補】

- `engine/utility/SystemDebug/editor/windows/inspector/InspectorWindow.Gimmick.cpp` の `DrawGimmickTypeSelector` は、型名の配列が**29件**、表示ラベルの配列が**36件**。`ImGui::Combo`には表示ラベルの件数を渡し、選択後は同じ添字で型名の配列を読む。
- 表示ラベルには「スターランチャー」など、型名配列にない項目が途中から混ざる。途中の選択肢は別の型へずれ、末尾の選択肢は型名配列外を読む可能性がある。これはソース上の件数と添字の関係から判断したもので、UI操作による発生確認は未実施。
- 対応案: 型名とラベルを一つのレコード配列にまとめ、選択数もその配列から求める。実装する際は既存Scene JSONの型名、生成Factory、ギミック別の初期値との対応を確認する。

### 2. ゲームプレイの先読み対象が複数の手書き一覧に集中する【中】

- `game/scene/gameplay/GamePlayScene.Setup.cpp` 冒頭に操作ガイド画像、MeshEffect、GPU粒子、VFX Sequence、破片の一覧が数百行ある。`BuildAsyncLoadManifest`と`InitializeLoadingStep`は同じ一覧を別の目的で参照し、フェーズ番号・進捗の固定単位数も手で対応させている。
- 新しい演出の素材を追加すると、使用箇所だけでなく先読み一覧、実際の管理器の初期化順、進捗件数を確認する必要がある。抜けると初回発動時の読込やロード進捗のずれが起こり得るが、現時点で発生を確認したわけではない。
- 対応案: 演出の所有側から必要素材を宣言する仕組みか、素材種別と読込方法を持つマニフェストへ寄せる。Scene JSONの参照を自動抽出する案だけでは、攻撃開始時に初めて使う素材を拾えない点に注意する。

### 3. 各シーンの3D描画パスに同形の判定が分散する【中】

- `GamePlayScene::Draw`、`TutorialScene::Draw`、`GameSelectScene::Draw` は近距離のプレイヤー子階層除外、不透明／半透明の振り分け、霧と特殊材質、GPU粒子の順をそれぞれ管理する。`GamePlayScene::Draw`内でも子階層をたどる判定が複数のパスに重複する。
- 材質の追加・描画順の変更時にシーン間で差が出やすい。既にゲームプレイ用には材質番号ごとの長い分岐があり、番号と描画関数の対応がこの関数へ集中している。
- 対応案: 共通描画ヘルパーに寄せる範囲と、シーン固有の演出パスを分けてから設計する。描画順、GrabTextureと深度SRVの状態遷移を変えない検証が必要。

### 4. VFX SequenceのJSON読込失敗が関数内で処理されない【高・実害候補】

- `engine/graphics/particle/VFXSequencer.cpp` の `VFXSequencer::Load` はファイルを開いた後に `file >> root` を例外処理なしで実行する。壊れたJSONで `nlohmann::json` の解析例外が出た場合、呼び出し元まで伝播する。`EffectObject3d::LoadFromJson` などは解析エラーを捕捉しているため、読込失敗時の扱いも揃っていない。
- 対応案: JSON構文・型の検証を行い、エラーをログやEditorへ返す。失敗時に再生中イベントとキャッシュをどの状態へ戻すかも決める。壊れたファイルでの実行確認は未実施。

### 5. セーブ差分プレビューはObject名が重複すると一件を失う【中・実害候補】

- `engine/utility/SystemDebug/editor/windows/SceneSavePreview.cpp` の `BuildObjectDiff` は旧版・新版のObjectを `std::map<std::string, json>` へ `name` をキーとして入れる。同名Objectが複数あると後の一件が前の一件を上書きし、追加・変更・削除の件数と詳細が実際の保存内容とずれる。
- 対応案: 保存JSON内の永続GUIDを優先するか、同名Objectの順序を含む複合キーで比較する。既存のScene JSONで同名が許容されるかを先に確認する。UI上の再現確認は未実施。

### 6. SpriteアニメーションとOrbitカメラの進行が更新回数に依存する【中】

- `engine/graphics/2d/Sprite.cpp` の `Sprite::Update` は呼び出しごとに `animationTimer_ += 1.0f / 60.0f` としており、実時間の経過秒を受け取らない。`engine/graphics/3d/camera/Camera.cpp` のOrbitモードも `orbitAngle_ += orbitSpeed_` としている。どちらも更新回数が60回/秒から外れると、設定値と見た目の速度の関係が変わる。
- 対応案: Spriteへ経過秒を渡す設計と既存呼び出し元を調べ、Orbit速度の単位を明示してフレーム時間で進める。ReplayとEditorプレビューの進行を崩さない確認が必要。

### 7. ボスHUDの初期化・更新・重ね順がGamePlaySceneへ集中する【中】

- `game/scene/gameplay/GamePlayScene.HUD.cpp` では `InitializeGameplayHUD` が多数のSpriteを手作業で生成し、`UpdatePrismBossHUD` がボス探索、テーマ変更、登場状態、HP遅延表示、偽王の段階・詠唱表示を一関数で処理する。配置値と色も関数内に多く、新しいボスや解像度への調整範囲が広い。
- 対応案: 先に既存のレイアウトJSONとEditorで調整できる値を整理し、ボスHUDの状態管理とSprite配置を分ける。描画順を変えないよう、現在の積層順を検証してから進める。

### 8. TitleScene描画でカメラ未設定時の参照が先に起こる【中・実害候補】

- `game/scene/title/TitleScene.cpp` の `Draw` は `GetActiveCamera()` の戻り値を受け取った直後、`#ifndef _DEBUG` 内で `camera->GetFollowTarget()` を呼ぶ。後段には `camera` のnull確認があるが、この判定より前の参照は保護されていない。通常はカメラが存在する設計かもしれないが、読み込み失敗・切替途中の保証はこの関数から確認できない。
- 対応案: カメラのライフサイクルを確認し、必要なら最初の判定もnull条件に含める。実際のクラッシュは未確認。

### 9. Project・HierarchyのUI関数に複数の操作責務が集中する【中】

- `engine/utility/SystemDebug/editor/windows/ProjectWindow.cpp` の `Draw` はAsset Databaseの状態表示、Model参照、Presetの保存・改名・削除、Prefab作成、VFX Asset表示までを一関数で扱う。`HierarchyWindow.cpp` の `Draw` と `DrawSceneAssetDialogs` もScene切替、各種Editor選択、Object作成・親解除、Scene Assetのモーダル操作を広く持つ。
- 条件の入れ子とImGuiの `Begin`/`End`、`Push`/`Pop` の対応が長い距離に分散し、画面の一部分だけ直す場合も関数全体を読む必要がある。現時点でスタック不整合は確認していない。
- 対応案: タブ・セクション単位の描画関数と、Scene/Assetの実操作関数を分ける。EditorのUndo、Dirty、保存確認の経路を維持する必要がある。

### 10. 攻撃プレビューのタイムラインに描画と操作が集中する【中】

- `engine/utility/SystemDebug/editor/windows/EnemyAttackPreviewWindow.Timeline.cpp` の `DrawTimelineWindow` は、ウィンドウ状態判定、キーボード操作、再生ツールバー、設定イベント、実測グラフ、ツールチップ、スクロール追従を一つの関数で処理する。`DrawCrownMovieTimeline` にも別形式の再生操作とレーン描画がある。
- 今回は読めるようにコメントと空行を補ったが、入力経路と表示内容を増やすと両方の操作を確認する必要がある。
- 対応案: シーク操作、時間軸の座標変換、レーン描画を切り分ける。通常攻撃と王冠ムービーで再生位置・入力の意味が同じか確認してから共通化する。

### 11. Stage1の組み込みPresetは配置名へ直接依存する【中】

- `game/editor/BuiltInCreatePresetRegistry.cpp` は `stage1_object.json` から `name` で元配置を検索し、コード内の `Stage1PresetDefinition` と対応させている。指定名が見つからない定義は登録ループでそのままスキップする。
- ステージの配置名をEditorで変更すると、PrefabやPresetの登録元が消えたように見える可能性がある。現在の配置名が一致するか、実行時の再登録結果は未検証。
- 対応案: 永続GUIDなどの安定した識別子を使い、欠落時はEditorへ知らせる。既存Presetの再生成方針とユーザー編集の保持を先に決める。

### 12. Replayの保存フィールドが手書きの対称関数に並ぶ【低】

- `engine/utility/SystemDebug/editor/windows/replay/ReplayDebugger.Support.cpp` の `ObjectReplayStateToJson` / `JsonToObjectReplayState`、Sprite用の同種関数には多数のフィールドが並ぶ。`Object3d::CaptureReplayState` / `RestoreReplayState` と合わせ、新たなReplay状態を追加する際の確認箇所が多い。
- 対応案: まず両方向のフィールド対応を検証するテストを用意する。保存形式の自動生成や共通シリアライズへ寄せる場合、過去Archiveの既定値とSchema Versionを維持する。

### 13. Missing参照の修復は元JSONを直接切り詰めて書く【中・データ保護】

- `engine/utility/SystemDebug/editor/windows/AssetReferenceExplorer.cpp` の `RepairMissingReference` は、元のJSON値が変わっていないことを確認した後、`std::ofstream(..., std::ios::trunc)` で同じファイルへ直接書く。途中で書き込み失敗やプロセス終了が起きたときの元ファイル復旧手順はこの関数にない。
- 対応案: 同じフォルダへ一時ファイルを書き、保存成功を確認してから置換する。既存のEditor保存・Backup処理と統一できるか調べる。障害注入による破損再現は未実施。

### 14. 特殊材質の設定UIが材質番号と共用パラメータに集中する【中】

- `engine/utility/SystemDebug/editor/windows/inspector/InspectorWindow.Material.cpp` の `DrawEffectMaterialSettings` は材質番号8〜22と26を長い `if/else if` で分け、水・炎・ゲート等を同じ `WaterParamData` の別のフィールド解釈で編集する。新しい材質を足すと番号、Shader側の意味、Editorの入力範囲を同時に揃える必要がある。
- 今回は字下げと処理段階のコメントを補った。UIでの設定反映は未検証。
- 対応案: 材質種別ごとの設定定義やEditor描画関数へ段階的に分ける。既存値の保存形式と水面診断表示、各材質の入力範囲を維持する。

### 15. 攻撃制作Hierarchyが保存・複製・Undo・配置まで担当する【中】

- `engine/utility/SystemDebug/editor/windows/EnemyAttackPreviewWindow.Authoring.cpp` の `DrawAttackAuthoringHierarchy` は攻撃選択、保存確認、見本からの生成、複製・削除、Event/Trackの追加、配置プレビュー、Undo/Redoまで一関数で扱う。途中の保存や再読込で `editableProfile_.attacks` の要素を入れ替えるため、参照を取り直す注意も関数内にある。
- 対応案: まず保存・生成・選択・編集UIを操作単位に分け、プロファイル更新後に保持する参照をなくす。Undo単位と「未保存で試す」「保存して再生」の違いを保つ必要がある。

### 16. Prefabの旧Property変換がRegistryと別の対応表に残る【低】

- `engine/utility/SystemDebug/editor/core/EditorPropertyRegistry.cpp` の `ResolveSerializedPath` と、`engine/utility/SystemDebug/editor/core/preset/PresetManager.Support.cpp` の `GetSerializedPropertyKey` は、編集用Property PathからJSON保存キーへの長い対応をそれぞれ持つ。後者はRegistryに登録があればそちらを優先するが、Fallback側にも多数の旧Propertyが列挙されている。
- 対応案: まずFallbackを使う外部Propertyが実在するか調べ、必要なものだけ残す。削る場合は旧Prefab VariantのOverrideとScene JSONの移行確認が必要。

### 17. GPU Particleの編集UIと保存形式が一つのファイルで密結合する【中】

- `engine/utility/SystemDebug/editor/vfx/GPUParticleEditor.cpp` の `DrawImGui` は発生条件、形状、曲線、環境、プレビュー、手書きのクイックPreset、保存一覧まで一関数で扱う。`Save` と `Load` には同じ設定項目の手書き対応が続き、旧Sprite項目と新しい `spriteAnimation` の読込順にも依存する。
- `Load` はJSONを例外処理なしで `file >> j` しており、壊れたPresetでは解析例外が呼び出し元へ伝わる可能性がある。また `Save` は保存先ディレクトリ作成や書込結果の確認をしていない。実ファイルを壊しての確認は行っていない。
- 対応案: 編集セクションを操作単位で分け、設定の保存と読込の対応を一箇所で検証する。旧JSONの既定値と上書き順を保ちながら、解析エラーと保存エラーをEditorへ返す。

### 18. Inspectorの材質・LOD・ParticleがCollisionの開閉に連動する【高・UI上の実害候補】

- `engine/utility/SystemDebug/editor/windows/inspector/InspectorWindow.cpp` の `Draw` では、「コリジョン設定」の `CollapsingHeader` を開いたブロックの中で、Graphics/Material、LOD、Particle、Mesh Effect、衝突属性・Maskまで描画する。ソースの波括弧上、Collisionを閉じるとこれらのUIも実行されない。
- 見出しは独立した項目に見えるが、材質やLODの編集のためにCollisionを開いておく必要がある。折りたたみ状態を変えた際の実画面確認は未実施。
- 対応案: Collisionの設定と属性・Maskを一つのセクションへまとめ、Material・LOD・演出は同じ階層の独立したヘッダーにする。巨大な `Draw` を分割する際に、`BeginDisabled`/`EndDisabled` と選択Objectの寿命を維持する。

## 検証の限界

この文書はコードの静的調査。ゲーム画面、エディター操作、初回エフェクト発動の実測はしていない。
