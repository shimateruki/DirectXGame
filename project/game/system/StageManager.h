#pragma once
#include <string>
#include <vector>

/// <summary>
/// 各ステージのメタデータを保持する構造体
/// </summary>
struct StageData {
    std::string name;           // ステージ表示名
    std::string levelPath;      // 3Dオブジェクトの配置データ
    std::string spritePath;     // スプライトの配置データ
    std::string bgmPath;        // そのステージで流すBGM
};

/// <summary>
/// ステージの構成情報を一括管理するマネージャ
/// </summary>
class StageManager {
public:
    static StageManager* GetInstance() {
        static StageManager instance;
        return &instance;
    }

    /// <summary>
    /// ステージリストの初期化 (本来はJSONから読み込むのが理想)
    /// </summary>
    void Initialize() {
        stages_.clear();
        
        // ステージ1
        stages_.push_back({
            "Stage 1",
            "Resources/json/3Dobject/stage1.json",
            "Resources/json/sprite/stage1_sprite.json",
            "Resources/bgm/Alarm02.mp3"
        });

        // ステージ2
        stages_.push_back({
            "Stage 2",
            "Resources/json/3Dobject/stage2.json",
            "Resources/json/sprite/stage2_sprite.json",
            "Resources/bgm/Alarm02.mp3"
        });

        // ステージ3
        stages_.push_back({
            "Stage 3",
            "Resources/json/3Dobject/stage3.json",
            "Resources/json/sprite/stage3_sprite.json",
            "Resources/bgm/Alarm02.mp3"
        });

        // ステージ4
        stages_.push_back({
            "Stage 4",
            "Resources/json/3Dobject/stage4.json",
            "Resources/json/sprite/stage4_sprite.json",
            "Resources/bgm/Alarm02.mp3"
        });

        // ステージ5
        stages_.push_back({
            "Stage 5",
            "Resources/json/3Dobject/stage5.json",
            "Resources/json/sprite/stage5_sprite.json",
            "Resources/bgm/Alarm02.mp3"
        });
    }

    // --- アクセッサ ---
    const StageData& GetCurrentStage() const { return stages_[currentStageIndex_]; }
    void SetCurrentStage(int index) { 
        if (index >= 0 && index < stages_.size()) currentStageIndex_ = index; 
    }
    int GetCurrentStageIndex() const { return currentStageIndex_; }
    const std::vector<StageData>& GetStages() const { return stages_; }

private:
    StageManager() = default;
    ~StageManager() = default;
    StageManager(const StageManager&) = delete;
    StageManager& operator=(const StageManager&) = delete;

    std::vector<StageData> stages_;
    int currentStageIndex_ = 0;
};
