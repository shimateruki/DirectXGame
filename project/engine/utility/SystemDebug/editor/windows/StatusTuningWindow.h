#pragma once

#include "IEditable.h"
#include "Object3d.h"

#include <array>
#include <string>

class DebugEditor;
class SceneManager;

class StatusTuningWindow : public IEditable {
public:
    void Initialize(DebugEditor* editor, SceneManager* sceneManager);
    void DrawImGui() override;
    std::string GetName() override { return "ステータス調整 (Status Tuning)"; }

    struct StatusPreset {
        float hp = 100.0f;
        float maxHp = 100.0f;
        float attackPower = 1.0f;
        float speed = 1.0f;
        float gravity = 50.0f;
        float jumpPower = 10.0f;
        float detectionRange = 20.0f;
    };

    struct EnemyTypeEntry {
        const char* type;
        const char* label;
        StatusPreset preset;
    };

private:
    void LoadPresets();
    void SavePresets();
    void ResetPresetsToDefaults();
    void DrawPresetEditor(StatusPreset& preset, const char* id);
    void DrawSelectedObjectEditor();
    void DrawCurrentSceneSummary();
    void DrawEnemyPresetRows();
    int ApplyPlayerPreset();
    int ApplyEnemyPreset(const std::string& enemyType, const StatusPreset& preset);
    void ApplyPresetToObject(Object3d* object, const StatusPreset& preset);
    Object3d::EntityParameter MakeParameter(const StatusPreset& preset, const Object3d::EntityParameter* baseParam) const;

private:
    DebugEditor* editor_ = nullptr;
    SceneManager* sceneManager_ = nullptr;
    StatusPreset playerPreset_;
    std::array<EnemyTypeEntry, 10> enemyPresets_;
    std::string statusText_ = "現在シーンのPlayer/Enemyへタイプ別ステータスを一括反映できます。";
};
