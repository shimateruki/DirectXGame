# CG2 ツール仕様まとめ

このドキュメントは、CG2プロジェクトで使う外部ツールとEditor内ツールの役割、現在の内部ロジック、生成物、注意点をまとめたものです。
以前のREADMEは文字化けしていたため、UTF-8日本語で再整理しています。

## 全体方針

ツールは大きく分けて、次の2種類です。

| 種類 | 主な場所 | 役割 |
| --- | --- | --- |
| Editor内ツール | `engine/utility/SystemDebug/editor/` | ImGui上で操作する制作支援ツール。ゲーム中の配置、プレビュー、調整、保存を担当します。 |
| 外部/補助ツール | `tools/` | PowerShell、C++ CLI、Pythonなどで重い生成処理や監査処理を行います。Editorから呼ばれるものと、手動実行するものがあります。 |

基本方針は、ゲーム本体の毎フレーム処理を重くしないことです。
PNG生成、DDS変換、LOD生成、アセット監査などは、Editor操作時または外部ツール側に寄せています。

## 主要ディレクトリ

| パス | 内容 |
| --- | --- |
| `tools/TextPngTool/` | テキストPNG生成用の外部CLI。現在の本命は `TextPngTool.exe` です。 |
| `tools/asset_audit/` | Resources配下のアセット監査ツール。重い素材、未使用候補、欠落参照を検出します。 |
| `tools/dds_cache/` | 画像からDDSキャッシュを作るツール。マニフェストとハッシュで不要な再変換を避けます。 |
| `tools/model_lod/` | モデルLOD生成ツール。Blenderまたはnative簡易生成を使います。 |
| `tools/shader_texture/` | シェーダー用の補助テクスチャを生成するツール。水、ゲート、炎、ガラスなどのマスクを作ります。 |
| `tools/terrain/` | 地形メッシュ、ペイントマップ、衝突用メタ情報を生成します。 |
| `tools/json_backup/` | JSON変更をバックアップするウォッチャーです。 |
| `tools/deps/` | SDL2など外部依存関係の復元ツールです。 |
| `tools/model_generation/` | 制作用モデルを生成する補助スクリプト群です。 |
| `tools/sprite_generation/` | UIや演出用Sprite素材を生成する補助スクリプト群です。 |
| `tools/docs/` | 仕様書などのドキュメント生成補助です。 |
| `tools/effect_previews/` | エフェクト案の確認用画像/GIF置き場です。 |

## Text PNG Generator

### 目的

入力した文字列をPNG画像として生成し、必要ならその場でSpriteとしてシーンに配置するツールです。
UI文字、看板、説明テキスト、演出用テキスト素材を作るために使います。

### 関連ファイル

| ファイル | 役割 |
| --- | --- |
| `engine/utility/SystemDebug/editor/windows/TextSpriteGenerator.h` | Editor側の状態、UI、プレビュー管理を持つクラス定義。 |
| `engine/utility/SystemDebug/editor/windows/TextSpriteGenerator.cpp` | ImGui、フォント一覧、外部CLI呼び出し、Sprite配置処理。 |
| `tools/TextPngTool/TextPngTool.cpp` | 現在のPNG生成本体。DirectWrite、Direct2D、WICで描画してPNG保存します。 |
| `tools/TextPngTool/TextPngTool.vcxproj` | TextPngTool.exeのビルド設定。 |
| `tools/TextPngTool/TextPngTool.ps1` | 旧PowerShell版。現在のEditor経路ではC++ CLIが本命です。 |

### 現在の処理フロー

1. Editorの `TextSpriteGenerator` で文字、フォント、サイズ、縁取り、影、出力名を設定します。
2. `Resources/font` と `Resources/sprite/meiryo.ttc` からフォント候補を集めます。
3. DirectWriteのカスタムフォントコレクションを作り、実際に選択したフォントファミリー名とフォントファイルパスを保持します。
4. PNG生成時に `Resources/generated/editor/text_png_request.json` を作ります。
5. `TextPngTool.exe render -config <json> -out <png>` を起動します。
6. `TextPngTool.exe` 側でDirectWrite/Direct2D/WICを使って透明背景PNGを生成します。
7. `Resources/generated/editor/text_png_result.json` に幅、高さ、使用フォント、fallback有無を書きます。
8. Editor側は結果PNGをTextureManagerで読み込み、GameViewにプレビューします。
9. 書き出し時は `Resources/sprite/generated/text/` に保存します。
10. 「Spriteとして追加」を押すと、生成PNGをSpriteとして現在のシーンに追加します。

### 生成されるファイル

| パス | 内容 |
| --- | --- |
| `Resources/generated/editor/text_png_request.json` | TextPngTool.exeへ渡す一時リクエスト。 |
| `Resources/generated/editor/text_png_result.json` | 生成結果レポート。 |
| `Resources/generated/editor/text_preview/_preview_XXXX.png` | GameViewプレビュー用の一時PNG。 |
| `Resources/sprite/generated/text/*.png` | 本番で使う生成済みテキストPNG。 |

### 重要な実装ポイント

- 旧PowerShell版の `PrivateFontCollection.Families[0]` 問題を避けるため、現在はC++ CLI側でDirectWriteを使います。
- フォントファイルパスだけでなく、フォントファミリー名も渡します。
- `.ttc` のように複数フェイスを持つフォントでも、できる限り選択したファミリーへ寄せます。
- PNGは `GUID_WICPixelFormat32bppPBGRA` で保存され、透明背景を保持します。
- 縁取りは文字を周囲に複数回描く方式です。
- 影は指定オフセットで先に描画します。
- 自動キャンバスがONの場合、DirectWriteのメトリクスから必要サイズを計算します。
- プレビュー自動更新は毎フレームCLIを叩かないように短い遅延を入れます。

### 注意点

- TextPngTool.exeが見つからない場合は、ソリューションをビルドする必要があります。
- プレビューPNGは一時生成物なので、DDSキャッシュ対象からは除外されます。
- 文字が空の場合はプレビューを消します。
- 生成ファイル名は危険文字を `_` に置換し、拡張子は `.png` に揃えます。

## Text 3D Generator

### 目的

入力した文字列を厚み付きの3D OBJモデルとして生成し、Editor上でプレビュー、配置できるツールです。
看板、立体タイトル、ステージ演出用の3D文字に使います。

### 関連ファイル

| ファイル | 役割 |
| --- | --- |
| `engine/utility/SystemDebug/editor/windows/Text3DGenerator.h` | 3Dテキスト生成ツールの状態と設定。 |
| `engine/utility/SystemDebug/editor/windows/Text3DGenerator.cpp` | DirectWrite文字マスク生成、OBJ出力、プレビューObject生成。 |

### 現在の処理フロー

1. Editorで文字、フォント、サイズ、サンプル幅、しきい値、厚み、高さを設定します。
2. DirectWrite/Direct2D/WICで文字を透明Bitmapへ描きます。
3. BitmapのAlpha値だけを取り出して文字マスクを作ります。
4. `sampleStep` ごとにマスクを格子化し、Alphaが `alphaThreshold` 以上のセルを塗りセルとして扱います。
5. 塗りセルごとに前面、背面、外周側面のQuadを作ります。
6. OBJとして `Resources/3DModel/GeneratedText/<name>/<name>.obj` に保存します。
7. 生成結果レポートもJSONで保存します。
8. プレビューONの場合、`_preview_text3d` モデルを生成してEditorOnly Objectとして表示します。
9. 「シーンへ追加」系の操作で通常のObject3Dとして配置します。

### 生成されるファイル

| パス | 内容 |
| --- | --- |
| `Resources/3DModel/GeneratedText/<name>/<name>.obj` | 本番用3Dテキストモデル。 |
| `Resources/3DModel/GeneratedText/<name>/<name>_text3d_report.json` | 生成レポート。 |
| `Resources/3DModel/GeneratedText/_preview_text3d/_preview_text3d.obj` | プレビュー用モデル。 |

### プレビュー仕様

- プレビューObject名は `__Editor_Text3DPreview` です。
- ClassNameは `EditorOnly` で、保存対象の本番Objectと混ざらないようにしています。
- 選択中のツールがText 3D GeneratorでなくなるとプレビューObjectは削除されます。
- `previewAttachToCamera_` がONの場合、現在のカメラ前方一定距離に表示されます。
- Auto Update ON時は、入力変更から少し待ってから再生成します。

### 安全制限

| 制限 | 内容 |
| --- | --- |
| プレビュー頂点数上限 | `240000` 頂点を超えるとプレビュー更新を止めます。 |
| 本生成頂点数上限 | `480000` 頂点を超えると本生成を中止します。 |
| sampleStep | 小さいほど滑らかですが、頂点数が爆発します。 |
| alphaThreshold | 低いほど細い部分まで拾いますが、ノイズも拾いやすくなります。 |

## Asset Audit

### 目的

`Resources` 配下を調査して、次を見つけるための監査ツールです。

| 検出項目 | 内容 |
| --- | --- |
| Heavy Assets | サイズが大きい画像、モデル、音声。 |
| Unused Assets | JSONやコードから参照が見つからない未使用候補。 |
| Missing References | JSONやコードには書かれているが、実ファイルが見つからない参照。 |
| Category Summary | Model、Sprite、Texture、BGM、SEなどの分類別集計。 |

### 関連ファイル

| ファイル | 役割 |
| --- | --- |
| `engine/utility/SystemDebug/editor/windows/AssetAuditWindow.h` | Editor側UIと状態。 |
| `engine/utility/SystemDebug/editor/windows/AssetAuditWindow.cpp` | 監査実行、結果表示、プレビュー、削除処理。 |
| `tools/asset_audit/asset_audit.ps1` | 実際にResourcesを走査する外部監査スクリプト。 |
| `tools/asset_audit/build_asset_audit.bat` | 監査ツール起動補助。 |

### 現在の処理フロー

1. EditorのAsset Auditから監査を実行します。
2. `asset_audit.ps1` を非同期/非表示で起動します。
3. スクリプトが `Resources` 配下のファイルを走査します。
4. `engine`、`game`、`application`、`Resources/json` などのコード/JSONを走査して参照文字列を集めます。
5. モデル依存関係も追加で解析します。
6. 使用中ファイル、未使用候補、欠落参照、重い素材をJSONにまとめます。
7. Editor側で `Resources/.cache/asset_audit/latest_report.json` を読みます。
8. ImGui上で検索、分類フィルタ、プレビュー、削除確認ができます。

### 分類ロジック

| 分類 | 主な判定 |
| --- | --- |
| Sprite | `.png/.dds/.jpg` などの画像で、パスに `/sprite/`、`/ui/`、`/generated/text/` を含むもの。 |
| Texture | Sprite扱いではない画像。PBRやマテリアル用画像を含みます。 |
| Model | `.gltf/.glb/.obj/.fbx`。 |
| ModelData | `.bin/.mtl`。 |
| Audio-BGM | audio settingsやパス名からBGMと判断した音声。 |
| Audio-SE | audio settingsやパス名からSEと判断した音声。 |
| Audio | BGM/SEに分類できない音声。 |
| GeneratedText | 生成テキストPNGやText3D生成物。 |
| PBRTexture | `texture/pbr` や `diffuse/normal/rough/metal/orm` 系の名前を持つテクスチャ。 |

### 参照解析の考え方

- JSON内の文字列を再帰的に調べます。
- コード内の `Resources/...` 文字列も拾います。
- `Resources/` から始まるパスは直接候補にします。
- 拡張子付き相対パスは `Resources`、`Resources/sprite`、`Resources/texture`、`Resources/3DModel`、`Resources/json` などを候補にします。
- 拡張子なしモデル名は `Resources/3DModel/<name>/<name>.gltf` なども候補にします。
- glTFはbufferや画像依存も使用中扱いにします。
- OBJは `mtllib` を読み、MTL内のテクスチャも使用中扱いにします。
- 画像が使われている場合、同名DDSも使用中扱いに寄せます。

### 未使用候補の扱い

- DDS単体を未使用候補に出しすぎないよう、元画像があるDDSは単独候補から除外します。
- PNG/JPG/TGA/HDRなどの元画像が未使用なら、同名DDSを `pairedFiles` として一緒に表示します。
- 削除判断は基本的に元画像を入口にします。
- 生成キャッシュ、ゴミ箱、LOD生成物、baked shaderなどは未使用候補から除外します。

### 削除仕様

現在のAsset Audit削除は、確認ポップアップ後に即時の完全削除です。
ゴミ箱へ移動ではありません。

削除対象に含める関連ファイルは次の通りです。

| 選択ファイル | 一緒に削除する可能性があるもの |
| --- | --- |
| `.png/.jpg/.jpeg/.bmp/.tga/.hdr` | 同名 `.dds`。 |
| `.dds` | 同名の元画像候補。 |
| `.gltf` | 同名 `.bin`。 |
| `.obj` | 同名 `.mtl`。 |

削除時の保護条件は次の通りです。

- `Resources` 配下以外は削除しません。
- `..` を含むパスは削除しません。
- `Resources/.cache/` は削除しません。
- `Resources/.trash/` は削除しません。
- 存在しないファイル、通常ファイルでないものは削除しません。

### プレビュー機能

| 種類 | プレビュー内容 |
| --- | --- |
| 画像 | TextureManagerで読み込み、サムネイル表示します。大きすぎる画像は外部確認に寄せます。 |
| 音声 | BGM/SEに応じてAudioPlayerで試聴します。BGMは停止ボタンもあります。 |
| モデル | 現在シーンにEditorOnlyプレビューObjectを一時配置します。 |
| 外部確認 | ShellExecuteでExplorerや関連アプリから開きます。 |

### 出力ファイル

| パス | 内容 |
| --- | --- |
| `Resources/.cache/asset_audit/latest_report.json` | Editorが読む詳細レポート。 |
| `Resources/.cache/asset_audit/asset_audit_report.md` | 人間向けの簡易Markdownレポート。 |

## DDS Cache Builder

### 目的

PNG/JPG/TGA/HDRなどからDDSキャッシュを作り、起動後や実行中の重いテクスチャ読み込みを次回以降軽くするためのツールです。

EditorではAsset Databaseの初期索引作成と手動更新の完了後に、DDS Cache Builderを非同期プロセスとして起動します。
Projectウィンドウには実行状態と手動の「未変換DDSを生成」ボタンを表示し、Editor本体を止めずにmissing/outdatedだけを処理します。

### 関連ファイル

| ファイル | 役割 |
| --- | --- |
| `tools/dds_cache/dds_cache_builder.ps1` | DDS変換本体。 |
| `tools/dds_cache/build_dds_cache_once.bat` | 一回だけ変換する起動補助。 |
| `tools/dds_cache/start_dds_cache_watcher.bat` | 監視起動補助。 |
| `tools/dds_cache/start_dds_cache_watcher.vbs` | コンソールを目立たせず起動する補助。 |

### 対象拡張子

| 拡張子 | 用途 |
| --- | --- |
| `.png` | 通常画像、Sprite、UI、テクスチャ。 |
| `.jpg/.jpeg` | 通常画像。 |
| `.tga` | テクスチャ。 |
| `.hdr` | HDR/環境系画像。 |

### 除外対象

- `Resources/generated/editor/text_preview/` 配下のText PNGプレビュー。
- `/generated/text/_preview_` のような一時プレビュー画像。
- 変換しても本番メリットが薄いEditor一時生成物。

### 変換形式の判定

| 条件 | DDS形式 |
| --- | --- |
| `.hdr` | `BC6H_UF16` |
| normal、rough、metal、ao、orm、arm、mask、noise、bakedshaderなど | `BC7_UNORM` |
| 通常カラー画像 | `BC7_UNORM_SRGB` |

### 再変換を避ける仕組み

DDS変換は毎回全ファイルを変換しません。
次の情報を使って `latest`、`missing`、`outdated` を判定します。
最新DDSの確認では元画像全体を読み込まず、更新日時とマニフェスト情報を優先して判定します。
元画像のSHA256はDDSが無い場合、または元画像の更新日時が新しい場合だけ計算します。

| 情報 | 役割 |
| --- | --- |
| DDSの有無 | なければ `missing`。 |
| 元画像の更新日時 | 元画像がDDSより新しければ再確認。 |
| 元画像のSHA256 | タイムスタンプだけ変わったケースでも、中身が同じなら再変換を避けます。 |
| DDS形式 | 前回と形式が変わっていれば再変換対象。 |
| DDSサイズ | 生成結果が前回と同等か確認する補助。 |

### Runtime Request Queue

`Resources/.cache/dds_cache_requests.jsonl` がある場合、実行中に「この画像の読み込みが重かった」という要求を後から拾えます。
`-RequestOnly` を使うと、通常スキャンではなく要求された画像を中心に変換できます。
`-MinRequestDurationMs` より短い読み込み時間の要求は無視できます。

### Watchモード

`-Watch` で監視モードになります。
多重起動を避けるため、グローバルMutex `Global\GE3_DDSCacheBuilder_Watcher` を使います。
一定間隔でmissing/outdatedや要求キューを見て、必要なものだけ変換します。
Editorの一括変換とRuntime要求の変換が重なった場合は、`Global\GE3_DDSCacheBuilder_Build` で変換処理を直列化します。

### 出力ファイル

| パス | 内容 |
| --- | --- |
| `Resources/.cache/dds_cache_manifest.json` | 元画像hash、DDS形式、DDSサイズなどの記録。 |
| `Resources/.cache/dds_cache_requests.jsonl` | 実行中の重い読み込み要求。 |
| `Resources/.cache/dds_cache_notifications.jsonl` | DDS生成完了通知ログ。 |
| 元画像と同じ場所の `.dds` | 実際のDDSキャッシュ。 |

## Model Optimizer / LOD Tool

### 目的

モデルのLODを制作時に生成し、ゲーム実行時はカメラ距離に応じて軽いモデルへ切り替えるためのツールです。
ゲーム中に毎回LODを生成するのではなく、Editorで作ってObjectに設定します。

### 関連ファイル

| ファイル | 役割 |
| --- | --- |
| `engine/utility/SystemDebug/editor/windows/ModelOptimizerWindow.h` | LOD Editorの状態。 |
| `engine/utility/SystemDebug/editor/windows/ModelOptimizerWindow.cpp` | UI、外部LOD生成、プレビュー、Object反映。 |
| `tools/model_lod/model_lod_builder.ps1` | LOD解析/生成スクリプト。 |
| `tools/model_lod/blender_lod.py` | Blender Decimate経路で使うPython。 |

### 生成バックエンド

| Backend | 内容 |
| --- | --- |
| Auto | Blenderが見つかればBlender、なければnative簡易生成。 |
| Blender | Blender CLIでDecimate系のLODを生成します。品質優先。 |
| Native | 内部の簡易グリッド/削減処理。速いが品質は落ちます。 |

### 対象モデル

| 対象 | 備考 |
| --- | --- |
| OBJ | 基本対応。 |
| GLB | 対応。 |
| glTF | 非スキン、非アニメーション、非モーフなど安全なものだけ対応。 |

スキン、アニメーション、モーフターゲット、非三角形primitiveなどがあるglTFは自動LOD生成から外します。
壊してはいけないモデルを無理に軽量化しないためです。

### 処理フロー

1. Editorで対象モデルを選びます。
2. LOD1/LOD2の保持率と距離を設定します。
3. `model_lod_builder.ps1` をバックグラウンド起動します。
4. 解析のみ、またはLOD生成を実行します。
5. `Resources/.cache/model_lod/latest_report.json` を読みます。
6. Previewを作ると、`__Editor_LODPreview_LOD0` などのEditorOnly Objectを並べます。
7. 見た目を確認して採用すると、選択ObjectへLOD設定を反映します。
8. Object側に `lod.enabled` と `lod.levels` が入り、シーン保存/読み込み経由でゲーム実行時にも使われます。

### 採用/破棄

| 操作 | 内容 |
| --- | --- |
| 採用 | 選択ObjectへLODモデル名と距離を設定し、距離LODを有効化します。 |
| 破棄 | 生成LODファイルとレポート候補を削除し、Preview Objectも消します。 |

### 実行時の考え方

LOD選択はEditorだけでは終わりません。
Objectに保存されたLOD設定を、シーンロード時に復元し、描画時にカメラ距離で切り替える必要があります。
現在はこの方針で組まれています。

## Camera Editor / Cinematic Camera

### 目的

ゲーム用カメラ、自由カメラ、演出用カメラの保存、確認、可視化、プレビューを行うEditorツールです。

### 関連ファイル

| ファイル | 役割 |
| --- | --- |
| `engine/utility/SystemDebug/editor/graphics/CameraEditor.h` | カメラ設定、演出用カメラ一覧、プレビュー状態。 |
| `engine/utility/SystemDebug/editor/graphics/CameraEditor.cpp` | ImGui、自由カメラ更新、ガイド描画、ImGuizmo編集。 |
| `Resources/json/camera/*.json` | カメラ設定ファイル。 |
| `Resources/json/camera/editor_camera_state.json` | 自由カメラの位置/角度保存。 |

### カメラモード

| Mode | 内容 |
| --- | --- |
| Game | 通常ゲーム用カメラ。追従、Aimable、Orbit、FirstPersonなどを扱います。 |
| Editor | WASD/マウスで動かす自由カメラ。制作中の確認用です。 |

### 保存される主な設定

- ゲームカメラ距離。
- 高さ。
- 角度。
- LockOn offset。
- Orbit半径、高さ、中心オフセット、開始角度。
- 自由カメラ位置と角度。
- カメラガイド表示設定。
- 演出用カメラ一覧。

### 演出用カメラの考え方

演出用カメラは、名前付きのOverride Cameraとして保存されます。
内部では `Camera::CameraOverrideParams` のmapとして保持されます。

主な操作は次の通りです。

| 操作 | 内容 |
| --- | --- |
| 現在のビューをコピー | 今見ている自由カメラ/ゲームカメラの位置と注視点を演出用カメラへ保存します。 |
| Scene上で選択 | 保存済み演出用カメラを選択状態にします。 |
| ImGuizmo編集 | Eyeだけ、Targetだけ、または構図ごと平行移動できます。 |
| プレビュー | 保存済みカメラ視点の見え方を小窓で確認します。 |
| 再生 | `PlayOverrideCamera` 経由でCameraへOverrideを渡します。 |

### 可視化仕様

- 通常ゲームカメラは黄色系で表示します。
- 演出用カメラは未選択が青、選択中が緑です。
- カメラ本体モデルは `Editor/camera_gizmo` を使います。
- フラスタム、注視線、ターゲット球、番号タグを描きます。
- 再生中は通常の3人称カメラ表示だけ非表示にし、演出用カメラ確認を邪魔しないようにします。

### プレビュー小窓

カメラプレビューは便利ですが重くなりやすいため、基本は必要なときだけONにします。
現在の設定では `cameraPreviewVisible` の初期値はOFFです。

プレビュー系は次の用途に分かれます。

| プレビュー | 内容 |
| --- | --- |
| 通常カメラプレビュー | Game/Editorカメラから見た画を確認します。 |
| Cinematic Preview | 保存済み演出用カメラから見た画を確認します。 |
| Scene上の可視化 | 実際のカメラ位置と向きをワイヤー/モデルで確認します。 |

### 注意点

- 演出中にゲームカメラを直接差し替えると、Editor側のカメラ参照と衝突することがあります。
- 専用の別カメラをactive cameraに差し替える方式は、Objectがカメラに張り付いて見える副作用が出ることがあります。
- 演出本番では、CameraManagerのactive差し替えより、既存カメラへ一時的に固定姿勢を流し込む方が安全です。
- Editor自由カメラが演出中にも更新される場合は、演出中フラグで自由カメラ更新を止める必要があります。

## Project Window

### 目的

モデル、プリセット、Prefab、VFX/Particleなどを一覧表示し、ドラッグ&ドロップでシーンへ配置するための制作窓です。

### 関連ファイル

| ファイル | 役割 |
| --- | --- |
| `engine/utility/SystemDebug/editor/windows/ProjectWindow.h` | サムネイル情報、ProjectWindow状態。 |
| `engine/utility/SystemDebug/editor/windows/ProjectWindow.cpp` | モデル一覧、プリセット一覧、サムネイル描画、ドラッグ&ドロップ。 |

### モデル一覧

- `Resources/3DModel/` をブラウズします。
- `.obj/.gltf/.glb` をモデルとして扱います。
- フォルダ単位のモデルも表示します。
- LOD生成物は黄色っぽく表示し、通常モデルと区別します。
- `MODEL_ASSET` payloadとしてドラッグできます。

### サムネイル生成

- サムネイル用に専用のRenderTargetを作ります。
- `studioCamera_` と専用 `Object3dCommon` を使って、シーン本体に依存しにくいプレビューを作ります。
- モデルサイズと中心から自動スケール/位置補正を行います。
- 明るく見えるようにLighting/Emissiveを補正します。
- 一度撮影したサムネイルはキャッシュし、必要なときだけ再撮影します。

### Preset / Prefab / VFX

| 項目 | 内容 |
| --- | --- |
| Preset | 選択Objectを保存し、サムネイル付きで再配置できます。 |
| Prefab v1 | 階層Objectをまとめて保存するための初期版です。PrefabリンクやOverrideは未対応です。 |
| VFX / Particles | `Resources/json/gpu_particles/` のJSONを一覧化し、Particle配置用payloadを出します。 |

## Shader Texture Baker

### 目的

シェーダーで使う補助テクスチャを手作業で描かず、スクリプトで生成します。
水面、ゲート、炎、ガラス、ダッシュパネルなどのマスクを揃えるためのツールです。

### 関連ファイル

| ファイル | 役割 |
| --- | --- |
| `tools/shader_texture/shader_texture_baker.ps1` | 補助テクスチャ生成本体。 |
| `tools/shader_texture/build_shader_textures.bat` | 起動補助。 |

### 生成プリセット

| Preset | 生成ファイル | Channel設計 |
| --- | --- | --- |
| `water_foam_mask` | `water_foam_mask.png/.dds` | R=foam、G=caustics、B=flow、A=opaque |
| `water_flow_noise` | `water_flow_noise.png/.dds` | R=flowX、G=flowY、B=surface breakup、A=opaque |
| `gate_swirl_mask` | `gate_swirl_mask.png/.dds` | R=swirl arms、G=rim、B=core、A=portal mask |
| `fire_flame_mask` | `fire_flame_mask.png/.dds` | R=heat、G=core、B=ember、A=flame mask |
| `fire_orb_mask` | `fire_orb_mask.png/.dds` | R=heat patch、G=crack、B=smoke、A=orb mask |
| `glass_crack_mask` | `glass_crack_mask.png/.dds` | R=crack、G=edge glow、B=shards、A=opaque |
| `dash_flow_mask` | `dash_flow_mask.png/.dds` | R=arrow、G=rail、B=streak、A=opaque |

### 出力

| パス | 内容 |
| --- | --- |
| `Resources/texture/BakedShader/*.png` | 生成されたPNG。 |
| `Resources/texture/BakedShader/*.dds` | texconvで変換したDDS。 |
| `Resources/texture/BakedShader/shader_texture_bake_manifest.json` | 生成物一覧、チャンネル説明、サイズなど。 |

### 注意点

- シェーダー側のコメントは英語にしてください。
- Channel設計を変えた場合、HLSL側の読み方も合わせて修正してください。
- `-NoDDS` を使うとPNGだけ生成します。
- `-Force` を使うと既存ファイルがあっても再生成します。

## Terrain Mesh Builder

### 目的

地形メッシュ、ペイントマップ、衝突用メタデータを生成するツールです。

### 関連ファイル

| ファイル | 役割 |
| --- | --- |
| `tools/terrain/terrain_mesh_builder.ps1` | 地形生成スクリプト。 |

### 主な生成物

- `Resources/3DModel/GeneratedTerrain/<name>/<name>.obj`
- ペイントマップPNG。
- 地形衝突用JSON。
- 生成レポートJSON。

### 処理概要

- 解像度、サイズ、高さ、ノイズ、テラス、ペイントプリセットなどから高さ場を作ります。
- 頂点数は `(Resolution + 1) * (Resolution + 1)` です。
- 三角形数は `Resolution * Resolution * 2` です。
- 高さサンプルもJSONに保存し、後で衝突やデバッグに使える形にします。

## JSON Backup Watcher

### 目的

`Resources/json` の変更をバックアップし、事故でシーンや設定を壊したときに戻せるようにするツールです。

### 関連ファイル

| ファイル | 役割 |
| --- | --- |
| `tools/json_backup/json_backup_watcher.ps1` | JSONバックアップ本体。 |

### 処理フロー

1. `Resources/json` 配下の `.json` を走査します。
2. JSONとして壊れていないか確認します。
3. SHA256とサイズを見て、前回から変わったものだけバックアップします。
4. `Resources/.backup/json/<timestamp>/...` にコピーします。
5. `Resources/.cache/json_backup_manifest.json` に履歴を保存します。
6. `MaxVersionsPerFile` を超えた古いバックアップは削除します。

### 出力

| パス | 内容 |
| --- | --- |
| `Resources/.backup/json/` | 実バックアップ。 |
| `Resources/.cache/json_backup_manifest.json` | どのファイルをいつバックアップしたか。 |
| `Resources/.cache/json_backup/latest_report.json` | 最新実行結果。 |

## SDL2 Restore Tool

### 目的

SDL2の `.lib/.dll/include` をリポジトリへ直接持ち続けず、必要なときに復元するためのツールです。
課題で指摘された「外部ライブラリ管理」を満たすための仕組みです。

### 関連ファイル

| ファイル | 役割 |
| --- | --- |
| `tools/deps/restore_sdl2.ps1` | SDL2復元スクリプト。 |

### 現在の仕様

- SDL2 versionは `2.32.10` です。
- ダウンロード元はSDL公式GitHub Releaseです。
- `externals/SDL2/lib/x64/SDL2.lib` と `SDL2.dll` があれば何もしません。
- なければ `externals/.cache` にzipをダウンロードして展開します。
- `externals/SDL2/include` と `externals/SDL2/lib/x64` に必要ファイルをコピーします。

### 注意点

- ネットワークが使えない環境では初回復元に失敗します。
- CIや別PCでは、ビルド前にこの復元処理を走らせる必要があります。
- リポジトリから `.dll/.lib` を外す方針なら、このツールがビルド前提になります。

## その他の生成補助

### model_generation

| ファイル | 用途 |
| --- | --- |
| `generate_breakable_block_assets.py` | 破壊可能ブロック系素材生成。 |
| `generate_bomb_slime_model.py` | 爆弾スライム系モデル生成。 |
| `generate_stylized_nature_models.py` | 草木などのスタイライズモデル生成。 |

### sprite_generation

| ファイル | 用途 |
| --- | --- |
| `generate_electric_particle_sprites.py` | 電撃系Particle Sprite生成。 |
| `generate_input_ui_sprites.py` | 入力UI用Sprite生成。 |
| `generate_morph_gauge_sprites.py` | モーフゲージ用Sprite生成。 |
| `generate_simple_pop_fade_frames.py` | ポップ/フェード系フレーム生成。 |

### docs

| ファイル | 用途 |
| --- | --- |
| `build_game_spec_pdf.py` | 仕様書PDF生成補助。 |

## Editor内のその他ツール一覧

| Tool | 主なファイル | 概要 |
| --- | --- | --- |
| Camera Editor | `graphics/CameraEditor.*` | カメラ設定、自由カメラ、演出用カメラ、可視化、ImGuizmo編集。 |
| Post Effect Editor | `graphics/PostEffectEditor.*` | ポストエフェクト設定。 |
| GPU Particle Editor | `vfx/GPUParticleEditor.*` | GPU Particleの発生/見た目/保存調整。 |
| Particle Editor | `vfx/ParticleEditor.*` | 通常Particle調整。 |
| VFX Sequencer | `vfx/VFXSequencerEditor.*` | 複数VFXを時間軸で組むためのEditor。 |
| Trail Emitter | `vfx/TrailEmitterEditor.*` | 軌跡系エフェクト調整。 |
| Mesh Effect | `vfx/MeshEffectEditor.*` | メッシュを使ったエフェクト調整。 |
| Debris Effect | `vfx/DebrisEffectEditor.*` | 破片、爆発、飛散系エフェクト調整。 |
| Ghost Recorder | `misc/GhostRecorder.*` | Objectやカメラの軌跡記録/再生。 |
| Scene Validator | `windows/SceneValidator.*` | シーン検証。 |
| Scene Save Preview | `windows/SceneSavePreview.*` | 保存前差分確認。 |
| Json Backup Window | `windows/JsonBackupWindow.*` | JSONバックアップ操作。 |
| Game Data Debug Editor | `windows/GameDataDebugEditor.*` | ゲームデータ確認/編集。 |
| Status Tuning | `windows/StatusTuningWindow.*` | ステータス調整。 |
| Material Preview | `windows/MaterialPreviewBoard.*` | マテリアル確認。 |
| Animation Workbench | `windows/AnimationWorkbench.*` | アニメーション確認/イベント確認。 |
| Capture Tool | `windows/CaptureToolWindow.*` | スクリーンショット等の補助。 |
| Executable Package | `windows/ExecutablePackageWindow.*` | 実行ファイルパッケージ確認/作成補助。 |

## 生成物をGitに入れるかどうかの目安

| 種類 | Git管理方針 |
| --- | --- |
| 手作り/本番アセット | 基本的に管理対象。 |
| `Resources/sprite/generated/text/*.png` | ゲーム本番で使うなら管理対象。試作なら削除候補。 |
| `Resources/3DModel/GeneratedText/*` | 本番で使うなら管理対象。プレビュー用 `_preview_text3d` は管理しない方が安全。 |
| `.dds` | 方針次第。現在はDDS Cache Builderで再生成可能にしているため、原則は管理しない方向が扱いやすいです。 |
| `Resources/.cache/*` | 管理しない。 |
| `Resources/.backup/*` | 管理しない。 |
| `externals/SDL2/lib/*.lib/.dll` | restore_sdl2.ps1で復元する方針なら管理しない。 |
| `tools/` 配下のスクリプト | 管理対象。 |
| `tools/effect_previews/*` | 参考資料として必要なら管理対象。不要なら除外。 |

## ツール追加/改修時の注意

- 日本語コメントはUTF-8で保存してください。
- HLSL/シェーダー側のコメントは英語にしてください。
- ImGuiを使うコードは `#ifdef USE_IMGUI` を使ってください。
- 大きい処理はEditorの毎フレーム処理に入れず、外部ツール化または遅延実行してください。
- プレビュー用のObjectは `EditorOnly` や専用名で識別し、保存/本番Objectと混ざらないようにしてください。
- 削除系ツールはResources外へ出ないこと、`.cache` や `.backup` を消さないことを必ず守ってください。
- 生成物を作るツールは、生成レポートJSONを残すと後から追いやすいです。
- フォント、DDS、LODのように環境差が出る処理は、失敗時にDebugConsoleへ理由を出してください。

## 今後の改善候補

| 優先度 | 改善案 | 理由 |
| --- | --- | --- |
| 高 | TextSpriteGenerator/Text3DGeneratorの日本語コメント文字化け修正 | 実装理解と保守性がかなり上がります。 |
| 高 | Asset Auditの削除ログ保存 | 完全削除なので、何を消したかログに残した方が安全です。 |
| 高 | Camera Editorの演出中入力ブロック整理 | 演出用カメラと自由カメラ更新が衝突しやすいためです。 |
| 中 | DDS Cacheの進捗件数表示 | 非同期実行の状態は確認できますが、総件数と完了件数も表示できると待ち時間を把握しやすくなります。 |
| 中 | Shader Texture Bakerのプレビュー一覧化 | チャンネル設計を目で確認しやすくなります。 |
| 中 | Text PNG/3D Textのプリセット保存 | よく使うフォント、縁取り、影、サイズを再利用しやすくなります。 |
| 中 | Asset Auditの安全削除モード追加 | 完全削除とゴミ箱移動を切り替えられると作業段階で安心です。 |
| 低 | Project WindowのSprite/Audio一覧統合 | Asset Auditとは別に、素材ブラウザとしても使いやすくなります。 |

## ざっくり使い分け

| やりたいこと | 使うツール |
| --- | --- |
| UI文字や看板PNGを作る | Text PNG Generator |
| 立体文字モデルを作る | Text 3D Generator |
| 未使用素材を探す | Asset Audit |
| PNGをDDSへ変換する | DDS Cache Builder |
| モデルを軽量化して距離LODを作る | Model Optimizer / LOD Tool |
| 演出カメラを保存/確認/動かす | Camera Editor / Cinematic Camera |
| モデルやプリセットを配置する | Project Window |
| 水/ゲート/炎などのマスクテクスチャを生成する | Shader Texture Baker |
| JSON事故に備える | JSON Backup Watcher |
| SDL2を復元する | SDL2 Restore Tool |
