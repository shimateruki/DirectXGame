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
    /// <summary>共有のStageManagerを返す。</summary>
    static StageManager* GetInstance() {
        static StageManager instance;
        return &instance;
    }

    /// <summary>ステージ一覧を読み、失敗時は内蔵一覧へ戻す。</summary>
    /// <remarks>Developmentの選択履歴も復元する。</remarks>
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
        // 読み込みに成功するまで公開中の一覧を差し替えない。
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

    /// <summary>現在選択中のステージ定義を返す。</summary>
    /// <remarks>Initialize後に呼ぶ。参照は次の一覧再読み込みまで有効。</remarks>
    const StageData& GetCurrentStage() const { return stages_[currentStageIndex_]; }
    /// <summary>有効な配列番号を現在ステージに設定する。</summary>
    /// <remarks>範囲外は無視し、開発用ビルドでは選択をuser_config.jsonへ記録する。</remarks>
    void SetCurrentStage(int index) {
        if (index >= 0 && index < static_cast<int>(stages_.size())) {
            currentStageIndex_ = index;
            SaveDevelopmentStageIndex();
        }
    }
    /// <summary>現在ステージの配列番号を返す。</summary>
    int GetCurrentStageIndex() const { return currentStageIndex_; }
    /// <summary>全ステージ定義を返す。参照は一覧再読み込みまで有効。</summary>
    const std::vector<StageData>& GetStages() const { return stages_; }
    /// <summary>保存データで用いるステージIDから配列番号を探す。</summary>
    /// <returns>未登録のIDなら-1。</returns>
    int FindStageIndexById(const std::string& stageId) const {
        for (int index = 0; index < static_cast<int>(stages_.size()); ++index) {
            if (stages_[index].id == stageId) {
                return index;
            }
        }
        return -1;
    }
    /// <summary>ステージIDから現在ステージを変更する。</summary>
    /// <returns>IDが見つからなければfalse。選択と設定ファイルは変更しない。</returns>
    bool SetCurrentStageById(const std::string& stageId) {
        const int index = FindStageIndexById(stageId);
        if (index < 0) {
            return false;
        }
        SetCurrentStage(index);
        return true;
    }

  private:
    /// <summary>共有インスタンスの初期状態を作る。</summary>
    StageManager() = default;
    /// <summary>共有インスタンスの保持データを破棄する。</summary>
    ~StageManager() = default;
    StageManager(const StageManager&) = delete;
    StageManager& operator=(const StageManager&) = delete;

    /// <summary>開発用ビルドで前回選択したステージの配列位置を復元する。</summary>
    /// <remarks>保存IDと照合しないため、一覧の並び替え後は別ステージを指す可能性がある。</remarks>
    void LoadDevelopmentStageIndex() {
#if defined(USE_IMGUI) && defined(NDEBUG) && !defined(DD)
        // 開発時の前回選択だけを復元し、通常のセーブデータには影響させない。
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

    /// <summary>開発用ビルドで現在の配列位置をユーザー設定へ保存する。</summary>
    void SaveDevelopmentStageIndex() const {
#if defined(USE_IMGUI) && defined(NDEBUG) && !defined(DD)
        // 既存のユーザー設定を読み、他の項目を消さずステージ選択だけ更新する。
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
