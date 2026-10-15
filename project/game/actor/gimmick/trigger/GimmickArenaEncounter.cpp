#define NOMINMAX
#include "GimmickArenaEncounter.h"

#include "BaseScene.h"
#include "CollisionConfig.h"
#include "game/actor/enemy/boss/prism/EnemyPrismSlime.h"
#include "BaseEnemy.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.h"
#include "game/scene/gameplay/GamePlayScene.h"
#include "Player.h"
#include "SceneManager.h"
#include "VFXSequencer.h"
#include "GameDataManager.h"
#include "StageManager.h"

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
} // namespace

void GimmickArenaEncounter::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    // 入場範囲はプレイヤー専用のトリガーとし、ボスや壁はイベント ID で参照します。
    SetClassName("Gimmick");
    SetGimmickType("ArenaEncounter");
    SetName("Gimmick_ArenaEncounter");
    SetStatic(false);
    SetCastShadow(false);
    SetCollisionAttribute(CollisionCategories::Get("trigger"));
    SetCollisionMask(CollisionCategories::Get("player"));

    engine::Object3d::ColliderConfig collider;
    collider.type = ColliderType::kOBB;
    collider.center = {0.0f, 1.5f, 0.0f};
    collider.size = {3.0f, 3.0f, 8.0f};
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
    // 編集中は進行を巻き戻し、再生中だけ封鎖からボス登場までの猶予を数えます。
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

void GimmickArenaEncounter::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!sceneManager || !sceneManager->IsPlaying()) {
        // 編集中だけ進入範囲を可視化し、ゲーム中は判定とイベント接続だけを残します。
        BaseGimmick::Draw(pointLightResource, spotLightResource);
    }
}

bool GimmickArenaEncounter::OnCollision(engine::Object3d* other) {
    // 待機中に有効な入場範囲へプレイヤーが触れた時だけ戦闘を開始します。
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

std::unique_ptr<engine::Object3d> GimmickArenaEncounter::Clone() const {
    // 進行状態は持ち込まず、配置とイベント設定を複製します。
    auto clone = std::make_unique<GimmickArenaEncounter>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    return clone;
}

void GimmickArenaEncounter::InitializeForPlay() {
    // 戦闘開始前はボスと隔離壁を停止し、報酬も隠しておきます。
    initializedForPlay_ = true;
    state_ = State::Waiting;
    stateTimer_ = 0.0f;
    SetCollisionAttribute(CollisionCategories::Get("trigger"));
    SetCollisionMask(CollisionCategories::Get("player"));
    SetBossActive(false);
    SetBarriersActive(false);
    if (IsHighCrownMode()) {
        SetRewardActive(false);
    }
}

void GimmickArenaEncounter::ResetForEditor() {
    // 戦闘後の衝突停止を編集画面へ持ち越さないようにします。
    initializedForPlay_ = false;
    state_ = State::Waiting;
    stateTimer_ = 0.0f;
    SetIsVisible(true);
    SetCollisionAttribute(CollisionCategories::Get("trigger"));
    SetCollisionMask(CollisionCategories::Get("player"));
}

// 入場時にボスと隔離壁を有効化し、撃破後の報酬と解放までを一つの進行として管理します。
void GimmickArenaEncounter::BeginEncounter() {
    // 入場判定を即時に外し、同じフレームの重複開始を防ぎます。
    state_ = State::Sealing;
    stateTimer_ = 0.0f;
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetBarriersActive(true);
    if (!IsHighCrownMode()) {
        VFXSequencer::PlayOneShot(IsMagmaMode() ? kMagmaSealSequence : kSealSequence, GetWorldPosition(),
                                  {4.0f, 1.0f, 4.0f});
    }
    StartBossPresentation();
}

void GimmickArenaEncounter::CompleteEncounter() {
    // 通常ボスも同じステージのチェックポイント復帰では撃破状態を引き継ぐ。
    // 最終ボスの報酬演出は別経路で管理するため、この一時記録には含めない。
    if (!IsHighCrownMode())
        GameDataManager::GetInstance()->RememberCheckpointEncounter(StageManager::GetInstance()->GetCurrentStageIndex(),
                                                                    GetEventID());
    state_ = State::Cleared;
    stateTimer_ = 0.0f;
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    if (IsHighCrownMode()) {
        SetRewardActive(true);
    } else {
        SetBarriersActive(false);
    }
    engine::Vector3 cuePosition = GetWorldPosition();
    if (IsHighCrownMode()) {
        BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
        const int rewardEventID = GetTargetID() + GetBarrierCount() + 1;
        if (scene) {
            if (engine::Object3d* reward = scene->FindObjectByEventID(rewardEventID)) {
                cuePosition = reward->GetTranslate();
                if (auto* gameplay = dynamic_cast<GamePlayScene*>(scene)) {
                    gameplay->StartArenaBossDefeatReward(reward);
                }
            }
        }
    }
    VFXSequencer::PlayOneShot(
        IsHighCrownMode() ? kHighCrownReleaseSequence : (IsMagmaMode() ? kMagmaReleaseSequence : kReleaseSequence),
        cuePosition, IsHighCrownMode() ? engine::Vector3{1.25f, 1.25f, 1.25f} : engine::Vector3{4.0f, 1.0f, 4.0f});
}

bool GimmickArenaEncounter::IsBossDefeated() const {
    // 最終撃破演出の保留中は HP がゼロでも解放を始めません。
    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    if (!scene || GetTargetID() <= 0) {
        return false;
    }

    auto* boss = dynamic_cast<BaseEnemy*>(scene->FindObjectByEventID(GetTargetID()));
    if (!boss) {
        return false;
    }
    if (auto* king=dynamic_cast<EnemyFalseKingSlime*>(boss); king && king->IsFinalDefeatPending()) return false;
    const float hp = boss->param_.has_value() ? boss->param_->hp : 1.0f;
    return boss->isDead || hp <= 0.0f;
}

void GimmickArenaEncounter::SetBossActive(bool active) {
    // Target ID が指すボスへ有効状態を送ります。
    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    if (!scene || GetTargetID() <= 0) {
        return;
    }
    scene->SetEventActive(GetTargetID(), active);
}

void GimmickArenaEncounter::SetBarriersActive(bool active) {
    // ボスの後続 ID を隔離壁の連番として扱います。
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
    // 報酬はボスと壁の連番の直後に配置されます。
    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    if (!scene || GetTargetID() <= 0) {
        return;
    }
    const int rewardEventID = GetTargetID() + GetBarrierCount() + 1;
    scene->SetEventActive(rewardEventID, active);
}

void GimmickArenaEncounter::StartBossPresentation() {
    // 高冠戦だけはゲームシーンの専用登場演出へ接続します。
    if (!IsHighCrownMode()) {
        return;
    }
    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    auto* gameplay = dynamic_cast<GamePlayScene*>(scene);
    if (!gameplay) {
        return;
    }
    engine::Object3d* boss = scene->FindObjectByEventID(GetTargetID());
    engine::Object3d* gate = scene->FindObjectByEventID(GetTargetID() + 1);
    gameplay->StartArenaBossIntro(boss, gate, GetBossRevealDelay());
}

float GimmickArenaEncounter::GetBossRevealDelay() const {
    // 設定が短すぎても封鎖演出を見せる時間を確保します。
    return param_.has_value() ? (std::max)(0.05f, param_->shakeDuration) : kDefaultBossRevealDelay;
}

int GimmickArenaEncounter::GetBarrierCount() const {
    // ID の連番探索が暴走しないよう壁数に上限を設けます。
    return param_.has_value() ? (std::clamp)(param_->maxCount, 1, 16) : kDefaultBarrierCount;
}

bool GimmickArenaEncounter::IsHighCrownMode() const {
    // 高冠戦だけ報酬や登場の進行を切り替えます。
    return param_.has_value() && param_->actionMode == kHighCrownActionMode;
}

bool GimmickArenaEncounter::IsMagmaMode() const {
    // 配置先ボスの型から封鎖・解放 VFX を選びます。
    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    const engine::Object3d* boss = scene && GetTargetID() > 0 ? scene->FindObjectByEventID(GetTargetID()) : nullptr;
    return boss && boss->GetEnemyType() == "MagmaSlime";
}

void GimmickArenaEncounter::RestoreCheckpointClear() {
    // チェックポイント復帰後は戦闘を再発火させず、壁を開いた状態へ戻します。
    initializedForPlay_ = true;
    state_ = State::Cleared;
    stateTimer_ = 0.0f;
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetBossActive(false);
    SetBarriersActive(false);
    if (IsHighCrownMode())
        SetRewardActive(true);
}
