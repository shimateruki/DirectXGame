#pragma once

#include "engine/utility/math/Math.h"
#include <unordered_set>

class Object3d;
class Player;

// 通常スライムでも敵へ対抗できる、形態に依存しない基礎攻撃を管理します。
// コピー能力の入力処理と分離し、ボス戦で能力を失っても詰まない状態を保証します。
class PlayerBaseCombatController {
public:
    void ProcessInput(Player& player, bool attackTriggered);
    void Update(Player& player, float deltaTime);
    void Cancel(Player& player, bool restoreControl = true);

    bool IsActive() const { return phase_ != Phase::Idle; }
    bool CanBreakImpactGate() const;

    struct ReplayState {
        int phase = 0;
        Vector3 direction{ 0.0f, 0.0f, 1.0f };
        float timer = 0.0f;
        float effectTimer = 0.0f;
        float inputBufferTimer = 0.0f;
        bool pressHitEnemy = false;
    };

    ReplayState CaptureReplayState() const;
    void RestoreReplayState(const ReplayState& state);

private:
    enum class Phase {
        Idle,
        BashWindup,
        BashActive,
        BashRecovery,
        PressWindup,
        PressFall,
        PressRecovery
    };

    void BeginBash(Player& player);
    void BeginPress(Player& player);
    void UpdateBash(Player& player, float deltaTime);
    void UpdatePress(Player& player, float deltaTime);
    void DamageBash(Player& player);
    void DamagePress(Player& player, bool landingImpact);
    void Finish(Player& player);

    Phase phase_ = Phase::Idle;
    Vector3 direction_{ 0.0f, 0.0f, 1.0f };
    std::unordered_set<Object3d*> hitTargets_;
    float timer_ = 0.0f;
    float effectTimer_ = 0.0f;
    float inputBufferTimer_ = 0.0f;
    bool pressHitEnemy_ = false;
};
