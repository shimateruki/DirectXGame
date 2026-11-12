#pragma once
#include "GpuParticleConfig.h"
#include "GpuParticleManager.h"
#include "IEditable.h"
#include "engine/utility/math/Math.h"
#include <string>

// 担当：GPU粒子の形状・色・寿命・画像を編集し、保存・プレビューする。
// 関連：GpuParticleManager（発生）、GpuParticleConfig（設定）、EffectPreviewStage（確認環境）
// 主な入口：Initialize / Update / DrawImGui / Save / Load
class GpuParticleEditor : public IEditable {
public:
    // GPU粒子の編集状態と候補一覧を初期化する。
    void Initialize();

        // プレビュー再生やパラメータ変更をフレーム時間に合わせて更新します。
void Update(float deltaTime, bool sceneIsPlaying = false);

        // 発生、速度、色、テクスチャなどの編集UIを描画します。
void DrawImGui() override;

    // Preview Stageの一時座標は保存せず、実行設定だけを共通形式へ変換する。
    void Save(const std::string& presetName);

    // 指定名のプリセットを編集用設定へ読み込む。
    void Load(const std::string& presetName);

    std::string GetName() override { return "GPU Particle Editor"; }

private:
    GpuParticleConfig config_;

    // Editor内プレビューと保存名入力の状態。
    float emitTimer_ = 0.0f;
    char presetNameInput_[64] = "FirePreset";
    char presetSearch_[128] = {};
    char textureSearch_[128] = {};
    bool isPreviewMode_ = true;
    float previewDistance_ = 5.0f;
    float previewTime_ = 0.0f;
    int lastStagePlayRequestSerial_ = 0; // 前回処理したプレビュー再生要求の番号。
    int lastStageStopRequestSerial_ = 0; // 前回処理したプレビュー停止要求の番号。
    int lastStageSeekRequestSerial_ = 0; // 前回処理したプレビューシーク要求の番号。

        // 現在の設定で一度発生させ、見た目を即確認できるようにします。
void EmitWithPreview();
};
