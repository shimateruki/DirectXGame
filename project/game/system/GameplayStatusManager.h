#pragma once

#include "Object3d.h"

#include <string>
#include <unordered_map>

/// PlayerとEnemyのタイプ共通ステータスを一元管理します。
/// シーンJSONや配置プリセットには個体別ステータスを持たせません。
class GameplayStatusManager {
  public:
    // キャラクター種ごとの共通調整値。配置個体の現在 HP ではなく、適用時の基準値を保持する。
    struct CharacterStatus {
        float maxHp = 100.0f;
        float attackPower = 1.0f;
        float speed = 1.0f;
        float gravity = 50.0f;
        float maxFallSpeed = 60.0f;
        float jumpPower = 10.0f;
        float detectionRange = 20.0f;
        engine::Vector3 scale = {1.0f, 1.0f, 1.0f};
        std::string modelName;
        bool morphLimited = true;
        float morphDuration = 5.0f;
        bool motorContinuousCollision = false;
        bool motorSnapToGround = false;
        float motorMaxSlopeDegrees = 45.573f;
        float motorStepHeight = 0.0f;
        float motorGroundProbeDistance = 0.18f;
        float motorSkinWidth = 0.025f;
    };

    // 共有設定マネージャーのインスタンスを返す。
    static GameplayStatusManager* GetInstance();
    ~GameplayStatusManager();

    /// 初回だけ設定JSONを読み込みます。
    void Initialize();
    /// 設定JSONを再読込します。失敗時は現在値を維持します。
    bool Reload();
    /// 現在値を設定JSONへ保存します。
    bool Save();
    /// ゲーム側の標準値へ戻します。
    void ResetToDefaults();

    // 可変参照を編集した呼び出し側は MarkDirty() を呼び、後で SaveIfDirty() する。
    CharacterStatus& GetMutablePlayerStatus() { return playerStatus_; }
    const CharacterStatus& GetPlayerStatus() const { return playerStatus_; }
    // 未登録の敵種別なら nullptr。可変参照の編集後は MarkDirty を呼ぶ。
    CharacterStatus* FindMutableEnemyStatus(const std::string& enemyType);
    const CharacterStatus* FindEnemyStatus(const std::string& enemyType) const;

    void MarkDirty() { dirty_ = true; }
    bool IsDirty() const { return dirty_; }
    // 未変更なら書き込まず true。変更があれば保存結果を返す。
    bool SaveIfDirty();

    /// resetCurrentHpがtrueなら出現時として全回復し、falseなら現在HP率を維持します。
    bool ApplyPlayerStatus(engine::Object3d* object, bool resetCurrentHp) const;
    bool ApplyEnemyStatus(engine::Object3d* object, bool resetCurrentHp) const;
    bool ApplyManagedStatus(engine::Object3d* object, bool resetCurrentHp) const;

    static bool IsManagedCharacter(const engine::Object3d* object);
    static const char* GetSettingsPath();

  private:
    GameplayStatusManager() = default;
    GameplayStatusManager(const GameplayStatusManager&) = delete;
    GameplayStatusManager& operator=(const GameplayStatusManager&) = delete;

    // 指定した設定JSONを読み込み、現在の調整値へ反映する。
    bool LoadFromFile(const std::string& path);
    // 読み込んだ値の範囲を調整し、Objectへ基本ステータスを適用する。
    static void Normalize(CharacterStatus& status);
    static void ApplyStatus(engine::Object3d* object, const CharacterStatus& status, bool resetCurrentHp);

  private:
    CharacterStatus playerStatus_;
    std::unordered_map<std::string, CharacterStatus> enemyStatuses_;
    bool initialized_ = false;
    bool dirty_ = false;
};
