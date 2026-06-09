#pragma once

#include "DebrisEffectManager.h"
#include "IEditable.h"

#include <array>
#include <string>
#include <vector>

class SceneManager;

class DebrisEffectEditor : public IEditable {
public:
    void Initialize(SceneManager* sceneManager);
    void Update(float deltaTime);
    void DrawImGui() override;
    std::string GetName() override { return "Debris Effect Editor"; }

private:
    void RefreshLists();
    void Save(const std::string& presetName);
    void Load(const std::string& presetName);
    void Preview();
    Vector3 GetPreviewPosition() const;
    void SyncModelBuffersFromConfig();
    void SyncConfigFromModelBuffers();
    void ApplyQuickPresetRock();
    void ApplyQuickPresetWood();
    void ApplyQuickPresetPebble();

    SceneManager* sceneManager_ = nullptr;
    DebrisEffectConfig config_;
    char presetNameBuffer_[64] = "rock_burst";
    std::array<char, 128> modelBuffer0_ = {};
    std::array<char, 128> modelBuffer1_ = {};
    std::array<char, 128> modelBuffer2_ = {};
    std::array<char, 128> modelBuffer3_ = {};
    std::vector<std::string> modelList_;
    std::vector<std::string> presetList_;
    int selectedPresetIndex_ = -1;
    int lastStagePlayRequestSerial_ = 0;
    bool clearBeforePreview_ = true;
    bool loopPreview_ = false;
    float loopInterval_ = 1.2f;
    float loopTimer_ = 0.0f;
    float previewDistance_ = 8.0f;
};
