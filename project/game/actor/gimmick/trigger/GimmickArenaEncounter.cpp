#define NOMINMAX
#include "GimmickArenaEncounter.h"
#include "game/actor/enemy/boss/core/MidbossEncounterEffects.h"
#include "ObjectPropertyDefinition.h"

#include "BaseScene.h"
#include "CollisionConfig.h"
#include "game/actor/enemy/boss/prism/EnemyPrismSlime.h"
#include "BaseEnemy.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.h"
#include "game/scene/gameplay/GamePlayScene.h"
#include "Player.h"
#include "SceneManager.h"
#include "VfxSequencer.h"
#include "game/system/progress/GameDataManager.h"
#include "game/system/progress/StageManager.h"

#include <algorithm>
#include <cassert>

namespace {
constexpr float kDefaultBossRevealDelay = 0.72f;
constexpr int kDefaultBarrierCount = 4;
constexpr int kHighCrownActionMode = 2;
constexpr const char* kHighCrownReleaseSequence = "false_king_defeat_reward_cue";
} // namespace

void GimmickArenaEncounter::Initialize(engine::Object3DCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    // 入場範囲はプレイヤー専用のトリガーとし、ボスや壁はイベント ID で参照します。
    SetClassName("Gimmick");
    SetGimmickType("ArenaEncounter");
    SetName("Gimmick_ArenaEncounter");
    SetStatic(false);
    SetCastShadow(false);
    SetCollisionAttribute(CollisionCategories::Get("trigger"));
    SetCollisionMask(CollisionCategories::Get("player"));

    engine::Object3D::ColliderConfig collider;
    collider.type = ColliderType::Obb;
    collider.center = {0.0f, 1.5f, 0.0f};
    collider.size = {3.0f, 3.0f, 8.0f};
    SetColliderConfig(collider);

    if (!GetParameters().has_value()) {
        ResetParameters();
    }

    SetParameter(&engine::EntityParameter::gimmickType, "ArenaEncounter");
    SetParameter(&engine::EntityParameter::maxCount, kDefaultBarrierCount);
    SetParameter(&engine::EntityParameter::shakeDuration, kDefaultBossRevealDelay);
    SetParameter(&engine::EntityParameter::startActive, true);
    SetParameter(&engine::EntityParameter::returnOnOff, false);

    // 編集項目の宣言です。動作処理と旧paramの保存キーは変更しません。
    using Self = GimmickArenaEncounter;
    engine::ObjectPropertyDefinition<Self> properties(*this, kTypeId, "中ボス遭遇管理",
        "Target IDは中ボスのMy Event ID、My Event IDは撃破通知の受信先です。障壁はTarget ID + 1から連番で指定します。");
    using Parameter = engine::Object3D::EntityParameter;
    properties.Parameter("障壁数", "maxCount", &Parameter::maxCount, 1.0f, 16.0f);
    properties.Parameter("ボス出現まで", "shakeDuration", &Parameter::shakeDuration, 0.05f, 3.0f);
    properties.Parameter("遭遇を有効にする", "startActive", &Parameter::startActive);
    properties.Parameter("登場演出JSON", "cinematicPath", &Parameter::cinematicPath, 0.0f, 0.0f,
        nullptr, {}, {.assetDirectory = "Resources/json/scenario", .assetEditor = EditorPropertyAssetEditor::Scenario});
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

        if (introTimelineActive_) {
            auto* gameplay = dynamic_cast<GamePlayScene*>(sceneManager->GetCurrentScene());
            if (!gameplay || !gameplay->IsArenaBossIntroActive()) {
                introTimelineActive_ = false;
            }
        }

        if (!introTimelineActive_ && stateTimer_ >= GetBossRevealDelay()) {
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

bool GimmickArenaEncounter::OnCollision(engine::Object3D* other) {
    // 待機中に有効な入場範囲へプレイヤーが触れた時だけ戦闘を開始します。
    if (state_ != State::Waiting || !other || other->GetClassName() != "Player") {
        return false;
    }

    if (!GetParameters().has_value() || !GetParameters()->startActive) {
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

std::unique_ptr<engine::Object3D> GimmickArenaEncounter::Clone() const {
    // 進行状態は持ち込まず、配置とイベント設定を複製します。
    auto clone = std::make_unique<GimmickArenaEncounter>();
    assert(GetObjectCommon() != nullptr);
    clone->Initialize(GetObjectCommon(), GetModelName());
    clone->CopyFrom(this);
    return clone;
}

void GimmickArenaEncounter::InitializeForPlay() {
    // 戦闘開始前はボスと隔離壁を停止し、報酬も隠しておきます。
    initializedForPlay_ = true;
    state_ = State::Waiting;
    stateTimer_ = 0.0f;
    introTimelineActive_ = false;
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
    introTimelineActive_ = false;
    SetIsVisible(true);
    SetCollisionAttribute(CollisionCategories::Get("trigger"));
    SetCollisionMask(CollisionCategories::Get("player"));
}

// 入場時にボスと隔離壁を有効化し、撃破後の報酬と解放までを一つの進行として管理します。
void GimmickArenaEncounter::BeginEncounter() {
    // 入場判定を即時に外し、同じフレームの重複開始を防ぎます。
    state_ = State::Sealing;
    stateTimer_ = 0.0f;
    introTimelineActive_ = false;
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetBarriersActive(true);

    if (!IsHighCrownMode()) {
        const auto& effects = IsMagmaMode() ? MidbossEncounterEffects::Magma() : MidbossEncounterEffects::Prism();
        VfxSequencer::PlayOneShot(effects.seal, GetWorldPosition(), {4.0f, 1.0f, 4.0f});
    }

    introTimelineActive_ = StartBossPresentation();
}

void GimmickArenaEncounter::CompleteEncounter() {
    // 通常ボスも同じステージのチェックポイント復帰では撃破状態を引き継ぐ。
    // 最終ボスの報酬演出は別経路で管理するため、この一時記録には含めない。
    if (!IsHighCrownMode())
        GameDataManager::GetInstance()->RememberCheckpointEncounter(StageManager::GetInstance()->GetCurrentStageIndex(),
                                                                    GetEventId());
    state_ = State::Cleared;
    stateTimer_ = 0.0f;
    introTimelineActive_ = false;
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
        const int rewardEventId = GetTargetId() + GetBarrierCount() + 1;

        if (scene) {
            if (engine::Object3D* reward = scene->FindObjectByEventId(rewardEventId)) {
                cuePosition = reward->GetTranslate();

                if (auto* gameplay = dynamic_cast<GamePlayScene*>(scene)) {
                    gameplay->StartArenaBossDefeatReward(reward);
                }
            }
        }
    }

    if (IsHighCrownMode()) {
        VfxSequencer::PlayOneShot(kHighCrownReleaseSequence, cuePosition, {1.25f, 1.25f, 1.25f});
    } else {
        const auto& effects = IsMagmaMode() ? MidbossEncounterEffects::Magma() : MidbossEncounterEffects::Prism();
        VfxSequencer::PlayOneShot(effects.release, cuePosition, {4.0f, 1.0f, 4.0f});
    }
}

bool GimmickArenaEncounter::IsBossDefeated() const {
    // 最終撃破演出の保留中は HP がゼロでも解放を始めません。
    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;

    if (!scene || GetTargetId() <= 0) {
        return false;
    }

    auto* boss = dynamic_cast<BaseEnemy*>(scene->FindObjectByEventId(GetTargetId()));

    if (!boss) {
        return false;
    }

    if (auto* king=dynamic_cast<EnemyFalseKingSlime*>(boss); king && king->IsFinalDefeatPending())
        return false;
    const float hp = boss->GetParameters().has_value() ? boss->GetParameters()->hp : 1.0f;
    return boss->IsDead() || hp <= 0.0f;
}

void GimmickArenaEncounter::SetBossActive(bool active) {
    // Target ID が指すボスへ有効状態を送ります。
    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;

    if (!scene || GetTargetId() <= 0) {
        return;
    }

    scene->SetEventActive(GetTargetId(), active);
}

void GimmickArenaEncounter::SetBarriersActive(bool active) {
    // ボスの後続 ID を隔離壁の連番として扱います。
    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;

    if (!scene || GetTargetId() <= 0) {
        return;
    }

    const int firstBarrierEventId = GetTargetId() + 1;

    for (int index = 0; index < GetBarrierCount(); ++index) {
        scene->SetEventActive(firstBarrierEventId + index, active);
    }
}

void GimmickArenaEncounter::SetRewardActive(bool active) {
    // 報酬はボスと壁の連番の直後に配置されます。
    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;

    if (!scene || GetTargetId() <= 0) {
        return;
    }

    const int rewardEventId = GetTargetId() + GetBarrierCount() + 1;
    scene->SetEventActive(rewardEventId, active);
}

bool GimmickArenaEncounter::StartBossPresentation() {
    // 配置Objectの演出JSONを使う。未設定・読み込み失敗時は通常の遅延起動へ戻す。
    if (!IsHighCrownMode() || !GetParameters().has_value() || GetParameters()->cinematicPath.empty()) {
        return false;
    }

    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    auto* gameplay = dynamic_cast<GamePlayScene*>(scene);
    if (!gameplay) {
        return false;
    }

    engine::Object3D* boss = scene->FindObjectByEventId(GetTargetId());
    engine::Object3D* gate = scene->FindObjectByEventId(GetTargetId() + 1);
    return gameplay->StartArenaBossIntro(boss, gate, GetParameters()->cinematicPath);
}

void GimmickArenaEncounter::ActivateBossFromTimeline() {
    // 対象の遭遇が封鎖中のときだけ、DirectorのSignalで戦闘を始める。
    SceneManager* manager = SceneManager::GetInstance();
    if (!manager || !manager->IsPlaying() || !IsHighCrownMode() || state_ != State::Sealing ||
        !introTimelineActive_) {
        return;
    }

    SetBossActive(true);
    state_ = State::Fighting;
    stateTimer_ = 0.0f;
    introTimelineActive_ = false;
}

float GimmickArenaEncounter::GetBossRevealDelay() const {
    // Timelineが使えない場合と通常ボスは、配置時の遅延設定で起動する。
    return GetParameters().has_value() ? (std::max)(0.05f, GetParameters()->shakeDuration) : kDefaultBossRevealDelay;
}

int GimmickArenaEncounter::GetBarrierCount() const {
    // ID の連番探索が暴走しないよう壁数に上限を設けます。
    return GetParameters().has_value() ? (std::clamp)(GetParameters()->maxCount, 1, 16) : kDefaultBarrierCount;
}

bool GimmickArenaEncounter::IsHighCrownMode() const {
    // 高冠戦だけ報酬や登場の進行を切り替えます。
    return GetParameters().has_value() && GetParameters()->actionMode == kHighCrownActionMode;
}

bool GimmickArenaEncounter::IsMagmaMode() const {
    // 配置先ボスの型から封鎖・解放 VFX を選びます。
    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    const engine::Object3D* boss = scene && GetTargetId() > 0 ? scene->FindObjectByEventId(GetTargetId()) : nullptr;
    return boss && boss->GetEnemyType() == "MagmaSlime";
}

void GimmickArenaEncounter::RestoreCheckpointClear() {
    // チェックポイント復帰後は戦闘を再発火させず、壁を開いた状態へ戻します。
    initializedForPlay_ = true;
    state_ = State::Cleared;
    stateTimer_ = 0.0f;
    introTimelineActive_ = false;
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetBossActive(false);
    SetBarriersActive(false);
    if (IsHighCrownMode())
        SetRewardActive(true);
}
