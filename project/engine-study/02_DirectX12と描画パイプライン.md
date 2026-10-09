# 2. DirectX 12 と描画パイプライン

詳細解説：[GPU 命令の同期と画面合成](02A_GPU命令の同期と画面合成.md)。

## 2-1. 最小単位を知る

CPU は画像を直接描くのではなく、GPU に渡す「命令」と「データ」を組み立てます。CG2 では [`DirectXCommon`](../engine/graphics/core/DirectXCommon.cpp) が Direct3D 12 の Device、Command Queue、Command Allocator、Command List、Swap Chain、RTV/DSV などの基盤を作ります。

| 用語 | このエンジンでの役割 | 読む入口 |
|---|---|---|
| Device | Buffer、Texture、Pipeline State 等を生成する GPU インターフェース | `DirectXCommon::Initialize` |
| Command List | このフレームの描画・Compute・Resource Barrier を記録する | `DirectXCommon::PreDraw` / `PostDraw` |
| Command Queue | 記録済み Command List を GPU に提出する | `DirectXCommon::PostDraw` |
| Swap Chain | 表示用 Back Buffer を切り替える | `DirectXCommon::CreateSwapChain` |
| RTV / DSV | 色の書き込み先 / 深度の書き込み先 | `CreateRTV`、`CreateDepthStencilTextureResource` |
| SRV / UAV / CBV | Shader が読む Texture/Buffer、書く Buffer、定数 Buffer | [`SRVManager`](../engine/graphics/core/SRVManager.cpp)、各描画クラス |
| Root Signature | Shader から見えるリソースの配置約束 | [`RootSignatureBuilder`](../engine/graphics/core/RootSignatureBuilder.cpp) |
| PSO | Shader・Blend・Depth・Rasterizer 等の組合せ | [`GraphicsPipelineBuilder`](../engine/graphics/core/GraphicsPipelineBuilder.cpp) |

`Root Signature` と `PSO` は C++ 側と HLSL 側の接点です。たとえば Shader が `register(t0)` を読むなら、C++ 側は対応する SRV を正しい Root Parameter に渡す必要があります。どちらか一方だけ変更しても期待どおりには描けません。

## 2-2. フレーム内の主な描画順

[`Game::DrawSceneToRenderTexture`](../engine/core/Game.cpp) では描画先を Render Texture にし、Shadow Map、3D、効果、UI などを通します。その結果へ [`PostEffect`](../engine/graphics/postprocess/PostEffect.cpp) を適用し、最後に Back Buffer 側へ出します。エディター時は Game View の画像として ImGui 側にも見せます。描画順が重要なのは、後から描く半透明や UI が前の結果に重なるためです。

Shadow Map は「光から見た深度」を先に描き、通常の Object Pixel Shader がその深度と現在ピクセルの光空間深度を比較します。[`Object3d.PS.hlsl`](../Resources/shader/object3d/Object3d.PS.hlsl) では近傍 3×3 の比較結果を平均する PCF 型の影のぼかしを行っています。深度バイアスは影の縞を抑えますが、大きすぎると接地影が浮きます。

## 2-3. 3D Object が画面へ出るまで

[`Object3d`](../engine/graphics/3d/object/Object3d.cpp) は位置・回転・拡縮、親子、描画用 Renderer 等をまとめます。`UpdateLocalMatrix` は自分の Transform、`UpdateWorldMatrix` は親の行列を含んだワールド行列を更新します。親を変える [`SetParent`](../engine/graphics/3d/object/Object3d.cpp) は循環参照を避け、`keepWorldTransform` で見た目の世界位置を保つか選べます。

[`MeshRenderer`](../engine/graphics/3d/MeshRenderer.cpp) がモデル・マテリアル・GPU Buffer を使って描画します。[`Object3dCommon`](../engine/graphics/3d/object/Object3dCommon.cpp) にはマテリアル別の Pipeline が集まり、描画時に PSO を選びます。通常の Object と専用マテリアルは見た目だけでなく、深度・Blend・Shader の選択も異なります。

## 2-4. モデル・アニメーション・2D

モデルデータは [`Model`](../engine/graphics/3d/model/Model.cpp) と関連クラスが読み、描画に使う頂点・Material・Bone を保持します。骨を使うモデルでは [`Skinning.CS.hlsl`](../Resources/shader/object3d/Skinning.CS.hlsl) の Compute Shader 経路もあります。Skinning は「骨行列に応じて頂点を変形する」処理です。Sprite は [`Sprite`](../engine/graphics/2d/Sprite.cpp) と [`SpriteCommon`](../engine/graphics/2d/SpriteCommon.cpp) の別経路を通ります。

## 2-5. なぜ Resource Barrier が必要なのか

同じ GPU Buffer を Compute Shader で書いた後、Vertex Shader で読む場合、GPU に利用段階の切替を知らせます。GPU パーティクルでは UAV への書き込み後に Barrier を入れ、`UNORDERED_ACCESS` から `NON_PIXEL_SHADER_RESOURCE`、Indirect Argument Buffer は `INDIRECT_ARGUMENT` へ遷移します。[`GPUParticleSystem::Draw`](../engine/graphics/particle/GPUParticleSystem.cpp) が具体例です。Barrier が欠けると、CPU のコード順が正しく見えても GPU 側で古い値を読む可能性があります。

## 2-6. 代替案と制約

現行は [`Game`](../engine/core/Game.cpp) と各 Manager が描画パスの順序を明示します。Render Graph 方式ならリソースの依存からパス順と Barrier を組み立てられますが、現行コードがその方式を採用しているわけではありません。新しい描画効果を追加する際は、Shader だけでなく `Object3dCommon` の Pipeline、Root Signature、描画順、必要な深度/Grab Texture を確認します。
