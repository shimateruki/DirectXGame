# VFX 制作エディターと時間軸

詳細解説：[タイムラインとカーブの実装](05A_タイムラインとカーブの実装.md)。

[目次](../00_目次と読み方.md)へ。GPU 粒子だけの計算方式は[前章](04_GPUパーティクルエディター.md)を参照。この章は「一つの効果」と「複数の効果を時間順に鳴らす演出」を区別する。

## まずデータの層を分ける

| 層 | 技術と役割 | 主な実装 |
| --- | --- | --- |
| 単体の効果 | CPU 粒子、GPU 粒子、3D メッシュ、破片などのパラメータ | [`ParticleSystem`](../../engine/graphics/particle/ParticleSystem.cpp)、[`GPUParticleSystem`](../../engine/graphics/particle/GPUParticleSystem.cpp)、[`MeshEffectManager`](../../engine/graphics/effect/MeshEffectManager.cpp)、[`DebrisEffectManager`](../../engine/graphics/effect/DebrisEffectManager.cpp) |
| 発生位置の軌跡 | 動く対象を追い、間隔を決めて効果を発生させる | [`TrailEmitter`](../../engine/graphics/effect/TrailEmitter.cpp) |
| 演出の時刻表 | 時刻と継続時間を持つイベントを実行する | [`VFXSequencer`](../../engine/graphics/particle/VFXSequencer.cpp) |
| 編集用の舞台 | カメラ前の位置、再生速度、停止、シークを各ツールへ渡す | [`EffectPreviewStage`](../../engine/utility/SystemDebug/editor/windows/EffectPreviewStage.cpp) |

この区別が重要なのは、パーティクルの「寿命」とシーケンスの「イベント開始時刻」は別の時計だから。例えば `0.2 秒に GPU 粒子を発生` という設定は、0.2 秒間だけ粒子が生きるという意味ではない。イベントが粒子の発生処理を呼ぶ時刻を決める。

## 単体エディターを操作したとき

### CPU Particle Editor

[`ParticleEditor`](../../engine/utility/SystemDebug/editor/vfx/ParticleEditor.cpp) の UI は `ParticleSystem` のパラメータを編集し、プレビュー対象へ反映する。`ParticleManager::SaveParam` / `LoadParam` が JSON の保存・読込入口。再生位置を戻す際は `ResetSimulation` した後に `1/60` 秒ずつ進め、指定時刻の状態を再構築する。これは過去の任意時刻の粒子状態を直接計算する仕組みではなく、初期状態から再シミュレーションする方式。

CPU 側で粒子の更新を追うなら [`ParticleSystem::Update`](../../engine/graphics/particle/ParticleSystem.cpp) を読む。発生、寿命、位置・色・大きさの変化と、描画に渡すデータの関係が分かる。GPU 版と比べると、粒子ごとの更新が CPU 側にある点が違う。

### Mesh Effect Editor と Debris Editor

[`MeshEffectEditor`](../../engine/utility/SystemDebug/editor/vfx/MeshEffectEditor.cpp) は `Resources/json/effect/` の設定と `EffectObject3d` のプレビューを扱う。モデルを持つ破片や衝撃表現は単なる板ポリゴン粒子とは見え方・描画コストが異なる。[`DebrisEffectEditor`](../../engine/utility/SystemDebug/editor/vfx/DebrisEffectEditor.cpp) は破片用パラメータの編集画面で、実行側は [`DebrisEffectManager`](../../engine/graphics/effect/DebrisEffectManager.cpp)。形状・物理的な移動・発生数のどれが見た目を支配するか、プリセットとプレビューを往復して確認する。

### Trail Emitter Editor

[`TrailEmitterEditor`](../../engine/utility/SystemDebug/editor/vfx/TrailEmitterEditor.cpp) は `Resources/json/trail_emitter/` に JSON を保存する。設定から `Resources/json/effect/` のメッシュ効果や `Resources/json/gpu_particles/` の GPU 粒子プリセットを参照できる。ここでいう Trail は「線を画面に一本描く」機能とは限らず、移動する発生源から別の効果を連続生成する構成。[`TrailEmitter`](../../engine/graphics/effect/TrailEmitter.cpp) の更新条件を読むと、距離や時間による発生の意味が分かる。シーク時にはダミー対象と効果をリセットし、CPU 側を固定刻みで進める。GPU 粒子はそのループ内で毎回 Dispatch されるわけではなく、Draw 時の年齢による近似復元になる。

### VFX Authoring の共通曲線 UI

[`VFXAuthoringEditor`](../../engine/utility/SystemDebug/editor/vfx/VFXAuthoringEditor.h) は独立したウィンドウではなく、VFX の曲線を描画・編集する共通 UI ヘルパー。`VFXFloatCurve::Evaluate` で 0～1 の時間範囲をサンプリングし、キーの時刻・値・補間方法を編集する。`EditorPropertyTransaction::TrackLastItem` はドラッグ開始から終了までを一回の Undo にまとめる。曲線の評価と、シーケンサーで効果を発火する時刻は別の階層。

## VFX Sequencer のタイムライン

[`VFXSequencerEditor`](../../engine/utility/SystemDebug/editor/vfx/VFXSequencerEditor.cpp) はイベントを画面上の横軸に配置する。ドラッグ位置を秒に変換し、必要なら `0.05` 秒刻みへスナップする。イベントには `triggerTime` と種類に応じた `duration` がある。[`VFXSequencer`](../../engine/graphics/particle/VFXSequencer.h) が経過時間とイベント時刻を照合して実行する。つまりタイムラインの四角形そのものが演出を動かすわけではなく、保存された時刻データを実行側の更新処理が読む。

現行のイベント種別は GPU 粒子、メッシュ効果、音、移動粒子、カメラ揺れ、ポストエフェクトのパルス、ライトのパルス、ヒットストップ、コントローラー振動、画角のパルス。種類ごとに必要な参照先や強度が違うため、イベント内容を[`VFXSequencer.cpp`](../../engine/graphics/particle/VFXSequencer.cpp)で追う。保存先は `Resources/json/vfx_sequence/`。プレビューで見えたシーケンスをゲームから使うには、ゲーム側がその名前をロードして再生する経路まで確認する。

シークではプレビューの Sequencer、メッシュ効果、GPU 粒子をリセットし、CPU 側の更新を `1/60` 秒と端数で進める。Sequencer は各刻みでイベントを処理するが、GPU 粒子は次の Draw まで時刻と要求を蓄積し、発生時の年齢で近似復元する。GPU の衝突や Field の履歴まで固定刻み再計算するわけではない。[GPU のシークの詳細](04A_GPUパーティクルのメモリと計算.md)を参照。

## プレビューで確かめられる範囲

[`EffectPreviewStage`](../../engine/utility/SystemDebug/editor/windows/EffectPreviewStage.cpp) は各 VFX エディターの再生・停止・速度・シークを共通の操作にする。これは見た目やタイミングを素早く比較するための専用環境。実シーンの深度、実際の壁や敵、カメラ演出との重なり、負荷の同時発生を保証するものではない。完成判定には実シーンでの Play が必要。

## 別の方法との比較

- **直接キーフレーム方式**なら任意時刻のプロパティを補間できるが、粒子の過去の衝突や乱数結果を単純な補間だけで復元できない。
- **全フレーム記録方式**なら記録した状態へ戻しやすいが、粒子数と再生時間に比例して記録量が増える。現行プレビューは CPU 側の再更新と GPU 側の年齢による近似を組み合わせる。
- **GPU 粒子だけで統一**すると大量描画に向く場合があるが、メッシュや音・カメラなど異なる表現を一つのイベント型へ押し込む必要がある。現行は種類別イベントを Sequencer で束ねる。
