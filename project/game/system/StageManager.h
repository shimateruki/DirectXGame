#pragma once

#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"
#include "game/system/StageCatalog.h"

// ステージ一覧と現在の選択を管理します。JSON が読めない場合は内蔵の一覧へ戻します。
class StageManager {
  public:
    static StageManager* GetInstance() {
        static StageManager instance;
        return &instance;
    }

    // ステージ一覧を読み込み、Development の選択履歴を復元します。
    void Initialize() {
        if (LoadFromJson("Resources/json/stage_select/stages.json")) {
            LoadDevelopmentStageIndex();
            return;
        }

        stages_ = StageCatalog::CreateFallback();
        currentStageIndex_ = 0;
        LoadDevelopmentStageIndex();
    }

    /// <summary>ステージ一覧を JSON から読み込みます。失敗時は既存の一覧を維持します。</summary>
    /// <param name="path">ステージ一覧 JSON のパス</param>
    bool LoadFromJson(const std::string& path) {
        std::vector<StageData> loadedStages;
        if (!StageCatalog::LoadFromJson(path, loadedStages)) {
            return false;
        }

        stages_ = std::move(loadedStages);
        if (currentStageIndex_ < 0 || currentStageIndex_ >= static_cast<int>(stages_.size())) {
            currentStageIndex_ = 0;
        }
        return true;
    }

    // Initialize() 後に呼び出します。参照の寿命は次の一覧再読み込みまでです。
    const StageData& GetCurrentStage() const { return stages_[currentStageIndex_]; }
    // 範囲外の index は無視します。開発用ビルドでは有効な選択を user_config.json に記録します。
    void SetCurrentStage(int index) {
        if (index >= 0 && index < static_cast<int>(stages_.size())) {
            currentStageIndex_ = index;
            SaveDevelopmentStageIndex();
        }
    }
    int GetCurrentStageIndex() const { return currentStageIndex_; }
    const std::vector<StageData>& GetStages() const { return stages_; }
    // 未登録の ID は -1 を返します。ID は配列の位置とは別に保存データとの対応へ使います。
    int FindStageIndexById(const std::string& stageId) const {
        for (int index = 0; index < static_cast<int>(stages_.size()); ++index) {
            if (stages_[index].id == stageId) {
                return index;
            }
        }
        return -1;
    }
    // ID が見つからない場合は選択と開発用設定ファイルを変更しません。
    bool SetCurrentStageById(const std::string& stageId) {
        const int index = FindStageIndexById(stageId);
        if (index < 0) {
            return false;
        }
        SetCurrentStage(index);
        return true;
    }

  private:
    StageManager() = default;
    ~StageManager() = default;
    StageManager(const StageManager&) = delete;
    StageManager& operator=(const StageManager&) = delete;

    // ImGui 有効の Development ビルドだけで前回の配列位置を復元します。
    // 保存 ID では照合しないため、一覧の並びを変更すると別のステージを指す可能性があります。
    void LoadDevelopmentStageIndex() {
#if defined(USE_IMGUI) && defined(NDEBUG) && !defined(DD)
        std::ifstream file(kUserConfigPath);
        if (!file.is_open()) {
            return;
        }

        try {
            nlohmann::json root;
            file >> root;
            if (!root.is_object()) {
                return;
            }

            int savedIndex = root.value("lastStageIndex", currentStageIndex_);
            if (savedIndex >= 0 && savedIndex < static_cast<int>(stages_.size())) {
                currentStageIndex_ = savedIndex;
            }
        } catch (...) {
        }
#endif
    }

    void SaveDevelopmentStageIndex() const {
#if defined(USE_IMGUI) && defined(NDEBUG) && !defined(DD)
        nlohmann::json root;
        {
            std::ifstream input(kUserConfigPath);
            if (input.is_open()) {
                try {
                    input >> root;
                } catch (...) {
                    root = nlohmann::json::object();
                }
            }
        }

        if (!root.is_object()) {
            root = nlohmann::json::object();
        }

        root["lastStageIndex"] = currentStageIndex_;
        if (currentStageIndex_ >= 0 && currentStageIndex_ < static_cast<int>(stages_.size())) {
            root["lastStageId"] = stages_[currentStageIndex_].id;
        }

        std::ofstream output(kUserConfigPath);
        if (output.is_open()) {
            output << root.dump(4);
        }
#endif
    }

    static constexpr const char* kUserConfigPath = "Resources/json/user_config.json";
    std::vector<StageData> stages_;
    int currentStageIndex_ = 0;
};
