#include "GimmickRotatingObject.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include <cmath>

void GimmickRotatingObject::Initialize(Object3dCommon* common, const std::string& modelName) {
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
        initializedForPlay_ = false;
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (!initializedForPlay_) {
        active_ = param_->startActive;
        initializedForPlay_ = true;
    }

    if (active_) {
        const float radiansPerSecond = param_->speed * 3.14159265f / 180.0f;
        Vector3 rot = GetTransform()->rotate;

        if (param_->actionMode == 0) {
            rot.x += radiansPerSecond * deltaTime;
        } else if (param_->actionMode == 2) {
            rot.z += radiansPerSecond * deltaTime;
        } else {
            rot.y += radiansPerSecond * deltaTime;
        }

        SetEulerAngles(rot);
        SetQuaternionMaster(false);
    }

    BaseGimmick::Update(deltaTime);
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

std::unique_ptr<Object3d> GimmickRotatingObject::Clone() const {
    // 回転開始時刻は複製せず、保存済みの配置と設定だけ引き継ぐ。
    auto newObj = std::make_unique<GimmickRotatingObject>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
