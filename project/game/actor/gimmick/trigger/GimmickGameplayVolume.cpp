#define NOMINMAX
#include "GimmickGameplayVolume.h"

#include "BaseScene.h"
#include "CollisionConfig.h"
#include "GameAudioSettings.h"
#include "Player.h"
#include "SceneManager.h"
#include "VFXSequencer.h"

#include <algorithm>
#include <cassert>

void GimmickGameplayVolume::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("GameplayVolume");
    SetName("Gimmick_GameplayVolume");
    SetStatic(false);
    SetCastShadow(false);
    SetColor({ 0.15f, 0.75f, 1.0f, 0.3f });
    SetBlendMode(BlendMode::kNormal);
    SetScale({ 3.0f, 3.0f, 3.0f });
    SetCollisionAttribute(CollisionAttribute::kTrigger);
    SetCollisionMask(CollisionAttribute::kPlayer);

    Object3d::ColliderConfig collider;
    collider.type = ColliderType::kOBB;
    collider.center = { 0.0f, 0.0f, 0.0f };
    collider.size = { 1.0f, 1.0f, 1.0f };
    SetColliderConfig(collider);

    if (!param_.has_value()) {
        param_.emplace();
    }
    param_->gimmickType = "GameplayVolume";
    param_->volumeMode = static_cast<int>(Mode::Event);
    param_->volumePayload.clear();
    param_->volumeTriggerOnce = true;
    param_->volumeTriggerOnExit = false;
    param_->volumeRearmDelay = 0.0f;
    param_->volumeBlendDuration = 0.75f;
    param_->startActive = true;
    param_->returnOnOff = false;
}

void GimmickGameplayVolume::Update(float deltaTime) {
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

    rearmTimer_ = (std::max)(0.0f, rearmTimer_ - (std::max)(0.0f, deltaTime));
    if (occupied_ && !collisionObserved_) {
        HandleExit(occupant_);
    }
    collisionObserved_ = false;

    BaseGimmick::Update(deltaTime);
}

void GimmickGameplayVolume::Draw(
    ID3D12Resource* pointLightResource,
    ID3D12Resource* spotLightResource) {
    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!sceneManager || !sceneManager->IsPlaying()) {
        BaseGimmick::Draw(pointLightResource, spotLightResource);
    }
}

bool GimmickGameplayVolume::OnCollision(Object3d* other) {
    auto* player = dynamic_cast<Player*>(other);
    if (!player) {
        return false;
    }
    if (!runtimeEnabled_) {
        return false;
    }

    collisionObserved_ = true;
    occupant_ = player;
    if (!occupied_) {
        occupied_ = true;
        HandleEnter(player);
    }
    return false;
}

void GimmickGameplayVolume::OnTrigger() {
    OnSwitchEvent(true);
}

void GimmickGameplayVolume::OnSwitchEvent(bool active) {
    runtimeEnabled_ = active;
    if (!runtimeEnabled_) {
        if (occupied_ && param_.has_value() && !param_->volumeTriggerOnExit && param_->returnOnOff) {
            Deactivate();
        }
        occupied_ = false;
        collisionObserved_ = false;
        occupant_ = nullptr;
    }
}

std::unique_ptr<Object3d> GimmickGameplayVolume::Clone() const {
    auto clone = std::make_unique<GimmickGameplayVolume>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    return clone;
}

void GimmickGameplayVolume::InitializeForPlay() {
    initializedForPlay_ = true;
    runtimeEnabled_ = param_.has_value() ? param_->startActive : true;
    occupied_ = false;
    collisionObserved_ = false;
    triggered_ = false;
    rearmTimer_ = 0.0f;
    occupant_ = nullptr;
    SetIsVisible(false);
    SetCollisionAttribute(CollisionAttribute::kTrigger);
    SetCollisionMask(CollisionAttribute::kPlayer);
}

void GimmickGameplayVolume::ResetForEditor() {
    initializedForPlay_ = false;
    runtimeEnabled_ = true;
    occupied_ = false;
    collisionObserved_ = false;
    triggered_ = false;
    rearmTimer_ = 0.0f;
    occupant_ = nullptr;
    SetIsVisible(true);
    SetCollisionAttribute(CollisionAttribute::kTrigger);
    SetCollisionMask(CollisionAttribute::kPlayer);
}

void GimmickGameplayVolume::HandleEnter(Player* player) {
    if (!param_.has_value() || param_->volumeTriggerOnExit) {
        return;
    }
    Activate(player);
}

void GimmickGameplayVolume::HandleExit(Player* player) {
    if (param_.has_value() && param_->volumeTriggerOnExit) {
        Activate(player);
    } else if (param_.has_value() && param_->returnOnOff) {
        Deactivate();
    }
    occupied_ = false;
    occupant_ = nullptr;
}

void GimmickGameplayVolume::Activate(Player* player) {
    if (!CanActivate() || !param_.has_value()) {
        return;
    }

    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    switch (GetMode()) {
    case Mode::Event:
        if (scene && GetTargetID() > 0) {
            if (param_->returnOnOff && !param_->volumeTriggerOnExit) {
                scene->SetEventActive(GetTargetID(), true);
            } else {
                scene->TriggerEvent(GetTargetID());
            }
        }
        break;
    case Mode::Checkpoint:
        if (player) {
            player->SetRespawnPosition(GetWorldPosition());
        }
        break;
    case Mode::FallDeath:
        if (player && !player->isDead) {
            Vector3 position = player->GetTranslate();
            position.y = -19.0f;
            player->SetTranslate(position);
            player->SetVelocity({ 0.0f, 0.0f, 0.0f });
        }
        break;
    case Mode::FeedbackCue:
        if (!param_->volumePayload.empty()) {
            VFXSequencer::PlayOneShot(param_->volumePayload, GetWorldPosition());
        }
        break;
    case Mode::Bgm:
        if (!param_->volumePayload.empty()) {
            GameAudioSettings::GetInstance()->PlayBGM(param_->volumePayload);
        }
        break;
    case Mode::Environment:
        if (!param_->volumePayload.empty()) {
            previousEnvironment_ =
                LightManager::GetInstance()->CaptureEnvironmentProfile();
            hasPreviousEnvironment_ = true;
            LightManager::GetInstance()->ApplyEnvironmentProfile(
                param_->volumePayload,
                param_->volumeBlendDuration);
        }
        break;
    }

    triggered_ = true;
    rearmTimer_ = (std::max)(0.0f, param_->volumeRearmDelay);
}

void GimmickGameplayVolume::Deactivate() {
    if (!param_.has_value()) {
        return;
    }

    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    if (GetMode() == Mode::Event && scene && GetTargetID() > 0) {
        scene->SetEventActive(GetTargetID(), false);
    } else if (GetMode() == Mode::Bgm && !param_->volumePayload.empty()) {
        GameAudioSettings::GetInstance()->Stop(param_->volumePayload);
    }
    else if (GetMode() == Mode::Environment && hasPreviousEnvironment_) {
        LightManager::GetInstance()->ApplyEnvironmentProfile(
            previousEnvironment_,
            param_->volumeBlendDuration);
        hasPreviousEnvironment_ = false;
    }
}

bool GimmickGameplayVolume::CanActivate() const {
    if (!runtimeEnabled_ || !param_.has_value() || rearmTimer_ > 0.0f) {
        return false;
    }
    return !param_->volumeTriggerOnce || !triggered_;
}

GimmickGameplayVolume::Mode GimmickGameplayVolume::GetMode() const {
    const int mode = param_.has_value() ? (std::clamp)(param_->volumeMode, 0, 5) : 0;
    return static_cast<Mode>(mode);
}
