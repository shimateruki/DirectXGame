#define NOMINMAX
#include "game/actor/player/copy/CopyAbilitySession.h"
#include "game/actor/player/Player.h"

// Controllerは能力の生成と寿命、呼び出しの転送だけを担当します。
using namespace CopyAbilityDetail;
namespace {
std::unique_ptr<ICopyAbilitySession> CreateSession(int morphType, const EnemyAttackProfile& profile) {
    // 変身種別に対応する状態付きセッションを生成し、未対応種別は空で返す。
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
    // 既定の攻撃プロファイルを読む際の敵種別へ変換する。
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

PlayerCopyAbilityController::PlayerCopyAbilityController() : impl_(std::make_unique<Impl>()) {
    // 能力セッションの具体型は非公開の実装側で所有する。
}
PlayerCopyAbilityController::~PlayerCopyAbilityController() = default;

void PlayerCopyAbilityController::Activate(int morphType, const EnemyAttackProfile& attackProfile) {
    // 種別を切り替える時に新しいセッションへ置き換え、前の状態を持ち越さない。
    impl_->morphType = morphType;
    impl_->session = CreateSession(morphType, attackProfile);
}

void PlayerCopyAbilityController::ActivateDefault(int morphType) {
    // コピー元の敵種別から共有プロファイルを読み、未対応なら能力を外す。
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
    // 入力の意味付けは能力別セッションへ委譲する。
    if (impl_->session) {
        impl_->session->ProcessInput(player, input);
    }
}

void PlayerCopyAbilityController::Update(Player& player, float deltaTime) {
    // 時間が進まないフレームでは能力の時計も進めない。
    if (impl_->session && deltaTime > 0.0f) {
        impl_->session->Update(player, deltaTime);
    }
}

void PlayerCopyAbilityController::Cancel(Player& player) {
    // セッションに演出を片付けさせてから破棄し、変身種別を未コピーへ戻す。
    if (impl_->session) {
        impl_->session->Cancel(player);
        impl_->session.reset();
    }
    impl_->morphType = static_cast<int>(Player::EnemyMorphType::None);
}

void PlayerCopyAbilityController::Draw(Player& player, ID3D12Resource* pointLight, ID3D12Resource* spotLight,
                                       Camera* camera, int previewBufferIndex) {
    // 能力固有の3D表示を、現在のセッションがある時だけ描く。
    if (impl_->session)
        impl_->session->Draw(player, pointLight, spotLight, camera, previewBufferIndex);
}

bool PlayerCopyAbilityController::HasOwnedVisuals() const {
    // 専用描画パスが必要かどうかを能力側の所有状態で決める。
    return impl_->session && impl_->session->HasOwnedVisuals();
}

const char* PlayerCopyAbilityController::GetStateLabel() const {
    // コピーがない間はUIへ「未コピー」を返す。
    return impl_->session ? impl_->session->GetStateLabel() : "未コピー";
}

void PlayerCopyAbilityController::SetPreviewAttackId(const std::string& id) {
    // 攻撃プレビュー中の絞り込み指定だけを現セッションへ渡す。
    if (impl_->session) impl_->session->SetPreviewAttackId(id);
}

void PlayerCopyAbilityController::DrawOwnedVisuals(Player& player, uint32_t depthSrvHandle, uint32_t grabSrvHandle,
                                                   Camera* camera, int previewBufferIndex) {
    // 深度・画面取得テクスチャを使う能力の見た目を専用パスへ転送する。
    if (impl_->session) {
        impl_->session->DrawOwnedVisuals(player, depthSrvHandle, grabSrvHandle, camera, previewBufferIndex);
    }
}

bool PlayerCopyAbilityController::NotifyGuardedHit(Player& player, const engine::Vector3& sourcePosition) {
    // ガード成功後の能力固有反応が発生したかを呼び出し元へ返す。
    return impl_->session && impl_->session->NotifyGuardedHit(player, sourcePosition);
}

bool PlayerCopyAbilityController::HandlesMorphType(int morphType) const {
    // セッションの実在と変身種別の一致をともに確認する。
    return impl_->session && impl_->morphType == morphType;
}

bool PlayerCopyAbilityController::IsActive() const {
    // 種別だけ残っていても、実行可能なセッションがなければ無効とみなす。
    return impl_->session != nullptr;
}

bool PlayerCopyAbilityController::CanBreakImpactGate() const {
    // ゲート破壊の可否は現在の能力動作に応じて変わる。
    return impl_->session && impl_->session->CanBreakImpactGate();
}

bool PlayerCopyAbilityController::IsPinkBounceSlamImpactActive() const {
    // ピンク能力の着地衝撃が有効な瞬間だけ判定を通す。
    return impl_->session && impl_->session->IsPinkBounceSlamImpactActive();
}
