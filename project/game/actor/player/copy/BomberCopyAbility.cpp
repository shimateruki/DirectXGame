#include "CopyAbilitySupport.h"
#include "game/system/combat/GroundEffectLocator.h"

namespace {
using namespace CopyAbilityDetail;
constexpr float kBlastWindupDuration = 0.07f;
constexpr float kBlastJetDuration = 0.24f;
constexpr float kBlastJetPitch = 1.5707963f;

// 爆弾の設置・投擲と爆風ジャンプを制御し、各操作の再使用待ち時間を独立して持つ。
class BomberCopyAbility final : public ICopyAbilitySession {
  public:
    using ICopyAbilitySession::ICopyAbilitySession;

    void ProcessInput(Player& player, const PlayerCopyAbilityInput& input) override {
        // 二次操作と特殊操作を各クールダウンで分離し、同じ入力で連続発火させない。
        if (blastWindupActive_)
            return;
        if (input.secondaryTriggered && blastCooldown_ <= 0.0f)
            BeginBlastJump(player);
        else if (input.specialTriggered && throwCooldown_ <= 0.0f)
            SpawnBomb(player, false);
        else if (input.primaryTriggered && placeCooldown_ <= 0.0f)
            SpawnBomb(player, true);
    }

    void Update(Player& player, float deltaTime) override {
        // 投擲・設置・爆風ジャンプの再使用時間を独立に進める。
        throwCooldown_ = std::max(0.0f, throwCooldown_ - deltaTime);
        placeCooldown_ = std::max(0.0f, placeCooldown_ - deltaTime);
        blastCooldown_ = std::max(0.0f, blastCooldown_ - deltaTime);
        trailTimer_ -= deltaTime;

        if (blastWindupActive_) {
            // 地上では一瞬沈み込ませ、爆発と上昇を同じフレームで発生させる。
            blastWindupTimer_ -= deltaTime;

            if (blastWindupTimer_ <= 0.0f) {
                blastWindupActive_ = false;
                LaunchBlastJump(player);
            }
        }

        if (!blastActive_)
            return;
        blastElapsed_ += deltaTime;

        if (blastElapsed_ <= kBlastJetDuration && trailTimer_ <= 0.0f) {
            const engine::Vector3 position = player.GetWorldPosition() - blastDirection_ * 0.18f + engine::Vector3{0.0f, 0.12f, 0.0f};

            // 突き形状の軸を下へ向け、上昇する体の下側に噴流を残す。
            SpawnEffect("Resources/json/effect/effect_player_bomb_blast_jump_trail.json", position,
                        {kBlastJetPitch, 0.0f, 0.0f});
            EmitDirectedPreset("player_bomb_blast_jump", position, {0.0f, -1.0f, 0.0f}, 0.62f);
            trailTimer_ = 0.075f;
        }

        if (blastElapsed_ > 0.16f && player.IsGrounded() && player.GetVelocity().y <= 0.0f) {
            SpawnEffect("Resources/json/effect/effect_player_bomb_blast_jump_land.json", player.GetWorldPosition());
            blastActive_ = false;
        }
    }

    void Cancel(Player& player) override {
        blastWindupActive_ = false;
        blastActive_ = false;
        player.SetAbilityAirImpulse({});
    }

  private:
    void SpawnBomb(Player& player, bool placed) {
        SceneManager* manager = SceneManager::GetInstance();
        BaseScene* scene = manager ? manager->GetCurrentScene() : nullptr;
        if (!scene || !scene->GetObject3dCommon())
            return;
        auto bomb = EnemyFactory::GetInstance()->CreateEnemy("Bomb", scene->GetObject3dCommon());
        if (!bomb)
            return;
        const engine::Vector3 forward = NormalizePlanar(player.GetLockOnAttackDirection());
        engine::Vector3 position = player.GetWorldPosition() + forward * (placed ? 1.55f : 2.1f);
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
        bomb->SetVelocity(placed ? engine::Vector3{0.0f, 1.8f, 0.0f} : engine::Vector3{forward.x * 24.0f, 8.0f, forward.z * 24.0f});
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

    void BeginBlastJump(Player& player) {
        // 地上だけ短く沈み込ませる。空中では入力遅延を増やさず即座に爆発させる。
        blastDirection_ = NormalizePlanar(player.GetForwardDirection());
        blastCooldown_ = 1.10f;
        blastWindupTimer_ = player.IsGrounded() ? kBlastWindupDuration : 0.0f;
        player.StartEvasionInvincibility(0.32f + blastWindupTimer_);

        if (blastWindupTimer_ > 0.0f) {
            player.TriggerSlimeImpulse({1.70f, 0.64f, 1.70f}, kBlastWindupDuration + 0.03f);
            blastWindupActive_ = true;
        } else {
            LaunchBlastJump(player);
        }
    }

    void LaunchBlastJump(Player& player) {
        // 爆発を起点に上へ跳ねさせ、水平の押し出しは移動処理にも短時間渡す。
        const engine::Vector3 center = player.GetWorldPosition();
        engine::Vector3 blastOrigin = player.IsGrounded() ? GroundEffectLocator::ResolveGroundPosition(center) : center;
        blastOrigin.y += 0.08f;
        player.SetVelocity({blastDirection_.x * 8.5f, 18.0f, blastDirection_.z * 8.5f});
        player.SetAbilityAirImpulse({blastDirection_.x * 8.5f, 0.0f, blastDirection_.z * 8.5f});
        player.SetMoveYaw(std::atan2(blastDirection_.x, blastDirection_.z));
        player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Jump, blastDirection_);
        player.TriggerSlimeImpulse({0.78f, 2.70f, 0.78f}, 0.16f);
        DamageSphere(player, center + engine::Vector3{0.0f, 0.58f, 0.0f}, 3.2f, 8.0f, DamageType::Explosion, 13.0f, 9.5f);
        SpawnEffect("Resources/json/effect/effect_player_bomb_blast_jump.json", blastOrigin);
        EmitPreset("player_bomb_blast_jump", blastOrigin);

        // 離陸の瞬間にも一筋出し、次フレームの更新を待たずに爆風の向きを見せる。
        SpawnEffect("Resources/json/effect/effect_player_bomb_blast_jump_trail.json", blastOrigin,
                    {kBlastJetPitch, 0.0f, 0.0f});
        trailTimer_ = 0.075f;
        blastElapsed_ = 0.0f;
        blastActive_ = true;
    }

    engine::Vector3 blastDirection_{0.0f, 0.0f, 1.0f};
    float throwCooldown_ = 0.0f;
    float placeCooldown_ = 0.0f;
    float blastCooldown_ = 0.0f;
    float trailTimer_ = 0.0f;
    float blastElapsed_ = 0.0f;
    float blastWindupTimer_ = 0.0f;
    bool blastWindupActive_ = false;
    bool blastActive_ = false;
};

// 王冠の欠片から借りる力。弾・衝撃波は実メッシュと実際の命中判定を同じ位置で更新します。
} // namespace

namespace CopyAbilityDetail {
std::unique_ptr<ICopyAbilitySession> CreateBomberCopyAbility(const EnemyAttackProfile& profile) {
    // 個々のコピー獲得ごとにクールダウン状態を持つセッションを作る。
    return std::make_unique<BomberCopyAbility>(profile);
}
} // namespace CopyAbilityDetail
