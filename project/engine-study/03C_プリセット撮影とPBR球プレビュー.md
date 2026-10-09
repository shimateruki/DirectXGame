# プリセットのサムネイル撮影とPBR球プレビュー

この文書は、エディターの「プリセットに画像が付いているように見える」仕組みと、PBR素材を球で選ぶ仕組みを、実際のコードに沿って説明します。対象は `ProjectWindow`、`PresetManager`、`InspectorTextureCatalog`、`InspectorWindow` です。

## まず「撮影」とは

ここでいう撮影は、画面のスクリーンショットを保存する操作ではありません。DirectX 12 の描画先を普段の画面から小さなテクスチャへ切り替え、撮影用の3DオブジェクトをGPUで描くことです。描いたテクスチャをImGuiの画像ボタンとして表示します。通常のプリセット一覧ではPNGを毎回作るわけではなく、画像はGPUメモリ上に保持します。HUD用画像の書き出しだけは別の処理でPNG化します。

```text
presets.json → PresetManagerのメモリ内JSON → 撮影用Object3d
                                             ↓
                                      3D描画パイプライン
                                             ↓
                              256×256のレンダーターゲット
                                             ↓
                                SRVを通じてImGuiに表示
```

## プリセットのデータを読む

`PresetManager::Initialize` は `Resources/json/preset/presets.json` を読みます。`LoadPresets` はJSONの各名前と内容を `presets_` に格納します。ファイルがサムネイル画像を持つのではなく、「モデル・位置・材質などの再現に必要なデータ」を持ちます。`ProjectWindow::Draw` は `GetPresets()` で一覧を取得し、表示する項目について `GetPresetThumbnailGpuPtr` を呼びます。

- 読込：`engine/utility/SystemDebug/editor/core/preset/PresetManager.cpp`
- 一覧：`engine/utility/SystemDebug/editor/windows/ProjectWindow.cpp` の `DrawPresetGrid`

## GPU上に小さな「画用紙」を作る

`ProjectWindow::CreatePresetThumbnailResource` がサムネイル用のテクスチャを作ります。大きさは256×256、形式は `DXGI_FORMAT_R16G16B16A16_FLOAT` です。同じテクスチャに対して二種類の見方を用意します。

| 用途 | DirectX 12の名前 | 何をするか |
| --- | --- | --- |
| 描き込む | RTV (Render Target View) | GPUが色を書き込む描画先になる |
| 読み取る | SRV (Shader Resource View) | ImGuiが画像として参照する |

奥行き判定には別のDSV (Depth Stencil View) を使います。これがないと、球の手前と奥の面を正しく重ねられません。撮影済み画像はキャッシュに置き、同じ項目を一覧に表示するたびに撮り直すことはしません。キャッシュ枠を再利用する際は、GPUのフェンス値を見て、まだGPUが読んでいる画像を破棄しないようにしています。

## 実際に描く手順

`Game::DrawSceneToRenderTexture` がエディター描画中に `ProjectWindow::CapturePendingThumbnails` を呼びます。この関数が撮影場所です。

1. 専用の `Object3dCommon` とスタジオ用カメラを使う。現在のアクティブカメラは一時的に退避する。
2. 描画先テクスチャを **SRVとして読む状態**から **RTVとして書く状態**へ `ResourceBarrier` で遷移させる。DirectX 12 では使用目的に応じた状態遷移が必要。
3. RTVとDSVを設定し、前の撮影の色と深度を消す。ビューポートとシザーを256×256にする。
4. 撮影用の `Object3d` を作る。プリセットJSONに記録された見た目を `ImportFromJson` で復元し、実際のゲームシーンのオブジェクトとは分けて描く。モデルの寸法から拡大率と位置を決めて、画像に収める。
5. `Object3d::Draw` で通常の3D描画パイプラインへ流す。頂点シェーダーがカメラに対する位置を計算し、ピクセルシェーダーがテクスチャや材質から色を計算して、RTVに書き込む。
6. 描画先を **RTV状態**から **SRV状態**へ戻す。カメラ・ビューポート・シザーも元に戻す。
7. `ProjectWindow::Draw` がSRVのGPUハンドルを `ImGui::ImageButton` に渡し、一覧の画像になる。

このように、「撮影」は **レンダー・トゥ・テクスチャ**です。CPUで3D画像を描いているわけではありません。撮影する枚数をフレームごとに制限しているので、一覧を開いた瞬間に大量の描画が集中しにくい構造です。

## 今回追加したPBR球プレビュー

PBRの元データはプリセットJSONと異なります。`InspectorTextureCatalog` が `Resources/texture/PBR/` を走査し、同じベース名のAlbedo・Normal・ORMを組にします。球ギャラリーを開くと、表示するセットに対して `ProjectWindow::GetPbrThumbnailGpuPtr` が撮影先を準備します。次の `CapturePendingThumbnails` が撮影用球に3枚を貼って描き、その画像をInspectorに返します。

球は通常材質 `materialType=0` を使います。AlbedoはsRGB、NormalとORMは線形データとして既存の `TextureManager` から読みます。法線マップを有効にし、粗さと金属度の係数を1にして、ORMの緑・青チャンネルの差がプレビューに出るようにしています。既存のモデル・プリセット用撮影は照明を無効にしていますが、PBRの粗さや金属光沢には光が必要なので、PBR球には専用の固定平行光源を渡します。点光源・スポットライトの入力は0個の専用バッファにします。これによりシーンの昼夜やフォグに左右されず、同じ条件で比較できます。

Inspectorでは球の画像をクリックすると、既存の文字一覧と同じく3枚のパスを選択中のオブジェクトへ適用します。複数選択にも適用し、シーンの変更フラグを立てます。画像自体は材質データではありません。保存対象はオブジェクトに設定されたテクスチャのパスです。「素材を再読込」でファイル一覧と撮影画像を更新できます。

## 注意点

- 球は材質を一定条件で比較するための見本です。最終的な見た目は実シーンの照明、環境反射、モデルのUV、ポストエフェクトによって変わります。
- 床や壁など、平面で見るべき素材は球だけでは模様の大きさを判断しにくいです。必要なら将来、平面・立方体への切替を追加できます。
- PBRテクスチャ3枚のセットと、色・粗さ・描画方式まで保存する `MaterialInstance` は別の機能です。この球ギャラリーは前者を選ぶものです。
