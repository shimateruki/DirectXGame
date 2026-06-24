#pragma once
#include "GPUParticleConfig.h"
#include "GPUParticleManager.h"
#include "IEditable.h"
#include "engine/utility/math/Math.h"
#include <string>

/// <summary>
/// GPUパーティクルのプリセット保存、読み込み、プレビュー発生を行う。
/// </summary>
class GPUParticleEditor : public IEditable {
public:
    void Initialize();
    void Update(float deltaTime);
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

    void EmitWithPreview();
};
