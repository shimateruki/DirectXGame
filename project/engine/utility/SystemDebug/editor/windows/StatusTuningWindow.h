#pragma once

#include "GameplayStatusManager.h"
#include "IEditable.h"

#include <array>
#include <string>
#include <vector>

class DebugEditor;
class SceneManager;

/// PlayerとEnemy Typeごとの共通ステータスをリアルタイム調整するウィンドウ。
class StatusTuningWindow : public IEditable {
public:
    void Initialize(DebugEditor* editor, SceneManager* sceneManager);
    void DrawImGui() override;
    std::string GetName() override { return "ステータス管理"; }

private:
    struct EnemyTypeInfo {
        const char* type;
        const char* label;
    };

    bool DrawStatusEditor(GameplayStatusManager::CharacterStatus& status, const char* id);
    bool DrawModelPicker(const char* label, const char* id, std::string& modelName);
    void DrawCurrentSceneSummary();
    void DrawEnemyStatusRows();
    void RefreshCharacterModelList();
    int ApplyPlayerStatusLive();
    int ApplyEnemyStatusLive(const std::string& enemyType);
    int ApplyAllStatusLive();

private:
    DebugEditor* editor_ = nullptr;
    SceneManager* sceneManager_ = nullptr;
    std::array<EnemyTypeInfo, 12> enemyTypes_ = {
        EnemyTypeInfo{ "Slime", "ピンクスライム" },
        EnemyTypeInfo{ "Bomb", "ボム" },
        EnemyTypeInfo{ "Bomber", "ボムスライム" },
        EnemyTypeInfo{ "Mushroom", "キノコ" },
        EnemyTypeInfo{ "FireSlime", "ファイアスライム" },
        EnemyTypeInfo{ "ThunderSlime", "サンダースライム" },
        EnemyTypeInfo{ "WindSlime", "風スライム" },
        EnemyTypeInfo{ "GiantSlime", "巨大スライム" },
        EnemyTypeInfo{ "PrismSlime", "プリズムスライム（中ボス）" },
        EnemyTypeInfo{ "Bat", "コウモリ" },
        EnemyTypeInfo{ "BeamDrone", "ビームドローン" },
        EnemyTypeInfo{ "BossCore", "ボスコア" }
    };
    std::vector<std::string> characterModelNames_;
    std::string statusText_ = "変更内容は現在シーンへリアルタイム反映され、操作終了時に自動保存されます。";
};
