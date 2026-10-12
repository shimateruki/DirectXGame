#pragma once
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"

// セーブデータ、ステージクリア状況、スターコイン、残機/コインを一元管理する
class GameDataManager {
  public:
    static constexpr int kSaveSlotCount = 3;
    static constexpr int kStarCoinsPerStage = 3;
    static constexpr int kStartingLives = 3;
    static constexpr int kMaxLives = 99;
    static constexpr int kMaxCoins = 9999;
    static constexpr int kCoinsPerLife = 100;

    // タイトルや設定画面で表示する、セーブスロットの概要情報
    struct SaveSlotSummary {
        bool exists = false;
        bool tutorialCleared = false;
        int clearedStageCount = 0;
        int collectedStarCoins = 0;
        int lives = kStartingLives;
        int coins = 0;
        int playTimeSeconds = 0;
    };

    // ステージクリア後にセレクト画面で王冠数を増やす演出リクエスト
    struct StageClearRewardPresentation {
        bool active = false;
        int stageIndex = -1;
        int previousCrownCount = 0;
        int newCrownCount = 0;
    };

    // クリア後にセレクトへ戻った直後、対応するゲートからプレイヤーを出す演出の予約情報。
    struct StageSelectReturnPresentation {
        bool active = false;
        int stageIndex = -1;
        int previousCrownCount = 0;
        int newCrownCount = 0;
        std::vector<int> newStarCoinIndices;
    };

    // ステージ内チェックポイントからの再開に必要な一時情報です。
    // セーブデータには書き込まず、死亡によるシーン再生成を一度だけ跨ぎます。
    struct StageCheckpointRespawn {
        bool active = false;
        int stageIndex = -1;
        float positionX = 0.0f;
        float positionY = 0.0f;
        float positionZ = 0.0f;
        std::string checkpointObjectName;
        std::string morphEnemyType;
        std::vector<int> clearedEncounterIds;
    };

    static GameDataManager* GetInstance() {
        static GameDataManager instance;
        return &instance;
    }

    // 起動時は現在のスロットから保存状態を復元します。
    void Initialize() { Load(); }

    /// <summary>有効なセーブスロットへ切り替え、その内容を読み込みます。</summary>
    /// <param name="slotIndex">0 始まりのスロット番号。範囲外は端の番号に丸めます。</param>
    void SetActiveSlot(int slotIndex) {
        if (slotIndex < 0)
            slotIndex = 0;
        if (slotIndex >= kSaveSlotCount)
            slotIndex = kSaveSlotCount - 1;
        activeSlot_ = slotIndex;
        Load();
    }

    int GetActiveSlot() const { return activeSlot_; }

    std::string GetSaveFilePath() const { return GetSaveFilePath(activeSlot_); }

    std::string GetSaveFilePath(int slotIndex) const {
        if (slotIndex < 0)
            slotIndex = 0;
        if (slotIndex >= kSaveSlotCount)
            slotIndex = kSaveSlotCount - 1;
        return "Resources/json/save/savedata_slot" + std::to_string(slotIndex + 1) + ".json";
    }

    /// <summary>指定スロットの一覧表示用情報を、現在のプレイ状態を変えずに読み取ります。</summary>
    /// <param name="slotIndex">0 始まりのスロット番号。</param>
    /// <returns>読み込めない場合は exists が false の概要。</returns>
    SaveSlotSummary GetSlotSummary(int slotIndex) const {
        SaveSlotSummary summary;
        nlohmann::json data;
        if (!ReadSaveJson(slotIndex, data)) {
            return summary;
        }

        summary.exists = true;
        summary.lives = std::clamp(ReadInteger(data, "lives", kStartingLives), 0, kMaxLives);
        summary.coins = std::clamp(ReadInteger(data, "coins", 0), 0, kMaxCoins);
        summary.playTimeSeconds = (std::max)(0, ReadInteger(data, "playTimeSeconds", 0));

        if (data.contains("clearedStages") && data["clearedStages"].is_array()) {
            for (const auto& stage : data["clearedStages"]) {
                int stageIndex = 0;
                if (!TryReadInteger(stage, stageIndex))
                    continue;
                // -1 はチュートリアルのクリア記録で、通常ステージの件数に含めない。
                if (stageIndex == -1) {
                    summary.tutorialCleared = true;
                } else if (stageIndex >= 0) {
                    summary.clearedStageCount++;
                }
            }
        }

        if (data.contains("starCoins") && data["starCoins"].is_object()) {
            for (auto it = data["starCoins"].begin(); it != data["starCoins"].end(); ++it) {
                if (!it.value().is_array())
                    continue;
                const auto count = (std::min)(it.value().size(), static_cast<size_t>(kStarCoinsPerStage));
                for (size_t index = 0; index < count; ++index) {
                    const auto& coin = it.value()[index];
                    if (coin.is_boolean() && coin.get<bool>()) {
                        summary.collectedStarCoins++;
                    }
                }
            }
        }

        return summary;
    }

    // 保存ファイルに未管理の項目があれば保持し、管理中の項目だけ更新します。
    void Save() {
        nlohmann::json data;

        // 既存ファイルを読み込み、未管理の値をできるだけ保持する
        std::ifstream inFile(GetSaveFilePath());
        // スロット制導入前のセーブを最初のスロットへ引き継ぐ。
        if (!inFile.is_open() && activeSlot_ == 0) {
            inFile.open(GetLegacySaveFilePath());
        }
        if (inFile.is_open()) {
            try {
                inFile >> data;
                if (!data.is_object()) {
                    data = nlohmann::json::object();
                }
            } catch (const nlohmann::json::exception&) {
                // 壊れた既存セーブは引き継がず、現在の正常な状態から書き直します。
                data = nlohmann::json::object();
            }
            inFile.close();
        }

        data["lives"] = lives_;
        data["coins"] = coins_;
        data["playTimeSeconds"] = playTimeSeconds_;
        data["clearedStages"] = clearedStages_; // クリア済みステージのインデックス。
        data["seenUnlockedStages"] = seenUnlockedStages_;

        // スターコイン取得状況を保存する
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

    // ファイルがないか壊れている場合は、新規ゲームの初期値へ戻します。
    void Load() {
        nlohmann::json data;
        if (!ReadSaveJson(activeSlot_, data)) {
            lives_ = kStartingLives;
            coins_ = 0;
            playTimeSeconds_ = 0;
            clearedStages_.clear();
            seenUnlockedStages_.clear();
            stageStarCoins_.clear();
            return;
        }

        lives_ = kStartingLives;
        coins_ = 0;
        playTimeSeconds_ = 0;
        clearedStages_.clear();
        seenUnlockedStages_.clear();
        stageStarCoins_.clear();

        lives_ = std::clamp(ReadInteger(data, "lives", kStartingLives), 0, kMaxLives);
        coins_ = std::clamp(ReadInteger(data, "coins", 0), 0, kMaxCoins);
        playTimeSeconds_ = (std::max)(0, ReadInteger(data, "playTimeSeconds", 0));

        auto readStageIndices = [&](const char* key, std::vector<int>& output) {
            if (!data.contains(key) || !data[key].is_array()) {
                return;
            }
            for (const auto& value : data[key]) {
                int stageIndex = 0;
                if (TryReadInteger(value, stageIndex)) {
                    output.push_back(stageIndex);
                }
            }
        };
        readStageIndices("clearedStages", clearedStages_);
        readStageIndices("seenUnlockedStages", seenUnlockedStages_);

        if (data.contains("starCoins") && data["starCoins"].is_object()) {
            for (auto it = data["starCoins"].begin(); it != data["starCoins"].end(); ++it) {
                int stageIdx = 0;
                const std::string& key = it.key();
                const auto result = std::from_chars(key.data(), key.data() + key.size(), stageIdx);
                if (result.ec != std::errc{} || result.ptr != key.data() + key.size() || !it.value().is_array()) {
                    continue;
                }
                std::vector<bool> starCoins(kStarCoinsPerStage, false);
                for (size_t index = 0; index < starCoins.size() && index < it.value().size(); ++index) {
                    if (it.value()[index].is_boolean()) {
                        starCoins[index] = it.value()[index].get<bool>();
                    }
                }
                stageStarCoins_[stageIdx] = std::move(starCoins);
            }
        }
    }

    // 残機操作。変更はその場で保存します。
    int GetLives() const { return lives_; }
    void SetLives(int lives) {
        lives_ = std::clamp(lives, 0, kMaxLives);
        Save();
    }
    void SubtractLife() {
        lives_--;
        if (lives_ < 0)
            lives_ = 0;
        Save();
    }
    void ResetLives() {
        lives_ = kStartingLives;
        Save();
    }

    // コイン操作。変更はその場で保存します。
    int GetCoins() const { return coins_; }
    void SetCoins(int coins) {
        coins_ = std::clamp(coins, 0, kMaxCoins);
        Save();
    }
    /// <summary>コインを加算し、100 枚ごとに残機へ換算します。</summary>
    /// <param name="amount">加算する枚数。0 以下は無視します。</param>
    void AddCoin(int amount = 1) {
        if (amount <= 0) {
            return;
        }
        // 大量取得でも int の加算をオーバーフローさせず、100 枚ごとに残機へ換算します。
        const int64_t totalCoins = static_cast<int64_t>(coins_) + amount;
        const int64_t earnedLives = totalCoins / kCoinsPerLife;
        lives_ =
            static_cast<int>((std::min)(static_cast<int64_t>(kMaxLives), static_cast<int64_t>(lives_) + earnedLives));
        coins_ = static_cast<int>(totalCoins % kCoinsPerLife);
        Save();
    }
    void ResetCoins() {
        coins_ = 0;
        Save();
    }

    int GetPlayTimeSeconds() const { return playTimeSeconds_; }
    void SetPlayTimeSeconds(int seconds) {
        playTimeSeconds_ = std::clamp(seconds, 0, 99 * 60 * 60 + 59 * 60 + 59);
        Save();
    }

    // リスポーン時の画面遷移は一回だけ消費する要求として保持します。
    void RequestRespawnIrisIn() { pendingRespawnIrisIn_ = true; }
    bool ConsumeRespawnIrisInRequest() {
        const bool requested = pendingRespawnIrisIn_;
        pendingRespawnIrisIn_ = false;
        return requested;
    }

    /// <summary>死亡後に戻るチェックポイントと、復帰させるコピー能力を予約します。</summary>
    /// <param name="stageIndex">復帰先ステージの番号。</param>
    /// <param name="positionX">復帰先のワールド X 座標。</param>
    /// <param name="positionY">復帰先のワールド Y 座標。</param>
    /// <param name="positionZ">復帰先のワールド Z 座標。</param>
    /// <param name="morphEnemyType">復帰後に維持するコピー元の敵種別。</param>
    /// <param name="checkpointObjectName">取得済みの旗をシーン再生成後に特定する名前。旗以外なら空。</param>
    void RequestStageCheckpointRespawn(int stageIndex, float positionX, float positionY, float positionZ,
                                       const std::string& morphEnemyType, const std::string& checkpointObjectName) {
        // 同じステージの再開要求なら、クリア済み遭遇イベントの記録を保つ。
        if (pendingStageCheckpointRespawn_.stageIndex != stageIndex)
            pendingStageCheckpointRespawn_.clearedEncounterIds.clear();
        pendingStageCheckpointRespawn_.active = stageIndex >= 0;
        pendingStageCheckpointRespawn_.stageIndex = stageIndex;
        pendingStageCheckpointRespawn_.positionX = positionX;
        pendingStageCheckpointRespawn_.positionY = positionY;
        pendingStageCheckpointRespawn_.positionZ = positionZ;
        pendingStageCheckpointRespawn_.morphEnemyType = morphEnemyType;
        pendingStageCheckpointRespawn_.checkpointObjectName = checkpointObjectName;
    }

    // 同じチェックポイントへの復帰では、クリア済み遭遇イベントを再起動しません。
    void RememberCheckpointEncounter(int stageIndex, int eventId) {
        if (pendingStageCheckpointRespawn_.stageIndex != stageIndex) {
            pendingStageCheckpointRespawn_.clearedEncounterIds.clear();
            pendingStageCheckpointRespawn_.stageIndex = stageIndex;
        }
        auto& ids = pendingStageCheckpointRespawn_.clearedEncounterIds;
        if (std::find(ids.begin(), ids.end(), eventId) == ids.end())
            ids.push_back(eventId);
    }

    // Consume 系の要求は返却時に空にし、シーン再生成で二重再生しません。
    StageCheckpointRespawn ConsumeStageCheckpointRespawn() {
        StageCheckpointRespawn request = pendingStageCheckpointRespawn_;
        pendingStageCheckpointRespawn_ = {};
        return request;
    }

    /// <summary>王冠数が増えた場合に、セレクト画面の報酬演出を予約します。</summary>
    /// <param name="stageIndex">クリアしたステージ番号。</param>
    /// <param name="previousCrownCount">演出開始前の王冠数。</param>
    /// <param name="newCrownCount">演出終了後の王冠数。</param>
    void RequestStageClearRewardPresentation(int stageIndex, int previousCrownCount, int newCrownCount) {
        if (stageIndex < 0 || newCrownCount <= previousCrownCount) {
            pendingStageClearReward_ = {};
            return;
        }

        pendingStageClearReward_.active = true;
        pendingStageClearReward_.stageIndex = stageIndex;
        pendingStageClearReward_.previousCrownCount = std::clamp(previousCrownCount, 0, 999);
        pendingStageClearReward_.newCrownCount = std::clamp(newCrownCount, 0, 999);
    }

    StageClearRewardPresentation ConsumeStageClearRewardPresentation() {
        StageClearRewardPresentation request = pendingStageClearReward_;
        pendingStageClearReward_ = {};
        return request;
    }

    /// <summary>クリア後にステージゲートから戻る演出の内容を予約します。</summary>
    /// <param name="stageIndex">戻り元のステージ番号。</param>
    /// <param name="previousCrownCount">クリア前の王冠数。</param>
    /// <param name="newCrownCount">クリア後の王冠数。</param>
    /// <param name="newStarCoinIndices">今回新たに取得したスターコインの番号。</param>
    void RequestStageSelectReturnPresentation(int stageIndex, int previousCrownCount, int newCrownCount,
                                              const std::vector<int>& newStarCoinIndices) {
        pendingStageSelectReturn_.active = true;
        pendingStageSelectReturn_.stageIndex = stageIndex;
        pendingStageSelectReturn_.previousCrownCount = std::clamp(previousCrownCount, 0, 999);
        pendingStageSelectReturn_.newCrownCount = std::clamp(newCrownCount, 0, 999);
        pendingStageSelectReturn_.newStarCoinIndices = newStarCoinIndices;
    }

    StageSelectReturnPresentation ConsumeStageSelectReturnPresentation() {
        StageSelectReturnPresentation request = pendingStageSelectReturn_;
        pendingStageSelectReturn_ = {};
        return request;
    }

    // ステージクリア・解放確認の状態。変更時のみ保存します。
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

    // スターコインはステージ番号とステージ内の番号で管理します。
    void MarkStarCoinCollected(int stageIdx, int coinIdx) {
        if (coinIdx < 0 || coinIdx >= kStarCoinsPerStage)
            return;
        SetStarCoinCollected(stageIdx, coinIdx, true);
    }

    void SetStarCoinCollected(int stageIdx, int coinIdx, bool collected) {
        if (coinIdx < 0 || coinIdx >= kStarCoinsPerStage)
            return;

        auto& coins = GetOrCreateStarCoins(stageIdx);
        if (coins[coinIdx] == collected)
            return;
        coins[coinIdx] = collected;
        Save();
    }

    bool IsStarCoinCollected(int stageIdx, int coinIdx) const {
        if (stageStarCoins_.find(stageIdx) == stageStarCoins_.end())
            return false;
        if (coinIdx < 0 || coinIdx >= kStarCoinsPerStage)
            return false;
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
        lives_ = kStartingLives;
        coins_ = 0;
        playTimeSeconds_ = 0;
        clearedStages_.clear();
        seenUnlockedStages_.clear();
        stageStarCoins_.clear();
        Save();
    }

    /// <summary>指定スロットの保存ファイルを削除します。</summary>
    /// <param name="slotIndex">0 始まりのスロット番号。</param>
    /// <returns>現在の保存ファイルを削除できた場合は true。</returns>
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
    GameDataManager() : lives_(kStartingLives), coins_(0), playTimeSeconds_(0) {}
    ~GameDataManager() = default;

    // JSON の整数が int の範囲外なら変換せず、破損した値を読み飛ばします。
    static bool TryReadInteger(const nlohmann::json& value, int& output) {
        if (!value.is_number_integer()) {
            return false;
        }
        if (value.is_number_unsigned()) {
            const uint64_t number = value.get<uint64_t>();
            if (number > static_cast<uint64_t>((std::numeric_limits<int>::max)())) {
                return false;
            }
            output = static_cast<int>(number);
            return true;
        }
        const int64_t number = value.get<int64_t>();
        if (number < (std::numeric_limits<int>::min)() || number > (std::numeric_limits<int>::max)()) {
            return false;
        }
        output = static_cast<int>(number);
        return true;
    }

    // セーブの数値項目が壊れていても、他の項目は復元できるよう既定値に戻します。
    static int ReadInteger(const nlohmann::json& data, const char* key, int fallback) {
        if (!data.contains(key)) {
            return fallback;
        }
        int value = fallback;
        return TryReadInteger(data[key], value) ? value : fallback;
    }

    std::string GetLegacySaveFilePath() const { return "Resources/json/save/savedata.json"; }

    /// <summary>スロット 0 では旧形式の保存パスも読み込み候補にします。</summary>
    /// <returns>JSON オブジェクトを読めた場合は true。</returns>
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
        } catch (...) {
            return false;
        }
        return outData.is_object();
    }

    std::vector<bool>& GetOrCreateStarCoins(int stageIdx) {
        auto& coins = stageStarCoins_[stageIdx];
        coins.resize(kStarCoinsPerStage, false);
        return coins;
    }

    int lives_ = kStartingLives;
    int coins_ = 0;
    int playTimeSeconds_ = 0;
    int activeSlot_ = 0;
    bool pendingRespawnIrisIn_ = false;
    StageCheckpointRespawn pendingStageCheckpointRespawn_;
    StageClearRewardPresentation pendingStageClearReward_;
    StageSelectReturnPresentation pendingStageSelectReturn_;
    std::vector<int> clearedStages_;
    std::vector<int> seenUnlockedStages_;
    std::map<int, std::vector<bool>> stageStarCoins_; // ステージ番号 -> [コイン0, コイン1, コイン2]
};
