#pragma once
// コピー能力内部の契約です。セッションはControllerが所有します。明示キャンセル時の後始末は各能力が担当します。
#include "game/actor/player/copy/PlayerCopyAbilityController.h"
#include "game/actor/enemy/combat/EnemyAttackProfile.h"
#include <cstdint>
#include <memory>
#include <utility>

namespace CopyAbilityDetail {
// コピー中の能力ごとの状態と演出を持つ境界。攻撃設定は値で保持し、解除時は Cancel で後始末する。
class ICopyAbilitySession {
  public:
    explicit ICopyAbilitySession(EnemyAttackProfile profile) : profile_(std::move(profile)) {}
    virtual ~ICopyAbilitySession() = default;
    // 入力受付と時間経過を分離します。deltaTime は秒です。
    virtual void ProcessInput(Player& player, const PlayerCopyAbilityInput& input) = 0;
    virtual void Update(Player& player, float deltaTime) = 0;
    // 能力切替や形態解除時に、能力が所有する演出・一時状態を後始末します。
    virtual void Cancel(Player& player) = 0;
    virtual void Draw(Player&, ID3D12Resource*, ID3D12Resource*, Camera*, int) {}
    virtual bool HasOwnedVisuals() const { return false; }
    virtual const char* GetStateLabel() const { return "コピー能力有効"; }
    virtual void SetPreviewAttackId(const std::string&) {}
    virtual void DrawOwnedVisuals(Player&, uint32_t, uint32_t, Camera*, int) {}
    // ガード成立時の追加演出を処理した場合のみ true を返します。
    virtual bool NotifyGuardedHit(Player&, const Vector3&) { return false; }
    virtual bool CanBreakImpactGate() const { return false; }
    virtual bool IsPinkBounceSlamImpactActive() const { return false; }

  protected:
    EnemyAttackProfile profile_;
};

std::unique_ptr<ICopyAbilitySession> CreatePinkSlimeCopyAbility(const EnemyAttackProfile& profile);
std::unique_ptr<ICopyAbilitySession> CreateFireSlimeCopyAbility(const EnemyAttackProfile& profile);
std::unique_ptr<ICopyAbilitySession> CreateThunderSlimeCopyAbility(const EnemyAttackProfile& profile);
std::unique_ptr<ICopyAbilitySession> CreateWindSlimeCopyAbility(const EnemyAttackProfile& profile);
std::unique_ptr<ICopyAbilitySession> CreateBomberCopyAbility(const EnemyAttackProfile& profile);
std::unique_ptr<ICopyAbilitySession> CreateCrownCopyAbility(const EnemyAttackProfile& profile);
} // namespace CopyAbilityDetail
