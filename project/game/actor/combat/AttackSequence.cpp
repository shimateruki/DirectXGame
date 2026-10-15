#include "AttackSequence.h"
#include "BulletManager.h"
#include "CollisionConfig.h"
#include "GPUParticleManager.h"
#include "MeshEffectManager.h"
#include "CollisionManager.h"
#include "EventManager.h"
#include "MeshRenderer.h"
#include <numeric>
#include <cmath>

bool AttackSequence::Start(const EnemyAttackDefinition& definition, const AttackSequenceContext& context) {
    if (IsRunning() || !definition.sequence.enabled || !definition.sequence.Validate()) return false;
    std::vector<TrackState> prepared(definition.sequence.tracks.size());
    for (size_t i = 0; i < prepared.size(); ++i) {
        const auto& track = definition.sequence.tracks[i];
        if (track.enabled && track.type == "body_animation" &&
            !prepared[i].bodyClip.Load(BodyAnimationClip::ResolveAssetPath(track.preset))) {
            label_ = "本体アニメーションを読み込めません: " + track.preset;
            return false; // 一部分だけを実行せず、開始前に失敗させます。
        }
    }
    if (!clock_.Start(definition.sequence)) return false;
    ++generation_;
    ReleaseTrackEffects();
    definition_ = definition;
    initialContext_ = context;
    initialContext_.owner = nullptr; // 保存するのは座標のみ。寿命のあるActor参照を持ち越しません。
    trackStates_ = std::move(prepared);
    bodyPose_ = std::make_shared<engine::Matrix4x4>(engine::Math::MakeIdentity4x4());
    if (context.owner && context.owner->GetMeshRenderer())
        context.owner->GetMeshRenderer()->SetTimelineVisualTransform(bodyPose_);
    Update(0.0f, context); // 時刻0の命令を入力されたフレームに実行します。
    return true;
}

void AttackSequence::Update(float dt, const AttackSequenceContext& context) {
    if (!IsRunning() || !std::isfinite(dt) || dt < 0) return;
    const float previous = clock_.GetElapsed();
    clock_.Update(dt, [&](const std::string& condition) { return CheckCondition(condition, context); },
        [&](const AttackSequenceEvent& event) { Execute(event, context); });
    // 条件停止の前に通過した判定区間は処理し、停止時刻より後は処理しません。
    if (!clock_.WasStoppedByCondition() || clock_.GetElapsed() > previous) UpdateTracks(previous, context);
    if (!IsRunning()) ReleaseTrackEffects();
    label_ = definition_.displayName + (IsRunning() ? " / 実行中" : " / 終了") +
        " / 発火 " + std::to_string(clock_.GetExecutedCount()) + " / 条件不成立 " + std::to_string(clock_.GetSkippedCount());
    size_t allowed = 0, skipped = 0, contacts = 0;
    for (const auto& state : trackStates_) {
        if (state.entered) { if (state.allowed) ++allowed; else ++skipped; }
        contacts += state.victims.size();
    }
    label_ += " / 部品実行 " + std::to_string(allowed) + " / 部品省略 " + std::to_string(skipped) +
        " / 接触 " + std::to_string(contacts);
}

bool AttackSequence::CheckCondition(const std::string& condition, const AttackSequenceContext& context) const {
    if (condition == "grounded") return context.grounded;
    if (condition == "airborne") return !context.grounded;
    if (condition == "target_in_range") {
        // 照準時に固定した発射速度用の距離と、発火時点の射程判定を分けます。
        engine::Vector3 difference = context.targetPosition - context.position;
        difference.y = 0;
        const float distance = engine::Math::Length(difference);
        return context.hasTarget && distance >= definition_.minRange && distance <= definition_.maxRange;
    }
    return condition == "always";
}

AttackSequence::~AttackSequence() {
    // シーケンスより長く残る演出スコープを終了させる。
    ReleaseTrackEffects();
}

void AttackSequence::ReleaseTrackEffects() {
    // トラックごとのエフェクトスコープを止め、次の攻撃へ残さない。
    bodyPose_.reset();
    for (auto& state : trackStates_) {
        if (state.effectScope) MeshEffectManager::GetInstance()->StopEffectScope(state.effectScope);
        state.effectScope = 0;
    }
}

void AttackSequence::Cancel() {
    // 世代番号を進め、以前の非同期イベントが現在の攻撃へ混入しないようにする。
    ++generation_;
    ReleaseTrackEffects();
    clock_.Reset();
    trackStates_.clear();
    label_ = "中断";
}

void AttackSequence::UpdateTracks(float previous, const AttackSequenceContext& context) {
    const float now = clock_.GetElapsed();
    std::vector<DamageEvent> damageEvents;
    if (bodyPose_) *bodyPose_ = engine::Math::MakeIdentity4x4();
    std::vector<size_t> order(definition_.sequence.tracks.size());
    std::iota(order.begin(), order.end(), size_t{0});
    // 一更新で分岐元の終了と後続の開始を跨いでも、JSONの配列順に依存させません。
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return definition_.sequence.tracks[a].begin < definition_.sequence.tracks[b].begin;
    });
    for (size_t i : order) {
        const auto& track = definition_.sequence.tracks[i];
        auto& state = trackStates_[i];
        if (!track.enabled || now < track.begin) continue;
        if (clock_.WasStoppedByCondition() && now <= track.begin) continue;
        if (!state.entered) {
            state.entered = true;
            state.allowed = CheckCondition(track.condition, context);
            if (track.condition == "contact" || track.condition == "no_contact" || track.condition == "track_completed") {
                const auto& source = trackStates_[track.conditionTrack];
                const bool completed = source.entered && source.allowed && now >= definition_.sequence.tracks[track.conditionTrack].end;
                state.allowed = completed && (track.condition == "track_completed" ||
                    (track.condition == "contact" ? !source.victims.empty() : source.victims.empty()));
            }
        }
        if (!state.allowed) continue;
        if (track.parentTrack >= 0 && !trackStates_[track.parentTrack].allowed) continue;
        auto origin = track.followOwner ? context : initialContext_;
        if (track.parentTrack >= 0) {
            origin.position = trackStates_[track.parentTrack].worldPosition;
            origin.forward = trackStates_[track.parentTrack].worldForward;
        }
        engine::Vector3 forward{origin.forward.x, 0, origin.forward.z};
        const float length = engine::Math::Length(forward);
        forward = length > 0.0001f ? forward * (1 / length) : engine::Vector3{0, 0, 1};
        const engine::Vector3 right{forward.z, 0, -forward.x};
        auto positionAt = [&](const AttackTransformKey& key) {
            if (track.motion != "keyframed" && state.motionInitialized) return state.motion.position;
            return origin.position + right * key.position[0] + engine::Vector3{0, key.position[1], 0} + forward * key.position[2];
        };
        const float localTime = std::clamp(now - track.begin, 0.0f, track.end - track.begin);
        const auto key = track.Evaluate(localTime);
        state.frameStartPosition = positionAt(track.Evaluate(std::clamp(previous - track.begin, 0.0f, track.end - track.begin)));
        if (track.motion != "keyframed") {
            if (!state.motionInitialized) {
                state.motion.position = positionAt(track.Evaluate(0));
                state.motion.direction = forward;
                state.motionInitialized = true;
            }
            state.motion.Advance(track, localTime, context.targetPosition + engine::Vector3{0, track.targetHeight, 0}, context.hasTarget);
        }
        state.worldPosition = positionAt(key);
        state.worldForward = track.motion != "keyframed" ? state.motion.direction : forward;
        if (track.type == "body_animation") {
            if (now >= track.end || !bodyPose_) continue;
            BodyAnimationClip::Sample sample;
            if (!state.bodyClip.Evaluate(track.clipOffset + localTime * track.playbackSpeed, track.loop, sample)) continue;
            const float weight = (std::min)(track.blendIn > 0 ? std::clamp(localTime / track.blendIn, 0.0f, 1.0f) : 1.0f,
                track.blendOut > 0 ? std::clamp((track.end - now) / track.blendOut, 0.0f, 1.0f) : 1.0f);
            engine::Matrix4x4 shear = engine::Math::MakeIdentity4x4();
            shear.m[1][0] = sample.shear.x * weight;
            shear.m[1][2] = sample.shear.z * weight;
            shear.m[3][0] = -sample.shearPivotY * shear.m[1][0];
            shear.m[3][2] = -sample.shearPivotY * shear.m[1][2];
            const engine::Vector3 scale{1 + (sample.scale.x - 1) * weight, 1 + (sample.scale.y - 1) * weight, 1 + (sample.scale.z - 1) * weight};
            *bodyPose_ = engine::Math::Multiply(shear, engine::Math::MakeAffineMatrix(scale, sample.rotate * weight, sample.translate * weight));
        } else if (track.type == "mesh_vfx") {
            auto* effects = MeshEffectManager::GetInstance();
            if (now >= track.end) {
                if (state.effectScope) effects->StopEffectScope(state.effectScope);
                state.effectScope = 0;
                continue;
            }
            if (!state.effectScope) {
                state.effectScope = effects->CreateEffectScope();
                if (!effects->StartTimelineEffect(state.effectScope, track.preset)) {
                    state.allowed = false; // 存在しないプリセットを毎フレーム読み直さない。
                    continue;
                }
            }
            effects->EvaluateTimelineEffect(state.effectScope, localTime, track.end - track.begin, positionAt(key),
                {key.rotation[0] - (track.motion != "keyframed" ? std::atan2(state.worldForward.y,
                    std::sqrt(state.worldForward.x * state.worldForward.x + state.worldForward.z * state.worldForward.z)) : 0),
                 key.rotation[1] + std::atan2(state.worldForward.x, state.worldForward.z), key.rotation[2]},
                {key.scale[0], key.scale[1], key.scale[2]});
        } else if (track.type == "hitbox" && previous < track.end && now >= track.begin) {
            PhysicsQueryFilter filter;
            filter.mask = CollisionCategories::Get(context.playerOwned ? "enemy" : "player");
            filter.ignoredObject = context.owner;
            const float previousLocal = std::clamp(previous - track.begin, 0.0f, track.end - track.begin);
            auto oldKey = track.Evaluate(previousLocal);
            engine::Vector3 oldPosition = state.hasPreviousPosition ? state.previousPosition : positionAt(oldKey);
            if (!state.hasPreviousPosition && track.parentTrack >= 0) {
                const auto& parent = definition_.sequence.tracks[track.parentTrack];
                const auto& parentState = trackStates_[track.parentTrack];
                const float frameBegin = (std::max)(previous, parent.begin);
                const float frameEnd = (std::min)(now, parent.end);
                const float fraction = frameEnd > frameBegin
                    ? std::clamp(((std::max)(previous, track.begin) - frameBegin) / (frameEnd - frameBegin), 0.0f, 1.0f) : 1;
                oldPosition = engine::Math::Lerp(parentState.frameStartPosition, parentState.worldPosition, fraction)
                    + positionAt(oldKey) - parentState.worldPosition;
            }
            std::vector<float> sampleTimes;
            // 一度の更新で往復するキーを通過しても、最初と最後だけを結んで軌道を失わない。
            for (const auto& knot : track.keys)
                if (knot.time > previousLocal && knot.time < localTime) sampleTimes.push_back(knot.time);
            sampleTimes.push_back(localTime);
            for (float sampleTime : sampleTimes) {
                const auto sample = track.Evaluate(sampleTime);
                const engine::Vector3 position = positionAt(sample);
                // 保持キーの切り替えは瞬間移動なので、中間空間へ攻撃判定を作らない。
                const auto hits = oldKey.interpolation == 0
                    ? CollisionManager::GetInstance()->OverlapSphere(position, track.radius, filter)
                    : CollisionManager::GetInstance()->OverlapCapsule(oldPosition, position, track.radius, filter);
                for (const auto& hit : hits) {
                    if (!hit.object) continue;
                    // 隔離プレビューの攻撃から、本編のActorへダメージを配信しない。
                    if (context.owner && context.owner->IsEditorInternal() != hit.object->IsEditorInternal()) continue;
                    if (!state.victims.insert(hit.object->GetReplayId()).second) continue;
                    DamageEvent damage;
                    damage.attacker = context.owner;
                    damage.target = hit.object;
                    damage.damageAmount = definition_.damage * track.damageScale;
                    damage.knockbackVelocity = definition_.ResolveKnockback(forward, {});
                    damage.invincibilityDuration = definition_.ResolveInvincibilityDuration();
                    damageEvents.push_back(damage);
                }
                oldKey = sample;
                oldPosition = position;
            }
            state.previousPosition = positionAt(key);
            state.hasPreviousPosition = true;
        }
    }
    // コールバックで攻撃が中断されても、走査中のトラックを無効化しないよう最後に通知します。
    const auto generation = generation_;
    for (const auto& damage : damageEvents) {
        if (generation != generation_) break;
        const auto& objects = CollisionManager::GetInstance()->GetObjects();
        if (std::find(objects.begin(), objects.end(), damage.target) != objects.end()) EventManager::GetInstance()->Dispatch(damage);
    }
}

void AttackSequence::Execute(const AttackSequenceEvent& event, const AttackSequenceContext& context) {
    // 定義された操作種別を共有の発生位置・速度へ解決してから実演出を起動する。
    const auto part = ResolveProjectileComponent(definition_, event);
    const auto emission = ResolveAttackSequenceEmission(event, context, part.minSpeed, part.maxSpeed);
    const engine::Vector3 position = emission.position;
    if (event.operation == "gpu_vfx") {
        auto* particles = GPUParticleManager::GetInstance();
        if (particles->IsInitialized() && !event.preset.empty()) {
            if (event.directed) particles->EmitDirected(event.preset, position, emission.direction, event.particleSpeedScale);
            else particles->Emit(event.preset, position);
        }
        return;
    }
    const engine::Vector3 direction = emission.direction;
    const float speed = emission.speed;
    BulletVisualConfig visual;
    visual.collisionRadiusIsWorld = true;
    visual.debugLabel = event.name;
    visual.visualScale = part.visualScale;
    visual.color = {part.appearance.red, part.appearance.green, part.appearance.blue, part.appearance.alpha};
    visual.emissive = part.appearance.emissive;
    if (part.style == "fire") {
        visual.materialType = 11;
        const auto& appearance = part.appearance;
        visual.color = {appearance.red, appearance.green, appearance.blue, appearance.alpha};
        visual.emissive = appearance.emissive;
        visual.effectType = 1;
        visual.effectScale = appearance.effectScale;
        visual.effectSoftness = appearance.softness;
        visual.effectIntensity = appearance.intensity;
        visual.billboardScale = appearance.billboardScale;
        visual.trailInterval = appearance.trailInterval;
        visual.trailSpeedScale = appearance.trailSpeedScale;
        visual.trailReverseDirection = true;
        visual.impactDirected = false;
    }
    visual.trailPreset = part.trail;
    visual.trailSecondaryPreset = part.secondaryTrail;
    visual.impactPreset = part.impact;
    visual.impactSecondaryPreset = part.secondaryImpact;
    visual.trailInterval = part.appearance.trailInterval;
    visual.trailSpeedScale = part.appearance.trailSpeedScale;
    StatusEffectApplication status;
    if (definition_.statusEffectType == "burning") status.type = StatusEffectType::Burning;
    status.duration = definition_.statusDuration;
    status.tickInterval = definition_.statusTickInterval;
    status.tickDamage = definition_.statusTickDamage;
    status.vfxPreset = definition_.statusVfx;
    const auto attribute = CollisionCategories::Get(context.playerOwned ? "player_attack" : "enemy_attack");
    const auto mask = CollisionCategories::Get(context.playerOwned ? "enemy" : "player") | CollisionCategories::Get("ground");
    for (int i = 0; i < event.count; ++i) {
        const float angle = event.count == 1 ? 0.0f : (static_cast<float>(i) / (event.count - 1) - 0.5f) * event.spreadDegrees * 0.01745329252f;
        const engine::Vector3 spread{direction.x * std::cos(angle) + direction.z * std::sin(angle), direction.y,
                            direction.z * std::cos(angle) - direction.x * std::sin(angle)};
        BulletManager::GetInstance()->Fire(position, spread * speed + engine::Vector3{0, event.verticalSpeed, 0},
            part.collisionEnabled ? attribute : 0, part.collisionEnabled ? mask : 0,
            part.model, part.radius, part.lifetime, visual, part.damage, status,
            part.style == "fire" ? DamageType::Fire : DamageType::Physical);
    }
}
