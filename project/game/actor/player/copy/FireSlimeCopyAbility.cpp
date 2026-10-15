#include "CopyAbilitySupport.h"
#include "CameraManager.h"
#include "Camera.h"
#include "game/actor/combat/AttackSequence.h"
#include <vector>

namespace {
using namespace CopyAbilityDetail;
// 炎コピーの入力と継続攻撃を管理し、専用 EffectScope で所有エフェクトをまとめて片付ける。
class FireSlimeCopyAbility final : public ICopyAbilitySession {
  public:
    explicit FireSlimeCopyAbility(EnemyAttackProfile profile) : ICopyAbilitySession(std::move(profile)) {
        // この能力が生成したメッシュ演出を、解除時にまとめて停止できるようスコープを確保する。
        if (MeshEffectManager* effects = MeshEffectManager::GetInstance()) {
            effectScope_ = effects->CreateEffectScope();
        }
    }

    ~FireSlimeCopyAbility() override {
        // セッションの破棄だけでも突進演出が残らないようにする。
        StopDashEffects();
    }
    const char* GetStateLabel() const override {
        // 優先度の高い継続行動を表示し、それ以外は単発攻撃の状態を使う。
        if (dashActive_) return "炎ダッシュ";
        if (breathMotionActive_) return "炎ブレス";
        return fireballSequence_.GetStateLabel();
    }
    void SetPreviewAttackId(const std::string& id) override {
        // プレビューでは指定IDの単発攻撃だけを選択候補にする。
        previewAttackId_ = id;
    }

    void ProcessInput(Player& player, const PlayerCopyAbilityInput& input) override {
        // ブレスの押し続け状態を保存し、突進中は火球の開始を受け付けない。
        primaryHeld_ = input.primaryHeld;
        if (input.secondaryTriggered && !dashActive_ && dashCooldown_ <= 0.0f) {
            BeginDash(player);
        } else if (input.specialTriggered && fireballCooldown_ <= 0.0f && !dashActive_) {
            Fireball(player);
        }
    }

    void Update(Player& player, float deltaTime) override {
        // 単発攻撃、突進、継続ブレスの順に更新し、同時実行を避ける。
        if (player.isDead || player.IsCinematicLocked()) fireballSequence_.Cancel();
        if (fireballSequence_.IsRunning()) fireballSequence_.Update(deltaTime, MakeSequenceContext(player));
        breathVisible_ = false;
        UpdateHeadVisual(player, deltaTime);
        fireballCooldown_ = std::max(0.0f, fireballCooldown_ - deltaTime);
        dashCooldown_ = std::max(0.0f, dashCooldown_ - deltaTime);
        breathDamageTimer_ -= deltaTime;
        breathEffectTimer_ -= deltaTime;
        dashEffectTimer_ -= deltaTime;
        dashGroundWakeTimer_ -= deltaTime;
        actionMotionTimer_ = std::max(0.0f, actionMotionTimer_ - deltaTime);
        if (dashActive_) {
            UpdateDash(player, deltaTime);
            return;
        }
        if (actionMotionTimer_ > 0.0f) {
            return;
        }
        if (!primaryHeld_) {
            StopBreathMotion(player);
            return;
        }
        if (!breathMotionActive_) {
            breathMotionActive_ = player.PlaySlimeAbilityMotion("player_ability_fire_breath", true, 1.0f, 0.05f, 0.08f);
        }
        const engine::Vector3 direction = NormalizePlanar(player.GetLockOnAttackDirection());
        const engine::Vector3 origin = player.GetWorldPosition() + engine::Vector3{0.0f, 0.78f, 0.0f};
        player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Idle, direction);
        UpdateBreathVisuals(player, origin, direction, deltaTime);
        if (breathEffectTimer_ <= 0.0f) {
            EmitDirectedPreset("fire_slime_breath", origin, direction, 1.0f);
            EmitDirectedPreset("fire_slime_breath_embers", origin, direction, 0.85f);
            breathEffectTimer_ = 0.065f;
        }
        if (breathDamageTimer_ <= 0.0f) {
            DamageBreath(player, origin, direction);
            breathDamageTimer_ = 0.22f;
        }
    }

    void Cancel(Player& player) override {
        // コピー解除時は演出・モーション・入力保持を消し、操作権を返す。
        fireballSequence_.Cancel();
        breathVisible_ = false;
        breathVisuals_.clear();
        headVisual_.reset();
        StopDashEffects();
        player.ClearSlimeAbilityMotion();
        dashActive_ = false;
        primaryHeld_ = false;
        breathMotionActive_ = false;
        breathDamageTimer_ = 0.0f;
        breathEffectTimer_ = 0.0f;
        dashTimer_ = 0.0f;
        dashEffectTimer_ = 0.0f;
        dashGroundWakeTimer_ = 0.0f;
        actionMotionTimer_ = 0.0f;
        hitTargets_.clear();
        player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::None);
        player.SetIsControlActive(true);
    }

    bool CanBreakImpactGate() const override {
        // 突進している間だけ衝撃ゲートを破壊できる。
        return dashActive_;
    }

    bool HasOwnedVisuals() const override {
        // 能力側が所有する頭部とブレスを、専用描画パスへ渡す判定に使う。
        return headVisual_ != nullptr || (breathVisible_ && !breathVisuals_.empty());
    }

    void DrawOwnedVisuals(Player& player, uint32_t depth, uint32_t grab, Camera* camera, int bufferIndex) override {
        // 死亡・演出ロック中は能力固有の見た目を描かず、通常カメラを補助的に使う。
        if (player.isDead || player.IsCinematicLocked())
            return;
        if (!camera)
            camera = CameraManager::GetInstance()->GetActiveCamera();
        if (!camera)
            return;
        if (headVisual_ && headVisual_->GetIsVisible()) {
            SyncHeadVisualTransform(player);
            headVisual_->DrawSpecialMaterialForCamera(camera, depth, grab, bufferIndex);
        }
        if (!breathVisible_)
            return;
        for (auto& flame : breathVisuals_) {
            flame->UpdateWorldMatrix(true);
            flame->DrawSpecialMaterialForCamera(camera, depth, grab, bufferIndex);
        }
    }

  private:
    void UpdateHeadVisual(Player& player, float dt) {
        // 頭部の炎は接触判定を持たない表示専用Objectとして遅延生成する。
        if (player.isDead || player.IsCinematicLocked()) {
            if (headVisual_)
                headVisual_->SetIsVisible(false);
            return;
        }
        if (!headVisual_) {
            auto* scene = SceneManager::GetInstance()->GetCurrentScene();
            if (!scene || !scene->GetObject3dCommon())
                return;
            headVisual_ = std::make_unique<engine::Object3d>();
            headVisual_->Initialize(scene->GetObject3dCommon());
            headVisual_->SetModel("Primitives/sphere");
            headVisual_->SetColliderType(ColliderType::kNone);
            headVisual_->SetCollisionAttribute(0);
            headVisual_->SetCollisionMask(0);
            headVisual_->SetMaterialType(11);
            headVisual_->SetBlendMode(BlendMode::kNormal);
            headVisual_->SetEnableLighting(false);
            headVisual_->SetEmissive(1.7f);
            if (auto* p = headVisual_->GetMeshRenderer()->GetWaterParamData()) {
                p->effectType = 2.0f;
                p->waveSpeed = 1.75f;
                p->waveFrequency = 2.2f;
                p->waveHeight = 0.55f;
                p->effectScale = 0.95f;
                p->effectSoftness = 0.48f;
                p->effectIntensity = 0.84f;
                p->billboardScale = 0.80f;
                p->effectScaleX = 0.92f;
                p->effectScaleY = 1.0f;
                p->effectScaleZ = 0.92f;
            }
        }

        headVisual_->SetIsVisible(player.GetIsVisible());
        headVisual_->SetColor({1.0f, 0.26f, 0.04f, 0.75f});
        headVisual_->Update(dt);
        SyncHeadVisualTransform(player);
    }

    void SyncHeadVisualTransform(Player& player) {
        // スライム本体の拡縮に追従させ、頭上の相対位置を維持する。
        const engine::Vector3 scale = player.GetScale();
        const float bodyScale = (std::max)({0.7f, std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)});
        const engine::Vector3 position =
            player.GetWorldPosition() + engine::Vector3{0.0f, 0.95f * (std::max)(0.65f, std::abs(scale.y)), 0.0f};
        headVisual_->SetTranslate(position);
        headVisual_->SetRotation({0.0f, player.GetMoveYaw(), 0.0f});
        headVisual_->SetScale({0.73f * bodyScale, 0.70f * bodyScale, 0.73f * bodyScale});
        headVisual_->UpdateWorldMatrix(true);
    }

    void UpdateBreathVisuals(Player& player, const engine::Vector3& origin, const engine::Vector3& direction, float dt) {
        if (player.isDead || player.IsCinematicLocked())
            return;
        if (breathVisuals_.empty()) {
            auto* scene = SceneManager::GetInstance()->GetCurrentScene();
            if (!scene || !scene->GetObject3dCommon())
                return;
            // 立体ブレスは能力側で所有し、能力解除時にまとめて破棄します。
            for (int i = 0; i < 7; ++i) {
                auto flame = std::make_unique<engine::Object3d>();
                flame->Initialize(scene->GetObject3dCommon());
                flame->SetModel("Primitives/sphere");
                flame->SetColliderType(ColliderType::kNone);
                flame->SetCollisionAttribute(0);
                flame->SetCollisionMask(0);
                flame->SetMaterialType(11);
                flame->SetBlendMode(BlendMode::kNormal);
                flame->SetEnableLighting(false);
                flame->SetEmissive(1.8f);
                if (auto* p = flame->GetMeshRenderer()->GetWaterParamData()) {
                    p->effectType = 5.0f;
                    p->waveSpeed = 2.45f;
                    p->effectScale = 0.92f;
                    p->effectSoftness = 0.58f;
                    p->effectIntensity = 0.92f;
                    p->billboardScale = 0.72f;
                    p->effectScaleX = 1.0f;
                    p->effectScaleY = 0.90f;
                    p->effectScaleZ = 1.15f;
                    p->uvOffsetX = 3.71f * (i + 1);
                    p->uvOffsetY = 6.13f * (i + 1);
                }
                breathVisuals_.push_back(std::move(flame));
            }
        }
        EnemyAttackDefinition fallback;
        fallback.maxRange = 8.0f;
        fallback.radius = 1.4f;
        const auto& attack = FindAttackOrFallback(profile_, "flame_breath", fallback);
        const float range = std::max(attack.maxRange, 4.0f);
        const float radius = std::max(attack.radius, 1.0f);
        breathVisible_ = true;
        breathVisualTime_ += dt;
        for (size_t i = 0; i < breathVisuals_.size(); ++i) {
            auto& flame = breathVisuals_[i];
            const float step = (static_cast<float>(i) + 0.5f) / breathVisuals_.size();
            const float pulse = std::sin(breathVisualTime_ * 9.0f + step * 7.0f) * 0.06f;
            const float width = std::max(range / 7.0f, radius * (0.65f + step * 0.55f));
            flame->SetTranslate(origin + direction * (step * range));
            flame->SetRotation({0.0f, std::atan2(direction.x, direction.z), 0.0f});
            flame->SetScale({width, (std::max)(radius * (0.48f + step * 0.22f + pulse), width * 0.68f), width * 1.12f});
            flame->SetColor({1.0f, 0.30f + step * 0.06f, 0.045f, 0.82f - step * 0.12f});
            if (auto* p = flame->GetMeshRenderer()->GetWaterParamData()) {
                p->flowSpeedY = 0.82f + step * 0.42f;
                p->effectScaleX = 1.08f + step * 0.56f;
                p->effectScaleY = 0.90f;
                p->effectScaleZ = 1.15f;
            }
            flame->Update(dt);
        }
    }

    void Fireball(Player& player) {
        // player_specialに紐づく攻撃から優先度最大の定義を選び、シーケンスを開始する。
        const EnemyAttackDefinition* selected = nullptr;
        for (const auto& candidate : profile_.attacks) {
            if (!candidate.sequence.enabled || candidate.sequence.binding != "player_special") continue;
            if (!previewAttackId_.empty() && candidate.id != previewAttackId_) continue;
            if (!selected || candidate.sequence.priority > selected->sequence.priority) selected = &candidate;
        }
        if (!selected || !fireballSequence_.Start(*selected, MakeSequenceContext(player))) return;
        StopBreathMotion(player);
        const bool authoredBody = std::any_of(selected->sequence.tracks.begin(), selected->sequence.tracks.end(),
            [](const auto& track) { return track.enabled && track.type == "body_animation"; });
        // 本体トラックがある攻撃では旧単発モーションを重ねて二重に変形させません。
        if (!authoredBody && !selected->animation.empty()) player.PlaySlimeAbilityMotion(selected->animation, false, 1.0f, 0.035f, 0.08f);
        actionMotionTimer_ = selected->sequence.duration;
        fireballCooldown_ = (std::max)(selected->cooldown, selected->sequence.duration);
    }

    AttackSequenceContext MakeSequenceContext(Player& player) const {
        // ロックオン方向と接地状態を渡し、データ駆動の攻撃イベントから利用できるようにする。
        AttackSequenceContext context;
        context.owner = &player;
        context.position = player.GetWorldPosition();
        context.forward = NormalizePlanar(player.GetLockOnAttackDirection());
        context.playerOwned = true;
        context.grounded = player.IsGrounded();
        return context;
    }

    void BeginDash(Player& player) {
        fireballSequence_.Cancel();
        // 前回の突進が異常終了していても、新しい炎を重ねる前に必ず回収します。
        StopDashEffects();
        StopBreathMotion(player);
        dashDirection_ = NormalizePlanar(player.GetLockOnAttackDirection());
        player.PlaySlimeAbilityMotion("player_ability_fire_dash", false, 1.0f, 0.025f, 0.055f);
        dashActive_ = true;
        dashTimer_ = 0.44f;
        dashCooldown_ = 1.05f;
        dashEffectTimer_ = 0.0f;
        dashGroundWakeTimer_ = 0.0f;
        actionMotionTimer_ = 0.0f;
        hitTargets_.clear();
        const engine::Vector3 position = player.GetWorldPosition();
        SpawnScopedEffect(effectScope_, "Resources/json/effect/effect_player_fire_blaze_burst.json",
                          position + engine::Vector3{0.0f, 0.18f, 0.0f});
        SpawnScopedEffect(effectScope_, "Resources/json/effect/effect_player_fire_blaze_body.json",
                          position + engine::Vector3{0.0f, 0.60f, 0.0f},
                          {0.0f, std::atan2(dashDirection_.x, dashDirection_.z), 0.0f});
        EmitPreset("player_fire_blaze_burst", position + engine::Vector3{0.0f, 0.42f, 0.0f});
        player.SetVelocity(
            {dashDirection_.x * 38.0f, std::max(player.GetVelocity().y, 0.0f), dashDirection_.z * 38.0f});
        player.SetMoveYaw(std::atan2(dashDirection_.x, dashDirection_.z));
        player.SetIsControlActive(false);
        player.StartEvasionInvincibility(0.50f);
        player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::BlazeDash);
        player.TriggerSlimeImpulse({1.08f, 1.16f, 3.62f}, 0.17f);
    }

    void UpdateDash(Player& player, float deltaTime) {
        // 突進中は向きと水平速度を固定し、終了時に制御と残速度を戻す。
        dashTimer_ = std::max(0.0f, dashTimer_ - deltaTime);
        const float progress = 1.0f - dashTimer_ / 0.44f;
        const float speed = 38.0f * (1.0f - progress * 0.36f);
        engine::Vector3 velocity = player.GetVelocity();
        velocity.x = dashDirection_.x * speed;
        velocity.z = dashDirection_.z * speed;
        if (player.IsGrounded() && velocity.y < 0.0f)
            velocity.y = 0.0f;
        player.SetVelocity(velocity);
        player.SetIsControlActive(false);
        player.SetMoveYaw(std::atan2(dashDirection_.x, dashDirection_.z));
        player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Dash, dashDirection_);
        player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::BlazeDash);
        DamageDash(player);
        if (dashEffectTimer_ <= 0.0f) {
            const engine::Vector3 trail = player.GetWorldPosition() - dashDirection_ * 0.72f + engine::Vector3{0.0f, 0.12f, 0.0f};
            SpawnScopedEffect(effectScope_, "Resources/json/effect/effect_player_fire_blaze_trail.json", trail,
                              {0.0f, std::atan2(dashDirection_.x, dashDirection_.z), 0.0f});
            SpawnScopedEffect(effectScope_, "Resources/json/effect/effect_player_fire_blaze_core.json",
                              trail + engine::Vector3{0.0f, 0.36f, 0.0f});
            EmitDirectedPreset("player_fire_blaze_trail", trail, dashDirection_ * -1.0f, 0.85f);
            dashEffectTimer_ = 0.055f;
        }
        if (player.IsGrounded() && dashGroundWakeTimer_ <= 0.0f) {
            const engine::Vector3 wake = player.GetWorldPosition() - dashDirection_ * 0.42f + engine::Vector3{0.0f, 0.04f, 0.0f};
            SpawnScopedEffect(effectScope_, "Resources/json/effect/effect_player_fire_blaze_ground_wake.json", wake,
                              {0.0f, std::atan2(dashDirection_.x, dashDirection_.z), 0.0f}, {0.92f, 0.92f, 1.16f});
            dashGroundWakeTimer_ = 0.10f;
        }
        if (dashTimer_ <= 0.0f) {
            StopDashEffects();
            dashActive_ = false;
            player.SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose::None);
            player.StopSlimeAbilityMotion(0.055f);
            player.SetIsControlActive(true);
            velocity.x *= 0.30f;
            velocity.z *= 0.30f;
            player.SetVelocity(velocity);
            hitTargets_.clear();
            EmitPreset("player_fire_blaze_burst", player.GetWorldPosition() + engine::Vector3{0.0f, 0.20f, 0.0f});
        }
    }

    void StopDashEffects() {
        // この能力が所有する突進演出スコープだけを停止する。
        if (MeshEffectManager* effects = MeshEffectManager::GetInstance()) {
            effects->StopEffectScope(effectScope_);
        }
    }

    void StopBreathMotion(Player& player) {
        // 押し続け解除時は見た目とループモーションを同時に止める。
        breathVisible_ = false;
        if (!breathMotionActive_) {
            return;
        }
        player.StopSlimeAbilityMotion(0.08f);
        breathMotionActive_ = false;
    }

    void DamageDash(Player& player) {
        // 重なるColliderを持つ敵でも、突進一回につき根Objectへ一度だけ与える。
        PhysicsQueryFilter filter;
        filter.mask = CollisionCategories::Get("enemy");
        filter.ignoredObject = &player;
        const engine::Vector3 center = player.GetWorldPosition() + engine::Vector3{0.0f, 0.72f, 0.0f};
        for (const PhysicsOverlapHit& hit : CollisionManager::GetInstance()->OverlapSphere(center, 1.55f, filter)) {
            engine::Object3d* target = FindEnemyRoot(hit.object);
            if (!target || !hitTargets_.insert(target).second)
                continue;
            EnemyAttackDefinition fallback;
            const EnemyAttackDefinition& attack = FindAttackOrFallback(profile_, "flame_breath", fallback);
            DamageEnemy(player, target, 12.0f, DamageType::Fire,
                        {dashDirection_.x * 11.0f, 5.5f, dashDirection_.z * 11.0f}, MakeBurnStatus(attack));
            VFXSequencer::PlayOneShot("player_fire_dash_hit_cue",
                                      target->GetWorldPosition() + engine::Vector3{0.0f, 0.58f, 0.0f});
        }
    }

    void DamageBreath(Player& player, const engine::Vector3& origin, const engine::Vector3& direction) {
        // 見た目に沿うカプセル形状で敵を探し、同じ更新内の多重ヒットを除く。
        EnemyAttackDefinition fallback;
        fallback.maxRange = 8.0f;
        fallback.radius = 1.4f;
        fallback.damage = 0.5f;
        const EnemyAttackDefinition& attack = FindAttackOrFallback(profile_, "flame_breath", fallback);
        PhysicsQueryFilter filter;
        filter.mask = CollisionCategories::Get("enemy");
        filter.ignoredObject = &player;
        std::unordered_set<engine::Object3d*> damaged;
        const engine::Vector3 end = origin + direction * std::max(attack.maxRange, 4.0f);
        for (const PhysicsOverlapHit& hit :
             CollisionManager::GetInstance()->OverlapCapsule(origin, end, std::max(attack.radius, 1.0f), filter)) {
            engine::Object3d* target = FindEnemyRoot(hit.object);
            if (!target || !damaged.insert(target).second)
                continue;
            DamageEnemy(player, target, std::max(attack.damage, 2.5f), DamageType::Fire,
                        {direction.x * 5.0f, 2.5f, direction.z * 5.0f}, MakeBurnStatus(attack));
        }
    }

    engine::Vector3 dashDirection_{0.0f, 0.0f, 1.0f};
    AttackSequence fireballSequence_;
    std::string previewAttackId_;
    std::unique_ptr<engine::Object3d> headVisual_;
    std::vector<std::unique_ptr<engine::Object3d>> breathVisuals_;
    bool breathVisible_ = false;
    float breathVisualTime_ = 0.0f;
    std::unordered_set<engine::Object3d*> hitTargets_;
    float fireballCooldown_ = 0.0f;
    float dashCooldown_ = 0.0f;
    float dashTimer_ = 0.0f;
    float dashEffectTimer_ = 0.0f;
    float breathDamageTimer_ = 0.0f;
    float breathEffectTimer_ = 0.0f;
    float dashGroundWakeTimer_ = 0.0f;
    float actionMotionTimer_ = 0.0f;
    MeshEffectManager::EffectScopeId effectScope_ = MeshEffectManager::kInvalidEffectScope;
    bool dashActive_ = false;
    bool primaryHeld_ = false;
    bool breathMotionActive_ = false;
};
} // namespace

namespace CopyAbilityDetail {
std::unique_ptr<ICopyAbilitySession> CreateFireSlimeCopyAbility(const EnemyAttackProfile& profile) {
    // 獲得ごとに独立したクールダウンと演出スコープを持つセッションを返す。
    return std::make_unique<FireSlimeCopyAbility>(profile);
}
} // namespace CopyAbilityDetail
