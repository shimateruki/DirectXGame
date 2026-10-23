#include "EnemyAttackPreviewWindow.h"
#include "EnemyAttackPreviewRegistry.h"
#include "CopyAbilityPreviewRegistry.h"
#include "EnemyAttackPreviewWindow.Constants.h"
#include "engine/animation/BodyAnimationClip.h"
#include "game/scene/gameplay/GamePlayScene.h"
#include "game/system/scene/SceneGameplayAccess.h"
#include "game/crown/CrownFinaleTuning.h"

#include "BaseEnemy.h"
#include "BaseScene.h"
#include "Camera.h"
#include "CameraManager.h"
#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.h"
#include "game/system/combat/BulletManager.h"
#include "CollisionConfig.h"
#include "DebrisEffectManager.h"
#include "EffectPreviewStage.h"
#include "game/actor/enemy/spawn/EnemyFactory.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.h"
#include "game/crown/CrownBarrageCamera.h"
#include "GPUParticleManager.h"
#include "game/system/rules/GameRule.h"
#include "game/system/rules/EventManager.h"
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

using EnemyAttackPreviewConstants::kFixedPreviewStep;
using EnemyAttackPreviewConstants::kPi;

namespace {
constexpr const char* kPreviewNamePrefix = "__Editor_EnemyAttackPreview_";
constexpr const char* kPreviewEnemyName = "__Editor_EnemyAttackPreview_Enemy";
constexpr const char* kPreviewTargetName = "__Editor_EnemyAttackPreview_Target";

bool HasPreviewPrefix(const engine::Object3d* object) {
    return object && object->GetName().rfind(kPreviewNamePrefix, 0) == 0;
}

}

void EnemyAttackPreviewWindow::SetPlayerCopyMode(bool enabled) {
    // コピー技の観測へ切り替える前に、敵攻撃用の一時オブジェクトを片付ける。
    if (sceneManager_ && sceneManager_->IsPlaying()) {
        // 本番再生中も基本値の対象は切り替え、隔離プレビューだけを止めます。
        StopPreview();
        statusTab_ = true;
    }
    if (crownMovieMode_) {
        ClearPreviewTransientState();
        RemovePreviewObjects();
        enabled_ = false;
        crownMovieMode_ = false;
    }

    if (playerCopyMode_ == enabled)
        return;
    if (enabled_) {
        ClearPreviewTransientState();
        RemovePreviewObjects();
    }

    playerCopyMode_ = enabled;
    authoringActorSelected_ = false;
    selectedTrack_ = -1;
    LoadSelectedProfile();
    if (enabled)
        loopDuration_ = 6.0f;
    else ApplyRecommendedSettings();
    if (enabled_)
        CreatePreviewObjects();
}

void EnemyAttackPreviewWindow::SetCrownMovieMode() {
    // 王冠演出は通常の敵攻撃プレビューとは別の時刻とシーンを使う。
    if (crownMovieMode_ || (sceneManager_ && sceneManager_->IsPlaying()))
        return;
    ClearPreviewTransientState();
    RemovePreviewObjects();
    EffectPreviewStage::GetInstance()->ReturnToScene();
    enabled_ = false;
    playing_ = false;
    playerCopyMode_ = false;
    crownMovieMode_ = true;
    loopDuration_ = GetCrownMovieDuration();
}

void EnemyAttackPreviewWindow::StopPreview() {
    // 下書きプロファイルの参照と一時オブジェクトを同時に解除する。
    useDraftProfile_ = false;
    draftRefreshPending_ = false;
    if (!enabled_)
        return;
    ClearPreviewTransientState();
    RemovePreviewObjects();
    enabled_ = false;
}

void EnemyAttackPreviewWindow::Initialize(SceneManager* sceneManager) {
    // 現在選択中の敵の攻撃プロファイルを読み、適切な距離と再生長を初期値にする。
    sceneManager_ = sceneManager;
    LoadSelectedProfile();
    ApplyRecommendedSettings();
}

void EnemyAttackPreviewWindow::Finalize() {
    // シーン破棄後に参照しないよう、一時演出を消して借用ポインタを外す。
    ClearPreviewTransientState();
    RemovePreviewObjects();
    sceneManager_ = nullptr;
    previewScene_ = nullptr;
}

void EnemyAttackPreviewWindow::Update(float deltaTime) {
    if (const auto& save = EnemyAttackPreviewRegistry::GetStatusEditor().savePending) save();
#ifdef USE_IMGUI
    if (statusRefreshPending_ && !ImGui::IsAnyItemActive()) RefreshStatusPreview();
    if (placementPreview_ && !IsAuthoringViewportActive()) {
        StopPreview();
        return;
    }
#endif
    BaseScene* currentScene = sceneManager_ ? sceneManager_->GetCurrentScene() : nullptr;

    if (previewScene_ != currentScene) {
        StopPlacementPreview();

        // Scene切り替え後は旧Sceneのポインタへ触れず、新しいScene側で作り直す。
        previewScene_ = currentScene;
        crownMoviePrepared_ = false;
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

    if (crownMovieMode_)
        loopDuration_ = GetCrownMovieDuration();

    // Sceneの切り替えと本番再生を確認してから、プレビューだけを再構築します。
    if (draftRefreshPending_ && useDraftProfile_ && !placementPreview_) {
        draftRefreshDelay_ += (std::max)(0.0f, deltaTime);
#ifdef USE_IMGUI
        if (!ImGui::IsAnyItemActive() && draftRefreshDelay_ >= 0.15f)
            PreviewDraft(true);
#endif
    }

    MarkPreviewFamilyEditorInternal();
    if (!crownMovieMode_)
        SyncTargetTransform();

    if (!HasPreviewActor() && !previewSequenceCompleted_) {
        CreatePreviewObjects();
    }

    if (placementPreview_) {
        if (playing_)
            RestartPreview(); // 通常の再生操作は保存済み攻撃の確認に戻ります。
        else { UpdatePlacementPreview(); return; }
    }

    if (!playing_ || (!HasPreviewActor() && !previewSequenceCompleted_)) {
        return;
    }

    const float safeDelta = deltaTime > 0.0001f ? deltaTime : kFixedPreviewStep;
    simulationAccumulator_ += (std::min)(safeDelta, 0.25f) * std::clamp(playbackSpeed_, 0.05f, 2.0f);

    while (simulationAccumulator_ >= kFixedPreviewStep && playing_ && (HasPreviewActor() || previewSequenceCompleted_)) {
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
    if (!crownMovieMode_ && !playerCopyMode_ && !GetPreviewDescriptor()) {
        enabled_ = false;
        return;
    }
    if (!IsCurrentSceneReady() || sceneManager_->IsPlaying()) {
        return;
    }

    if (crownMovieMode_) {
        previewScene_ = sceneManager_->GetCurrentScene();
        auto* game = dynamic_cast<GamePlayScene*>(previewScene_);
        crownMoviePrepared_ = game && game->BeginCrownMoviePreview(crownDefeatMode_);
        if (!crownMoviePrepared_)
            enabled_ = false;
        elapsedTime_ = simulationAccumulator_ = 0.0f;
        loopDuration_ = GetCrownMovieDuration();
        return;
    }

    EffectPreviewStage* previewStage = EffectPreviewStage::GetInstance();
    previewStage->EnableForToolPreview();

    if (requestCameraRecenter) {
        previewStage->RequestCameraRecenter();
    }

    BaseScene* scene = sceneManager_->GetCurrentScene();
    previewScene_ = scene;
    spawnSession_ = std::make_shared<SpawnSession>();
    const engine::Vector3 ground = previewStage->GetGroundPosition();

    previewPlayer_ = nullptr;

    if (playerCopyMode_ || (usePlayerTarget_ && !IsGiantHookSplitPreview())) {
        auto player = std::make_unique<Player>();
        player->Initialize(
            scene->GetObject3dCommon(),
            engine::InputManager::GetInstance(),
            scene->GetParticleSystem(),
            scene->GetSpriteCommon());
        player->SetName(kPreviewTargetName);
        player->SetClassName("Player");
        player->SetEditorInternal(true);
        player->SetIsLocked(true);

        // シーンで調整した実プレイヤーの形状を使い、属性とマスクで衝突から隔離します。
        for (const auto& object : scene->GetObjects()) {
            if (auto* scenePlayer = dynamic_cast<Player*>(object.get()); scenePlayer && !HasPreviewPrefix(scenePlayer)) {
                player->SetColliderConfig(scenePlayer->GetColliderConfig());
                break;
            }
        }

        // 接触応答はmask=0で止め、共通攻撃のPhysics Queryには標的として見える状態にします。
        player->SetCollisionAttribute(CollisionCategories::Get("player"));
        player->SetCollisionMask(0);
        player->SetMaterialType(25);
        player->SetEnableLighting(true);
        player->SetIsControlActive(false);
        player->SetMoveYaw(kPi);
        player->SetVelocity({ 0.0f, 0.0f, 0.0f });
        player->SetGrounded(true);

        if (!playerCopyMode_) {
            player->SetGravity(0.0f);
            player->SetMaxFallSpeed(0.0f);
        }
        if (!playerCopyMode_ && player->param_.has_value()) {
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
        auto target = std::make_unique<engine::Object3d>();
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

    if (playerCopyMode_) {
        previewPlayer_->SetTranslate(ground);
        // 未コピー時はPlayerの基本モデルを使い、コピー開始時に能力本来の見た目へ切り替えます。
        previewPlayer_->SetMoveYaw(0.0f);
        previewPlayer_->UpdateLocalMatrix();
        previewPlayer_->UpdateWorldMatrix(true);
        previewPlayer_->SetCopyPreviewInput({});
        previewPlayer_->SetCopyPreviewGroundHeight(ground.y);

        const auto* copy = GetCopyDescriptor();
        const auto* action = GetCopyAction();
        if (copy && action && !action->startsUncopied)
            previewPlayer_->ApplyStoredCopy(copy->type);
        if (useDraftProfile_)
            previewPlayer_->SetCopyPreviewProfile(draftProfile_);
        if (const auto* attack = GetSelectedAttackDefinition(); attack && attack->sequence.enabled && attack->sequence.binding == "player_special")
            previewPlayer_->SetCopyPreviewAttackId(attack->id);
        previewBaseScale_ = previewPlayer_->GetScale();

        if (showTarget_) {
            auto dummy = EnemyFactory::GetInstance()->CreateEnemy("Slime", scene->GetObject3dCommon());

            if (dummy) {
                dummy->SetName(kPreviewEnemyName);
                dummy->SetEditorInternal(true);
                dummy->SetIsLocked(true);
                dummy->SetTranslate(ground + engine::Vector3{0,0,targetDistance_});
                dummy->SetCollisionAttribute(CollisionCategories::Get("enemy"));
                dummy->SetCollisionMask(0);

                if (dummy->param_) {
                    dummy->param_->maxHp = dummy->param_->hp = 100000.0f;
                }
                dummy->UpdateWorldMatrix(true);
                previewEnemy_ = dummy.get();
                scene->AddObject(std::move(dummy));
            }
        }

        elapsedTime_ = simulationAccumulator_ = 0;
        copyActionTriggered_ = false;
        previewSequenceCompleted_ = false;
        playing_ = true;
        timelineSamples_.clear();
        RecordTimelineSample();
        return;
    }

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
    // Scene切替・停止・巻き戻しで、前のActorの生成先を最初に無効化します。
    spawnSession_.reset();
    // Scene が所有する Object は削除要求へ渡し、先に衝突属性を無効化する。
    BaseScene* currentScene = sceneManager_ ? sceneManager_->GetCurrentScene() : nullptr;

    if (crownMoviePrepared_ && currentScene && currentScene == previewScene_) {
        if (auto* game = dynamic_cast<GamePlayScene*>(currentScene))
            game->EndCrownMoviePreview();
    }

    crownMoviePrepared_ = false;

    if (currentScene && currentScene == previewScene_) {
        std::unordered_set<engine::Object3d*> removalTargets;
        if (previewEnemy_)
            removalTargets.insert(previewEnemy_);
        if (previewTarget_)
            removalTargets.insert(previewTarget_);
        for (engine::Object3d* object : spawnedPreviewObjects_) {
            if (object)
                removalTargets.insert(object);
        }

        for (const auto& object : currentScene->GetObjects()) {
            if (HasPreviewPrefix(object.get())) {
                removalTargets.insert(object.get());
            }
        }

        for (engine::Object3d* object : removalTargets) {
            const bool present = std::any_of(currentScene->GetObjects().begin(), currentScene->GetObjects().end(),
                [object](const auto& entry) { return entry.get() == object; });
            if (!present) continue;
            object->SetCollisionAttribute(0);
            object->SetCollisionMask(0);
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
    // 現在の設定を先頭から反映するため、一時状態を消して観測対象を作り直す。
    if (crownMovieMode_ && crownMoviePrepared_) {
        const bool resume = playing_;
        SeekPreview(0.0f);
        playing_ = resume;
        return;
    }
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

    if (crownMovieMode_ && crownMoviePrepared_) {
        if (auto* game = dynamic_cast<GamePlayScene*>(previewScene_))
            game->RewindCrownMoviePreview();
        elapsedTime_ = 0.0f;
    } else {
        RemovePreviewObjects();
        CreatePreviewObjects(false);
    }

    playing_ = false;

    for (int frame = 0; frame < targetFrame && (HasPreviewActor() || previewSequenceCompleted_); ++frame) {
        StepSimulation(kFixedPreviewStep);
    }

    elapsedTime_ = (std::min)(static_cast<float>(targetFrame) * kFixedPreviewStep, safeDuration);
    simulationAccumulator_ = 0.0f;

    // GPUパーティクルエディターと同様に、先頭から固定刻みで再構築した状態をそのまま保持します。
    ApplyEffectPlaybackState();
}

void EnemyAttackPreviewWindow::ClearPreviewTransientState() {
    // シークや再開時に前回の弾・破片・粒子が残らないよう各演出系を初期化する。
    StopPlacementPreview();
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
        if (engine::ParticleSystem* particles = previewScene_->GetParticleSystem()) {
            particles->ResetSimulation();
            particles->SetSimulationTimeScale(1.0f);
        }
    }
}

void EnemyAttackPreviewWindow::SetEffectSimulationScale(float timeScale) {
    // メッシュ、破片、GPU・CPU 粒子へ同じ時間倍率を配る。
    const float safeScale = (std::max)(0.0f, timeScale);
    MeshEffectManager::GetInstance()->SetTimeScale(safeScale);
    DebrisEffectManager::GetInstance()->SetTimeScale(safeScale);

    GPUParticleManager* gpuParticles = GPUParticleManager::GetInstance();

    if (gpuParticles->IsInitialized()) {
        gpuParticles->SetTimeScale(safeScale);
    }

    if (previewScene_) {
        if (engine::ParticleSystem* particles = previewScene_->GetParticleSystem()) {
            particles->SetSimulationTimeScale(safeScale);
        }
    }
}

void EnemyAttackPreviewWindow::ApplyEffectPlaybackState() {
    if (!enabled_ || !sceneManager_ || sceneManager_->IsPlaying()) {
        return;
    }

    // 再生速度は固定刻みの実行回数へ既に反映済みなので、演出側は再生中1、停止中0で統一します。
    // ムービーは手動の固定刻みで全管理系を進めるため、外側の実時間更新は常に止めます。
    SetEffectSimulationScale(crownMovieMode_ ? 0.0f : (playing_ ? 1.0f : 0.0f));
}

void EnemyAttackPreviewWindow::StepSimulation(float deltaTime) {
    if (crownMovieMode_) {
        if (auto* game = dynamic_cast<GamePlayScene*>(previewScene_)) {
            SetEffectSimulationScale(1.0f);
            game->StepCrownMoviePreview(deltaTime);
            VFXSequencer::UpdateOneShots(deltaTime);
            if (auto* particles = game->GetParticleSystem())
                particles->Update(deltaTime);
            MeshEffectManager::GetInstance()->UpdateEditorPreviewStep(deltaTime);
            DebrisEffectManager::GetInstance()->Update(deltaTime);
            if (auto* gpu = GPUParticleManager::GetInstance(); gpu->IsInitialized())
                gpu->UpdateEditorPreviewStep(deltaTime);
            elapsedTime_ += deltaTime;
            ApplyEffectPlaybackState();
        }

        return;
    }

    if (!previewTarget_ || (!HasPreviewActor() && !previewSequenceCompleted_)) {
        return;
    }

    SetEffectSimulationScale(1.0f);

    const engine::Vector3 ground = EffectPreviewStage::GetInstance()->GetGroundPosition();
    BaseEnemy* primaryEnemy = playerCopyMode_ ? nullptr : previewEnemy_;

    if (playerCopyMode_ && previewEnemy_) {
        previewEnemy_->BaseEnemy::Update(deltaTime);
        previewEnemy_->SetTranslate(ground + engine::Vector3{0,0,targetDistance_});
        previewEnemy_->SetVelocity({});
        previewEnemy_->SetGrounded(true);
        previewEnemy_->UpdateWorldMatrix(true);
    }

    if (primaryEnemy) {
        if (previewHitReaction_ && !previewHitTriggered_ && elapsedTime_ >= previewHitTime_) {
            engine::Vector3 knockback = primaryEnemy->GetWorldPosition() - previewTarget_->GetWorldPosition();
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

        const auto* descriptor = GetPreviewDescriptor();
        if (descriptor && descriptor->updateInteraction &&
            descriptor->updateInteraction(*primaryEnemy, deltaTime, GetEnemyPreviewContext())) {
            RecordTimelineSample();
            previewSequenceCompleted_ = true;
            // 本番の分裂処理がSceneへ加えた子も、共通の削除対象へ取り込みます。
            for (const auto& object : previewScene_->GetObjects()) {
                if (!object || object.get() == primaryEnemy || object.get() == previewTarget_ || !HasPreviewPrefix(object.get()))
                    continue;
                if (std::find(spawnedPreviewObjects_.begin(), spawnedPreviewObjects_.end(), object.get()) == spawnedPreviewObjects_.end())
                    spawnedPreviewObjects_.push_back(object.get());
            }
            previewEnemy_ = nullptr;
        }
    }

    // 更新中の再召喚でvectorが伸びても、反復子を無効化しません。追加分は次の固定刻みから更新します。
    const size_t spawnedCount = spawnedPreviewObjects_.size();
    for (size_t index = 0; index < spawnedCount; ++index) {
        auto* raw = spawnedPreviewObjects_[index];
        const bool present = std::any_of(previewScene_->GetObjects().begin(), previewScene_->GetObjects().end(),
            [raw](const auto& object) { return object.get() == raw; });
        BaseEnemy* spawnedEnemy = present ? dynamic_cast<BaseEnemy*>(raw) : nullptr;

        if (spawnedEnemy) {
            MaintainGrounding(spawnedEnemy, ground.y);
            spawnedEnemy->Update(deltaTime);
            MaintainGrounding(spawnedEnemy, ground.y);
        }
    }

    BulletManager::GetInstance()->Update(deltaTime);

    if (playerCopyMode_ && previewEnemy_) {
        for (const auto& bullet : BulletManager::GetInstance()->GetBullets()) {
            if (!bullet || bullet->IsDead() ||
                !(bullet->GetCollisionAttribute() & CollisionCategories::Get("player_attack")) ||
                !(bullet->GetCollisionMask() & CollisionCategories::Get("enemy"))) continue;

            // 隔離Actorのmaskは0なのでObject3dの応答フィルターを通さず、実形状を照合します。
            if (!bullet->GetCollider() || !previewEnemy_->GetCollider() ||
                !bullet->GetCollider()->CheckCollision(previewEnemy_->GetCollider()).isColliding) continue;
            DamageEvent event;
            event.target = previewEnemy_;
            event.attacker = previewPlayer_;
            event.damageAmount = bullet->GetDamage();
            event.damageType = bullet->GetDamageType();
            event.statusEffect = bullet->GetStatusEffect();
            EventManager::GetInstance()->Dispatch(event);
            bullet->Expire(true);
        }
    }

    if (!playerCopyMode_)
        ResolvePreviewProjectileHits();

    if (previewScene_ && !playerCopyMode_) {
        if (GameRule* gameRule = GetSceneGameRule(previewScene_)) {
            gameRule->Update(deltaTime);
        }
    }

    if (previewPlayer_) {
        if (playerCopyMode_) {
            // プレビュー用PlayerはColliderを無効化しているため、入力処理より先に
            // 簡易床との接地状態を確定させます。E技とガードの地上条件を実ゲームと揃えます。
            engine::Vector3 previewPosition = previewPlayer_->GetTranslate();
            engine::Vector3 previewVelocity = previewPlayer_->GetVelocity();
            const bool touchingPreviewGround =
                previewPosition.y <= ground.y + 0.001f && previewVelocity.y <= 0.0f;
            if (touchingPreviewGround) {
                previewPosition.y = ground.y;
                previewVelocity.y = 0.0f;
                previewPlayer_->SetTranslate(previewPosition);
                previewPlayer_->SetVelocity(previewVelocity);
            }

            previewPlayer_->SetGrounded(touchingPreviewGround);

            PlayerCopyAbilityInput input{};
            const auto* action = GetCopyAction();
            const auto* copy = GetCopyDescriptor();
            if (copy && action) {
                CopyAbilityPreviewFrame frame{elapsedTime_, deltaTime, copyHoldTime_, action->startTime,
                    !copyActionTriggered_ && elapsedTime_ >= action->startTime};
                if (frame.triggered) {
                    if (action->onTrigger) action->onTrigger(*previewPlayer_, copy->type);
                    copyActionTriggered_ = true;
                }
                if (action->buildInput) input = action->buildInput(frame);
            }
            previewPlayer_->SetCopyPreviewInput(input);
        }

        previewPlayer_->SetGravity(playerCopyMode_ ? 50.0f : 0.0f);
        previewPlayer_->SetMaxFallSpeed(playerCopyMode_ ? 60.0f : 0.0f);
        previewPlayer_->SetIsControlActive(false);
        previewPlayer_->Update(deltaTime);
        if (playerCopyMode_)
            previewPlayer_->StepCopyPreviewObjects(deltaTime, ground.y);
        if (previewPlayer_->GetTranslate().y < ground.y) {
            engine::Vector3 playerPosition = previewPlayer_->GetTranslate();
            playerPosition.y = ground.y;
            previewPlayer_->SetTranslate(playerPosition);
            engine::Vector3 velocity = previewPlayer_->GetVelocity();
            velocity.y = 0;
            previewPlayer_->SetVelocity(velocity);
        }

        previewPlayer_->SetGrounded(previewPlayer_->GetTranslate().y <= ground.y + 0.001f && previewPlayer_->GetVelocity().y <= 0.0f);
        previewPlayer_->UpdateWorldMatrix(true);

        if (!playerCopyMode_) {
            // 隔離中の敵・プレイヤーだけ一時的に接続し、ゲームと同じ本体命中処理を通します。
            // シーン全体の衝突判定には参加させず、通常接触もこの共通経路で無害化されます。
            const auto resolveBodyHit = [this](BaseEnemy* enemy) {
                if (!enemy || enemy->GetActiveBodyAttackDamage() <= 0.0f)
                    return;
                const auto playerAttribute = previewPlayer_->GetCollisionAttribute();
                const auto playerMask = previewPlayer_->GetCollisionMask();
                const auto enemyAttribute = enemy->GetCollisionAttribute();
                const auto enemyMask = enemy->GetCollisionMask();
                previewPlayer_->SetCollisionAttribute(CollisionCategories::Get("player"));
                previewPlayer_->SetCollisionMask(CollisionCategories::Get("enemy"));
                enemy->SetCollisionAttribute(CollisionCategories::Get("enemy"));
                enemy->SetCollisionMask(CollisionCategories::Get("player"));
                previewPlayer_->OnCollision(enemy);
                previewPlayer_->SetCollisionAttribute(playerAttribute);
                previewPlayer_->SetCollisionMask(playerMask);
                enemy->SetCollisionAttribute(enemyAttribute);
                enemy->SetCollisionMask(enemyMask);
            };

            resolveBodyHit(previewEnemy_);

            for (engine::Object3d* object : spawnedPreviewObjects_) {
                resolveBodyHit(dynamic_cast<BaseEnemy*>(object));
            }
        }
    }

    if (previewScene_) {
        if (engine::ParticleSystem* particles = previewScene_->GetParticleSystem()) {
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
    ApplySealBarrageMovieCamera();
    MarkPreviewFamilyEditorInternal();
    RecordTimelineSample();
}

void EnemyAttackPreviewWindow::ApplySealBarrageMovieCamera() {
    if (!IsSealBarragePreview() || !sealBarrageMovieCameraPreview_ || !previewEnemy_ || !previewTarget_)
        return;

    // Reveal と詠唱を省略しない。実戦と同じ失敗確定時刻からカメラを切り替えます。
    const float movieTime = elapsedTime_ - 1.25f - CrownFinaleTuning::Get().sealCastTime;
    if (movieTime < 0.0f || movieTime >= CrownFinaleTuning::Get().sealBarrageMovieDuration)
        return;
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera)
        return;
    const auto& tuning = CrownFinaleTuning::Get();
    const CrownBarrageCameraPose pose = EvaluateCrownBarrageCamera(
        movieTime, previewEnemy_->GetWorldPosition(), previewTarget_->GetWorldPosition(), tuning);
    const engine::Vector3 direction = pose.target - pose.eye;
    const float horizontal = std::sqrt(direction.x * direction.x + direction.z * direction.z);
    camera->SetFollowTarget(nullptr);
    camera->SetEye(pose.eye);
    camera->SetTarget(pose.target);
    camera->SetRotation({-std::atan2(direction.y, horizontal), std::atan2(direction.x, direction.z), 0.0f});
    camera->SetFovY(pose.fov);
    camera->Update();
}

void EnemyAttackPreviewWindow::ResolvePreviewProjectileHits() {
    if (!previewPlayer_ || previewPlayer_->isDead) {
        return;
    }

    const engine::Vector3 targetPosition = previewPlayer_->GetTranslate();

    for (const auto& bulletPtr : BulletManager::GetInstance()->GetBullets()) {
        Bullet* bullet = bulletPtr.get();
        if (!bullet || bullet->IsDead() ||
            !(bullet->GetCollisionAttribute() & CollisionCategories::Get("enemy_attack")) ||
            !(bullet->GetCollisionMask() & CollisionCategories::Get("player"))) {
            continue;
        }

        const engine::Vector3 difference = targetPosition - bullet->GetTranslate();

        // Bullet::UpdateのSphereCastと同じ実Colliderで命中を確認します。
        // 原点中心の簡易球だと、中心がずれた標的の手前で弾だけが停止し、ダメージが発生しません。
        // 標的のmask=0は隔離用のため、Object3d::CheckCollisionによる応答除外は適用しません。
        if (!bullet->GetCollider() || !previewPlayer_->GetCollider() ||
            !bullet->GetCollider()->CheckCollision(previewPlayer_->GetCollider()).isColliding) {
            continue;
        }

        engine::Vector3 hitDirection = bullet->GetVelocity();
        hitDirection.y = 0.0f;

        if (engine::Math::Length(hitDirection) <= 0.001f) {
            hitDirection = difference;
            hitDirection.y = 0.0f;
        }

        if (engine::Math::Length(hitDirection) <= 0.001f) {
            hitDirection = { 0.0f, 0.0f, 1.0f };
        }

        hitDirection = engine::Math::Normalize(hitDirection);

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

    engine::Vector3 direction = previewPlayer_->GetTranslate() - previewEnemy_->GetTranslate();
    direction.y = 0.0f;

    if (engine::Math::Length(direction) <= 0.001f) {
        direction = { 0.0f, 0.0f, 1.0f };
    }

    direction = engine::Math::Normalize(direction);

    const EnemyAttackDefinition* attack = GetSelectedAttackDefinition();
    DamageEvent event;
    event.target = previewPlayer_;
    event.attacker = previewEnemy_;
    event.damageAmount = attack ? attack->damage : 1.0f;
    event.damageType = GetPreviewDamageType();
    event.statusEffect = GetPreviewStatusEffect();

    if (attack && engine::Math::Length(attack->knockbackVelocity) > 0.001f) {
        const engine::Vector3 right = { direction.z, 0.0f, -direction.x };
        event.knockbackVelocity =
            right * attack->knockbackVelocity.x +
            engine::Vector3{ 0.0f, attack->knockbackVelocity.y, 0.0f } +
            direction * attack->knockbackVelocity.z;
    } else {
        // Existing profiles keep the legacy preview knockback.
        event.knockbackVelocity = { direction.x * 9.5f, 4.2f, direction.z * 9.5f };
    }

    event.invincibilityDuration = attack && attack->invincibilityDuration > 0.0f
        ? attack->invincibilityDuration
        : 1.0f;
    EventManager::GetInstance()->Dispatch(event);
}

DamageType EnemyAttackPreviewWindow::GetPreviewDamageType() const {
    // 観測対象の属性から、ダミーへの命中試験で使うダメージ種別を選ぶ。
    const auto* descriptor = GetPreviewDescriptor();
    return descriptor ? descriptor->damageType : DamageType::Physical;
}

StatusEffectApplication EnemyAttackPreviewWindow::GetPreviewStatusEffect() const {
    // 選択中の攻撃定義に状態異常がある場合だけ、持続時間と VFX を再現する。
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
    // 実際のプレビュー対象から速度・高さ・変形量・演出数を取得し、同時刻の標本は置換する。
    engine::Object3d* actor = playerCopyMode_ ? static_cast<engine::Object3d*>(previewPlayer_) : static_cast<engine::Object3d*>(previewEnemy_);

    if (!actor) {
        return;
    }

    const engine::Vector3 velocity = playerCopyMode_ ? previewPlayer_->GetVelocity() : previewEnemy_->GetVelocity();
    const engine::Vector3 position = actor->GetTranslate();
    const float groundY = EffectPreviewStage::GetInstance()->GetGroundPosition().y;

    TimelineSample sample;
    sample.time = elapsedTime_;
    sample.speed = std::sqrt(
        velocity.x * velocity.x +
        velocity.y * velocity.y +
        velocity.z * velocity.z);
    sample.height = (std::max)(0.0f, position.y - groundY);
    engine::Vector3 scaleMultiplier = {
        std::abs(previewBaseScale_.x) > 0.0001f ? std::abs(actor->GetScale().x / previewBaseScale_.x) : 1.0f,
        std::abs(previewBaseScale_.y) > 0.0001f ? std::abs(actor->GetScale().y / previewBaseScale_.y) : 1.0f,
        std::abs(previewBaseScale_.z) > 0.0001f ? std::abs(actor->GetScale().z / previewBaseScale_.z) : 1.0f
    };

    if (const MeshRenderer* renderer = actor->GetMeshRenderer()) {
        const engine::Vector3& visualScale = renderer->GetVisualScale();
        scaleMultiplier.x *= std::abs(visualScale.x);
        scaleMultiplier.y *= std::abs(visualScale.y);
        scaleMultiplier.z *= std::abs(visualScale.z);
    }

    sample.deformation = (std::max)({
        std::abs(scaleMultiplier.x - 1.0f),
        std::abs(scaleMultiplier.y - 1.0f),
        std::abs(scaleMultiplier.z - 1.0f)
    });
    sample.hitReaction = previewEnemy_ ? previewEnemy_->GetDamageReactionWeight() : 0.0f;
    sample.gpuParticleSystems = static_cast<float>(GPUParticleManager::GetInstance()->GetActiveSystemCount());

    if (previewScene_) {
        if (const engine::ParticleSystem* particles = previewScene_->GetParticleSystem()) {
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
    // 観測対象の種類ごとに実行中の状態を取得し、タイムラインの段階名へ使う。
    if (playerCopyMode_ && previewPlayer_)
        return previewPlayer_->GetCopyAbilityStateLabel();
    if (previewSequenceCompleted_ && !previewEnemy_)
        return "分裂後";
    const auto* descriptor = GetPreviewDescriptor();
    if (previewEnemy_ && descriptor && descriptor->phaseName)
        if (const char* label = descriptor->phaseName(*previewEnemy_)) return label;

    return previewEnemy_ ? "攻撃シミュレーション" : "未生成";
}

bool EnemyAttackPreviewWindow::IsGiantHookSplitPreview() const {
    // 巨大スライムの分裂プレビューは通常攻撃と距離・再生長が異なる。
    return !playerCopyMode_ && GetPreviewOption() &&
        (previewOptionId_ == "hook_split_ground" || previewOptionId_ == "hook_split_air");
}

void EnemyAttackPreviewWindow::MaintainGrounding(BaseEnemy* enemy, float groundY) {
    // プレビュー地面を下回ったときだけ着地させ、上昇中は空中状態を保つ。
    if (!enemy) {
        return;
    }

    engine::Vector3 position = enemy->GetTranslate();
    engine::Vector3 velocity = enemy->GetVelocity();

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
    // 標的距離の操作をシーン内のダミーへ反映し、実プレイヤー型でも移動入力を止める。
    if (playerCopyMode_)
        return;
    if (!previewTarget_) {
        return;
    }

    const engine::Vector3 ground = EffectPreviewStage::GetInstance()->GetGroundPosition();
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
    // 観測用 Object を保存・選択対象から外し、必要な衝突属性だけ残す。
    if (!previewScene_) {
        return;
    }

    for (const auto& object : previewScene_->GetObjects()) {
        if (!HasPreviewPrefix(object.get())) {
            continue;
        }

        object->SetEditorInternal(true);
        object->SetIsLocked(true);
        const uint32_t attribute = object.get() == previewPlayer_ ? CollisionCategories::Get("player")
            : (playerCopyMode_ && object.get() == previewEnemy_ ? CollisionCategories::Get("enemy") : 0);
        object->SetCollisionAttribute(attribute);
        object->SetCollisionMask(0);
    }
}

void EnemyAttackPreviewWindow::ConfigureEnemy(BaseEnemy* enemy) {
    // 本番用の敵型を使いながら、編集用の名前・判定・標的へ差し替える。
    if (!enemy) {
        return;
    }

    const engine::Vector3 ground = EffectPreviewStage::GetInstance()->GetGroundPosition();
    PreparePreviewMetadata(*enemy, std::string(kPreviewEnemyName) + "_" + GetSelectedEnemyType());
    if (useDraftProfile_) enemy->SetEditorPreviewProfile(draftProfile_);
    enemy->SetTranslate(ground);
    enemy->SetVelocity({ 0.0f, 0.0f, 0.0f });
    enemy->SetGrounded(true);
    enemy->SetDetectionRange(30.0f);

    // Profileの攻撃IDと追加確認IDを、ゲーム側で登録した実際の敵APIへ渡します。
    const auto* descriptor = GetPreviewDescriptor();
    const auto* attack = GetSelectedAttackDefinition();
    const auto* option = GetPreviewOption();
    if (descriptor && !descriptor->selectAttack(*enemy,
        option ? option->id : (attack ? attack->id : std::string{})))
        profileStatus_ = "敵とプレビュー登録の型が一致しません。";

    if (descriptor && descriptor->prepare) descriptor->prepare(*enemy, GetEnemyPreviewContext());
    ConnectPreviewSpawns(*enemy);
}

EnemyAttackPreviewContext EnemyAttackPreviewWindow::GetEnemyPreviewContext() const {
    return {previewTarget_, previewScene_ ? previewScene_->GetParticleSystem() : nullptr,
        EffectPreviewStage::GetInstance()->GetGroundPosition(), previewOptionId_, prismChantOnly_};
}

void EnemyAttackPreviewWindow::PreparePreviewMetadata(BaseEnemy& enemy, const std::string& name) {
    enemy.SetName(name);
    enemy.SetClassName("EditorOnly");
    enemy.SetEditorInternal(true);
    enemy.SetIsLocked(true);
    enemy.SetCollisionAttribute(0);
    enemy.SetCollisionMask(0);
    enemy.SetTarget(previewTarget_);
}

void EnemyAttackPreviewWindow::ConnectPreviewSpawns(BaseEnemy& enemy) {
    if (const auto* entry = EnemyAttackPreviewRegistry::Find(enemy.GetEnemyType()); entry && entry->connectSpawns)
        entry->connectSpawns(enemy, MakePreviewSpawnCallback());
}

std::function<void(std::unique_ptr<BaseEnemy>)> EnemyAttackPreviewWindow::MakePreviewSpawnCallback() {
    const std::weak_ptr<SpawnSession> session = spawnSession_;
    return [this, session](std::unique_ptr<BaseEnemy> enemy) {
        // トークン失効後はthisもSceneも参照しません。停止後の古いコールバックを安全に捨てます。
        if (session.expired()) return;
        AddPreviewSpawn(std::move(enemy));
    };
}

void EnemyAttackPreviewWindow::AddPreviewSpawn(std::unique_ptr<BaseEnemy> enemy) {
    if (!enemy || !spawnSession_ || !sceneManager_ || sceneManager_->GetCurrentScene() != previewScene_) return;
    PreparePreviewMetadata(*enemy, std::string(kPreviewNamePrefix) + "Spawned_" + std::to_string(spawnedPreviewObjects_.size()));
    ConnectPreviewSpawns(*enemy);
    auto* raw = enemy.get();
    spawnedPreviewObjects_.push_back(raw);
    previewScene_->AddObject(std::move(enemy));
}

void EnemyAttackPreviewWindow::ApplyRecommendedSettings() {
    // 選択攻撃の推奨距離と長さを UI の初期値に戻す。
    targetDistance_ = GetRecommendedTargetDistance();
    loopDuration_ = GetRecommendedLoopDuration();
}

void EnemyAttackPreviewWindow::LoadSelectedProfile() {
    // 攻撃の切り替え時は Undo と下書き選択を初期化し、最新の JSON を読む。
    authoringUndo_.clear();
    authoringRedo_.clear();
    authoringGestureOpen_ = false;
    profileAttackIndex_ = 0;
    previewOptionId_.clear();
    if (playerCopyMode_) {
        const auto* copy = GetCopyDescriptor();
        if (copy && !copy->FindAction(copyActionId_)) copyActionId_ = copy->actions.front().id;
    }
    const std::string enemyType = GetSelectedEnemyType();
    EnemyAttackProfile::InvalidateCache(enemyType);
    const bool loaded = editableProfile_.LoadForEnemy(enemyType, &profileStatus_);

    if (playerCopyMode_) {
        for (int i = 0; i < static_cast<int>(editableProfile_.attacks.size()); ++i) {
            if (editableProfile_.attacks[i].sequence.enabled && editableProfile_.attacks[i].sequence.binding == "player_special") {
                profileAttackIndex_ = i;
                break;
            }
        }
    }

    if (editableProfile_.attacks.empty()) {
        profileStatus_ = enemyType + " は攻撃プロファイル編集の対象外です。";
    } else if (loaded) {
        profileStatus_ = "読み込みました: " + EnemyAttackProfile::GetDefaultPath(enemyType);
    }

    profileDirty_ = false;
}

void EnemyAttackPreviewWindow::SaveSelectedProfile(bool restartPreview) {
    if (editableProfile_.attacks.empty()) {
        return;
    }

    // 調整中の不正なキーを補正で隠さず、保存前に問題を表示します。
    for (const auto& attack : editableProfile_.attacks) {
        if (!attack.sequence.Validate(&profileStatus_))
            return;
        if (!attack.sequence.enabled)
            continue;
        for (const auto& track : attack.sequence.tracks) {
            if (!track.enabled || track.type != "body_animation")
                continue;
            BodyAnimationClip clip;

            if (!clip.Load(BodyAnimationClip::ResolveAssetPath(track.preset))) {
                profileStatus_ = "本体アニメーションを読み込めません: " + track.preset;
                return;
            }
        }
    }

    if (const auto* selected = GetSelectedAttackDefinition(); selected && !selected->sequence.assetPath.empty()) {
        if (!EnemyAttackProfile::SaveSequenceAsset(selected->sequence.assetPath, selected->sequence, &profileStatus_))
            return;
        EnemyAttackProfile::InvalidateCache();
    }

    editableProfile_.Sanitize();
    const std::string path = EnemyAttackProfile::GetDefaultPath(editableProfile_.enemyType);

    if (editableProfile_.SaveToFile(path, &profileStatus_)) {
        EnemyAttackProfile::InvalidateCache(editableProfile_.enemyType);
        profileStatus_ = "保存しました: " + path;
        profileDirty_ = false;
        ApplyRecommendedSettings();

        if (enabled_ && restartPreview) {
            useDraftProfile_ = false;
            draftRefreshPending_ = false;
            RestartPreview();
        }
    }
}

void EnemyAttackPreviewWindow::ResetSelectedProfile() {
    // 既定値への復帰は編集メモリ内だけで行い、保存ボタンまで JSON を触らない。
    editableProfile_ = EnemyAttackProfile::CreateDefault(GetSelectedEnemyType());
    profileAttackIndex_ = 0;
    profileDirty_ = !editableProfile_.attacks.empty();
    profileStatus_ = "既定値へ戻しました。保存するまでJSONには反映されません。";
    ApplyRecommendedSettings();
}

const EnemyAttackDefinition* EnemyAttackPreviewWindow::GetSelectedAttackDefinition() const {
    // 選択添字を有効範囲へ収め、王冠専用プレビューでは通常攻撃を返さない。
    const auto* option = GetPreviewOption();
    if ((option && option->suppressProfile) || editableProfile_.attacks.empty()) {
        return nullptr;
    }

    const int clampedIndex = std::clamp(profileAttackIndex_, 0, static_cast<int>(editableProfile_.attacks.size()) - 1);
    return &editableProfile_.attacks[clampedIndex];
}

EnemyAttackDefinition* EnemyAttackPreviewWindow::GetSelectedAttackDefinition() {
    // 読み取り版と同じ境界確認を使い、編集可能な参照へ変換する。
    return const_cast<EnemyAttackDefinition*>(static_cast<const EnemyAttackPreviewWindow*>(this)->GetSelectedAttackDefinition());
}

float EnemyAttackPreviewWindow::GetRecommendedTargetDistance() const {
    // 追加確認項目、保存済みProfile、登録時の既定値の順に採用します。
    if (const auto* option = GetPreviewOption()) return option->targetDistance;
    if (const auto* attack = GetSelectedAttackDefinition()) return attack->recommendedTargetDistance;
    const auto* descriptor = GetPreviewDescriptor();
    return descriptor ? descriptor->targetDistance : 6.0f;
}

float EnemyAttackPreviewWindow::GetRecommendedLoopDuration() const {
    if (const auto* option = GetPreviewOption()) return option->duration();
    if (const auto* attack = GetSelectedAttackDefinition()) return attack->previewDuration;
    const auto* descriptor = GetPreviewDescriptor();
    return descriptor ? descriptor->duration : 4.6f;
}

const EnemyAttackPreviewDescriptor* EnemyAttackPreviewWindow::GetPreviewDescriptor() const {
    return EnemyAttackPreviewRegistry::Find(GetSelectedEnemyType());
}

const EnemyAttackPreviewOption* EnemyAttackPreviewWindow::GetPreviewOption() const {
    return playerCopyMode_ ? nullptr : EnemyAttackPreviewRegistry::FindOption(enemyType_, previewOptionId_);
}

bool EnemyAttackPreviewWindow::IsSealBarragePreview() const {
    return GetPreviewOption() && previewOptionId_ == "seal_barrage_preview";
}

bool EnemyAttackPreviewWindow::IsAirHookSplitPreview() const {
    return GetPreviewOption() && previewOptionId_ == "hook_split_air";
}

const char* EnemyAttackPreviewWindow::GetSelectedEnemyType() const {
    // コピー技の候補と敵攻撃の候補で別の一覧を使う。
    if (playerCopyMode_) {
        const auto* copy = GetCopyDescriptor();
        return copy ? copy->profileType.c_str() : "Slime";
    }

    return enemyType_.c_str();
}

bool EnemyAttackPreviewWindow::IsCurrentSceneReady() const {
    // 現シーンと描画基盤が揃うまで観測用 Object を生成しない。
    return sceneManager_ && sceneManager_->GetCurrentScene() && sceneManager_->GetCurrentScene()->GetObject3dCommon();
}

float EnemyAttackPreviewWindow::GetCrownMovieDuration() const {
    const auto* game=sceneManager_?dynamic_cast<GamePlayScene*>(sceneManager_->GetCurrentScene()):nullptr;
    return game ? (crownDefeatMode_ ? game->GetCrownDefeatDuration() : game->GetCrownFinaleTiming().movieDuration)
                : (crownDefeatMode_ ? 12.0f : 18.0f);
}

const CopyAbilityPreviewDescriptor* EnemyAttackPreviewWindow::GetCopyDescriptor() const {
    return CopyAbilityPreviewRegistry::Find(copyType_);
}

const CopyAbilityPreviewAction* EnemyAttackPreviewWindow::GetCopyAction() const {
    const auto* copy = GetCopyDescriptor();
    return copy ? copy->FindAction(copyActionId_) : nullptr;
}

void EnemyAttackPreviewWindow::SelectCopyProfileAction() {
    const auto* copy = GetCopyDescriptor();
    if (copy && copy->FindAction(copy->profileActionId)) copyActionId_ = copy->profileActionId;
}

void EnemyAttackPreviewWindow::RefreshStatusPreview() {
    statusRefreshPending_ = false;
    if (!enabled_ || !IsCurrentSceneReady() || sceneManager_->IsPlaying()) return;
    const bool wasPlaying = playing_;
    // 初期値から再生成し、ダミーの耐久値やコピー状態を通常Sceneの一括適用から保護します。
    RestartPreview();
    playing_ = wasPlaying;
    ApplyEffectPlaybackState();
}

void EnemyAttackPreviewWindow::SelectStatusTarget(const std::string& type) {
    StopPreview();
    statusTab_ = true;
    SetPlayerCopyMode(type.empty());
    if (!type.empty()) {
        enemyType_ = type;
        LoadSelectedProfile();
        ApplyRecommendedSettings();
    }
    authoringActorSelected_ = false;
    selectedTrack_ = -1;
}
