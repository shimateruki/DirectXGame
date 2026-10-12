#include "CopyAbilitySupport.h"

namespace {
using namespace CopyAbilityDetail;
// 王冠能力の射撃と降下弾を制御し、弾の表示・移動・寿命をセッション内で管理する。
class CrownCopyAbility final : public ICopyAbilitySession {
  public:
    using ICopyAbilitySession::ICopyAbilitySession;

    void ProcessInput(Player& player, const PlayerCopyAbilityInput& input) override {
        // 演出ロック中は受け付けず、突進・衝撃波・槍・溜め落下弾を各再使用時間で分ける。
        if (player.IsCinematicLocked() || player.isDead)
            return;
        held_ = input.primaryHeld;
        if (dashTime_ > 0.0f)
            return;
        if (input.specialTriggered && dashCooldown_ <= 0.0f) {
            direction_ = NormalizePlanar(player.GetLockOnAttackDirection());
            dashTime_ = 0.38f;
            dashCooldown_ = 1.3f;
            dashHits_.clear();
            player.SetIsControlActive(false);
            player.StartEvasionInvincibility(0.38f);
            player.TriggerSlimeImpulse({1.3f, 1.4f, 2.8f}, 0.16f);
            EmitDirectedPreset("false_king_rush_wake", player.GetWorldPosition(), direction_);
        }
        if (input.secondaryTriggered && waveCooldown_ <= 0.0f) {
            waveCenter_ = player.GetWorldPosition();
            waveTime_ = 0.01f;
            waveCooldown_ = 2.4f;
            waveHits_.clear();
            player.TriggerSlimeImpulse({2.6f, 0.75f, 2.6f}, 0.2f);
            EmitPreset("false_king_shockwave", waveCenter_);
        }
        if (input.primaryTriggered && lanceCooldown_ <= 0.0f) {
            const Vector3 forward = NormalizePlanar(player.GetLockOnAttackDirection());
            const Vector3 side{forward.z, 0.0f, -forward.x};
            for (int i = -1; i <= 1; ++i) {
                SpawnShot(player.GetWorldPosition() + Vector3{0.0f, 1.7f, 0.0f} + side * (0.65f * i),
                          NormalizePlanar(forward + side * (0.08f * i)) * 36.0f, false);
            }
            lanceCooldown_ = 0.55f;
        }
        if (!input.primaryHeld && chargeTime_ > 0.85f && rainCooldown_ <= 0.0f) {
            Vector3 center = player.GetWorldPosition() + NormalizePlanar(player.GetForwardDirection()) * 14.0f;
            if (auto* scene = SceneManager::GetInstance()->GetCurrentScene()) {
                float nearest = 32.0f;
                for (const auto& object : scene->GetObjects()) {
                    auto* enemy = dynamic_cast<BaseEnemy*>(object.get());
                    if (!enemy || enemy->isDead || !enemy->GetIsVisible())
                        continue;
                    const float distance = Math::Length(enemy->GetWorldPosition() - player.GetWorldPosition());
                    if (distance < nearest) {
                        nearest = distance;
                        center = enemy->GetWorldPosition();
                    }
                }
            }
            for (int i = 0; i < 6; ++i) {
                const float angle = static_cast<float>(i) * kPi / 3.0f;
                SpawnShot(center + Vector3{std::sin(angle) * 1.5f, 12.0f + i * 1.3f, std::cos(angle) * 1.5f},
                          {0.0f, -32.0f, 0.0f}, true);
            }
            rainCooldown_ = 3.0f;
            chargeTime_ = 0.0f;
        }
    }

    void Update(Player& player, float dt) override {
        // 突進と衝撃波は領域判定、槍は移動線分判定でフレーム間のすり抜けを防ぐ。
        time_ += dt;
        lanceCooldown_ = std::max(0.0f, lanceCooldown_ - dt);
        waveCooldown_ = std::max(0.0f, waveCooldown_ - dt);
        dashCooldown_ = std::max(0.0f, dashCooldown_ - dt);
        rainCooldown_ = std::max(0.0f, rainCooldown_ - dt);
        chargeTime_ = held_ ? std::min(1.5f, chargeTime_ + dt) : 0.0f;
        EnsureVisuals();
        const auto& tuning = CrownFinaleTuning::Get();
        if (dashTime_ > 0.0f) {
            dashTime_ = std::max(0.0f, dashTime_ - dt);
            player.SetVelocity({direction_.x * 32.0f, player.GetVelocity().y, direction_.z * 32.0f});
            HitArea(player, player.GetWorldPosition() + direction_ * 1.2f, 2.8f, tuning.dashDamage, dashHits_, false);
            if (dashTime_ <= 0.0f) {
                player.SetIsControlActive(true);
                player.SetVelocity({0.0f, player.GetVelocity().y, 0.0f});
            }
        }
        if (waveTime_ > 0.0f) {
            waveTime_ += dt;
            const float radius = std::min(16.0f, waveTime_ * 22.0f);
            HitArea(player, waveCenter_ + Vector3{0.0f, 1.0f, 0.0f}, radius, tuning.waveDamage, waveHits_, true);
            if (wave_) {
                wave_->SetTranslate(waveCenter_ + Vector3{0.0f, 0.15f, 0.0f});
                wave_->SetScale({radius / 1.115f, 0.8f, radius / 1.115f});
                wave_->Update(dt);
            }
            if (waveTime_ > 0.8f)
                waveTime_ = 0.0f;
        }
        for (auto it = shots_.begin(); it != shots_.end();) {
            it->life -= dt;
            const Vector3 movement = it->velocity * dt;
            // 高速弾は線分で調べ、フレーム間で敵や地形を飛び越えません。
            PhysicsQueryFilter filter;
            filter.mask = CollisionCategories::Get("enemy") | CollisionCategories::Get("ground");
            filter.ignoredObject = &player;
            const float length = Math::Length(movement);
            const auto hit = CollisionManager::GetInstance()->Raycast(
                it->position, length > 0.0001f ? movement / length : Vector3{0.0f, -1.0f, 0.0f}, length, filter);
            if (hit.isHit) {
                if (Object3d* enemy = FindEnemyRoot(hit.hitObject)) {
                    DamageEnemy(player, enemy, tuning.lanceDamage * (it->rain ? 1.4f : 1.0f), DamageType::Physical, {});
                }
                EmitPreset("false_king_lance_impact", hit.hitPoint);
                it = shots_.erase(it);
                continue;
            }
            it->position += movement;
            it->visual->SetTranslate(it->position);
            it->visual->Update(dt);
            if (it->life <= 0.0f)
                it = shots_.erase(it);
            else
                ++it;
        }
        ambientTimer_ -= dt;
        if (ambientTimer_ <= 0.0f) {
            EmitPreset(chargeTime_ > 0.85f ? "false_king_charge" : "crown_goal_idle_sparkle",
                       player.GetWorldPosition() + Vector3{0.0f, 1.8f, 0.0f});
            ambientTimer_ = chargeTime_ > 0.85f ? 0.18f : 0.65f;
        }
    }

    void Draw(Player& player, ID3D12Resource* point, ID3D12Resource* spot, Camera* camera,
              int previewBufferIndex) override {
        EnsureVisuals();
        // 王冠は覇王スライム本体に一体化し、離れた頭上へ二重表示しない。
        if (wings_ && dashTime_ > 0.0f && !player.IsCinematicLocked()) {
            wings_->SetTranslate(player.GetWorldPosition() + Vector3{0.0f, 0.9f, 0.0f});
            wings_->SetScale({0.48f, 0.48f, 0.48f});
            wings_->SetRotation({0.0f, player.GetMoveYaw(), 0.0f});
            DrawVisual(wings_.get(), point, spot, camera, previewBufferIndex);
        }
        if (waveTime_ > 0.0f)
            DrawVisual(wave_.get(), point, spot, camera, previewBufferIndex);
        for (const auto& shot : shots_)
            DrawVisual(shot.visual.get(), point, spot, camera, previewBufferIndex);
    }

    void Cancel(Player& player) override {
        // 突進中に解除した場合は操作権を戻し、能力側の表示弾を消す。
        if (dashTime_ > 0.0f && !player.IsCinematicLocked())
            player.SetIsControlActive(true);
        dashTime_ = waveTime_ = 0.0f;
        shots_.clear();
        wings_.reset();
        wave_.reset();
    }

  private:
    // 王冠能力の弾表示と移動状態。life は残り秒、rain は落下弾の扱いを選ぶ。
    struct Shot {
        std::unique_ptr<Object3d> visual;
        Vector3 position{}, velocity{};
        float life = 2.2f;
        bool rain = false;
    };
    std::unique_ptr<Object3d> MakeVisual(const char* model) {
        // 弾や翼は表示専用で生成し、物理Colliderの対象には入れない。
        auto* scene = SceneManager::GetInstance()->GetCurrentScene();
        if (!scene || !scene->GetObject3dCommon())
            return {};
        auto visual = std::make_unique<Object3d>();
        visual->Initialize(scene->GetObject3dCommon());
        visual->SetModel(model);
        visual->SetMaterialType(0);
        visual->SetBlendMode(BlendMode::kNone);
        visual->SetColliderType(ColliderType::kNone);
        visual->SetCollisionAttribute(0);
        visual->SetCollisionMask(0);
        visual->SetColor({1.0f, 0.92f, 0.58f, 1.0f});
        visual->SetEmissive(1.7f);
        visual->SetEnableLighting(true);
        return visual;
    }
    void EnsureVisuals() {
        // シーンが利用できる時点で表示モデルを遅延生成する。
        if (!wings_)
            wings_ = MakeVisual("Effects/false_king_rush_wings");
        if (!wave_)
            wave_ = MakeVisual("Effects/false_king_royal_wave");
    }
    static void DrawVisual(Object3d* visual, ID3D12Resource* point, ID3D12Resource* spot, Camera* camera,
                           int previewBufferIndex) {
        // 指定カメラがあればプレビューへ描き、なければ現在シーンのカメラを使う。
        if (!visual)
            return;
        visual->UpdateWorldMatrix(true);
        if (camera)
            visual->DrawForCamera(camera, point, spot, previewBufferIndex);
        else {
            visual->RefreshRenderCameraData();
            visual->Draw(point, spot);
        }
    }
    void SpawnShot(const Vector3& position, const Vector3& velocity, bool rain) {
        if (shots_.size() >= 24)
            return;
        Shot shot;
        shot.visual = MakeVisual("Effects/false_king_crown_lance");
        if (!shot.visual)
            return;
        shot.position = position;
        shot.velocity = velocity;
        shot.rain = rain;
        shot.visual->SetTranslate(position);
        shot.visual->SetScale({0.6f, 0.8f, 0.6f});
        // 元モデルは下向きの槍。射撃時は穂先を進行方向へ向けます。
        if (!rain)
            shot.visual->SetRotation({-kPi / 2.0f, std::atan2(velocity.x, velocity.z), 0.0f});
        shots_.push_back(std::move(shot));
    }
    static void HitArea(Player& player, const Vector3& center, float radius, float damage,
                        std::unordered_set<Object3d*>& hits, bool openArmor) {
        // 同じ攻撃の多重ヒットを除き、衝撃波では王冠装甲を開く処理も行う。
        PhysicsQueryFilter filter;
        filter.mask = CollisionCategories::Get("enemy");
        filter.ignoredObject = &player;
        for (const auto& hit : CollisionManager::GetInstance()->OverlapSphere(center, radius, filter)) {
            Object3d* target = FindEnemyRoot(hit.object);
            if (!target || !hits.insert(target).second)
                continue;
            if (openArmor) {
                if (auto* king = dynamic_cast<EnemyFalseKingSlime*>(target))
                    king->OpenCrownArmor();
            }
            DamageEnemy(player, target, damage, DamageType::Physical, {});
            EmitPreset("false_king_lance_impact", target->GetWorldPosition() + Vector3{0.0f, 1.0f, 0.0f});
        }
    }
    std::vector<Shot> shots_;
    std::unique_ptr<Object3d> wings_, wave_;
    std::unordered_set<Object3d*> dashHits_, waveHits_;
    Vector3 direction_{0.0f, 0.0f, 1.0f}, waveCenter_{};
    float time_ = 0.0f, ambientTimer_ = 0.0f, chargeTime_ = 0.0f;
    float dashTime_ = 0.0f, waveTime_ = 0.0f;
    float lanceCooldown_ = 0.0f, waveCooldown_ = 0.0f, dashCooldown_ = 0.0f, rainCooldown_ = 0.0f;
    bool held_ = false;
};
} // namespace

namespace CopyAbilityDetail {
std::unique_ptr<ICopyAbilitySession> CreateCrownCopyAbility(const EnemyAttackProfile& profile) {
    // 王冠能力の一時状態を獲得ごとに独立させる。
    return std::make_unique<CrownCopyAbility>(profile);
}
} // namespace CopyAbilityDetail
