#pragma once
#include "IEditable.h"
#include "GPUParticleConfig.h"
#include "GPUParticleManager.h"
#include "engine/utility/math/Math.h"
#include <string>

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

    // エディタ専用の内部変数（タイマーや文字入力バッファ）だけ残す
    float emitTimer_ = 0.0f;
    char presetNameInput_[64] = "FirePreset";

    bool isPreviewMode_ = true;
    float previewDistance_ = 5.0f;
    void EmitWithPreview();
};