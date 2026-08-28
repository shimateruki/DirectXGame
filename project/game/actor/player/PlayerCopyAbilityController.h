#pragma once

#include <memory>

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

    bool HandlesMorphType(int morphType) const;
    bool IsActive() const;
    bool IsGiantRushActive() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
