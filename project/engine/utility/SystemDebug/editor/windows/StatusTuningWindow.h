#pragma once

#include "IEditable.h"
#include "Object3d.h"

#include <array>
#include <string>
#include <vector>

class DebugEditor;
class SceneManager;

/// プレイヤーや敵のステータスプリセットを編集し、シーンやステージJSONへ一括反映するウィンドウ。
class StatusTuningWindow : public IEditable {
public:
    void Initialize(DebugEditor* editor, SceneManager* sceneManager);
    void DrawImGui() override;
    std::string GetName() override { return "ステータス調整"; }

    /// HP、攻撃力、移動速度、モデル名など、1種類のキャラクター設定をまとめたプリセット。
    struct StatusPreset {
        float hp = 100.0f;
        float maxHp = 100.0f;
        float attackPower = 1.0f;
        float speed = 1.0f;
        float gravity = 50.0f;
        float jumpPower = 10.0f;
        float detectionRange = 20.0f;
        Vector3 scale = { 0.0f, 0.0f, 0.0f };
        std::string modelName;
        bool morphLimited = true;
        float morphDuration = 5.0f;
    };

    /// 敵タイプ名と対応プリセットをひとまとめにした編集用レコード。
    struct EnemyTypeEntry {
        const char* type;
        const char* label;
        StatusPreset preset;
    };

private:
    /// 保存済みのステータスプリセットJSONを読み込み、UIへ反映する。
    void LoadPresets();
    /// 現在のプリセット設定をJSONへ保存し、次回起動後も使えるようにする。
    void SavePresets();
    void ResetPresetsToDefaults();
    bool DrawPresetEditor(StatusPreset& preset, const char* id);
    void DrawSelectedObjectEditor();
    void DrawCurrentSceneSummary();
    void DrawEnemyPresetRows();
    bool DrawModelPicker(const char* label, const char* id, std::string& modelName);
    void RefreshCharacterModelList();
    bool CapturePlayerPresetFromScene();
    int ApplyPlayerPreset();
    int ApplyPlayerPresetToStageFiles();
    int ApplyEnemyPreset(const std::string& enemyType, const StatusPreset& preset);
    /// 指定敵タイプのプリセットをステージJSON内の該当Enemyへまとめて書き込む。
    int ApplyEnemyPresetToStageFiles(const std::string& enemyType, const StatusPreset& preset);
    void ApplyPresetToObject(Object3d* object, const StatusPreset& preset);
    Object3d::EntityParameter MakeParameter(const StatusPreset& preset, const Object3d::EntityParameter* baseParam) const;

private:
    DebugEditor* editor_ = nullptr;
    SceneManager* sceneManager_ = nullptr;
    StatusPreset playerPreset_;
    std::array<EnemyTypeEntry, 10> enemyPresets_;
    std::vector<std::string> characterModelNames_;
    bool characterModelListReady_ = false;
    std::string statusText_ = "現在シーンのプレイヤー/敵へタイプ別ステータスを一括反映できます。";
};
