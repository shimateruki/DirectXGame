#include "GimmickRotatingObject.h"
#include "ObjectPropertyDefinition.h"
#include "CollisionConfig.h"
#include "GhostRecorder.h"
#include "BaseScene.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>

void GimmickRotatingObject::Initialize(engine::Object3dCommon* common, const std::string& modelName) {
    // 回転軸と速度の旧既定値を用意し、地形として足場判定を有効にする。
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("RotatingFloor");
    SetName("Gimmick_RotatingFloor");
    SetCollisionAttribute(CollisionCategories::Get("ground"));
    SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
    SetColor({0.95f, 0.65f, 0.35f, 1.0f});
    SetScale({3.0f, 0.3f, 1.2f});
    SetStatic(false);

    if (!param_.has_value())
        param_.emplace();
    param_->speed = 45.0f;
    param_->actionMode = 1;
    param_->startActive = true;
    param_->returnOnOff = true;

    // 編集項目の宣言です。動作処理と旧paramの保存キーは変更しません。
    using Self = GimmickRotatingObject;
    engine::ObjectPropertyDefinition<Self> properties(*this, kTypeId, "回転床・回転柱",
        "スイッチ連動で回転の開始・停止ができます。回転床と回転柱は同じクラスで動きます。");
    using Parameter = engine::Object3d::EntityParameter;
    properties.Parameter("回転軸", "actionMode", &Parameter::actionMode, 0.0f, 2.0f,
        nullptr, {"X", "Y", "Z"});
    properties.Parameter("回転速度（度/秒）", "speed", &Parameter::speed, -720.0f, 720.0f,
        nullptr, {}, {.speed = 1.0f});
    properties.Parameter("開始時に有効", "startActive", &Parameter::startActive);
    properties.Parameter("OFFで元に戻す", "returnOnOff", &Parameter::returnOnOff);
}

// Editorでは配置角度を維持し、Play中だけ設定軸の回転を進めます。
void GimmickRotatingObject::Update(float deltaTime) {
    // 編集中は回転を進めず、Play開始時に初期姿勢を取り直す。
    if (!param_.has_value())
        param_.emplace();

    bool isPlaying = false;

    if (SceneManager* sceneManager = SceneManager::GetInstance()) {
        isPlaying = sceneManager->IsPlaying();
    }

    if (!isPlaying) {
        if (supportFloor_) {
            SetTranslate(startPosition_);
            SetEulerAngles(startRotation_);
            SetIsVisible(initialVisible_);
            SetCollisionAttribute(initialCollisionAttribute_);
            supportFloor_ = nullptr;
        }
        initializedForPlay_ = false;
        pathRotationOffset_ = 0.0f;
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (!initializedForPlay_) {
        active_ = param_->startActive;
        pathRotationOffset_ = 0.0f;
        startPosition_ = GetTranslate();
        startRotation_ = GetRotation();
        initialCollisionAttribute_ = GetCollisionAttribute();
        initialVisible_ = GetIsVisible();
        initializedForPlay_ = true;
    }

    // Recorderの移動姿勢を先に確定し、経路を持つ物の自転を上書きされないようにする。
    BaseGimmick::Update(deltaTime);

    // Target IDに対応する床の剛体移動だけを追う。親の非等方スケールを棘へ掛けない。
    if (GetTargetID() >= 0 && !supportFloor_) {
        if (auto* scene = SceneManager::GetInstance()->GetCurrentScene()) {
            for (const auto& object : scene->GetObjects()) {
                if (object && object.get() != this && object->GetEventID() == GetTargetID()) {
                    supportFloor_ = object.get();
                    supportFloor_->UpdateLocalMatrix();
                    supportFloor_->UpdateWorldMatrix(false);
                    supportLocalPosition_ = engine::Math::Transform(
                        GetWorldPosition(), engine::Math::Inverse(supportFloor_->GetWorldMatrix()));
                    supportStartRotation_ = supportFloor_->GetRotation();
                    break;
                }
            }
        }
    }

    const float step = param_->speed * 3.14159265f / 180.0f * (std::max)(0.0f, deltaTime);
    const bool playingPath = recorder_ && recorder_->GetState() == GhostRecorder::State::Playing &&
                             !GetRecordPathName().empty();
    if (supportFloor_) {
        if (active_)
            pathRotationOffset_ = std::fmod(pathRotationOffset_ + step, 2.0f * 3.14159265f);
        supportFloor_->UpdateWorldMatrix(false);
        SetTranslate(engine::Math::Transform(supportLocalPosition_, supportFloor_->GetWorldMatrix()));
        const auto floorRotation = supportFloor_->GetRotation();
        engine::Vector3 rotation = {
            startRotation_.x + floorRotation.x - supportStartRotation_.x,
            startRotation_.y + floorRotation.y - supportStartRotation_.y,
            startRotation_.z + floorRotation.z - supportStartRotation_.z};
        if (param_->actionMode == 0) rotation.x += pathRotationOffset_;
        else if (param_->actionMode == 2) rotation.z += pathRotationOffset_;
        else rotation.y += pathRotationOffset_;
        SetEulerAngles(rotation);
        SetIsVisible(initialVisible_ && supportFloor_->GetIsVisible());
        SetCollisionAttribute(supportFloor_->GetCollisionAttribute() ? initialCollisionAttribute_ : 0);
        UpdateLocalMatrix();
        UpdateWorldMatrix(false);
    } else if (playingPath) {
        if (active_) {
            pathRotationOffset_ = std::fmod(pathRotationOffset_ + step, 2.0f * 3.14159265f);
        }
        engine::Vector3 rot = GetRotation();
        if (param_->actionMode == 0) rot.x += pathRotationOffset_;
        else if (param_->actionMode == 2) rot.z += pathRotationOffset_;
        else rot.y += pathRotationOffset_;
        SetEulerAngles(rot);
    } else if (active_) {
        // 経路を使わない従来の回転床は現在の姿勢から少しずつ進める。
        engine::Vector3 rot = GetRotation();
        if (param_->actionMode == 0) rot.x += step;
        else if (param_->actionMode == 2) rot.z += step;
        else rot.y += step;
        SetEulerAngles(rot);
    }
}

void GimmickRotatingObject::OnTrigger() {
    // トリガーはスイッチの ON と同じ経路で扱う。
    OnSwitchEvent(true);
}

// returnOnOff が無効なら、スイッチ解除後も回転を続けます。
void GimmickRotatingObject::OnSwitchEvent(bool active) {
    // OFF で停止するかは設定値で決め、スイッチの状態を床の回転へ反映する。
    if (!param_.has_value())
        param_.emplace();
    if (!active && !param_->returnOnOff)
        return;
    active_ = active;
}

std::unique_ptr<engine::Object3d> GimmickRotatingObject::Clone() const {
    // 回転開始時刻は複製せず、保存済みの配置と設定だけ引き継ぐ。
    auto newObj = std::make_unique<GimmickRotatingObject>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
