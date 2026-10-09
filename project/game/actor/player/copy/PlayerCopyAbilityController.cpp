#define NOMINMAX
#include "game/actor/player/copy/CopyAbilitySession.h"
#include "game/actor/player/Player.h"

// Controllerは能力の生成と寿命、呼び出しの転送だけを担当します。
using namespace CopyAbilityDetail;
namespace {
std::unique_ptr<ICopyAbilitySession> CreateSession(int morphType, const EnemyAttackProfile& profile) {
    if (morphType == static_cast<int>(Player::EnemyMorphType::Crown)) {
        return CreateCrownCopyAbility(profile);
    }
    if (morphType == static_cast<int>(Player::EnemyMorphType::Slime)) {
        return CreatePinkSlimeCopyAbility(profile);
    }
    if (morphType == static_cast<int>(Player::EnemyMorphType::FireSlime)) {
        return CreateFireSlimeCopyAbility(profile);
    }
    if (morphType == static_cast<int>(Player::EnemyMorphType::ThunderSlime)) {
        return CreateThunderSlimeCopyAbility(profile);
    }
    if (morphType == static_cast<int>(Player::EnemyMorphType::WindSlime)) {
        return CreateWindSlimeCopyAbility(profile);
    }
    if (morphType == static_cast<int>(Player::EnemyMorphType::Bomber)) {
        return CreateBomberCopyAbility(profile);
    }
    return nullptr;
}

const char* GetEnemyTypeForMorph(int morphType) {
    if (morphType == static_cast<int>(Player::EnemyMorphType::Crown))
        return "FalseKingSlime";
    if (morphType == static_cast<int>(Player::EnemyMorphType::Slime))
        return "Slime";
    if (morphType == static_cast<int>(Player::EnemyMorphType::FireSlime))
        return "FireSlime";
    if (morphType == static_cast<int>(Player::EnemyMorphType::ThunderSlime))
        return "ThunderSlime";
    if (morphType == static_cast<int>(Player::EnemyMorphType::WindSlime))
        return "WindSlime";
    if (morphType == static_cast<int>(Player::EnemyMorphType::Bomber))
        return "Bomber";
    return "";
}
} // namespace

// 現在のコピー種別と能力セッションの所有者。具体的な能力型を公開ヘッダーから隠す。
class PlayerCopyAbilityController::Impl {
  public:
    int morphType = static_cast<int>(Player::EnemyMorphType::None);
    std::unique_ptr<ICopyAbilitySession> session;
};

PlayerCopyAbilityController::PlayerCopyAbilityController() : impl_(std::make_unique<Impl>()) {}
PlayerCopyAbilityController::~PlayerCopyAbilityController() = default;

void PlayerCopyAbilityController::Activate(int morphType, const EnemyAttackProfile& attackProfile) {
    impl_->morphType = morphType;
    impl_->session = CreateSession(morphType, attackProfile);
}

void PlayerCopyAbilityController::ActivateDefault(int morphType) {
    const char* enemyType = GetEnemyTypeForMorph(morphType);
    if (!enemyType || enemyType[0] == '\0') {
        impl_->session.reset();
        impl_->morphType = static_cast<int>(Player::EnemyMorphType::None);
        return;
    }
    EnemyAttackProfile profile;
    EnemyAttackProfile::LoadCachedForEnemy(enemyType, profile);
    Activate(morphType, profile);
}

void PlayerCopyAbilityController::ProcessInput(Player& player, const PlayerCopyAbilityInput& input) {
    if (impl_->session) {
        impl_->session->ProcessInput(player, input);
    }
}

void PlayerCopyAbilityController::Update(Player& player, float deltaTime) {
    if (impl_->session && deltaTime > 0.0f) {
        impl_->session->Update(player, deltaTime);
    }
}

void PlayerCopyAbilityController::Cancel(Player& player) {
    if (impl_->session) {
        impl_->session->Cancel(player);
        impl_->session.reset();
    }
    impl_->morphType = static_cast<int>(Player::EnemyMorphType::None);
}

void PlayerCopyAbilityController::Draw(Player& player, ID3D12Resource* pointLight, ID3D12Resource* spotLight,
                                       Camera* camera, int previewBufferIndex) {
    if (impl_->session)
        impl_->session->Draw(player, pointLight, spotLight, camera, previewBufferIndex);
}

bool PlayerCopyAbilityController::HasOwnedVisuals() const {
    return impl_->session && impl_->session->HasOwnedVisuals();
}

const char* PlayerCopyAbilityController::GetStateLabel() const {
    return impl_->session ? impl_->session->GetStateLabel() : "未コピー";
}

void PlayerCopyAbilityController::SetPreviewAttackId(const std::string& id) {
    if (impl_->session) impl_->session->SetPreviewAttackId(id);
}

void PlayerCopyAbilityController::DrawOwnedVisuals(Player& player, uint32_t depthSrvHandle, uint32_t grabSrvHandle,
                                                   Camera* camera, int previewBufferIndex) {
    if (impl_->session) {
        impl_->session->DrawOwnedVisuals(player, depthSrvHandle, grabSrvHandle, camera, previewBufferIndex);
    }
}

bool PlayerCopyAbilityController::NotifyGuardedHit(Player& player, const Vector3& sourcePosition) {
    return impl_->session && impl_->session->NotifyGuardedHit(player, sourcePosition);
}

bool PlayerCopyAbilityController::HandlesMorphType(int morphType) const {
    return impl_->session && impl_->morphType == morphType;
}

bool PlayerCopyAbilityController::IsActive() const { return impl_->session != nullptr; }

bool PlayerCopyAbilityController::CanBreakImpactGate() const {
    return impl_->session && impl_->session->CanBreakImpactGate();
}

bool PlayerCopyAbilityController::IsPinkBounceSlamImpactActive() const {
    return impl_->session && impl_->session->IsPinkBounceSlamImpactActive();
}
