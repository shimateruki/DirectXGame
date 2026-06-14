#pragma once
#include <string>
#include <fstream>
#include <vector>
#include <algorithm>
#include <map>
#include <sstream>
#include <filesystem>
#include <utility>
#include "json.hpp"

class GameDataManager {
public:
    static constexpr int kSaveSlotCount = 3;

    struct SaveSlotSummary {
        bool exists = false;
        bool tutorialCleared = false;
        int clearedStageCount = 0;
        int collectedStarCoins = 0;
        int lives = 3;
        int coins = 0;
        int playTimeSeconds = 0;
    };

    static GameDataManager* GetInstance() {
        static GameDataManager instance;
        return &instance;
    }

    void Initialize() {
        Load();
    }

    void SetActiveSlot(int slotIndex) {
        if (slotIndex < 0) slotIndex = 0;
        if (slotIndex >= kSaveSlotCount) slotIndex = kSaveSlotCount - 1;
        activeSlot_ = slotIndex;
        Load();
    }

    int GetActiveSlot() const { return activeSlot_; }

    std::string GetSaveFilePath() const {
        return GetSaveFilePath(activeSlot_);
    }

    std::string GetSaveFilePath(int slotIndex) const {
        if (slotIndex < 0) slotIndex = 0;
        if (slotIndex >= kSaveSlotCount) slotIndex = kSaveSlotCount - 1;
        return "Resources/json/save/savedata_slot" + std::to_string(slotIndex + 1) + ".json";
    }

    SaveSlotSummary GetSlotSummary(int slotIndex) const {
        SaveSlotSummary summary;
        nlohmann::json data;
        if (!ReadSaveJson(slotIndex, data)) {
            return summary;
        }

        summary.exists = true;
        summary.lives = data.value("lives", 3);
        summary.coins = data.value("coins", 0);
        summary.playTimeSeconds = data.value("playTimeSeconds", 0);

        if (data.contains("clearedStages") && data["clearedStages"].is_array()) {
            for (const auto& stage : data["clearedStages"]) {
                if (!stage.is_number_integer()) continue;
                int stageIndex = stage.get<int>();
                if (stageIndex == -1) {
                    summary.tutorialCleared = true;
                } else if (stageIndex >= 0) {
                    summary.clearedStageCount++;
                }
            }
        }

        if (data.contains("starCoins") && data["starCoins"].is_object()) {
            for (auto it = data["starCoins"].begin(); it != data["starCoins"].end(); ++it) {
                if (!it.value().is_array()) continue;
                for (const auto& coin : it.value()) {
                    if (coin.is_boolean() && coin.get<bool>()) {
                        summary.collectedStarCoins++;
                    }
                }
            }
        }

        return summary;
    }

    // --- セーブ・ロード ---
    void Save() {
        nlohmann::json data;
        
        // 既存のファイルを読み込んで他の値を保持する
        std::ifstream inFile(GetSaveFilePath());
        if (!inFile.is_open() && activeSlot_ == 0) {
            inFile.open(GetLegacySaveFilePath());
        }
        if (inFile.is_open()) {
            inFile >> data;
            inFile.close();
        }

        data["lives"] = lives_;
        data["coins"] = coins_;
        data["playTimeSeconds"] = playTimeSeconds_;
        data["clearedStages"] = clearedStages_; // クリア済みステージのインデックス
        data["seenUnlockedStages"] = seenUnlockedStages_;

        // スターコイン情報の保存
        nlohmann::json coinData;
        for (auto& [stageIdx, coins] : stageStarCoins_) {
            coinData[std::to_string(stageIdx)] = coins;
        }
        data["starCoins"] = coinData;

        std::filesystem::create_directories(std::filesystem::path(GetSaveFilePath()).parent_path());
        std::ofstream outFile(GetSaveFilePath());
        if (outFile.is_open()) {
            outFile << data.dump(4);
            outFile.close();
        }
    }

    void Load() {
        nlohmann::json data;
        if (!ReadSaveJson(activeSlot_, data)) {
            lives_ = 3;
            coins_ = 0;
            playTimeSeconds_ = 0;
            clearedStages_.clear();
            seenUnlockedStages_.clear();
            stageStarCoins_.clear();
            return;
        }

        lives_ = 3;
        coins_ = 0;
        playTimeSeconds_ = 0;
        clearedStages_.clear();
        seenUnlockedStages_.clear();
        stageStarCoins_.clear();

        if (data.contains("lives")) lives_ = data["lives"];
        if (data.contains("coins")) coins_ = data["coins"];
        if (data.contains("playTimeSeconds")) playTimeSeconds_ = data["playTimeSeconds"];
        if (data.contains("clearedStages")) clearedStages_ = data["clearedStages"].get<std::vector<int>>();
        if (data.contains("seenUnlockedStages")) seenUnlockedStages_ = data["seenUnlockedStages"].get<std::vector<int>>();
        
        if (data.contains("starCoins")) {
            for (auto it = data["starCoins"].begin(); it != data["starCoins"].end(); ++it) {
                int stageIdx = std::stoi(it.key());
                std::vector<bool> coins = it.value().get<std::vector<bool>>();
                coins.resize(3, false);
                stageStarCoins_[stageIdx] = std::move(coins);
            }
        }
    }

    // --- 残機操作 ---
    int GetLives() const { return lives_; }
    void SetLives(int lives) { lives_ = std::clamp(lives, 0, 99); Save(); }
    void SubtractLife() { lives_--; if (lives_ < 0) lives_ = 0; Save(); }
    void ResetLives() { lives_ = 3; Save(); }

    // --- コイン操作 ---
    int GetCoins() const { return coins_; }
    void SetCoins(int coins) { coins_ = std::clamp(coins, 0, 9999); Save(); }
    void AddCoin(int amount = 1) {
        coins_ += amount;
        while (coins_ >= 100) {
            coins_ -= 100;
            lives_++;
        }
        Save();
    }
    void ResetCoins() { coins_ = 0; Save(); }

    int GetPlayTimeSeconds() const { return playTimeSeconds_; }
    void SetPlayTimeSeconds(int seconds) {
        playTimeSeconds_ = std::clamp(seconds, 0, 99 * 60 * 60 + 59 * 60 + 59);
        Save();
    }

    void RequestRespawnIrisIn() { pendingRespawnIrisIn_ = true; }
    bool ConsumeRespawnIrisInRequest() {
        const bool requested = pendingRespawnIrisIn_;
        pendingRespawnIrisIn_ = false;
        return requested;
    }

    // --- ステージクリア状況 ---
    void MarkStageCleared(int index) {
        if (std::find(clearedStages_.begin(), clearedStages_.end(), index) == clearedStages_.end()) {
            clearedStages_.push_back(index);
            Save();
        }
    }

    void SetStageCleared(int index, bool cleared) {
        auto it = std::find(clearedStages_.begin(), clearedStages_.end(), index);
        if (cleared) {
            if (it == clearedStages_.end()) {
                clearedStages_.push_back(index);
                Save();
            }
            return;
        }

        if (it != clearedStages_.end()) {
            clearedStages_.erase(it);
            Save();
        }
    }

    bool IsStageCleared(int index) const {
        return std::find(clearedStages_.begin(), clearedStages_.end(), index) != clearedStages_.end();
    }

    void MarkStageUnlockSeen(int index) {
        if (std::find(seenUnlockedStages_.begin(), seenUnlockedStages_.end(), index) == seenUnlockedStages_.end()) {
            seenUnlockedStages_.push_back(index);
            Save();
        }
    }

    void SetStageUnlockSeen(int index, bool seen) {
        auto it = std::find(seenUnlockedStages_.begin(), seenUnlockedStages_.end(), index);
        if (seen) {
            if (it == seenUnlockedStages_.end()) {
                seenUnlockedStages_.push_back(index);
                Save();
            }
            return;
        }

        if (it != seenUnlockedStages_.end()) {
            seenUnlockedStages_.erase(it);
            Save();
        }
    }

    bool IsStageUnlockSeen(int index) const {
        return std::find(seenUnlockedStages_.begin(), seenUnlockedStages_.end(), index) != seenUnlockedStages_.end();
    }

    // --- スターコイン操作 ---
    void MarkStarCoinCollected(int stageIdx, int coinIdx) {
        if (coinIdx < 0 || coinIdx >= 3) return;
        SetStarCoinCollected(stageIdx, coinIdx, true);
    }

    void SetStarCoinCollected(int stageIdx, int coinIdx, bool collected) {
        if (coinIdx < 0 || coinIdx >= 3) return;

        auto& coins = GetOrCreateStarCoins(stageIdx);
        if (coins[coinIdx] == collected) return;
        coins[coinIdx] = collected;
        Save();
    }

    bool IsStarCoinCollected(int stageIdx, int coinIdx) const {
        if (stageStarCoins_.find(stageIdx) == stageStarCoins_.end()) return false;
        if (coinIdx < 0 || coinIdx >= 3) return false;
        return stageStarCoins_.at(stageIdx)[coinIdx];
    }

    int GetClearedStageCount() const {
        int count = 0;
        for (int stageIndex : clearedStages_) {
            if (stageIndex >= 0) {
                ++count;
            }
        }
        return count;
    }

    int GetCollectedStarCoinCount() const {
        int count = 0;
        for (const auto& [stageIndex, coins] : stageStarCoins_) {
            (void)stageIndex;
            for (bool collected : coins) {
                if (collected) {
                    ++count;
                }
            }
        }
        return count;
    }

    void ResetAll() {
        lives_ = 3;
        coins_ = 0;
        playTimeSeconds_ = 0;
        clearedStages_.clear();
        seenUnlockedStages_.clear();
        stageStarCoins_.clear();
        Save();
    }

    bool DeleteSlot(int slotIndex) {
        if (slotIndex < 0 || slotIndex >= kSaveSlotCount) {
            return false;
        }

        std::error_code ec;
        const bool removed = std::filesystem::remove(GetSaveFilePath(slotIndex), ec);
        if (slotIndex == 0) {
            std::filesystem::remove(GetLegacySaveFilePath(), ec);
        }

        if (slotIndex == activeSlot_) {
            Load();
        }
        return removed;
    }

private:
    GameDataManager() : lives_(3), coins_(0), playTimeSeconds_(0) {}
    ~GameDataManager() = default;

    std::string GetLegacySaveFilePath() const {
        return "Resources/json/save/savedata.json";
    }

    bool ReadSaveJson(int slotIndex, nlohmann::json& outData) const {
        std::ifstream file(GetSaveFilePath(slotIndex));
        if (!file.is_open() && slotIndex == 0) {
            file.open(GetLegacySaveFilePath());
        }
        if (!file.is_open()) {
            return false;
        }

        try {
            file >> outData;
        }
        catch (...) {
            return false;
        }
        return true;
    }

    std::vector<bool>& GetOrCreateStarCoins(int stageIdx) {
        auto& coins = stageStarCoins_[stageIdx];
        coins.resize(3, false);
        return coins;
    }

    int lives_ = 3;
    int coins_ = 0;
    int playTimeSeconds_ = 0;
    int activeSlot_ = 0;
    bool pendingRespawnIrisIn_ = false;
    std::vector<int> clearedStages_;
    std::vector<int> seenUnlockedStages_;
    std::map<int, std::vector<bool>> stageStarCoins_; // ステージ番号 -> [コイン0, コイン1, コイン2]
};
