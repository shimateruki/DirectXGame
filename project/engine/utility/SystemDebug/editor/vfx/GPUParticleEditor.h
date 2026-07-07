#pragma once
#include "GPUParticleConfig.h"
#include "GPUParticleManager.h"
#include "IEditable.h"
#include "engine/utility/math/Math.h"
#include <string>

/// <summary>
/// GPUパーティクルのプリセット保存、読み込み、プレビュー発生を行う。
/// </summary>
// GPUParticleEditorは、GPUパーティクルの発生形状、色、寿命、テクスチャを編集してプレビューします。
class GPUParticleEditor : public IEditable {
public:
    void Initialize();
        // プレビュー再生やパラメータ変更をフレーム時間に合わせて更新します。
void Update(float deltaTime);
        // 発生、速度、色、テクスチャなどの編集UIを描画します。
void DrawImGui() override;

    void Save(const std::string& presetName);
    void Load(const std::string& presetName);
    std::string GetName() override { return "GPU Particle Editor"; }

private:
    GPUParticleConfig config_;

    // Editor内プレビューと保存名入力の状態。
    float emitTimer_ = 0.0f;
    char presetNameInput_[64] = "FirePreset";
    bool isPreviewMode_ = true;
    float previewDistance_ = 5.0f;
    int lastStagePlayRequestSerial_ = 0;

        // 現在の設定で一度発生させ、見た目を即確認できるようにします。
void EmitWithPreview();
};
