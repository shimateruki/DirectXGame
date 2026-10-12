#define NOMINMAX
#include "GimmickMagmaHazard.h"

#include "CollisionConfig.h"
#include "EventManager.h"
#include "Player.h"
#include "SceneManager.h"

#include <algorithm>
#include <cmath>

void GimmickMagmaHazard::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("MagmaHazard");
    SetName("Gimmick_MagmaHazard");
    SetMaterialType(9);
    SetColor({1.0f, 0.24f, 0.015f, 1.0f});
    SetEmissive(1.8f);
    SetRoughness(0.38f);
    SetMetallic(0.0f);
    SetCollisionAttribute(CollisionCategories::Get("trigger"));
    SetCollisionMask(CollisionCategories::Get("player"));
    // マグマ面は移動しないため、巨大Colliderを静的グリッドへ一度だけ登録する。
    SetStatic(true);

    Object3d::ColliderConfig collider;
    collider.type = ColliderType::kOBB;
    collider.center = {0.0f, 0.0f, 0.0f};
    collider.size = {1.0f, 1.0f, 1.0f};
    SetColliderConfig(collider);

    if (!param_.has_value()) {
        param_.emplace();
    }
    param_->speed = 12.0f;
    param_->interval = 0.8f;
}

void GimmickMagmaHazard::Update(float deltaTime) {
    // 配置データがない場合も既定値で危険域を更新できるようにする。
    if (!param_.has_value()) {
        param_.emplace();
    }

    const bool isPlaying = SceneManager::GetInstance() && SceneManager::GetInstance()->IsPlaying();
    if (!isPlaying) {
        initializedForPlay_ = false;
        damageCooldownTimer_ = 0.0f;
        SetEmissive(baseEmissive_);
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (!initializedForPlay_) {
        baseEmissive_ = (std::max)(0.1f, GetEmissive());
        visualTimer_ = 0.0f;
        damageCooldownTimer_ = 0.0f;
        initializedForPlay_ = true;
    }

    damageCooldownTimer_ = (std::max)(0.0f, damageCooldownTimer_ - deltaTime);
    visualTimer_ += deltaTime;
    const float pulse = 0.92f + std::sin(visualTimer_ * 2.6f) * 0.08f;
    SetEmissive(baseEmissive_ * pulse);

    BaseGimmick::Update(deltaTime);
}

bool GimmickMagmaHazard::OnCollision(Object3d* /*other*/) {
    // 液面接触による死亡はPlayer側で波の高さも含めて判定します。
    // 旧Colliderによるダメージ・炎上・ノックバックを重ねないようにします。
    return true;
}

float GimmickMagmaHazard::GetDamage() const {
    // 共通パラメータのspeed欄をダメージ量として使い、負値は0へ丸める。
    return param_.has_value() ? (std::max)(0.0f, param_->speed) : 12.0f;
}

float GimmickMagmaHazard::GetDamageInterval() const {
    // 接触中の連続被弾を防ぐため、間隔は最低0.1秒に制限する。
    return param_.has_value() ? (std::max)(0.1f, param_->interval) : 0.8f;
}

std::unique_ptr<Object3d> GimmickMagmaHazard::Clone() const {
    // 複製先を初期化してから危険域の設定と配置を移す。
    auto clone = std::make_unique<GimmickMagmaHazard>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    return clone;
}
