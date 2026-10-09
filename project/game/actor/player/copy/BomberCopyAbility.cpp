#include "CopyAbilitySupport.h"

namespace {
using namespace CopyAbilityDetail;
// 爆弾の設置・投擲と爆風ジャンプを制御し、各操作の再使用待ち時間を独立して持つ。
class BomberCopyAbility final : public ICopyAbilitySession {
  public:
    using ICopyAbilitySession::ICopyAbilitySession;

    void ProcessInput(Player& player, const PlayerCopyAbilityInput& input) override {
        if (input.secondaryTriggered && blastCooldown_ <= 0.0f)
            BlastJump(player);
        else if (input.specialTriggered && throwCooldown_ <= 0.0f)
            SpawnBomb(player, false);
        else if (input.primaryTriggered && placeCooldown_ <= 0.0f)
            SpawnBomb(player, true);
    }

    void Update(Player& player, float deltaTime) override {
        throwCooldown_ = std::max(0.0f, throwCooldown_ - deltaTime);
        placeCooldown_ = std::max(0.0f, placeCooldown_ - deltaTime);
        blastCooldown_ = std::max(0.0f, blastCooldown_ - deltaTime);
        trailTimer_ -= deltaTime;
        if (!blastActive_)
            return;
        blastElapsed_ += deltaTime;
        if (trailTimer_ <= 0.0f) {
            const Vector3 position = player.GetWorldPosition() - blastDirection_ * 0.32f + Vector3{0.0f, 0.12f, 0.0f};
            SpawnEffect("Resources/json/effect/effect_player_bomb_blast_jump_trail.json", position);
            EmitDirectedPreset("player_bomb_blast_jump", position, {0.0f, -1.0f, 0.0f}, 0.62f);
            trailTimer_ = 0.055f;
        }
        if (blastElapsed_ > 0.16f && player.IsGrounded() && player.GetVelocity().y <= 0.0f) {
            SpawnEffect("Resources/json/effect/effect_player_bomb_blast_jump_land.json", player.GetWorldPosition());
            blastActive_ = false;
        }
    }

    void Cancel(Player&) override { blastActive_ = false; }

  private:
    void SpawnBomb(Player& player, bool placed) {
        SceneManager* manager = SceneManager::GetInstance();
        BaseScene* scene = manager ? manager->GetCurrentScene() : nullptr;
        if (!scene || !scene->GetObject3dCommon())
            return;
        auto bomb = EnemyFactory::GetInstance()->CreateEnemy("Bomb", scene->GetObject3dCommon());
        if (!bomb)
            return;
        const Vector3 forward = NormalizePlanar(player.GetLockOnAttackDirection());
        Vector3 position = player.GetWorldPosition() + forward * (placed ? 1.55f : 2.1f);
        position.y += placed ? 0.42f : 2.15f;
        bomb->SetTranslate(position);
        bomb->SetRotationY(std::atan2(forward.x, forward.z));
        bomb->SetTarget(&player);
        if (player.IsEditorInternal()) {
            bomb->SetName("__Editor_EnemyAttackPreview_CopyBomb");
            bomb->SetEditorInternal(true);
            bomb->SetIsLocked(true);
        }
        if (EnemyBomb* enemyBomb = dynamic_cast<EnemyBomb*>(bomb.get())) {
            enemyBomb->SetPlayerOwned(true);
            enemyBomb->Ignite(placed ? 1.35f : 2.25f);
        }
        bomb->SetCarried(false);
        bomb->SetVelocity(placed ? Vector3{0.0f, 1.8f, 0.0f} : Vector3{forward.x * 24.0f, 8.0f, forward.z * 24.0f});
        SpawnEffect(placed ? "Resources/json/effect/effect_player_bomb_place.json"
                           : "Resources/json/effect/effect_carry_bomber_throw_burst.json",
                    position);
        EmitPreset(placed ? "player_bomb_place_fuse" : "carry_bomber_throw_sparks", position);
        // プレビューはシーンの追加待ちキューを経由せず、巻き戻し中も同じ固定刻みで爆弾を更新します。
        if (player.IsEditorInternal())
            player.AddCopyPreviewObject(std::move(bomb));
        else
            scene->AddObject(std::move(bomb));
        if (placed) {
            placeCooldown_ = 0.85f;
            player.TriggerSlimeImpulse({1.62f, 0.78f, 1.62f}, 0.13f);
        } else {
            throwCooldown_ = 0.42f;
        }
    }

    void BlastJump(Player& player) {
        blastDirection_ = NormalizePlanar(player.GetForwardDirection());
        const Vector3 center = player.GetWorldPosition();
        player.SetVelocity({blastDirection_.x * 8.5f, 18.0f, blastDirection_.z * 8.5f});
        player.StartEvasionInvincibility(0.32f);
        player.SetMoveYaw(std::atan2(blastDirection_.x, blastDirection_.z));
        player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Jump, blastDirection_);
        player.TriggerSlimeImpulse({2.55f, 0.62f, 2.55f}, 0.17f);
        DamageSphere(player, center + Vector3{0.0f, 0.58f, 0.0f}, 3.2f, 8.0f, DamageType::Explosion, 13.0f, 9.5f);
        SpawnEffect("Resources/json/effect/effect_player_bomb_blast_jump.json", center);
        EmitPreset("player_bomb_blast_jump", center);
        blastCooldown_ = 1.10f;
        trailTimer_ = 0.0f;
        blastElapsed_ = 0.0f;
        blastActive_ = true;
    }

    Vector3 blastDirection_{0.0f, 0.0f, 1.0f};
    float throwCooldown_ = 0.0f;
    float placeCooldown_ = 0.0f;
    float blastCooldown_ = 0.0f;
    float trailTimer_ = 0.0f;
    float blastElapsed_ = 0.0f;
    bool blastActive_ = false;
};

// 王冠の欠片から借りる力。弾・衝撃波は実メッシュと実際の命中判定を同じ位置で更新します。
} // namespace

namespace CopyAbilityDetail {
std::unique_ptr<ICopyAbilitySession> CreateBomberCopyAbility(const EnemyAttackProfile& profile) {
    return std::make_unique<BomberCopyAbility>(profile);
}
} // namespace CopyAbilityDetail
