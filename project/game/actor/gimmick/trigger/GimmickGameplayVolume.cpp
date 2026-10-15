#define NOMINMAX
#include "GimmickGameplayVolume.h"

#include "BaseScene.h"
#include "CollisionConfig.h"
#include "GameAudioSettings.h"
#include "GameDataManager.h"
#include "Player.h"
#include "SceneManager.h"
#include "VFXSequencer.h"

#include <algorithm>
#include <cassert>
#include <cmath>

void GimmickGameplayVolume::Initialize(Object3dCommon* common, const std::string& modelName) {
    // プレイヤーだけを検知する透明な OBB とし、用途は JSON の volumeMode で選ぶ。
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("GameplayVolume");
    SetName("Gimmick_GameplayVolume");
    SetStatic(false);
    SetCastShadow(false);
    SetColor({0.15f, 0.75f, 1.0f, 0.3f});
    SetBlendMode(BlendMode::kNormal);
    SetScale({3.0f, 3.0f, 3.0f});
    SetCollisionAttribute(CollisionCategories::Get("trigger"));
    SetCollisionMask(CollisionCategories::Get("player"));

    engine::Object3d::ColliderConfig collider;
    collider.type = ColliderType::kOBB;
    collider.center = {0.0f, 0.0f, 0.0f};
    collider.size = {1.0f, 1.0f, 1.0f};
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
    // 衝突通知がないフレームを退出として扱い、再発動待ち時間を更新する。
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

void GimmickGameplayVolume::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    // 編集中は範囲を見せ、ゲーム再生中は検知形状を描画しない。
    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!sceneManager || !sceneManager->IsPlaying()) {
        BaseGimmick::Draw(pointLightResource, spotLightResource);
    }
}

bool GimmickGameplayVolume::OnCollision(engine::Object3d* other) {
    // プレイヤーが入った最初のフレームだけ入場処理を呼び、物理的には押し戻さない。
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
    // 外部イベントからの起動をスイッチ ON と同じ状態へ流す。
    OnSwitchEvent(true);
}

void GimmickGameplayVolume::OnSwitchEvent(bool active) {
    // 無効化時は占有を解き、設定によって有効化中の環境変更も戻す。
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

std::unique_ptr<engine::Object3d> GimmickGameplayVolume::Clone() const {
    // 実行時の占有と一度きりの発動履歴は複製先へ引き継がない。
    auto clone = std::make_unique<GimmickGameplayVolume>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    return clone;
}

void GimmickGameplayVolume::InitializeForPlay() {
    // 再生開始時に設定の有効状態へ戻し、編集用の可視化を消す。
    initializedForPlay_ = true;
    runtimeEnabled_ = param_.has_value() ? param_->startActive : true;
    occupied_ = false;
    collisionObserved_ = false;
    triggered_ = false;
    rearmTimer_ = 0.0f;
    occupant_ = nullptr;
    SetIsVisible(false);
    SetCollisionAttribute(CollisionCategories::Get("trigger"));
    SetCollisionMask(CollisionCategories::Get("player"));
}

void GimmickGameplayVolume::ResetForEditor() {
    // 再生中の検知履歴を消し、Inspector で範囲を見られるよう可視化する。
    initializedForPlay_ = false;
    runtimeEnabled_ = true;
    occupied_ = false;
    collisionObserved_ = false;
    triggered_ = false;
    rearmTimer_ = 0.0f;
    occupant_ = nullptr;
    SetIsVisible(true);
    SetCollisionAttribute(CollisionCategories::Get("trigger"));
    SetCollisionMask(CollisionCategories::Get("player"));
}

void GimmickGameplayVolume::HandleEnter(Player* player) {
    // 退出時発動の設定なら入場時には何も起動しない。
    if (!param_.has_value() || param_->volumeTriggerOnExit) {
        return;
    }
    Activate(player);
}

void GimmickGameplayVolume::HandleExit(Player* player) {
    // 退出時発動と、滞在中だけ有効な用途の終了を分けて扱う。
    if (param_.has_value() && param_->volumeTriggerOnExit) {
        Activate(player);
    } else if (param_.has_value() && param_->returnOnOff) {
        Deactivate();
    }
    occupied_ = false;
    occupant_ = nullptr;
}

void GimmickGameplayVolume::Activate(Player* player) {
    // ボリュームの用途に応じてチェックポイント、VFX、BGM、環境光を切り替えます。
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
            const engine::Vector3 checkpointPosition = GetWorldPosition();
            const engine::Vector3 previousRespawnPosition = player->GetRespawnPosition();
            const bool restoringSameCheckpoint =
                std::abs(previousRespawnPosition.x - checkpointPosition.x) < 0.01f &&
                std::abs(previousRespawnPosition.y - checkpointPosition.y) < 0.01f &&
                std::abs(previousRespawnPosition.z - checkpointPosition.z) < 0.01f;
            player->ActivateCheckpoint(checkpointPosition);
            if (!restoringSameCheckpoint && !player->IsEditorInternal()) {
                // 領域型のチェックポイントも旗と同じ報酬を与え、復活直後の再進入は除外します。
                GameDataManager* gameData = GameDataManager::GetInstance();
                if (gameData->GetLives() < GameDataManager::kMaxLives) {
                    gameData->SetLives(gameData->GetLives() + 1);
                }
                player->RestoreHpToFull();
            }
            VFXSequencer::PlayOneShot("checkpoint_activate_cue", checkpointPosition);
        }
        break;
    case Mode::FallDeath:
        if (player && !player->isDead) {
            engine::Vector3 position = player->GetTranslate();
            position.y = -19.0f;
            player->SetTranslate(position);
            player->SetVelocity({0.0f, 0.0f, 0.0f});
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
            previousEnvironment_ = LightManager::GetInstance()->CaptureEnvironmentProfile();
            hasPreviousEnvironment_ = true;
            LightManager::GetInstance()->ApplyEnvironmentProfile(param_->volumePayload, param_->volumeBlendDuration);
        }
        break;
    }

    triggered_ = true;
    rearmTimer_ = (std::max)(0.0f, param_->volumeRearmDelay);
}

void GimmickGameplayVolume::Deactivate() {
    // イベント・BGM・環境設定のうち継続する作用だけを元に戻す。
    if (!param_.has_value()) {
        return;
    }

    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    if (GetMode() == Mode::Event && scene && GetTargetID() > 0) {
        scene->SetEventActive(GetTargetID(), false);
    } else if (GetMode() == Mode::Bgm && !param_->volumePayload.empty()) {
        GameAudioSettings::GetInstance()->Stop(param_->volumePayload);
    } else if (GetMode() == Mode::Environment && hasPreviousEnvironment_) {
        LightManager::GetInstance()->ApplyEnvironmentProfile(previousEnvironment_, param_->volumeBlendDuration);
        hasPreviousEnvironment_ = false;
    }
}

bool GimmickGameplayVolume::CanActivate() const {
    // 一度きり指定と再発動待ち時間を合わせて発動可否を決める。
    if (!runtimeEnabled_ || !param_.has_value() || rearmTimer_ > 0.0f) {
        return false;
    }
    return !param_->volumeTriggerOnce || !triggered_;
}

GimmickGameplayVolume::Mode GimmickGameplayVolume::GetMode() const {
    // 不正な JSON 値は定義済みの用途範囲に丸める。
    const int mode = param_.has_value() ? (std::clamp)(param_->volumeMode, 0, 5) : 0;
    return static_cast<Mode>(mode);
}
