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
    // プリセットとモデル候補を揃え、最初の編集状態に岩の破片設定を用意します。
    void Initialize(SceneManager* sceneManager);

        // プレビュー中の破片エフェクトを更新します。
void Update(float deltaTime);

        // 破片モデル、発生数、速度、プリセット関連のUIを描画します。
void DrawImGui() override;

    std::string GetName() override { return "Debris Effect Editor"; }

private:
        // 利用可能なモデルや保存済み設定の一覧を更新します。
void RefreshLists();

    // モデル入力欄を設定へ同期してからJSONを保存し、実行時のPreset登録も更新します。
    void Save(const std::string& presetName);

    // 読込成功時だけ現在の設定と入力欄を差し替えます。
    void Load(const std::string& presetName);

        // 現在の設定で破片エフェクトを発生させ、見た目を確認します。
void Preview();

    engine::Vector3 GetPreviewPosition() const;

        // 設定データからUI編集用のモデルバッファへ内容を反映します。
void SyncModelBuffersFromConfig();

    // 編集中の入力バッファを確定したときだけ、保存対象の設定に反映します。
    void SyncConfigFromModelBuffers();

    // 岩の塊向けに大きめの破片と上方向の初速を設定します。
    void ApplyQuickPresetRock();

    // 木片向けに少数の破片と横への散り方を設定します。
    void ApplyQuickPresetWood();

    // 小石向けに小粒の破片を多めに散らす設定へ戻します。
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

    float previewTime_ = 0.0f;

    int lastStagePlayRequestSerial_ = 0;
    int lastStageStopRequestSerial_ = 0;
    int lastStageSeekRequestSerial_ = 0;

    bool clearBeforePreview_ = true;

    bool loopPreview_ = false;
    float loopInterval_ = 1.2f;
    float loopTimer_ = 0.0f;

    float previewDistance_ = 8.0f;
};
