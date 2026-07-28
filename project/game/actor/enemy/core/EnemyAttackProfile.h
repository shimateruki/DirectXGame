#pragma once

#include <string>
#include <vector>

// 敵の攻撃1種類分を表す、実行時とEditorで共有するデータです。
struct EnemyAttackDefinition {
    std::string id;
    std::string displayName;

    float minRange = 0.0f;
    float maxRange = 10.0f;
    float radius = 1.0f;

    float windupDuration = 0.3f;
    float activeDuration = 0.2f;
    float recoveryDuration = 0.3f;
    float cooldown = 1.0f;
    float warningLeadTime = 0.15f;

    float damage = 1.0f;
    std::string statusEffectType;
    float statusDuration = 0.0f;
    float statusTickInterval = 0.5f;
    float statusTickDamage = 0.0f;
    std::string statusVfx;
    float minSpeed = 0.0f;
    float maxSpeed = 0.0f;
    float lifetime = 0.0f;

    float recommendedTargetDistance = 5.0f;
    float previewDuration = 4.0f;

    std::string animation;
    std::string windupVfx;
    std::string activeVfx;
    std::string impactVfx;
    std::string audioCue;
};

// 敵1種類に含まれる攻撃をまとめたJSONアセットです。
// 読み込みに失敗した場合も既定値を保持し、既存シーンの動作を継続します。
class EnemyAttackProfile {
public:
    static EnemyAttackProfile CreateDefault(const std::string& enemyType);
    static std::string GetDefaultPath(const std::string& enemyType);
    static bool LoadCachedForEnemy(const std::string& enemyType, EnemyAttackProfile& destination, std::string* errorMessage = nullptr);
    static void InvalidateCache(const std::string& enemyType = {});

    bool LoadForEnemy(const std::string& enemyType, std::string* errorMessage = nullptr);
    bool LoadFromFile(const std::string& path, std::string* errorMessage = nullptr);
    bool SaveToFile(const std::string& path, std::string* errorMessage = nullptr) const;

    const EnemyAttackDefinition* FindAttack(const std::string& attackId) const;
    EnemyAttackDefinition* FindAttack(const std::string& attackId);
    void Sanitize();

    int version = 1;
    std::string enemyType;
    std::string displayName;
    std::vector<EnemyAttackDefinition> attacks;
};
