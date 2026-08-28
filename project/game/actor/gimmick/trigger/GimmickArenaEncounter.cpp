#define NOMINMAX
#include "GimmickArenaEncounter.h"

#include "BaseScene.h"
#include "CollisionConfig.h"
#include "EnemyPrismSlime.h"
#include "BaseEnemy.h"
#include "GamePlayScene.h"
#include "Player.h"
#include "SceneManager.h"
#include "VFXSequencer.h"

#include <algorithm>
#include <cassert>

namespace {
constexpr float kDefaultBossRevealDelay = 0.72f;
constexpr int kDefaultBarrierCount = 4;
constexpr const char* kSealSequence = "prism_arena_seal_cue";
constexpr const char* kReleaseSequence = "prism_arena_release_cue";
constexpr const char* kMagmaSealSequence = "magma_arena_seal_cue";
constexpr const char* kMagmaReleaseSequence = "magma_arena_release_cue";
constexpr int kHighCrownActionMode = 2;
constexpr const char* kHighCrownReleaseSequence = "false_king_defeat_reward_cue";
}

void GimmickArenaEncounter::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("ArenaEncounter");
    SetName("Gimmick_ArenaEncounter");
    SetStatic(false);
    SetCastShadow(false);
    SetCollisionAttribute(CollisionAttribute::kTrigger);
    SetCollisionMask(CollisionAttribute::kPlayer);

    Object3d::ColliderConfig collider;
    collider.type = ColliderType::kOBB;
    collider.center = { 0.0f, 1.5f, 0.0f };
    collider.size = { 3.0f, 3.0f, 8.0f };
    SetColliderConfig(collider);

    if (!param_.has_value()) {
        param_.emplace();
    }
    param_->gimmickType = "ArenaEncounter";
    param_->maxCount = kDefaultBarrierCount;
    param_->shakeDuration = kDefaultBossRevealDelay;
    param_->startActive = true;
    param_->returnOnOff = false;
}

void GimmickArenaEncounter::Update(float deltaTime) {
    SceneManager* sceneManager = SceneManager::GetInstance();
    const bool isPlaying = sceneManager && sceneManager->IsPlaying();
    if (!isPlaying) {
        ResetForEditor();
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (!initializedForPlay_) {
        InitializeForPlay();
    }

    if (state_ == State::Sealing) {
        stateTimer_ += (std::max)(0.0f, deltaTime);
        if (stateTimer_ >= GetBossRevealDelay()) {
            SetBossActive(true);
            state_ = State::Fighting;
            stateTimer_ = 0.0f;
        }
    }

    BaseGimmick::Update(deltaTime);
}

void GimmickArenaEncounter::Draw(
    ID3D12Resource* pointLightResource,
    ID3D12Resource* spotLightResource) {
    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!sceneManager || !sceneManager->IsPlaying()) {
        // 編集中だけ進入範囲を可視化し、ゲーム中は判定とイベント接続だけを残します。
        BaseGimmick::Draw(pointLightResource, spotLightResource);
    }
}

bool GimmickArenaEncounter::OnCollision(Object3d* other) {
    if (state_ != State::Waiting || !other || other->GetClassName() != "Player") {
        return false;
    }

    if (!param_.has_value() || !param_->startActive) {
        return false;
    }

    BeginEncounter();
    return true;
}

void GimmickArenaEncounter::OnTrigger() {
    // My Event IDは中ボスの撃破通知専用です。
    // 誤った接触通知が届いても、実HPが残っている間は封鎖を解除しません。
    if ((state_ == State::Sealing || state_ == State::Fighting) && IsBossDefeated()) {
        CompleteEncounter();
    }
}

std::unique_ptr<Object3d> GimmickArenaEncounter::Clone() const {
    auto clone = std::make_unique<GimmickArenaEncounter>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    return clone;
}

void GimmickArenaEncounter::InitializeForPlay() {
    initializedForPlay_ = true;
    state_ = State::Waiting;
    stateTimer_ = 0.0f;
    SetCollisionAttribute(CollisionAttribute::kTrigger);
    SetCollisionMask(CollisionAttribute::kPlayer);
    SetBossActive(false);
    SetBarriersActive(false);
    if (IsHighCrownMode()) {
        SetRewardActive(false);
    }
}

void GimmickArenaEncounter::ResetForEditor() {
    initializedForPlay_ = false;
    state_ = State::Waiting;
    stateTimer_ = 0.0f;
    SetIsVisible(true);
    SetCollisionAttribute(CollisionAttribute::kTrigger);
    SetCollisionMask(CollisionAttribute::kPlayer);
}

void GimmickArenaEncounter::BeginEncounter() {
    state_ = State::Sealing;
    stateTimer_ = 0.0f;
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetBarriersActive(true);
    if (!IsHighCrownMode()) {
        VFXSequencer::PlayOneShot(
            IsMagmaMode() ? kMagmaSealSequence : kSealSequence,
            GetWorldPosition(),
            { 4.0f, 1.0f, 4.0f });
    }
    StartBossPresentation();
}

void GimmickArenaEncounter::CompleteEncounter() {
    state_ = State::Cleared;
    stateTimer_ = 0.0f;
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    if (IsHighCrownMode()) {
        SetRewardActive(true);
    } else {
        SetBarriersActive(false);
    }
    Vector3 cuePosition = GetWorldPosition();
    if (IsHighCrownMode()) {
        BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
        const int rewardEventID = GetTargetID() + GetBarrierCount() + 1;
        if (scene) {
            if (Object3d* reward = scene->FindObjectByEventID(rewardEventID)) {
                cuePosition = reward->GetTranslate();
                if (auto* gameplay = dynamic_cast<GamePlayScene*>(scene)) {
                    gameplay->StartArenaBossDefeatReward(reward);
                }
            }
        }
    }
    VFXSequencer::PlayOneShot(
        IsHighCrownMode()
            ? kHighCrownReleaseSequence
            : (IsMagmaMode() ? kMagmaReleaseSequence : kReleaseSequence),
        cuePosition,
        IsHighCrownMode() ? Vector3{ 1.25f, 1.25f, 1.25f } : Vector3{ 4.0f, 1.0f, 4.0f });
}

bool GimmickArenaEncounter::IsBossDefeated() const {
    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    if (!scene || GetTargetID() <= 0) {
        return false;
    }

    auto* boss = dynamic_cast<BaseEnemy*>(scene->FindObjectByEventID(GetTargetID()));
    if (!boss) {
        return false;
    }
    const float hp = boss->param_.has_value() ? boss->param_->hp : 1.0f;
    return boss->isDead || hp <= 0.0f;
}

void GimmickArenaEncounter::SetBossActive(bool active) {
    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    if (!scene || GetTargetID() <= 0) {
        return;
    }
    scene->SetEventActive(GetTargetID(), active);
}

void GimmickArenaEncounter::SetBarriersActive(bool active) {
    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    if (!scene || GetTargetID() <= 0) {
        return;
    }

    const int firstBarrierEventID = GetTargetID() + 1;
    for (int index = 0; index < GetBarrierCount(); ++index) {
        scene->SetEventActive(firstBarrierEventID + index, active);
    }
}

void GimmickArenaEncounter::SetRewardActive(bool active) {
    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    if (!scene || GetTargetID() <= 0) {
        return;
    }
    const int rewardEventID = GetTargetID() + GetBarrierCount() + 1;
    scene->SetEventActive(rewardEventID, active);
}

void GimmickArenaEncounter::StartBossPresentation() {
    if (!IsHighCrownMode()) {
        return;
    }
    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    auto* gameplay = dynamic_cast<GamePlayScene*>(scene);
    if (!gameplay) {
        return;
    }
    Object3d* boss = scene->FindObjectByEventID(GetTargetID());
    Object3d* gate = scene->FindObjectByEventID(GetTargetID() + 1);
    gameplay->StartArenaBossIntro(boss, gate, GetBossRevealDelay());
}

float GimmickArenaEncounter::GetBossRevealDelay() const {
    return param_.has_value()
        ? (std::max)(0.05f, param_->shakeDuration)
        : kDefaultBossRevealDelay;
}

int GimmickArenaEncounter::GetBarrierCount() const {
    return param_.has_value()
        ? (std::clamp)(param_->maxCount, 1, 16)
        : kDefaultBarrierCount;
}

bool GimmickArenaEncounter::IsHighCrownMode() const {
    return param_.has_value() && param_->actionMode == kHighCrownActionMode;
}

bool GimmickArenaEncounter::IsMagmaMode() const {
    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    const Object3d* boss = scene && GetTargetID() > 0
        ? scene->FindObjectByEventID(GetTargetID())
        : nullptr;
    return boss && boss->GetEnemyType() == "MagmaSlime";
}
