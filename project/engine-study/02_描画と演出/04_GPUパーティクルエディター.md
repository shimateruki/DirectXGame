# 4. GPU パーティクルエディターを内部まで追う

このページは入口です。[メモリ・並列処理・運動方程式・シークの詳細](04A_GPUパーティクルのメモリと計算.md)で、実際のバッファと数式を順に説明します。

## 4-1. 画面で何を編集しているのか

[`GPUParticleEditor`](../../engine/utility/SystemDebug/editor/vfx/GPUParticleEditor.cpp) は [`GPUParticleConfig`](../../engine/graphics/particle/GPUParticleConfig.h) を編集します。形状（Box/Sphere/Cone）、発生数、寿命、初速、色とサイズの中間点、Texture、Blend、Sprite Sheet、重力、風、乱流、Field、衝突、ループ間隔などが一つの設定に入っています。**Editor は粒子そのものを毎粒 CPU で動かしているのではなく、設定を作って Runtime へ渡す画面です。**

プリセットは `GPUParticleEditor::Save` / `Load` が JSON に読み書きします。ゲーム側は [`GPUParticleManager::LoadAllPresets`](../../engine/graphics/particle/GPUParticleManager.cpp) でプリセットを読み、`Emit` / `EmitDirected` / `EmitFromConfig` から発生要求を出します。保存は現在の Editor 設定、発生はその設定を使う別の操作です。

## 4-2. CPU と GPU の担当を分ける

```text
ImGui 操作
  → GPUParticleConfig
  → GPUParticleManager::EmitFromConfig
  → GPUParticleSystem の発生要求
  → ParticleCS.hlsl の Init / Emit / Update
  → 生存粒子インデックスと描画引数
  → GPUParticleVS/PS + ExecuteIndirect
  → Game View または Preview Stage
```

CPU は「いつ、どの設定で何個発生させるか」を決めます。GPU は [`ParticleCS.hlsl`](../../Resources/shader/particle/ParticleCS.hlsl) の Compute Shader で各粒子の状態を並列に更新します。`RWStructuredBuffer<Particle>` は粒子本体、Free List は空きスロット、生存粒子リストは描画対象の番号を保持します。消えた粒子のスロットを再利用できるので、毎フレーム CPU で巨大配列を作り直す必要がありません。

## 4-3. Compute Shader の段階

[`GPUParticleSystem::Draw`](../../engine/graphics/particle/GPUParticleSystem.cpp) は必要に応じて `InitCS` を Dispatch して Buffer を初期化し、Draw Argument をリセットしてから `UpdateCS` を実行します。更新には `deltaTime`、重力、風、Field、深度 Texture などの設定が入ります。次に発生要求ごとに `EmitCS` を Dispatch します。初期化と発生が同じフレームに重なるシーク時は、発生直後の粒子も見えるよう生存リストを再構築します。

`Dispatch` は Compute Shader のスレッド群を起動する命令です。粒子数に応じてグループ数を計算しているため、同じ Shader で発生数を変えられます。UAV Barrier は Compute の書き込み同士の順序を保証し、描画前には UAV から SRV / Indirect Argument へリソース状態を切り替えます。

## 4-4. 描画で使う技術

[`GPUParticleVS.hlsl`](../../Resources/shader/particle/GPUParticleVS.hlsl) は生存粒子インデックスから粒子データを読み、カメラに向く Billboard などの頂点を作ります。[`GPUParticlePS.hlsl`](../../Resources/shader/particle/GPUParticlePS.hlsl) が Texture、色、透明度、Soft Particle 等を計算します。Sprite Sheet は寿命に応じたコマ送り、Velocity Alignment は移動方向に沿った見た目を作る設定です。

`ExecuteIndirect` は GPU が書いた描画引数を GPU 自身が読み、描画数を決める技術です。CPU が全粒子を数えて描画コマンドを作る経路より、GPU 内で生存判定から描画までつなぎやすくなります。PSO は加算・半透明・歪み用に切り替えます。

## 4-5. プレビューと時間移動

[`EffectPreviewStage`](../../engine/utility/SystemDebug/editor/windows/EffectPreviewStage.cpp) が有効なら、そのプレビュー位置を発生原点に使います。`GPUParticleEditor::Update` は再生・停止・シーク要求を監視します。シークは Reset 後に CPU 側の Manager を 1/60 秒ずつ進めますが、GPU Dispatch は Draw まで行われません。次の Draw で `initialAge` を使い、重力・風・減衰による位置を近似復元します。過去の衝突、Field、乱流の履歴を再実行する方式ではありません。

**プレビューで見えることと、実ステージで見えることは別です。** 実ステージでは発生位置、カメラ、深度、Blend、ライト、周囲の材質が変わります。完成判定は Preview Stage に加え、ゲーム内で使うカメラ角度と背景でも確認します。

## 4-6. 設計上の制約と代替案

GPU 粒子は大量の単純な粒子を並列に動かすのに向きます。一粒ずつ複雑なゲームロジックや敵との厳密なイベント連携が必要なら、CPU 側の Effect/Object が適する場合があります。現行の GPU シークは年齢を使う近似なので、連続再生との完全一致を保証しません。GPU の状態 Snapshot やステップごとの Dispatch は改善案として考えられますが、現行の実装とは区別します。
