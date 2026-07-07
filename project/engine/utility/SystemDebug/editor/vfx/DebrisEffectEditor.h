#pragma once

#include "DebrisEffectManager.h"
#include "IEditable.h"

#include <array>
#include <string>
#include <vector>

class SceneManager;

// DebrisEffectEditorは、破片エフェクトのモデル、飛散量、速度、プリセットを編集するツールです。
class DebrisEffectEditor : public IEditable {
public:
    void Initialize(SceneManager* sceneManager);
        // プレビュー中の破片エフェクトを更新します。
void Update(float deltaTime);
        // 破片モデル、発生数、速度、プリセット関連のUIを描画します。
void DrawImGui() override;
    std::string GetName() override { return "Debris Effect Editor"; }

private:
        // 利用可能なモデルや保存済み設定の一覧を更新します。
void RefreshLists();
    void Save(const std::string& presetName);
    void Load(const std::string& presetName);
        // 現在の設定で破片エフェクトを発生させ、見た目を確認します。
void Preview();
    Vector3 GetPreviewPosition() const;
        // 設定データからUI編集用のモデルバッファへ内容を反映します。
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
