#include "EnemyAttackPreviewWindow.h"

#include "BaseEnemy.h"
#include "BaseScene.h"
#include "BulletManager.h"
#include "CollisionConfig.h"
#include "DebrisEffectManager.h"
#include "EffectPreviewStage.h"
#include "EnemyBomber.h"
#include "EnemyFactory.h"
#include "EnemyGiantSlime.h"
#include "EnemyPrismSlime.h"
#include "EnemySlime.h"
#include "EnemyThunderSlime.h"
#include "EnemyWindSlime.h"
#include "GPUParticleManager.h"
#include "GameRule.h"
#include "EventManager.h"
#include "InputManager.h"
#include "MeshEffectManager.h"
#include "MeshRenderer.h"
#include "Object3d.h"
#include "ParticleSystem.h"
#include "Player.h"
#include "SceneManager.h"
#include "VFXSequencer.h"

#ifdef USE_IMGUI
#include "IconsFontAwesome5.h"
#include "imgui.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <iterator>
#include <unordered_set>

namespace {
constexpr const char* kPreviewNamePrefix = "__Editor_EnemyAttackPreview_";
constexpr const char* kPreviewEnemyName = "__Editor_EnemyAttackPreview_Enemy";
constexpr const char* kPreviewTargetName = "__Editor_EnemyAttackPreview_Target";
constexpr float kFixedPreviewStep = 1.0f / 60.0f;
constexpr float kPi = 3.1415926535f;

constexpr const char* kEnemyTypes[] = {
    "Slime",
    "FireSlime",
    "ThunderSlime",
    "GiantSlime",
    "PrismSlime",
    "Bomber",
    "WindSlime",
};

constexpr const char* kEnemyLabels[] = {
    "通常スライム（溜めジャンプ急降下）",
    "炎スライム",
    "雷スライム（近距離放電／連続落雷）",
    "巨大スライム（ジャンププレス）",
    "プリズムスライム（無属性／炎／雷／風）",
    "ボマースライム（爆弾投げ）",
    "風スライム（暴風ブレス／空中三連風弾）",
};

bool HasPreviewPrefix(const Object3d* object) {
    return object && object->GetName().rfind(kPreviewNamePrefix, 0) == 0;
}

#ifdef USE_IMGUI
bool DrawProfileString(const char* label, std::string& value) {
    std::array<char, 512> buffer{};
    const size_t copyLength = (std::min)(value.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), value.data(), copyLength);
    if (!ImGui::InputText(label, buffer.data(), buffer.size())) {
        return false;
    }
    value = buffer.data();
    return true;
}

ImU32 GetPhaseColor(const std::string& phase, int alpha = 230) {
    if (phase == "溜め") return IM_COL32(255, 188, 64, alpha);
    if (phase == "近距離放電・溜め" || phase == "連続落雷・溜め") return IM_COL32(247, 210, 73, alpha);
    if (phase == "連続落雷・落雷中") return IM_COL32(103, 218, 255, alpha);
    if (phase == "三連風弾・風で上昇" || phase == "三連風弾・頭上に形成") return IM_COL32(128, 245, 211, alpha);
    if (phase == "三連風弾・順番に投射") return IM_COL32(105, 218, 255, alpha);
    if (phase == "三連風弾・着地") return IM_COL32(164, 226, 207, alpha);
    if (phase == "放電後・反発") return IM_COL32(167, 126, 255, alpha);
    if (phase == "再使用待ち") return IM_COL32(84, 126, 158, alpha);
    if (phase == "上昇") return IM_COL32(112, 205, 255, alpha);
    if (phase == "突進") return IM_COL32(255, 92, 116, alpha);
    if (phase == "着地反発") return IM_COL32(171, 116, 255, alpha);
    if (phase == "待機・移動") return IM_COL32(104, 190, 153, alpha);
    if (phase == "空中拘束・落下") return IM_COL32(82, 172, 235, alpha);
    if (phase == "着地・受け止め") return IM_COL32(242, 185, 69, alpha);
    if (phase == "踏ん張り・抵抗") return IM_COL32(83, 188, 139, alpha);
    if (phase == "引きずられ") return IM_COL32(241, 132, 66, alpha);
    if (phase == "限界・分裂寸前") return IM_COL32(238, 76, 118, alpha);
    if (phase == "分裂完了" || phase == "分裂後") return IM_COL32(187, 105, 244, alpha);
    return IM_COL32(128, 164, 196, alpha);
}

void DrawTimelineBlock(
    ImDrawList* drawList,
    const ImVec2& origin,
    float labelWidth,
    float pixelsPerSecond,
    float laneY,
    float laneHeight,
    float beginTime,
    float endTime,
    ImU32 color,
    const char* label) {
    if (!drawList || endTime <= beginTime) {
        return;
    }

    const float x0 = origin.x + labelWidth + beginTime * pixelsPerSecond;
    const float x1 = origin.x + labelWidth + endTime * pixelsPerSecond;
    drawList->AddRectFilled(
        ImVec2(x0 + 1.0f, laneY + 3.0f),
        ImVec2(x1 - 1.0f, laneY + laneHeight - 3.0f),
        color,
        3.0f);
    if (label && x1 - x0 >= 48.0f) {
        drawList->PushClipRect(
            ImVec2(x0 + 3.0f, laneY),
            ImVec2(x1 - 3.0f, laneY + laneHeight),
            true);
        drawList->AddText(
            ImVec2(x0 + 6.0f, laneY + 6.0f),
            IM_COL32(245, 248, 252, 255),
            label);
        drawList->PopClipRect();
    }
}
#endif
}

void EnemyAttackPreviewWindow::Initialize(SceneManager* sceneManager) {
    sceneManager_ = sceneManager;
    LoadSelectedProfile();
    ApplyRecommendedSettings();
}

void EnemyAttackPreviewWindow::Finalize() {
    ClearPreviewTransientState();
    RemovePreviewObjects();
    sceneManager_ = nullptr;
    previewScene_ = nullptr;
}

void EnemyAttackPreviewWindow::Update(float deltaTime) {
    BaseScene* currentScene = sceneManager_ ? sceneManager_->GetCurrentScene() : nullptr;
    if (previewScene_ != currentScene) {
        // Scene切り替え後は旧Sceneのポインタへ触れず、新しいScene側で作り直す。
        previewScene_ = currentScene;
        previewEnemy_ = nullptr;
        previewTarget_ = nullptr;
        previewPlayer_ = nullptr;
        spawnedPreviewObjects_.clear();
        previewSequenceCompleted_ = false;
        timelineSamples_.clear();
        elapsedTime_ = 0.0f;
        simulationAccumulator_ = 0.0f;
        if (enabled_ && IsCurrentSceneReady() && !sceneManager_->IsPlaying()) {
            CreatePreviewObjects();
        }
    }

    if (!enabled_ || !IsCurrentSceneReady()) {
        return;
    }

    if (sceneManager_->IsPlaying()) {
        enabled_ = false;
        ClearPreviewTransientState();
        RemovePreviewObjects();
        return;
    }

    MarkPreviewFamilyEditorInternal();
    SyncTargetTransform();

    if (!previewEnemy_ && !previewSequenceCompleted_) {
        CreatePreviewObjects();
    }
    if (!playing_ || (!previewEnemy_ && !previewSequenceCompleted_)) {
        return;
    }

    const float safeDelta = deltaTime > 0.0001f ? deltaTime : kFixedPreviewStep;
    simulationAccumulator_ += (std::min)(safeDelta, 0.25f) * std::clamp(playbackSpeed_, 0.05f, 2.0f);
    while (simulationAccumulator_ >= kFixedPreviewStep && playing_ && (previewEnemy_ || previewSequenceCompleted_)) {
        simulationAccumulator_ -= kFixedPreviewStep;
        StepSimulation(kFixedPreviewStep);

        if (loopDuration_ > 0.05f && elapsedTime_ >= loopDuration_) {
            if (loop_) {
                RestartPreview();
            } else {
                elapsedTime_ = loopDuration_;
                simulationAccumulator_ = 0.0f;
                playing_ = false;
            }
        }
    }
}

void EnemyAttackPreviewWindow::CreatePreviewObjects(bool requestCameraRecenter) {
    if (!IsCurrentSceneReady() || sceneManager_->IsPlaying()) {
        return;
    }

    EffectPreviewStage* previewStage = EffectPreviewStage::GetInstance();
    previewStage->EnableForToolPreview();
    if (requestCameraRecenter) {
        previewStage->RequestCameraRecenter();
    }

    BaseScene* scene = sceneManager_->GetCurrentScene();
    previewScene_ = scene;
    const Vector3 ground = previewStage->GetGroundPosition();

    previewPlayer_ = nullptr;
    if (usePlayerTarget_ && !IsGiantHookSplitPreview()) {
        auto player = std::make_unique<Player>();
        player->Initialize(
            scene->GetObject3dCommon(),
            InputManager::GetInstance(),
            scene->GetParticleSystem(),
            scene->GetSpriteCommon());
        player->SetName(kPreviewTargetName);
        player->SetClassName("Player");
        player->SetEditorInternal(true);
        player->SetIsLocked(true);
        player->SetColliderType(ColliderType::kNone);
        player->SetCollisionAttribute(0);
        player->SetCollisionMask(0);
        player->SetMaterialType(25);
        player->SetEnableLighting(true);
        player->SetIsControlActive(false);
        player->SetMoveYaw(kPi);
        player->SetVelocity({ 0.0f, 0.0f, 0.0f });
        player->SetGravity(0.0f);
        player->SetMaxFallSpeed(0.0f);
        player->SetGrounded(true);
        if (player->param_.has_value()) {
            player->param_->maxHp = 20.0f;
            player->param_->hp = 20.0f;
            player->param_->gravity = 0.0f;
            player->param_->maxFallSpeed = 0.0f;
        }
        previewPlayer_ = player.get();
        previewTarget_ = player.get();
        scene->AddObject(std::move(player));
    }
    else {
        auto target = std::make_unique<Object3d>();
        target->Initialize(scene->GetObject3dCommon());
        target->SetName(kPreviewTargetName);
        target->SetClassName("EditorOnly");
        target->SetEditorInternal(true);
        target->SetIsLocked(true);
        target->SetModel("Primitives/cylinder");
        target->SetColliderType(ColliderType::kNone);
        target->SetCollisionAttribute(0);
        target->SetCollisionMask(0);
        target->SetEnableLighting(false);
        target->SetColor({ 0.20f, 0.82f, 1.0f, 0.72f });
        target->SetEmissive(1.35f);
        target->SetScale({ 0.34f, 0.72f, 0.34f });
        previewTarget_ = target.get();
        scene->AddObject(std::move(target));
    }
    SyncTargetTransform();

    std::unique_ptr<BaseEnemy> enemy = EnemyFactory::GetInstance()->CreateEnemy(GetSelectedEnemyType(), scene->GetObject3dCommon());
    if (!enemy) {
        RemovePreviewObjects();
        return;
    }

    ConfigureEnemy(enemy.get());
    previewBaseScale_ = enemy->GetScale();
    previewHitTriggered_ = false;
    previewEnemy_ = enemy.get();
    scene->AddObject(std::move(enemy));

    elapsedTime_ = 0.0f;
    simulationAccumulator_ = 0.0f;
    previewSequenceCompleted_ = false;
    previewHitTriggered_ = false;
    playing_ = true;
    SyncTargetTransform();
    timelineSamples_.clear();
    RecordTimelineSample();
}

void EnemyAttackPreviewWindow::RemovePreviewObjects() {
    BaseScene* currentScene = sceneManager_ ? sceneManager_->GetCurrentScene() : nullptr;
    if (currentScene && currentScene == previewScene_) {
        std::unordered_set<Object3d*> removalTargets;
        if (previewEnemy_) removalTargets.insert(previewEnemy_);
        if (previewTarget_) removalTargets.insert(previewTarget_);
        for (Object3d* object : spawnedPreviewObjects_) {
            if (object) removalTargets.insert(object);
        }
        for (const auto& object : currentScene->GetObjects()) {
            if (HasPreviewPrefix(object.get())) {
                removalTargets.insert(object.get());
            }
        }
        for (Object3d* object : removalTargets) {
            currentScene->RequestRemoveObject(object);
        }
    }

    previewEnemy_ = nullptr;
    previewTarget_ = nullptr;
    previewPlayer_ = nullptr;
    spawnedPreviewObjects_.clear();
    previewSequenceCompleted_ = false;
    elapsedTime_ = 0.0f;
    simulationAccumulator_ = 0.0f;
    timelineSamples_.clear();
    SetEffectSimulationScale(1.0f);
}

void EnemyAttackPreviewWindow::RestartPreview() {
    ClearPreviewTransientState();
    RemovePreviewObjects();
    CreatePreviewObjects(false);
}

void EnemyAttackPreviewWindow::SeekPreview(float targetTime) {
    if (!enabled_ || !IsCurrentSceneReady()) {
        return;
    }

    const float safeDuration = (std::max)(loopDuration_, kFixedPreviewStep);
    const float clampedTime = std::clamp(targetTime, 0.0f, safeDuration);
    const int targetFrame = static_cast<int>(std::round(clampedTime / kFixedPreviewStep));

    ClearPreviewTransientState();
    RemovePreviewObjects();
    CreatePreviewObjects(false);
    playing_ = false;

    for (int frame = 0; frame < targetFrame && (previewEnemy_ || previewSequenceCompleted_); ++frame) {
        StepSimulation(kFixedPreviewStep);
    }

    elapsedTime_ = (std::min)(static_cast<float>(targetFrame) * kFixedPreviewStep, safeDuration);
    simulationAccumulator_ = 0.0f;
    // GPUパーティクルエディターと同様に、先頭から固定刻みで再構築した状態をそのまま保持します。
    ApplyEffectPlaybackState();
}

void EnemyAttackPreviewWindow::ClearPreviewTransientState() {
    SetEffectSimulationScale(1.0f);
    MeshEffectManager::GetInstance()->ClearActiveEffects();
    DebrisEffectManager::GetInstance()->Clear();
    BulletManager::GetInstance()->Clear();
    VFXSequencer::ClearOneShots();

    GPUParticleManager* gpuParticles = GPUParticleManager::GetInstance();
    if (gpuParticles->IsInitialized()) {
        gpuParticles->ResetSimulation();
        gpuParticles->SetTimeScale(1.0f);
    }

    if (previewScene_) {
        if (ParticleSystem* particles = previewScene_->GetParticleSystem()) {
            particles->ResetSimulation();
            particles->SetSimulationTimeScale(1.0f);
        }
    }
}

void EnemyAttackPreviewWindow::SetEffectSimulationScale(float timeScale) {
    const float safeScale = (std::max)(0.0f, timeScale);
    MeshEffectManager::GetInstance()->SetTimeScale(safeScale);
    DebrisEffectManager::GetInstance()->SetTimeScale(safeScale);

    GPUParticleManager* gpuParticles = GPUParticleManager::GetInstance();
    if (gpuParticles->IsInitialized()) {
        gpuParticles->SetTimeScale(safeScale);
    }

    if (previewScene_) {
        if (ParticleSystem* particles = previewScene_->GetParticleSystem()) {
            particles->SetSimulationTimeScale(safeScale);
        }
    }
}

void EnemyAttackPreviewWindow::ApplyEffectPlaybackState() {
    if (!enabled_ || !sceneManager_ || sceneManager_->IsPlaying()) {
        return;
    }
    // 再生速度は固定刻みの実行回数へ既に反映済みなので、演出側は再生中1、停止中0で統一します。
    SetEffectSimulationScale(playing_ ? 1.0f : 0.0f);
}

void EnemyAttackPreviewWindow::StepSimulation(float deltaTime) {
    if (!previewTarget_ || (!previewEnemy_ && !previewSequenceCompleted_)) {
        return;
    }

    SetEffectSimulationScale(1.0f);

    const Vector3 ground = EffectPreviewStage::GetInstance()->GetGroundPosition();
    BaseEnemy* primaryEnemy = previewEnemy_;
    if (primaryEnemy) {
        if (previewHitReaction_ && !previewHitTriggered_ && elapsedTime_ >= previewHitTime_) {
            Vector3 knockback = primaryEnemy->GetWorldPosition() - previewTarget_->GetWorldPosition();
            knockback.y = 0.0f;
            const float planarLength = std::sqrt(knockback.x * knockback.x + knockback.z * knockback.z);
            if (planarLength > 0.001f) {
                knockback.x = knockback.x / planarLength * 11.0f;
                knockback.z = knockback.z / planarLength * 11.0f;
            } else {
                knockback = { 0.0f, 0.0f, -11.0f };
            }
            knockback.y = 3.2f;
            primaryEnemy->PlayDamageReaction(previewTarget_, knockback, 10.0f);
            previewHitTriggered_ = true;
        }
        MaintainGrounding(primaryEnemy, ground.y);
        primaryEnemy->Update(deltaTime);
        MaintainGrounding(primaryEnemy, ground.y);

        if (IsGiantHookSplitPreview()) {
            if (auto* giantSlime = dynamic_cast<EnemyGiantSlime*>(primaryEnemy)) {
                ParticleSystem* particles = previewScene_ ? previewScene_->GetParticleSystem() : nullptr;
                if (giantSlime->UpdateHookSplitPull(deltaTime, previewTarget_->GetWorldPosition(), particles)) {
                    RecordTimelineSample();
                    previewSequenceCompleted_ = true;
                    for (const auto& object : previewScene_->GetObjects()) {
                        if (!object || object.get() == primaryEnemy || object.get() == previewTarget_ || !HasPreviewPrefix(object.get())) {
                            continue;
                        }
                        if (std::find(spawnedPreviewObjects_.begin(), spawnedPreviewObjects_.end(), object.get()) == spawnedPreviewObjects_.end()) {
                            spawnedPreviewObjects_.push_back(object.get());
                        }
                    }
                    previewEnemy_ = nullptr;
                }
            }
        }
    }

    for (Object3d* object : spawnedPreviewObjects_) {
        BaseEnemy* spawnedEnemy = dynamic_cast<BaseEnemy*>(object);
        if (spawnedEnemy) {
            MaintainGrounding(spawnedEnemy, ground.y);
            spawnedEnemy->Update(deltaTime);
            MaintainGrounding(spawnedEnemy, ground.y);
        }
    }

    BulletManager::GetInstance()->Update(deltaTime);
    ResolvePreviewProjectileHits();

    if (previewScene_) {
        if (GameRule* gameRule = previewScene_->GetGameRule()) {
            gameRule->Update(deltaTime);
        }
    }

    if (previewPlayer_) {
        previewPlayer_->SetGravity(0.0f);
        previewPlayer_->SetMaxFallSpeed(0.0f);
        previewPlayer_->SetIsControlActive(false);
        previewPlayer_->Update(deltaTime);
        if (previewPlayer_->GetTranslate().y < ground.y) {
            Vector3 playerPosition = previewPlayer_->GetTranslate();
            playerPosition.y = ground.y;
            previewPlayer_->SetTranslate(playerPosition);
        }
        previewPlayer_->SetGrounded(true);
    }

    if (previewScene_) {
        if (ParticleSystem* particles = previewScene_->GetParticleSystem()) {
            particles->Update(deltaTime);
        }
    }

    // GPUパーティクルエディターと同じ固定刻みで、1画面フレーム内の複数ステップも確実に進めます。
    VFXSequencer::UpdateOneShots(deltaTime);
    MeshEffectManager::GetInstance()->UpdateEditorPreviewStep(deltaTime);
    DebrisEffectManager::GetInstance()->Update(deltaTime);
    GPUParticleManager* gpuParticles = GPUParticleManager::GetInstance();
    if (gpuParticles->IsInitialized()) {
        gpuParticles->SetTimeScale(1.0f);
        gpuParticles->UpdateEditorPreviewStep(deltaTime);
    }

    elapsedTime_ += deltaTime;
    MarkPreviewFamilyEditorInternal();
    RecordTimelineSample();
}

void EnemyAttackPreviewWindow::ResolvePreviewProjectileHits() {
    if (!previewPlayer_ || previewPlayer_->isDead) {
        return;
    }

    const Vector3 targetPosition = previewPlayer_->GetTranslate();
    const Vector3 targetScale = previewPlayer_->GetScale();
    const float targetRadius = (std::max)({
        0.75f,
        std::abs(targetScale.x) * 0.50f,
        std::abs(targetScale.y) * 0.42f,
        std::abs(targetScale.z) * 0.50f
    });

    for (const auto& bulletPtr : BulletManager::GetInstance()->GetBullets()) {
        Bullet* bullet = bulletPtr.get();
        if (!bullet || bullet->IsDead() || (bullet->GetCollisionAttribute() & kEnemyAttack) == 0) {
            continue;
        }

        const Vector3 difference = targetPosition - bullet->GetTranslate();
        const float hitRadius = targetRadius + (std::max)(0.15f, bullet->GetCollisionRadius());
        if (Math::Length(difference) > hitRadius) {
            continue;
        }

        Vector3 hitDirection = bullet->GetVelocity();
        hitDirection.y = 0.0f;
        if (Math::Length(hitDirection) <= 0.001f) {
            hitDirection = difference;
            hitDirection.y = 0.0f;
        }
        if (Math::Length(hitDirection) <= 0.001f) {
            hitDirection = { 0.0f, 0.0f, 1.0f };
        }
        hitDirection = Math::Normalize(hitDirection);

        DamageEvent event;
        event.target = previewPlayer_;
        event.attacker = bullet;
        event.damageAmount = bullet->GetDamage();
        event.damageType = bullet->GetDamageType();
        event.statusEffect = bullet->GetStatusEffect();
        event.knockbackVelocity = { hitDirection.x * 9.5f, 4.2f, hitDirection.z * 9.5f };
        EventManager::GetInstance()->Dispatch(event);
        bullet->Expire(true);
    }
}

void EnemyAttackPreviewWindow::TriggerPreviewPlayerDamage() {
    if (!previewPlayer_ || !previewEnemy_) {
        return;
    }

    Vector3 direction = previewPlayer_->GetTranslate() - previewEnemy_->GetTranslate();
    direction.y = 0.0f;
    if (Math::Length(direction) <= 0.001f) {
        direction = { 0.0f, 0.0f, 1.0f };
    }
    direction = Math::Normalize(direction);

    const EnemyAttackDefinition* attack = GetSelectedAttackDefinition();
    DamageEvent event;
    event.target = previewPlayer_;
    event.attacker = previewEnemy_;
    event.damageAmount = attack ? attack->damage : 1.0f;
    event.damageType = GetPreviewDamageType();
    event.statusEffect = GetPreviewStatusEffect();
    event.knockbackVelocity = { direction.x * 9.5f, 4.2f, direction.z * 9.5f };
    EventManager::GetInstance()->Dispatch(event);
}

DamageType EnemyAttackPreviewWindow::GetPreviewDamageType() const {
    switch (enemyTypeIndex_) {
    case 1:
        return DamageType::Fire;
    case 2:
        return DamageType::Electric;
    case 4:
        return DamageType::Explosion;
    default:
        return DamageType::Physical;
    }
}

StatusEffectApplication EnemyAttackPreviewWindow::GetPreviewStatusEffect() const {
    StatusEffectApplication status;
    const EnemyAttackDefinition* attack = GetSelectedAttackDefinition();
    if (!attack || attack->statusEffectType.empty()) {
        return status;
    }

    if (attack->statusEffectType == "burning") {
        status.type = StatusEffectType::Burning;
    }
    status.duration = attack->statusDuration;
    status.tickInterval = attack->statusTickInterval;
    status.tickDamage = attack->statusTickDamage;
    status.vfxPreset = attack->statusVfx;
    return status;
}

void EnemyAttackPreviewWindow::RecordTimelineSample() {
    if (!previewEnemy_) {
        return;
    }

    const Vector3 velocity = previewEnemy_->GetVelocity();
    const Vector3 position = previewEnemy_->GetTranslate();
    const float groundY = EffectPreviewStage::GetInstance()->GetGroundPosition().y;

    TimelineSample sample;
    sample.time = elapsedTime_;
    sample.speed = std::sqrt(
        velocity.x * velocity.x +
        velocity.y * velocity.y +
        velocity.z * velocity.z);
    sample.height = (std::max)(0.0f, position.y - groundY);
    Vector3 scaleMultiplier = {
        std::abs(previewBaseScale_.x) > 0.0001f ? std::abs(previewEnemy_->GetScale().x / previewBaseScale_.x) : 1.0f,
        std::abs(previewBaseScale_.y) > 0.0001f ? std::abs(previewEnemy_->GetScale().y / previewBaseScale_.y) : 1.0f,
        std::abs(previewBaseScale_.z) > 0.0001f ? std::abs(previewEnemy_->GetScale().z / previewBaseScale_.z) : 1.0f
    };
    if (const MeshRenderer* renderer = previewEnemy_->GetMeshRenderer()) {
        const Vector3& visualScale = renderer->GetVisualScale();
        scaleMultiplier.x *= std::abs(visualScale.x);
        scaleMultiplier.y *= std::abs(visualScale.y);
        scaleMultiplier.z *= std::abs(visualScale.z);
    }
    sample.deformation = (std::max)({
        std::abs(scaleMultiplier.x - 1.0f),
        std::abs(scaleMultiplier.y - 1.0f),
        std::abs(scaleMultiplier.z - 1.0f)
    });
    sample.hitReaction = previewEnemy_->GetDamageReactionWeight();
    sample.gpuParticleSystems = static_cast<float>(GPUParticleManager::GetInstance()->GetActiveSystemCount());
    if (previewScene_) {
        if (const ParticleSystem* particles = previewScene_->GetParticleSystem()) {
            sample.cpuParticles = static_cast<float>(particles->GetActiveParticleCount());
        }
    }
    sample.meshEffects = static_cast<float>(MeshEffectManager::GetInstance()->GetActiveEffects().size());
    sample.debrisPieces = static_cast<float>(DebrisEffectManager::GetInstance()->GetActivePieceCount());
    sample.phase = GetCurrentPhaseName();

    if (!timelineSamples_.empty() && std::abs(timelineSamples_.back().time - sample.time) < kFixedPreviewStep * 0.25f) {
        timelineSamples_.back() = std::move(sample);
    } else {
        timelineSamples_.push_back(std::move(sample));
    }
}

const char* EnemyAttackPreviewWindow::GetCurrentPhaseName() const {
    if (previewSequenceCompleted_ && !previewEnemy_) {
        return "分裂後";
    }
    if (const auto* giantSlime = dynamic_cast<const EnemyGiantSlime*>(previewEnemy_)) {
        if (giantSlime->IsHookSplitPulled() || giantSlime->HasSplit()) {
            return giantSlime->GetDebugHookSplitPhaseName();
        }
    }
    if (const auto* slime = dynamic_cast<const EnemySlime*>(previewEnemy_)) {
        return slime->GetDebugMoveStateName();
    }
    if (const auto* thunderSlime = dynamic_cast<const EnemyThunderSlime*>(previewEnemy_)) {
        return thunderSlime->GetDebugAttackPhaseName();
    }
    if (const auto* windSlime = dynamic_cast<const EnemyWindSlime*>(previewEnemy_)) {
        return windSlime->GetDebugAttackPhaseName();
    }
    if (const auto* prismSlime = dynamic_cast<const EnemyPrismSlime*>(previewEnemy_)) {
        return prismSlime->GetDebugAttackPhaseName();
    }
    return previewEnemy_ ? "攻撃シミュレーション" : "未生成";
}

bool EnemyAttackPreviewWindow::IsGiantHookSplitPreview() const {
    return enemyTypeIndex_ == 3 && giantPreviewModeIndex_ != 0;
}

void EnemyAttackPreviewWindow::MaintainGrounding(BaseEnemy* enemy, float groundY) {
    if (!enemy) {
        return;
    }

    Vector3 position = enemy->GetTranslate();
    Vector3 velocity = enemy->GetVelocity();
    if (position.y <= groundY && velocity.y <= 0.0f) {
        position.y = groundY;
        velocity.y = 0.0f;
        enemy->SetTranslate(position);
        enemy->SetVelocity(velocity);
        enemy->SetGrounded(true);
    } else if (position.y > groundY + 0.001f) {
        enemy->SetGrounded(false);
    }
}

void EnemyAttackPreviewWindow::SyncTargetTransform() {
    if (!previewTarget_) {
        return;
    }

    const Vector3 ground = EffectPreviewStage::GetInstance()->GetGroundPosition();
    const float targetY = previewPlayer_ ? ground.y : ground.y + 0.72f;
    previewTarget_->SetTranslate({ ground.x, targetY, ground.z + targetDistance_ });
    previewTarget_->SetIsVisible(showTarget_);
    if (previewPlayer_) {
        previewPlayer_->SetGravity(0.0f);
        previewPlayer_->SetMaxFallSpeed(0.0f);
        previewPlayer_->SetIsControlActive(false);
        previewPlayer_->SetGrounded(true);
    }
    previewTarget_->UpdateLocalMatrix();
    previewTarget_->UpdateWorldMatrix();
}

void EnemyAttackPreviewWindow::MarkPreviewFamilyEditorInternal() {
    if (!previewScene_) {
        return;
    }

    for (const auto& object : previewScene_->GetObjects()) {
        if (!HasPreviewPrefix(object.get())) {
            continue;
        }
        object->SetEditorInternal(true);
        object->SetIsLocked(true);
        object->SetCollisionAttribute(0);
        object->SetCollisionMask(0);
    }
}

void EnemyAttackPreviewWindow::ConfigureEnemy(BaseEnemy* enemy) {
    if (!enemy) {
        return;
    }

    const Vector3 ground = EffectPreviewStage::GetInstance()->GetGroundPosition();
    enemy->SetName(std::string(kPreviewEnemyName) + "_" + GetSelectedEnemyType());
    enemy->SetClassName("EditorOnly");
    enemy->SetEditorInternal(true);
    enemy->SetIsLocked(true);
    enemy->SetCollisionAttribute(0);
    enemy->SetCollisionMask(0);
    enemy->SetTranslate(ground);
    enemy->SetVelocity({ 0.0f, 0.0f, 0.0f });
    enemy->SetGrounded(true);
    enemy->SetDetectionRange(30.0f);
    enemy->SetTarget(previewTarget_);

    if (auto* thunderSlime = dynamic_cast<EnemyThunderSlime*>(enemy)) {
        const EnemyAttackDefinition* selectedAttack = GetSelectedAttackDefinition();
        thunderSlime->SetDebugPreviewAttackId(selectedAttack ? selectedAttack->id : std::string{});
    }
    if (auto* windSlime = dynamic_cast<EnemyWindSlime*>(enemy)) {
        const EnemyAttackDefinition* selectedAttack = GetSelectedAttackDefinition();
        windSlime->SetDebugPreviewAttackId(selectedAttack ? selectedAttack->id : std::string{});
    }
    if (auto* prismSlime = dynamic_cast<EnemyPrismSlime*>(enemy)) {
        const EnemyAttackDefinition* selectedAttack = GetSelectedAttackDefinition();
        prismSlime->SetDebugPreviewAttackId(selectedAttack ? selectedAttack->id : std::string{});
        prismSlime->SetSpawnCallback([this](std::unique_ptr<BaseEnemy> spawnedEnemy) {
            if (!spawnedEnemy || !sceneManager_ || sceneManager_->GetCurrentScene() != previewScene_) {
                return;
            }

            spawnedEnemy->SetName(std::string(kPreviewNamePrefix) + "SummonedSlime_" +
                std::to_string(spawnedPreviewObjects_.size()));
            spawnedEnemy->SetClassName("EditorOnly");
            spawnedEnemy->SetEditorInternal(true);
            spawnedEnemy->SetIsLocked(true);
            spawnedEnemy->SetCollisionAttribute(0);
            spawnedEnemy->SetCollisionMask(0);
            spawnedEnemy->SetTarget(previewTarget_);

            if (auto* summonedBomber = dynamic_cast<EnemyBomber*>(spawnedEnemy.get())) {
                summonedBomber->SetSpawnCallback([this](std::unique_ptr<BaseEnemy> spawnedBomb) {
                    if (!spawnedBomb || !sceneManager_ || sceneManager_->GetCurrentScene() != previewScene_) {
                        return;
                    }
                    spawnedBomb->SetName(std::string(kPreviewNamePrefix) + "SummonedBomb_" +
                        std::to_string(spawnedPreviewObjects_.size()));
                    spawnedBomb->SetClassName("EditorOnly");
                    spawnedBomb->SetEditorInternal(true);
                    spawnedBomb->SetIsLocked(true);
                    spawnedBomb->SetCollisionAttribute(0);
                    spawnedBomb->SetCollisionMask(0);
                    spawnedBomb->SetTarget(previewTarget_);
                    Object3d* rawBomb = spawnedBomb.get();
                    spawnedPreviewObjects_.push_back(rawBomb);
                    previewScene_->AddObject(std::move(spawnedBomb));
                });
            }

            Object3d* rawObject = spawnedEnemy.get();
            spawnedPreviewObjects_.push_back(rawObject);
            previewScene_->AddObject(std::move(spawnedEnemy));
        });
    }

    if (auto* giantSlime = dynamic_cast<EnemyGiantSlime*>(enemy); giantSlime && IsGiantHookSplitPreview()) {
        if (giantPreviewModeIndex_ == 2) {
            enemy->SetTranslate({ ground.x, ground.y + 5.5f, ground.z });
            enemy->SetVelocity({ 0.0f, -1.5f, 0.0f });
            enemy->SetGrounded(false);
        }
        giantSlime->BeginHookSplitPull(previewTarget_->GetWorldPosition());
    }

    if (auto* bomber = dynamic_cast<EnemyBomber*>(enemy)) {
        bomber->SetSpawnCallback([this](std::unique_ptr<BaseEnemy> spawnedEnemy) {
            if (!spawnedEnemy || !sceneManager_ || sceneManager_->GetCurrentScene() != previewScene_) {
                return;
            }
            spawnedEnemy->SetName(std::string(kPreviewNamePrefix) + "SpawnedBomb_" + std::to_string(spawnedPreviewObjects_.size()));
            spawnedEnemy->SetClassName("EditorOnly");
            spawnedEnemy->SetEditorInternal(true);
            spawnedEnemy->SetIsLocked(true);
            spawnedEnemy->SetCollisionAttribute(0);
            spawnedEnemy->SetCollisionMask(0);
            spawnedEnemy->SetTarget(previewTarget_);
            Object3d* rawObject = spawnedEnemy.get();
            spawnedPreviewObjects_.push_back(rawObject);
            previewScene_->AddObject(std::move(spawnedEnemy));
        });
    }
}

void EnemyAttackPreviewWindow::ApplyRecommendedSettings() {
    targetDistance_ = GetRecommendedTargetDistance();
    loopDuration_ = GetRecommendedLoopDuration();
}

void EnemyAttackPreviewWindow::LoadSelectedProfile() {
    profileAttackIndex_ = 0;
    const std::string enemyType = GetSelectedEnemyType();
    EnemyAttackProfile::InvalidateCache(enemyType);
    const bool loaded = editableProfile_.LoadForEnemy(enemyType, &profileStatus_);
    if (editableProfile_.attacks.empty()) {
        profileStatus_ = enemyType + " は攻撃プロファイル編集の対象外です。";
    } else if (loaded) {
        profileStatus_ = "読み込みました: " + EnemyAttackProfile::GetDefaultPath(enemyType);
    }
    profileDirty_ = false;
}

void EnemyAttackPreviewWindow::SaveSelectedProfile() {
    if (editableProfile_.attacks.empty()) {
        return;
    }

    editableProfile_.Sanitize();
    const std::string path = EnemyAttackProfile::GetDefaultPath(editableProfile_.enemyType);
    if (editableProfile_.SaveToFile(path, &profileStatus_)) {
        EnemyAttackProfile::InvalidateCache(editableProfile_.enemyType);
        profileStatus_ = "保存しました: " + path;
        profileDirty_ = false;
        ApplyRecommendedSettings();
        if (enabled_) {
            RestartPreview();
        }
    }
}

void EnemyAttackPreviewWindow::ResetSelectedProfile() {
    editableProfile_ = EnemyAttackProfile::CreateDefault(GetSelectedEnemyType());
    profileAttackIndex_ = 0;
    profileDirty_ = !editableProfile_.attacks.empty();
    profileStatus_ = "既定値へ戻しました。保存するまでJSONには反映されません。";
    ApplyRecommendedSettings();
}

const EnemyAttackDefinition* EnemyAttackPreviewWindow::GetSelectedAttackDefinition() const {
    if (editableProfile_.attacks.empty()) {
        return nullptr;
    }
    const int clampedIndex = std::clamp(profileAttackIndex_, 0, static_cast<int>(editableProfile_.attacks.size()) - 1);
    return &editableProfile_.attacks[clampedIndex];
}

EnemyAttackDefinition* EnemyAttackPreviewWindow::GetSelectedAttackDefinition() {
    return const_cast<EnemyAttackDefinition*>(static_cast<const EnemyAttackPreviewWindow*>(this)->GetSelectedAttackDefinition());
}

float EnemyAttackPreviewWindow::GetRecommendedTargetDistance() const {
    if (IsGiantHookSplitPreview()) {
        return 8.0f;
    }
    if (const EnemyAttackDefinition* attack = GetSelectedAttackDefinition()) {
        return attack->recommendedTargetDistance;
    }
    switch (enemyTypeIndex_) {
    case 1:
        return profileAttackIndex_ == 0 ? 3.2f : 8.0f;
    case 2:
        return 3.2f;
    case 3:
        return 9.0f;
    case 4:
        return 12.0f;
    case 0:
    default:
        return 6.0f;
    }
}

float EnemyAttackPreviewWindow::GetRecommendedLoopDuration() const {
    if (IsGiantHookSplitPreview()) {
        return giantPreviewModeIndex_ == 2 ? 4.2f : 3.5f;
    }
    if (const EnemyAttackDefinition* attack = GetSelectedAttackDefinition()) {
        return attack->previewDuration;
    }
    switch (enemyTypeIndex_) {
    case 1:
        return profileAttackIndex_ == 0 ? 3.0f : 3.35f;
    case 2:
        return 3.2f;
    case 3:
        return 5.0f;
    case 4:
        return 4.2f;
    case 0:
    default:
        return 4.6f;
    }
}

const char* EnemyAttackPreviewWindow::GetSelectedEnemyType() const {
    const int index = std::clamp(enemyTypeIndex_, 0, static_cast<int>(std::size(kEnemyTypes)) - 1);
    return kEnemyTypes[index];
}

bool EnemyAttackPreviewWindow::IsCurrentSceneReady() const {
    return sceneManager_ && sceneManager_->GetCurrentScene() && sceneManager_->GetCurrentScene()->GetObject3dCommon();
}

void EnemyAttackPreviewWindow::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text(ICON_FA_PAW " 敵攻撃プレビュー");
    ImGui::Separator();
    ImGui::TextWrapped("ゲーム本体と同じ敵AIを、隔離ステージ上の実プレイヤーまたは無害なダミーへ向けて再生します。");
    ImGui::TextDisabled("実プレイヤー時はHP、属性別被弾、状態異常、火球の命中まで同じDamageEventで確認できます。");

    if (!IsCurrentSceneReady()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.28f, 1.0f), "Object3dを配置できるSceneが必要です。");
        return;
    }
    if (sceneManager_->IsPlaying()) {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.20f, 1.0f), "実行中はプレビューを停止します。編集モードへ戻してください。");
        return;
    }

    bool nextEnabled = enabled_;
    if (ImGui::Checkbox("プレビューを有効化", &nextEnabled)) {
        enabled_ = nextEnabled;
        if (enabled_) {
            CreatePreviewObjects();
        } else {
            ClearPreviewTransientState();
            RemovePreviewObjects();
        }
    }

    ImGui::SeparatorText("攻撃対象");
    const char* currentLabel = enemyTypeIndex_ == 5
        ? "Wind Slime"
        : kEnemyLabels[std::clamp(enemyTypeIndex_, 0, static_cast<int>(std::size(kEnemyLabels)) - 1)];
    if (ImGui::BeginCombo("敵タイプ", currentLabel)) {
        for (int index = 0; index < static_cast<int>(std::size(kEnemyTypes)); ++index) {
            const bool selected = enemyTypeIndex_ == index;
            const char* itemLabel = kEnemyLabels[index];
            if (ImGui::Selectable(itemLabel, selected)) {
                enemyTypeIndex_ = index;
                LoadSelectedProfile();
                ApplyRecommendedSettings();
                if (enabled_) RestartPreview();
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (editableProfile_.attacks.size() > 1 && !IsGiantHookSplitPreview()) {
        const EnemyAttackDefinition* currentAttack = GetSelectedAttackDefinition();
        const char* attackLabel = currentAttack ? currentAttack->displayName.c_str() : "(攻撃なし)";
        if (ImGui::BeginCombo("攻撃種類", attackLabel)) {
            for (int index = 0; index < static_cast<int>(editableProfile_.attacks.size()); ++index) {
                const bool selected = profileAttackIndex_ == index;
                if (ImGui::Selectable(editableProfile_.attacks[index].displayName.c_str(), selected)) {
                    profileAttackIndex_ = index;
                    ApplyRecommendedSettings();
                    if (enabled_) RestartPreview();
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    if (enemyTypeIndex_ == 3) {
        const char* giantPreviewModes[] = {
            "ジャンププレス攻撃",
            "フック分裂（接地から抵抗）",
            "フック分裂（空中拘束から着地）"
        };
        int nextMode = giantPreviewModeIndex_;
        if (ImGui::Combo("確認モーション", &nextMode, giantPreviewModes, IM_ARRAYSIZE(giantPreviewModes))) {
            giantPreviewModeIndex_ = nextMode;
            ApplyRecommendedSettings();
            if (enabled_) RestartPreview();
        }
        if (IsGiantHookSplitPreview()) {
            ImGui::TextDisabled("青いダミーをフック所有者として、抵抗から分裂までを再生します。");
        }
    }

    bool restartForDistance = ImGui::DragFloat("標的までの距離", &targetDistance_, 0.1f, 1.0f, 18.0f, "%.1f m");
    bool restartForTargetMode = false;
    ImGui::BeginDisabled(IsGiantHookSplitPreview());
    restartForTargetMode = ImGui::Checkbox("実プレイヤーを攻撃対象にする", &usePlayerTarget_);
    ImGui::EndDisabled();
    if (IsGiantHookSplitPreview()) {
        ImGui::TextDisabled("フック分裂確認だけは、フック所有者用のダミーを使用します。");
    }
    ImGui::Checkbox(previewPlayer_ ? "プレイヤー標的を表示" : "ダミー標的を表示", &showTarget_);

    if (previewPlayer_) {
        const float maxHp = (std::max)(1.0f, previewPlayer_->GetMaxHp());
        const float hp = std::clamp(previewPlayer_->GetHp(), 0.0f, maxHp);
        char hpLabel[64]{};
        std::snprintf(hpLabel, sizeof(hpLabel), "HP %.1f / %.1f", hp, maxHp);
        ImGui::ProgressBar(hp / maxHp, ImVec2(-1.0f, 0.0f), hpLabel);
        if (ImGui::Button("選択中の攻撃で今すぐ被弾")) {
            TriggerPreviewPlayerDamage();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("通常再生でも攻撃が届けば自動で被弾します");
    }

    bool restartForHitPreview = ImGui::Checkbox("敵側の被弾リアクションを重ねて確認", &previewHitReaction_);
    if (previewHitReaction_) {
        restartForHitPreview |= ImGui::DragFloat(
            "被弾させる時刻",
            &previewHitTime_,
            0.05f,
            0.0f,
            (std::max)(0.0f, loopDuration_ - kFixedPreviewStep),
            "%.2f sec");
        ImGui::TextDisabled("標的側から敵が攻撃された状態を重ね、敵自身の潰れ・反発を確認します。");
    }
    if (ImGui::Button("推奨距離と長さへ戻す")) {
        ApplyRecommendedSettings();
        restartForDistance = true;
    }
    if ((restartForDistance || restartForTargetMode || restartForHitPreview) && enabled_) {
        RestartPreview();
    }

    ImGui::SeparatorText("攻撃プロファイル");
    const std::string profilePath = EnemyAttackProfile::GetDefaultPath(GetSelectedEnemyType());
    ImGui::TextDisabled("%s", profilePath.c_str());

    EnemyAttackDefinition* editedAttack = GetSelectedAttackDefinition();
    if (editedAttack) {
        ImGui::Text("攻撃ID: %s", editedAttack->id.c_str());
        bool changed = false;
        changed |= DrawProfileString("表示名", editedAttack->displayName);

        if (ImGui::TreeNodeEx("距離・範囲", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::DragFloat("最小発動距離", &editedAttack->minRange, 0.05f, 0.0f, 50.0f, "%.2f m");
            changed |= ImGui::DragFloat("最大発動距離", &editedAttack->maxRange, 0.05f, 0.0f, 50.0f, "%.2f m");
            changed |= ImGui::DragFloat("攻撃半径・幅", &editedAttack->radius, 0.05f, 0.01f, 50.0f, "%.2f m");
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("タイミング", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::DragFloat("予備動作", &editedAttack->windupDuration, 0.01f, 0.0f, 20.0f, "%.2f sec");
            changed |= ImGui::DragFloat("攻撃継続", &editedAttack->activeDuration, 0.01f, 0.0f, 20.0f, "%.2f sec");
            changed |= ImGui::DragFloat("硬直", &editedAttack->recoveryDuration, 0.01f, 0.0f, 20.0f, "%.2f sec");
            changed |= ImGui::DragFloat("再使用間隔", &editedAttack->cooldown, 0.01f, 0.0f, 30.0f, "%.2f sec");
            changed |= ImGui::DragFloat("警告先行時間", &editedAttack->warningLeadTime, 0.01f, 0.0f, 5.0f, "%.2f sec");
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("威力・移動", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::DragFloat("ダメージ", &editedAttack->damage, 0.05f, 0.0f, 999.0f, "%.2f");
            changed |= ImGui::DragFloat("最低速度", &editedAttack->minSpeed, 0.1f, 0.0f, 200.0f, "%.1f");
            changed |= ImGui::DragFloat("最高速度", &editedAttack->maxSpeed, 0.1f, 0.0f, 200.0f, "%.1f");
            changed |= ImGui::DragFloat("寿命", &editedAttack->lifetime, 0.05f, 0.0f, 30.0f, "%.2f sec");
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("状態異常", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* statusTypes[] = { "なし", "炎上" };
            int statusIndex = editedAttack->statusEffectType == "burning" ? 1 : 0;
            if (ImGui::Combo("種類", &statusIndex, statusTypes, IM_ARRAYSIZE(statusTypes))) {
                editedAttack->statusEffectType = statusIndex == 1 ? "burning" : "";
                if (statusIndex == 1 && editedAttack->statusDuration <= 0.0f) {
                    editedAttack->statusDuration = 2.2f;
                    editedAttack->statusTickInterval = 0.55f;
                    editedAttack->statusTickDamage = 0.12f;
                    editedAttack->statusVfx = "status_burning_flame";
                }
                changed = true;
            }
            ImGui::BeginDisabled(statusIndex == 0);
            changed |= ImGui::DragFloat("持続時間", &editedAttack->statusDuration, 0.05f, 0.0f, 30.0f, "%.2f sec");
            changed |= ImGui::DragFloat("継続間隔", &editedAttack->statusTickInterval, 0.01f, 0.05f, 10.0f, "%.2f sec");
            changed |= ImGui::DragFloat("継続ダメージ", &editedAttack->statusTickDamage, 0.01f, 0.0f, 999.0f, "%.2f");
            changed |= DrawProfileString("状態VFX", editedAttack->statusVfx);
            ImGui::EndDisabled();
            ImGui::TextDisabled("同種の再付与はスタックせず、残り時間と威力を更新します。");
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("プレビュー設定", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::DragFloat("推奨標的距離", &editedAttack->recommendedTargetDistance, 0.1f, 0.0f, 50.0f, "%.1f m");
            changed |= ImGui::DragFloat("推奨再生時間", &editedAttack->previewDuration, 0.05f, 0.1f, 30.0f, "%.2f sec");
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("演出参照")) {
            changed |= DrawProfileString("アニメーション", editedAttack->animation);
            changed |= DrawProfileString("予備動作VFX", editedAttack->windupVfx);
            changed |= DrawProfileString("攻撃中VFX", editedAttack->activeVfx);
            changed |= DrawProfileString("命中・着地VFX", editedAttack->impactVfx);
            changed |= DrawProfileString("SEキュー", editedAttack->audioCue);
            ImGui::TreePop();
        }

        profileDirty_ |= changed;
        if (profileDirty_) {
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.22f, 1.0f), "未保存の変更があります");
        }

        if (ImGui::Button(ICON_FA_SAVE " 保存してプレビューへ反映")) {
            SaveSelectedProfile();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_SYNC " 再読み込み")) {
            LoadSelectedProfile();
            ApplyRecommendedSettings();
            if (enabled_) RestartPreview();
        }
        ImGui::SameLine();
        if (ImGui::Button("既定値へ戻す")) {
            ResetSelectedProfile();
        }
    } else {
        ImGui::TextDisabled("この敵は、今回のスライム攻撃プロファイル対象外です。");
    }

    if (!profileStatus_.empty()) {
        ImGui::TextWrapped("%s", profileStatus_.c_str());
    }

    ImGui::SeparatorText("再生");
    ImGui::BeginDisabled(!enabled_);
    if (ImGui::Button(playing_ ? ICON_FA_PAUSE " 一時停止" : ICON_FA_PLAY " 再生")) {
        playing_ = !playing_;
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_REDO " 先頭から")) {
        RestartPreview();
    }
    ImGui::SameLine();
    if (ImGui::Button("-10F")) {
        playing_ = false;
        SeekPreview(elapsedTime_ - kFixedPreviewStep * 10.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("-1F")) {
        playing_ = false;
        SeekPreview(elapsedTime_ - kFixedPreviewStep);
    }
    ImGui::SameLine();
    if (ImGui::Button("+1F")) {
        playing_ = false;
        SeekPreview(elapsedTime_ + kFixedPreviewStep);
    }
    ImGui::SameLine();
    if (ImGui::Button("+10F")) {
        playing_ = false;
        SeekPreview(elapsedTime_ + kFixedPreviewStep * 10.0f);
    }

    ImGui::SliderFloat("再生速度", &playbackSpeed_, 0.05f, 2.0f, "%.2fx");
    ImGui::Checkbox("ループ", &loop_);
    ImGui::DragFloat("1周の長さ", &loopDuration_, 0.05f, 0.5f, 12.0f, "%.2f sec");
    float seekTime = elapsedTime_;
    if (ImGui::SliderFloat("タイムライン", &seekTime, 0.0f, (std::max)(loopDuration_, kFixedPreviewStep), "%.2f sec")) {
        playing_ = false;
        SeekPreview(seekTime);
    }
    const float progress = loopDuration_ > 0.001f ? std::clamp(elapsedTime_ / loopDuration_, 0.0f, 1.0f) : 0.0f;
    char progressLabel[64]{};
    sprintf_s(progressLabel, "%.2f / %.2f sec", elapsedTime_, loopDuration_);
    ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), progressLabel);
    if (previewEnemy_) {
        ImGui::Text("現在フェーズ: %s", GetCurrentPhaseName());
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("確認ポイント");
    ImGui::BulletText("予兆が攻撃より先に読めるか");
    ImGui::BulletText("溜め・発射・着地・硬直の姿勢が連続して見えるか");
    ImGui::BulletText("0.25x～0.5xでエフェクトの発生位置と残り方を確認");
#endif
}

void EnemyAttackPreviewWindow::DrawTimelineWindow() {
#ifdef USE_IMGUI
    constexpr const char* kWindowName = "敵攻撃プレビュー - Timeline";
    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove;
    if (!ImGui::Begin(kWindowName, nullptr, windowFlags)) {
        ImGui::End();
        return;
    }

    if (!IsCurrentSceneReady()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.28f, 1.0f), "Object3dを配置できるSceneが必要です。");
        ImGui::End();
        return;
    }
    if (sceneManager_->IsPlaying()) {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.20f, 1.0f), "編集モードへ戻すと敵攻撃タイムラインを使用できます。");
        ImGui::End();
        return;
    }

    if (!enabled_) {
        ImGui::TextDisabled("右のInspectorまたは下のボタンからプレビューを有効にしてください。");
        ImGui::SameLine();
        if (ImGui::Button("プレビューを開始")) {
            enabled_ = true;
            CreatePreviewObjects();
        }
        ImGui::End();
        return;
    }

    const bool timelineFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (timelineFocused && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            playing_ = !playing_;
        }
        const int stepFrames = ImGui::GetIO().KeyShift ? 10 : 1;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
            playing_ = false;
            SeekPreview(elapsedTime_ - kFixedPreviewStep * static_cast<float>(stepFrames));
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
            playing_ = false;
            SeekPreview(elapsedTime_ + kFixedPreviewStep * static_cast<float>(stepFrames));
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
            RestartPreview();
        }
    }

    ImGui::PushID("EnemyAttackTimelineToolbar");
    if (ImGui::Button(playing_ ? "■ 一時停止" : "▶ 再生", ImVec2(108.0f, 0.0f))) {
        playing_ = !playing_;
    }
    ImGui::SameLine();
    if (ImGui::Button("|<")) RestartPreview();
    ImGui::SameLine();
    if (ImGui::Button("-10F")) {
        playing_ = false;
        SeekPreview(elapsedTime_ - kFixedPreviewStep * 10.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("-1F")) {
        playing_ = false;
        SeekPreview(elapsedTime_ - kFixedPreviewStep);
    }
    ImGui::SameLine();
    if (ImGui::Button("+1F")) {
        playing_ = false;
        SeekPreview(elapsedTime_ + kFixedPreviewStep);
    }
    ImGui::SameLine();
    if (ImGui::Button("+10F")) {
        playing_ = false;
        SeekPreview(elapsedTime_ + kFixedPreviewStep * 10.0f);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(96.0f);
    ImGui::SliderFloat("速度", &playbackSpeed_, 0.05f, 2.0f, "%.2fx");
    ImGui::SameLine();
    ImGui::Checkbox("ループ", &loop_);
    ImGui::SameLine();
    if (ImGui::Button("カメラを初期位置へ")) {
        EffectPreviewStage::GetInstance()->RequestCameraRecenter();
    }
    ImGui::PopID();

    const TimelineSample* currentSample = timelineSamples_.empty() ? nullptr : &timelineSamples_.back();
    if (ImGui::BeginTable("EnemyAttackTimelineSummary", 4, ImGuiTableFlags_SizingStretchSame)) {
        const char* labels[] = { "時間 / Frame", "AIフェーズ", "速度", "接地からの高さ" };
        char values[4][64]{};
        sprintf_s(values[0], "%.3f / %.2f 秒  |  %dF", elapsedTime_, loopDuration_, static_cast<int>(std::round(elapsedTime_ / kFixedPreviewStep)));
        sprintf_s(values[1], "%s", GetCurrentPhaseName());
        sprintf_s(values[2], "%.2f m/s", currentSample ? currentSample->speed : 0.0f);
        sprintf_s(values[3], "%.2f m", currentSample ? currentSample->height : 0.0f);
        for (int column = 0; column < 4; ++column) {
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", labels[column]);
            ImGui::Text("%s", values[column]);
        }
        ImGui::EndTable();
    }

    ImGui::SetNextItemWidth(170.0f);
    ImGui::SliderFloat("タイムライン拡大", &timelinePixelsPerSecond_, 60.0f, 280.0f, "%.0f px/s");
    ImGui::SameLine();
    ImGui::Checkbox("再生位置を追従", &timelineFollowPlayhead_);
    ImGui::SameLine();
    ImGui::TextDisabled("Space: 再生/停止  ←/→: 1F  Shift+←/→: 10F  Home: 先頭");

    constexpr float kLabelWidth = 118.0f;
    constexpr float kHeaderHeight = 28.0f;
    constexpr float kLaneHeight = 30.0f;
    constexpr int kLaneCount = 10;
    constexpr float kBottomPadding = 8.0f;
    const float timelineHeight = kHeaderHeight + kLaneHeight * static_cast<float>(kLaneCount) + kBottomPadding;
    const float duration = (std::max)(loopDuration_, kFixedPreviewStep);
    const float visibleWidth = (std::max)(ImGui::GetContentRegionAvail().x, 320.0f);
    const float contentWidth = (std::max)(visibleWidth, kLabelWidth + duration * timelinePixelsPerSecond_ + 32.0f);

    ImGui::BeginChild(
        "##EnemyAttackTimelineScroll",
        ImVec2(0.0f, timelineHeight + ImGui::GetStyle().ScrollbarSize + 6.0f),
        true,
        ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##EnemyAttackTimelineCanvas", ImVec2(contentWidth, timelineHeight));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 canvasEnd(origin.x + contentWidth, origin.y + timelineHeight);

    drawList->AddRectFilled(origin, canvasEnd, IM_COL32(22, 25, 31, 255), 4.0f);
    drawList->AddRectFilled(origin, ImVec2(origin.x + kLabelWidth, canvasEnd.y), IM_COL32(34, 38, 47, 255), 4.0f);
    drawList->AddLine(
        ImVec2(origin.x + kLabelWidth, origin.y),
        ImVec2(origin.x + kLabelWidth, canvasEnd.y),
        IM_COL32(105, 115, 135, 255));

    const bool giantHookPreview = IsGiantHookSplitPreview();
    const char* laneNames[kLaneCount] = {
        giantHookPreview ? "専用モーション" : "設定タイミング",
        "実測AIフェーズ",
        "移動速度",
        "ジャンプ高さ",
        "変形量",
        "被弾リアクション",
        "GPU Particle系統",
        "通常Particle数",
        "Mesh Effect数",
        "Debris数"
    };
    for (int lane = 0; lane < kLaneCount; ++lane) {
        const float y0 = origin.y + kHeaderHeight + kLaneHeight * static_cast<float>(lane);
        const float y1 = y0 + kLaneHeight;
        if ((lane % 2) == 0) {
            drawList->AddRectFilled(ImVec2(origin.x, y0), ImVec2(canvasEnd.x, y1), IM_COL32(255, 255, 255, 6));
        }
        drawList->AddLine(ImVec2(origin.x, y1), ImVec2(canvasEnd.x, y1), IM_COL32(62, 68, 80, 190));
        drawList->AddText(ImVec2(origin.x + 8.0f, y0 + 7.0f), IM_COL32(205, 213, 226, 255), laneNames[lane]);
    }

    float tickStep = 1.0f;
    if (timelinePixelsPerSecond_ >= 220.0f) tickStep = 0.25f;
    else if (timelinePixelsPerSecond_ >= 110.0f) tickStep = 0.5f;
    const int tickCount = static_cast<int>(std::ceil(duration / tickStep));
    for (int tick = 0; tick <= tickCount; ++tick) {
        const float time = static_cast<float>(tick) * tickStep;
        const float x = origin.x + kLabelWidth + time * timelinePixelsPerSecond_;
        const bool major = std::fmod(time + 0.001f, 1.0f) < 0.01f;
        drawList->AddLine(
            ImVec2(x, origin.y + (major ? 0.0f : 14.0f)),
            ImVec2(x, canvasEnd.y),
            major ? IM_COL32(80, 88, 105, 220) : IM_COL32(60, 66, 78, 130));
        if (major) {
            char label[24]{};
            sprintf_s(label, "%.0fs", time);
            drawList->AddText(ImVec2(x + 4.0f, origin.y + 5.0f), IM_COL32(185, 194, 210, 255), label);
        }
    }

    if (giantHookPreview) {
        const float laneY = origin.y + kHeaderHeight;
        DrawTimelineBlock(
            drawList,
            origin,
            kLabelWidth,
            timelinePixelsPerSecond_,
            laneY,
            kLaneHeight,
            0.0f,
            duration,
            IM_COL32(70, 105, 132, 190),
            giantPreviewModeIndex_ == 2
                ? "空中拘束 → 着地 → 抵抗 → 分裂"
                : "踏ん張り → 引きずられ → 分裂");
    } else if (const EnemyAttackDefinition* attack = GetSelectedAttackDefinition()) {
        const float laneY = origin.y + kHeaderHeight;
        float cursorTime = 0.0f;
        const float windupEnd = (std::min)(duration, cursorTime + attack->windupDuration);
        DrawTimelineBlock(drawList, origin, kLabelWidth, timelinePixelsPerSecond_, laneY, kLaneHeight, cursorTime, windupEnd, IM_COL32(214, 155, 51, 210), "予備動作");
        cursorTime = windupEnd;
        const float activeEnd = (std::min)(duration, cursorTime + attack->activeDuration);
        DrawTimelineBlock(drawList, origin, kLabelWidth, timelinePixelsPerSecond_, laneY, kLaneHeight, cursorTime, activeEnd, IM_COL32(223, 72, 92, 220), "攻撃");
        cursorTime = activeEnd;
        const float recoveryEnd = (std::min)(duration, cursorTime + attack->recoveryDuration);
        DrawTimelineBlock(drawList, origin, kLabelWidth, timelinePixelsPerSecond_, laneY, kLaneHeight, cursorTime, recoveryEnd, IM_COL32(126, 91, 205, 215), "硬直");
        cursorTime = recoveryEnd;
        const float cooldownEnd = (std::min)(duration, cursorTime + attack->cooldown);
        DrawTimelineBlock(drawList, origin, kLabelWidth, timelinePixelsPerSecond_, laneY, kLaneHeight, cursorTime, cooldownEnd, IM_COL32(75, 120, 145, 190), "再使用待ち");
    }

    if (!timelineSamples_.empty()) {
        const float phaseLaneY = origin.y + kHeaderHeight + kLaneHeight;
        std::size_t segmentBegin = 0;
        for (std::size_t index = 1; index <= timelineSamples_.size(); ++index) {
            const bool segmentEnds = index == timelineSamples_.size() || timelineSamples_[index].phase != timelineSamples_[segmentBegin].phase;
            if (!segmentEnds) {
                continue;
            }
            const float startTime = std::clamp(timelineSamples_[segmentBegin].time, 0.0f, duration);
            const float rawEndTime = index < timelineSamples_.size()
                ? timelineSamples_[index].time
                : (std::max)(elapsedTime_, startTime + kFixedPreviewStep);
            const float endTime = std::clamp(rawEndTime, 0.0f, duration);
            DrawTimelineBlock(
                drawList,
                origin,
                kLabelWidth,
                timelinePixelsPerSecond_,
                phaseLaneY,
                kLaneHeight,
                startTime,
                endTime,
                GetPhaseColor(timelineSamples_[segmentBegin].phase),
                timelineSamples_[segmentBegin].phase.c_str());
            segmentBegin = index;
        }

        float maxSpeed = 1.0f;
        float maxHeight = 1.0f;
        float maxDeformation = 0.25f;
        float maxGpuParticleSystems = 1.0f;
        float maxCpuParticles = 1.0f;
        float maxMeshEffects = 1.0f;
        float maxDebrisPieces = 1.0f;
        for (const TimelineSample& sample : timelineSamples_) {
            maxSpeed = (std::max)(maxSpeed, sample.speed);
            maxHeight = (std::max)(maxHeight, sample.height);
            maxDeformation = (std::max)(maxDeformation, sample.deformation);
            maxGpuParticleSystems = (std::max)(maxGpuParticleSystems, sample.gpuParticleSystems);
            maxCpuParticles = (std::max)(maxCpuParticles, sample.cpuParticles);
            maxMeshEffects = (std::max)(maxMeshEffects, sample.meshEffects);
            maxDebrisPieces = (std::max)(maxDebrisPieces, sample.debrisPieces);
        }
        if (const EnemyAttackDefinition* attack = GetSelectedAttackDefinition()) {
            maxSpeed = (std::max)(maxSpeed, attack->maxSpeed);
        }

        const auto drawGraph = [&](int lane, float TimelineSample::* valueMember, float maxValue, ImU32 color) {
            const float laneTop = origin.y + kHeaderHeight + kLaneHeight * static_cast<float>(lane) + 4.0f;
            const float laneBottom = laneTop + kLaneHeight - 8.0f;
            for (std::size_t index = 1; index < timelineSamples_.size(); ++index) {
                const TimelineSample& previous = timelineSamples_[index - 1];
                const TimelineSample& current = timelineSamples_[index];
                const float x0 = origin.x + kLabelWidth + std::clamp(previous.time, 0.0f, duration) * timelinePixelsPerSecond_;
                const float x1 = origin.x + kLabelWidth + std::clamp(current.time, 0.0f, duration) * timelinePixelsPerSecond_;
                const float y0 = laneBottom - std::clamp((previous.*valueMember) / maxValue, 0.0f, 1.0f) * (laneBottom - laneTop);
                const float y1 = laneBottom - std::clamp((current.*valueMember) / maxValue, 0.0f, 1.0f) * (laneBottom - laneTop);
                drawList->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), color, 2.0f);
            }
        };
        drawGraph(2, &TimelineSample::speed, maxSpeed, IM_COL32(75, 210, 255, 255));
        drawGraph(3, &TimelineSample::height, maxHeight, IM_COL32(255, 196, 72, 255));
        drawGraph(4, &TimelineSample::deformation, maxDeformation, IM_COL32(241, 121, 196, 255));
        drawGraph(5, &TimelineSample::hitReaction, 1.0f, IM_COL32(255, 92, 92, 255));
        drawGraph(6, &TimelineSample::gpuParticleSystems, maxGpuParticleSystems, IM_COL32(255, 224, 74, 255));
        drawGraph(7, &TimelineSample::cpuParticles, maxCpuParticles, IM_COL32(90, 226, 255, 255));
        drawGraph(8, &TimelineSample::meshEffects, maxMeshEffects, IM_COL32(255, 139, 72, 255));
        drawGraph(9, &TimelineSample::debrisPieces, maxDebrisPieces, IM_COL32(190, 157, 112, 255));

        char scaleText[64]{};
        sprintf_s(scaleText, "max %.1f", maxSpeed);
        drawList->AddText(
            ImVec2(origin.x + kLabelWidth - 55.0f, origin.y + kHeaderHeight + kLaneHeight * 2.0f + 7.0f),
            IM_COL32(120, 194, 220, 255),
            scaleText);
        sprintf_s(scaleText, "max %.1f", maxHeight);
        drawList->AddText(
            ImVec2(origin.x + kLabelWidth - 55.0f, origin.y + kHeaderHeight + kLaneHeight * 3.0f + 7.0f),
            IM_COL32(224, 181, 102, 255),
            scaleText);
        sprintf_s(scaleText, "max %.2f", maxDeformation);
        drawList->AddText(
            ImVec2(origin.x + kLabelWidth - 62.0f, origin.y + kHeaderHeight + kLaneHeight * 4.0f + 7.0f),
            IM_COL32(224, 137, 201, 255),
            scaleText);
        const auto drawCountScale = [&](int lane, float maxValue, ImU32 color) {
            sprintf_s(scaleText, "max %.0f", maxValue);
            drawList->AddText(
                ImVec2(origin.x + kLabelWidth - 55.0f, origin.y + kHeaderHeight + kLaneHeight * static_cast<float>(lane) + 7.0f),
                color,
                scaleText);
        };
        drawCountScale(6, maxGpuParticleSystems, IM_COL32(226, 205, 98, 255));
        drawCountScale(7, maxCpuParticles, IM_COL32(112, 205, 224, 255));
        drawCountScale(8, maxMeshEffects, IM_COL32(224, 153, 112, 255));
        drawCountScale(9, maxDebrisPieces, IM_COL32(190, 166, 130, 255));
    }

    const float playheadTime = std::clamp(elapsedTime_, 0.0f, duration);
    const float playheadX = origin.x + kLabelWidth + playheadTime * timelinePixelsPerSecond_;
    drawList->AddLine(ImVec2(playheadX, origin.y), ImVec2(playheadX, canvasEnd.y), IM_COL32(255, 82, 82, 255), 2.0f);
    drawList->AddTriangleFilled(
        ImVec2(playheadX - 6.0f, origin.y),
        ImVec2(playheadX + 6.0f, origin.y),
        ImVec2(playheadX, origin.y + 9.0f),
        IM_COL32(255, 82, 82, 255));

    if (ImGui::IsItemHovered()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (mouse.x >= origin.x + kLabelWidth) {
            const float hoverTime = std::clamp(
                (mouse.x - origin.x - kLabelWidth) / timelinePixelsPerSecond_,
                0.0f,
                duration);
            ImGui::BeginTooltip();
            ImGui::Text("%.3f 秒  |  %dF", hoverTime, static_cast<int>(std::round(hoverTime / kFixedPreviewStep)));
            if (!timelineSamples_.empty()) {
                const auto nearest = (std::min_element)(
                    timelineSamples_.begin(),
                    timelineSamples_.end(),
                    [hoverTime](const TimelineSample& lhs, const TimelineSample& rhs) {
                        return std::abs(lhs.time - hoverTime) < std::abs(rhs.time - hoverTime);
                    });
                ImGui::Text("フェーズ: %s", nearest->phase.c_str());
                ImGui::Text("速度 %.2f m/s  |  高さ %.2f m", nearest->speed, nearest->height);
                ImGui::Text("変形 %.2f  |  被弾 %.2f", nearest->deformation, nearest->hitReaction);
                ImGui::Text("GPU系統 %.0f  |  通常粒子 %.0f", nearest->gpuParticleSystems, nearest->cpuParticles);
                ImGui::Text("Mesh Effect %.0f  |  Debris %.0f", nearest->meshEffects, nearest->debrisPieces);
            }
            ImGui::EndTooltip();

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                timelineFollowPlayhead_ = false;
                playing_ = false;
                SeekPreview(hoverTime);
            }
        }
        if (ImGui::GetIO().KeyCtrl && std::abs(ImGui::GetIO().MouseWheel) > 0.0f) {
            timelinePixelsPerSecond_ = std::clamp(
                timelinePixelsPerSecond_ + ImGui::GetIO().MouseWheel * 18.0f,
                60.0f,
                280.0f);
        }
    }

    if (playing_ && timelineFollowPlayhead_) {
        const float playheadLocalX = kLabelWidth + playheadTime * timelinePixelsPerSecond_;
        const float desiredScroll = (std::max)(0.0f, playheadLocalX - visibleWidth * 0.75f);
        ImGui::SetScrollX((std::min)(desiredScroll, ImGui::GetScrollMaxX()));
    }
    ImGui::EndChild();

    ImGui::End();
#endif
}
