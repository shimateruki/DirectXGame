#pragma once
#include <string>
#include <fstream>
#include <vector>
#include <algorithm>
#include <map>
#include "json.hpp"

class GameDataManager {
public:
    static GameDataManager* GetInstance() {
        static GameDataManager instance;
        return &instance;
    }

    void Initialize() {
        Load();
    }

    // --- セーブ・ロード ---
    void Save() {
        nlohmann::json data;
        
        // 既存のファイルを読み込んで他の値を保持する
        std::ifstream inFile("Resources/json/save/savedata.json");
        if (inFile.is_open()) {
            inFile >> data;
            inFile.close();
        }

        data["lives"] = lives_;
        data["coins"] = coins_;
        data["clearedStages"] = clearedStages_; // クリア済みステージのインデックス

        // スターコイン情報の保存
        nlohmann::json coinData;
        for (auto& [stageIdx, coins] : stageStarCoins_) {
            coinData[std::to_string(stageIdx)] = coins;
        }
        data["starCoins"] = coinData;

        std::ofstream outFile("Resources/json/save/savedata.json");
        if (outFile.is_open()) {
            outFile << data.dump(4);
            outFile.close();
        }
    }

    void Load() {
        std::ifstream file("Resources/json/save/savedata.json");
        if (!file.is_open()) {
            lives_ = 3;
            clearedStages_.clear();
            stageStarCoins_.clear();
            return;
        }

        nlohmann::json data;
        file >> data;
        file.close();

        if (data.contains("lives")) lives_ = data["lives"];
        if (data.contains("coins")) coins_ = data["coins"];
        if (data.contains("clearedStages")) clearedStages_ = data["clearedStages"].get<std::vector<int>>();
        
        if (data.contains("starCoins")) {
            stageStarCoins_.clear();
            for (auto it = data["starCoins"].begin(); it != data["starCoins"].end(); ++it) {
                int stageIdx = std::stoi(it.key());
                stageStarCoins_[stageIdx] = it.value().get<std::vector<bool>>();
            }
        }
    }

    // --- 残機操作 ---
    int GetLives() const { return lives_; }
    void SubtractLife() { lives_--; if (lives_ < 0) lives_ = 0; Save(); }
    void ResetLives() { lives_ = 3; Save(); }

    // --- コイン操作 ---
    int GetCoins() const { return coins_; }
    void AddCoin(int amount = 1) {
        coins_ += amount;
        while (coins_ >= 100) {
            coins_ -= 100;
            lives_++;
        }
        Save();
    }
    void ResetCoins() { coins_ = 0; Save(); }

    // --- ステージクリア状況 ---
    void MarkStageCleared(int index) {
        if (std::find(clearedStages_.begin(), clearedStages_.end(), index) == clearedStages_.end()) {
            clearedStages_.push_back(index);
            Save();
        }
    }

    bool IsStageCleared(int index) const {
        return std::find(clearedStages_.begin(), clearedStages_.end(), index) != clearedStages_.end();
    }

    // --- スターコイン操作 ---
    void MarkStarCoinCollected(int stageIdx, int coinIdx) {
        if (coinIdx < 0 || coinIdx >= 3) return;
        
        // 該当ステージの配列がなければ作成
        if (stageStarCoins_.find(stageIdx) == stageStarCoins_.end()) {
            stageStarCoins_[stageIdx] = { false, false, false };
        }
        
        stageStarCoins_[stageIdx][coinIdx] = true;
        Save();
    }

    bool IsStarCoinCollected(int stageIdx, int coinIdx) const {
        if (stageStarCoins_.find(stageIdx) == stageStarCoins_.end()) return false;
        if (coinIdx < 0 || coinIdx >= 3) return false;
        return stageStarCoins_.at(stageIdx)[coinIdx];
    }

    void ResetAll() {
        lives_ = 3;
        coins_ = 0;
        clearedStages_.clear();
        stageStarCoins_.clear();
        Save();
    }

private:
    GameDataManager() : lives_(3), coins_(0) {}
    ~GameDataManager() = default;

    int lives_ = 3;
    int coins_ = 0;
    std::vector<int> clearedStages_;
    std::map<int, std::vector<bool>> stageStarCoins_; // ステージ番号 -> [コイン0, コイン1, コイン2]
};
