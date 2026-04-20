#pragma once
#include "IEditable.h"
#include "VFXSequencer.h"

// ==========================================================
// VFXシーケンサーをImGuiで組み立てる専用エディタ
// ==========================================================
class VFXSequencerEditor : public IEditable {
public:

    void Initialize();
    void Update(float deltaTime);
    void DrawImGui() override;

    std::string GetName() override { return "VFX Sequencer Editor"; }
    void RefreshFileList();
private:
    VFXSequencer previewSequencer_; // エディタ上で再生テストするための本体
    char sequenceNameInput_[64] = "UltimateMeteor";
    std::vector<std::string> particlePresetList_; // パーティクル（素材）のリスト
    std::vector<std::string> sequenceFileList_;   // シーケンス（必殺技）のリスト
    std::vector<std::string> meshEffectList_;
    std::vector<std::string> seFileList_;

};