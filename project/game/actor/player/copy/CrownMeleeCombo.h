#pragma once
#include "CopyAbilitySupport.h"
#include "CrownMeleeSettings.h"
#include "CrownMeleeTrail.h"
#include "VfxSequencer.h"
#include "CrownAbilityEffects.h"

// 槍の姿勢・弧に沿った判定・体モーション・追加入力を、同じ段と時計から更新します。
// 関連：Playerから連続入力を受け、CrownMeleeTrailとCrownAbilityEffectsで槍の軌跡と命中演出を表示する。
// 主な入口：Begin / Update / End
class CrownMeleeCombo {
    using Vec = engine::Vector3;
    // ワールド空間の槍の握り位置・単位軸・穂先。表示と命中判定で同じ標本を使う。
    struct Pose { Vec grip, axis, tip; };
    CrownMeleeSettings settings_;
    CrownPresentationSettings presentation_;
    CrownAbilityEffects effects_;
    float lastFloorImpact_ = 1.0f; // 最後の床衝撃からの経過秒数。重複した床エフェクトを抑える。
    std::unique_ptr<engine::Object3D> lance_, gate_;
    CrownMeleeTrail trail_;
    std::unordered_set<engine::Object3D*> hits_;
    Vec direction_{0, 0, 1}, stageOrigin_{}, gateCenter_{}, gateNormal_{};
    float age_ = 0.0f, startHp_ = 0.0f, cooldown_ = 0.0f; // 段の経過秒数、開始時HP、次に開始できるまでの残り秒数。
    int stage_ = -1; // 現在の連撃段。-1は未実行、0〜2が三段の攻撃。
    bool queued_ = false, restoreControl_ = false, hitStopSent_ = false; // 次段の入力予約、開始前の操作許可、現在段のヒットストップ済み状態。
    bool impactSent_ = false; // 現在段の床衝撃を発生済みか。

    static constexpr std::array<float, 3> kHitStart{0.29f, 0.25f, 0.42f};
    static constexpr std::array<float, 3> kHitEnd{0.57f, 0.60f, 0.66f};
    static constexpr std::array<const char*, 3> kClips{
        "player_ability_crown_melee_sweep", "player_ability_crown_melee_rise", "player_ability_crown_melee_slam"};
    static float Ease(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
    static Vec Unit(Vec value, Vec fallback = {0, 0, 1}) {
        const float length = engine::Math::Length(value);
        return length > 0.0001f ? value / length : fallback;
    }
    static engine::Quaternion Aim(Vec axis, Vec direction) {
        const auto to = Unit(direction);
        const float dot = std::clamp(engine::Math::Dot(axis, to), -1.0f, 1.0f);
        if (dot > 0.9999f) return {0, 0, 0, 1};
        if (dot < -0.9999f) return {1, 0, 0, 0};
        const auto cross = engine::Math::Cross(axis, to);
        const float q = std::sqrt((1 + dot) * 2);
        return {cross.x / q, cross.y / q, cross.z / q, q * 0.5f};
    }
    // normalizedは現在段の進行率（0〜1）。表示と判定に共用するワールド姿勢を返す。
    Pose Sample(Player& player, float normalized) const {
        const float swing = Ease((normalized - kHitStart[stage_]) / (kHitEnd[stage_] - kHitStart[stage_]));
        const Vec right{direction_.z, 0, -direction_.x};
        const auto bounds = player.GetAabb();
        const float height = (std::max)(0.8f, bounds.max.y - bounds.min.y);
        const Vec center = player.GetWorldPosition() + Vec{0, height * 0.56f, 0};
        float yaw = -1.30f + 2.6f * swing;
        float pitch = 0.10f;
        if (stage_ == 1) {
            yaw = 1.25f - 2.25f * swing;
            pitch = -0.28f + 1.05f * swing;
        } else if (stage_ == 2) {
            yaw = -0.15f + 0.25f * swing;
            pitch = 1.46f - 1.95f * swing;
        }
        if (stage_ == 2 && (player.IsGrounded() || player.IsEditorInternal())) {
            // 地上の叩きつけは足元の高さで止め、長い槍の先端を床へ埋めません。
            const float floorPitch = std::asin(std::clamp(
                (bounds.min.y + 0.06f - center.y) / settings_.reach[stage_], -0.95f, 0.0f));
            pitch = (std::max)(pitch, floorPitch);
        }
        const Vec axis = Unit((direction_ * std::cos(yaw) + right * std::sin(yaw)) * std::cos(pitch)
                              + Vec{0, std::sin(pitch), 0});
        // 根元は体の側面に沿わせ、振りと同じ向きへ体がねじれるクリップを重ねます。
        const Vec pullOffset = stage_ == 2 ? Vec{0, height * 0.90f, 0}
                                            : right * (stage_ == 0 ? -1.10f : 1.10f);
        const float pull = 1.0f - Ease(normalized / kHitStart[stage_]);
        const Vec grip = center + direction_ * 0.24f + right * (0.28f * std::sin(yaw)) + pullOffset * pull;
        return {grip, axis, grip + axis * settings_.reach[stage_]};
    }
    // 段の時計・命中済み集合・帯の区間をリセットし、その段の照準を確定する。
    void BeginStage(Player& player, int stage) {
        stage_ = stage;
        age_ = 0.0f;
        queued_ = hitStopSent_ = impactSent_ = false;
        hits_.clear();
        trail_.Break();
        direction_ = CopyAbilityDetail::NormalizePlanar(player.GetLockOnAttackDirection());
        stageOrigin_ = player.GetWorldPosition();
        player.SetMoveYaw(std::atan2(direction_.x, direction_.z));
        player.SetSlimeAnimationDirection(direction_);
        player.SetVelocity({0, player.GetVelocity().y, 0});
        const auto pose = Sample(player, 0);
        gateCenter_ = pose.grip;
        gateNormal_ = pose.axis;
        // クリップの基準尺は1秒。Editorの段時間に合わせて全身と武器を同期します。
        player.PlaySlimeAbilityMotion(kClips[stage_], false, 1.0f / settings_.duration[stage_], 0.015f, 0.04f);
        UpdateVisuals(player, 0);
        CrownAbilityEffects::Emit("player_crown_portal_dust", gateCenter_, gateNormal_,
                                 presentation_.meleeGateScale * (stage_ == 2 ? 1.1f : 0.7f));
    }
    // この段で未命中の敵だけに通知する。同期DamageEventによる技の中断後は処理を続けない。
    void Hit(Player& player, const Pose& pose) {
        const int attackingStage = stage_;
        PhysicsQueryFilter enemies;
        enemies.mask = CollisionCategories::Get("enemy");
        enemies.ignoredObject = &player;
        const auto root = pose.grip + pose.axis * 0.35f;
        auto* world = CollisionManager::GetInstance();
        PhysicsQueryFilter ground = enemies;
        ground.mask = CollisionCategories::Get("ground");
        for (const auto& hit : world->OverlapCapsule(root, pose.tip, settings_.radius, enemies)) {
            auto* target = CopyAbilityDetail::FindEnemyRoot(hit.object);
            if (!target || hits_.contains(target)) continue;
            const auto bounds = hit.object->GetAabb();
            const Vec center = (bounds.min + bounds.max) * 0.5f;
            const float along = std::clamp(engine::Math::Dot(center - root, pose.axis), 0.0f,
                                          engine::Math::Length(pose.tip - root));
            const Vec projected = root + pose.axis * along;
            // 判定の半径で届いた敵は、槍の中心線より奥にいる場合があります。
            // 接触候補を敵の境界内へ寄せ、薄い壁を半径分だけ越えて当てないようにします。
            const Vec contact{std::clamp(projected.x, bounds.min.x, bounds.max.x),
                              std::clamp(projected.y, bounds.min.y, bounds.max.y),
                              std::clamp(projected.z, bounds.min.z, bounds.max.z)};
            const Vec ray = contact - pose.grip;
            const float distance = engine::Math::Length(ray);
            const auto wall = world->SphereCast(pose.grip, 0.04f, Unit(ray), distance, ground);
            if (wall.isHit && wall.distance + 0.04f < distance) continue;
            hits_.insert(target);
            const bool finishing = stage_ == 2;
            const float push = finishing ? 7.0f : 0.8f;
            CopyAbilityDetail::DamageEnemy(player, target, settings_.damage[stage_], DamageType::Physical,
                                           direction_ * push + Vec{0, finishing ? 2.0f : 0.0f, 0});
            // DamageEventは同期配信され、ボス撃破の演出がこの呼出し中に技を終了する場合があります。
            if (!Active() || stage_ != attackingStage) return;
            const Vec right{direction_.z,0,-direction_.x};
            const Vec sweep = stage_ == 0 ? right : stage_ == 1 ? right * -0.6f + Vec{0,0.8f,0} : Vec{0,-1,0};
            effects_.Hit(stage_, contact, pose.axis * -1.0f, sweep, presentation_);
            if (!hitStopSent_) {
                VfxSequencer::RequestHitStop(settings_.hitStop[stage_], 0.0f);
                hitStopSent_ = true;
            }
        }
    }
    // previous/currentは段内の進行率。弧を細分化し、大きなdtでも穂先の飛び越しを防ぐ。
    void Sweep(Player& player, float previous, float current) {
        const float from = (std::max)(previous, kHitStart[stage_]);
        const float to = (std::min)(current, kHitEnd[stage_]);
        if (to < from) return;
        // 大きいdtでも弧を細分化してから判定し、先端が敵を飛び越えないようにします。
        constexpr std::array<float, 3> kAngularTravel{2.6f, 3.3f, 2.2f};
        const float span = (to - from) / (kHitEnd[stage_] - kHitStart[stage_]);
        // SmoothStepの最大傾き1.5を含め、穂先が判定半径以上進む前に標本を置きます。
        const float maxTravel = span * 1.5f * kAngularTravel[stage_] * (settings_.reach[stage_] + 0.3f);
        const int steps = std::clamp(static_cast<int>(std::ceil(maxTravel / settings_.radius)), 1, 320);
        for (int i = 0; i <= steps; ++i) {
            const float t = from + (to - from) * float(i) / float(steps);
            const auto pose = Sample(player, t);
            Hit(player, pose);
            if (!Active()) return;
            trail_.Add(pose.tip, pose.axis, settings_.trailWidth * presentation_.trailScale * (stage_ == 2 ? 1.35f : 1.0f),
                       (current - t) * settings_.duration[stage_]);
        }
        if (stage_ == 2 && !impactSent_ && current >= kHitEnd[stage_]) {
            impactSent_ = true;
            const auto pose = Sample(player, kHitEnd[stage_]);
            PhysicsQueryFilter filter;
            filter.mask = CollisionCategories::Get("ground");
            filter.ignoredObject = &player;
            const auto floor = CollisionManager::GetInstance()->SphereCast(pose.tip + Vec{0, 0.8f, 0},
                                                                           0.06f, {0,-1,0}, 1.6f, filter);
            const bool previewFloor = !floor.isHit && player.IsEditorInternal();
            if ((floor.isHit || previewFloor) && lastFloorImpact_ > 0.1f) {
                lastFloorImpact_ = 0;
                // 命中の閃光を先に見せ、地面の衝撃波と破片はわずかに遅らせます。
                const Vec point = floor.isHit ? floor.hitPoint
                    : Vec{pose.tip.x,player.GetAabb().min.y,pose.tip.z};
                const Vec normal = floor.isHit ? floor.normal : Vec{0,1,0};
                effects_.Hit(2, point, normal, direction_, presentation_);
                effects_.Ring(point, normal, 2.8f * presentation_.ringScale, true, 0.035f);
            }
        }
    }
    // 現在段とage_から槍・ゲートの姿勢を作る。dtは秒で、命中判定はここでは発生させない。
    void UpdateVisuals(Player& player, float deltaTime) {
        if (!Active()) return;
        const float t = age_ / settings_.duration[stage_];
        const auto pose = Sample(player, t);
        const float reveal = Ease(t / (kHitStart[stage_] * 0.8f));
        const float fade = Ease((t - 0.80f) / 0.20f);
        const float length = settings_.reach[stage_] * (0.10f + reveal * 0.90f);
        const float scale = length / 3.65f;
        const Vec tip = pose.grip + pose.axis * length;
        const Vec center = tip - pose.axis * (2.02f * scale);
        lance_->SetTranslate(center);
        lance_->SetScale({scale * 0.85f * presentation_.meleeThickness, scale,
                          scale * 0.85f * presentation_.meleeThickness});
        lance_->SetQuaternion(Aim({0,-1,0}, pose.axis));
        engine::Object3D::PortalClipSettings clip;
        clip.enabled = true;
        clip.normal = fade > 0 ? pose.axis : pose.axis * -1.0f;
        clip.center = fade > 0 ? tip - pose.axis * (length + 0.30f) * fade : pose.grip;
        clip.progress = 0.47f;
        clip.edgeWidth = 0.10f;
        clip.dissolve = fade > 0 ? 0.20f : 0.04f;
        clip.color = {1.55f, 1.2f, 0.55f, 1.0f};
        lance_->SetPortalClip(clip);
        lance_->Update(deltaTime);

        const float gateOpen = Ease(t / 0.10f) * (1.0f - Ease((t - kHitStart[stage_]) / 0.18f));
        gate_->SetIsVisible(gateOpen > 0.005f);
        gate_->SetTranslate(gateCenter_ + player.GetWorldPosition() - stageOrigin_);
        gate_->SetQuaternion(Aim({0,0,1}, gateNormal_));
        const float extent = (stage_ == 2 ? 0.90f : 0.58f) * presentation_.meleeGateScale;
        gate_->SetScale({extent * gateOpen, extent, extent});
        gate_->SetColor({1.0f, 0.80f, 0.48f, 0.035f});
        if (auto* material = gate_->GetMeshRenderer()->GetWaterParamData()) {
            material->effectType = -1.0f;
            material->uvOffsetX = gateOpen;
            material->uvOffsetY = gateOpen;
            material->effectIntensity = 0.8f;
        }
        gate_->Update(deltaTime);
    }
public:
    bool Active() const { return stage_ >= 0; }
    int Stage() const { return stage_; }
    bool Ready() const { return !Active() && cooldown_ <= 0.0f; }
    bool CanDodge() const { return Active() && age_ >= settings_.duration[stage_] * kHitEnd[stage_]; }
    bool CanQueueNext() const {
        return Active() && stage_ < 2 && age_ >= settings_.queueStart && age_ < settings_.duration[stage_];
    }
    // 次段を予約できる進行区間だけ入力を受理する。受理できなければfalse。
    void QueueNext() { if (CanQueueNext()) queued_ = true; }

    // 表示物の生成に成功した場合だけ操作を止めて一段目を始める。開始不可ならfalse。
    template<class MakeVisual, class MakeGate>
    bool Begin(Player& player, MakeVisual makeVisual, MakeGate makeGate) {
        if (!Ready() || (!player.IsControlActive() && !player.IsEditorInternal())) return false;
        auto lance = makeVisual("Effects/false_king_crown_lance");
        auto gate = makeGate();
        if (!lance || !gate) return false;
        lance_ = std::move(lance);
        gate_ = std::move(gate);
        lance_->SetQuaternionMaster(true);
        lance_->SetColor({1.0f, 0.96f, 0.86f, 1.0f});
        lance_->SetEmissive(1.40f);
        settings_ = CrownMeleeSettings::Get();
        settings_.Sanitize();
        presentation_ = CrownPresentationSettings::Get();
        presentation_.Sanitize();
        settings_.radius *= presentation_.meleeThickness;
        auto* scene = SceneManager::GetInstance()->GetCurrentScene();
        trail_.Initialize(scene ? scene->GetObject3DCommon() : nullptr);
        startHp_ = player.GetHp();
        restoreControl_ = player.IsControlActive();
        player.SetIsControlActive(false);
        BeginStage(player, 0);
        return true;
    }
    // 槍・ゲートを解放し、条件が許す場合だけ開始前の操作許可を復元する。
    void End(Player& player, bool restore = true) {
        if (!Active()) return;
        stage_ = -1;
        queued_ = false;
        cooldown_ = 0.10f;
        hits_.clear();
        trail_.Break();
        lance_.reset();
        gate_.reset();
        player.StopSlimeAbilityMotion(0.04f);
        if (restore && restoreControl_ && !player.IsDead() && !player.IsCinematicLocked())
            player.SetIsControlActive(true);
        // 被弾で与えられた速度を消さないよう、通常終了だけ水平速度を止めます。
        if (player.GetHp() >= startHp_)
            player.SetVelocity({0, player.GetVelocity().y, 0});
    }
    // dtは秒。段末を越えた時間を次段へ渡し、被弾・死亡・演出開始では技を終了する。
    void Update(Player& player, float deltaTime) {
        if (!std::isfinite(deltaTime) || deltaTime <= 0) return;
        cooldown_ = (std::max)(0.0f, cooldown_ - deltaTime);
        effects_.Update(deltaTime);
        lastFloorImpact_ += deltaTime;
        trail_.Tick(deltaTime, settings_.trailLife);
        if (!Active()) return;
        if (player.IsDead() || player.IsCinematicLocked() || player.GetHp() < startHp_) { End(player); return; }
        float remaining = deltaTime, travel = 0.0f;
        // 最多3段で終了します。段末を越えたdtも次段へ渡します。
        while (Active() && remaining > 0.0f) {
            const float duration = settings_.duration[stage_];
            const float step = (std::min)(remaining, duration - age_);
            const float previous = age_ / duration;
            age_ = (std::min)(duration, age_ + step);
            const float current = age_ / duration;
            travel += settings_.stepDistance * (Ease((current - 0.12f) / 0.38f) - Ease((previous - 0.12f) / 0.38f));
            Sweep(player, previous, current);
            remaining -= step;
            if (age_ >= duration) {
                if (queued_ && stage_ < 2) BeginStage(player, stage_ + 1);
                else End(player);
            }
        }
        if (Active()) {
            player.SetVelocity(direction_ * (travel / deltaTime) + Vec{0, player.GetVelocity().y, 0});
            player.SetMoveYaw(std::atan2(direction_.x, direction_.z));
            player.ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Idle, direction_);
            UpdateVisuals(player, deltaTime);
        }
    }
    void Draw(ID3D12Resource* point, ID3D12Resource* spot, Camera* camera, int index) {
        effects_.Draw(point, spot, camera, index);
        trail_.Draw(point, spot, camera, index);
        if (!Active()) return;
        lance_->UpdateWorldMatrix(true);
        if (camera) lance_->DrawForCamera(camera, point, spot, index);
        else { lance_->RefreshRenderCameraData(); lance_->Draw(point, spot); }
    }
    // 攻撃後に残る残光と帯の履歴も消す。技の段階変更とは別に呼べる。
    void ClearEffects() { effects_.Clear(); trail_.Clear(); }
    void DrawGate(Camera* camera, uint32_t depth, uint32_t grab, int index) {
        if (!Active() || !gate_->GetIsVisible()) return;
        gate_->UpdateWorldMatrix(true);
        gate_->DrawSpecialMaterialForCamera(camera, depth, grab, index);
    }
};
