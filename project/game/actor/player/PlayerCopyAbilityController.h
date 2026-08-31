#pragma once

#include <memory>

#include "engine/utility/math/Math.h"

class Player;
class EnemyAttackProfile;

// 1フレーム分のコピー能力入力です。
// 入力デバイスの詳細を能力実装へ持ち込まず、再生・AI操作にも流用できる形にします。
struct PlayerCopyAbilityInput {
    bool specialTriggered = false;
    bool primaryTriggered = false;
    bool primaryHeld = false;
    bool secondaryTriggered = false;
    bool secondaryHeld = false;
};

// 吸収後のコピー能力をプレイヤーの寿命で管理します。
// 元になった敵は設定値の提供だけを担当し、能力中の状態や更新処理は保持しません。
class PlayerCopyAbilityController {
public:
    PlayerCopyAbilityController();
    ~PlayerCopyAbilityController();

    PlayerCopyAbilityController(const PlayerCopyAbilityController&) = delete;
    PlayerCopyAbilityController& operator=(const PlayerCopyAbilityController&) = delete;

    void Activate(int morphType, const EnemyAttackProfile& attackProfile);
    void ActivateDefault(int morphType);
    void ProcessInput(Player& player, const PlayerCopyAbilityInput& input);
    void Update(Player& player, float deltaTime);
    void Cancel(Player& player);
    // ガード中に攻撃を受け止めたことを能力へ通知します。
    // ダメージ判定とコピー固有の反応を分離し、敵クラスへ能力処理を戻しません。
    bool NotifyGuardedHit(Player& player, const Vector3& sourcePosition);

    bool HandlesMorphType(int morphType) const;
    bool IsActive() const;
    bool CanBreakImpactGate() const;
    bool IsPinkBounceSlamImpactActive() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
