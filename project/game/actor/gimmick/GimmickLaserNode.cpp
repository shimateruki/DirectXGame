#define NOMINMAX
#include "GimmickLaserNode.h"
#include "BaseScene.h"
#include "CollisionConfig.h"
#include "EventManager.h"
#include "Player.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>

void GimmickLaserNode::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("LaserNode");
    SetName("Gimmick_LaserNode");
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetColor({ 1.0f, 0.18f, 0.08f, 1.0f });
    SetBlendMode(BlendMode::kAdd);
    SetMaterialType(3);
    SetTexture("Resources/sprite/white.png");
    SetEmissive(3.5f);
    SetScale({ 0.35f, 0.35f, 0.35f });
    SetStatic(false);

    Object3d::ColliderConfig colConfig;
    colConfig.type = ColliderType::kOBB;
    colConfig.size = { 1.0f, 1.0f, 1.0f };
    SetColliderConfig(colConfig);

    if (!param_.has_value()) param_.emplace();
    param_->speed = 10.0f;
    param_->interval = 0.7f;
    param_->moveAmount = 0.14f;
    param_->startActive = true;
    param_->returnOnOff = true;

    beamVisual_ = std::make_unique<Object3d>();
    beamVisual_->Initialize(common);
    beamVisual_->SetName("LaserNode_Beam");
    beamVisual_->SetClassName("Effect");
    beamVisual_->SetModel("Primitives/cylinder");
    beamVisual_->SetTexture("Resources/sprite/white.png");
    beamVisual_->SetBlendMode(BlendMode::kAdd);
    beamVisual_->SetMaterialType(12);
    beamVisual_->SetColor({ 1.0f, 0.05f, 0.02f, 0.88f });
    beamVisual_->SetEmissive(8.0f);
    beamVisual_->SetScale({ 0.0f, 0.0f, 0.0f });
    beamVisual_->SetCollisionAttribute(0);
    beamVisual_->SetCollisionMask(0);
    beamVisual_->SetIsVisible(false);
}

void GimmickLaserNode::Update(float deltaTime) {
    if (!param_.has_value()) param_.emplace();

    bool isPlaying = false;
    if (SceneManager* sceneManager = SceneManager::GetInstance()) {
        isPlaying = sceneManager->IsPlaying();
    }

    if (!isPlaying) {
        initializedForPlay_ = false;
        active_ = param_->startActive;
        SetCollisionAttribute(0);
        SetCollisionMask(0);
        if (beamVisual_) beamVisual_->SetIsVisible(false);
        SetIsVisible(true);
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (!initializedForPlay_) {
        basePosition_ = GetTransform()->translate;
        baseRotation_ = GetTransform()->rotate;
        baseScale_ = GetTransform()->scale;
        active_ = param_->startActive;
        damageCooldownTimer_ = 0.0f;
        pulseTimer_ = 0.0f;
        initializedForPlay_ = true;
    }

    if (damageCooldownTimer_ > 0.0f) {
        damageCooldownTimer_ = (std::max)(0.0f, damageCooldownTimer_ - deltaTime);
    }

    Vector3 target = {};
    const bool hasTarget = FindTargetPosition(target);
    if (!active_ || !hasTarget) {
        SetTranslate(basePosition_);
        SetRotation(baseRotation_);
        SetScale(baseScale_);
        SetColor({ 1.0f, 0.16f, 0.08f, hasTarget ? 0.55f : 1.0f });
        SetIsVisible(true);
        SetCollisionAttribute(0);
        SetCollisionMask(0);
        if (beamVisual_) beamVisual_->SetIsVisible(false);
        BaseGimmick::Update(deltaTime);
        return;
    }

    pulseTimer_ += deltaTime;
    ApplyBeamTransform(basePosition_, target);
    UpdateBeamDamage(basePosition_, target);

    const float pulse = 0.78f + std::sin(pulseTimer_ * 12.0f) * 0.14f;
    SetColor({ 1.0f, 0.12f + pulse * 0.08f, 0.04f, 1.0f });
    SetIsVisible(true);

    BaseGimmick::Update(deltaTime);
}

void GimmickLaserNode::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    BaseGimmick::Draw(pointLightResource, spotLightResource);
    if (beamVisual_ && beamVisual_->GetIsVisible()) {
        beamVisual_->Draw(pointLightResource, spotLightResource);
    }
}

bool GimmickLaserNode::OnCollision(Object3d* other) {
    (void)other;
    return true;
}

void GimmickLaserNode::OnTrigger() {
    OnSwitchEvent(!active_);
}

void GimmickLaserNode::OnSwitchEvent(bool active) {
    if (!param_.has_value()) param_.emplace();
    if (!active && !param_->returnOnOff) return;
    active_ = active;
}

Vector3 GimmickLaserNode::GetLaserAnchorPosition() const {
    return initializedForPlay_ ? basePosition_ : GetWorldPosition();
}

bool GimmickLaserNode::FindTargetPosition(Vector3& outTarget) const {
    if (GetTargetID() == -1) return false;

    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!sceneManager) return false;

    BaseScene* scene = sceneManager->GetCurrentScene();
    if (!scene) return false;

    Object3d* target = scene->FindObjectByEventID(GetTargetID());
    if (!target || target == this) return false;

    if (GimmickLaserNode* targetNode = dynamic_cast<GimmickLaserNode*>(target)) {
        outTarget = targetNode->GetLaserAnchorPosition();
    }
    else {
        outTarget = target->GetWorldPosition();
    }
    return true;
}

void GimmickLaserNode::ApplyBeamTransform(const Vector3& source, const Vector3& target) {
    if (!beamVisual_) return;

    Vector3 diff = target - source;
    const float length = Math::Length(diff);
    if (length < 0.001f) {
        beamVisual_->SetIsVisible(false);
        return;
    }

    const float pulse = 0.94f + std::sin(pulseTimer_ * 18.0f) * 0.06f;
    const float thickness = GetThickness() * pulse;
    const Vector3 midpoint = source + diff * 0.5f;

    beamVisual_->SetTranslate(midpoint);
    beamVisual_->SetScale({ thickness, length * 0.5f, thickness });
    beamVisual_->GetTransform()->quaternion = MakeYAxisToDirectionQuaternion(diff);
    beamVisual_->GetTransform()->isQuaternionMaster = true;
    beamVisual_->SetColor({ 1.0f, 0.04f, 0.02f, 0.88f });
    beamVisual_->SetIsVisible(true);
    beamVisual_->UpdateWorldMatrix();
}

void GimmickLaserNode::UpdateBeamDamage(const Vector3& source, const Vector3& target) {
    if (damageCooldownTimer_ > 0.0f) return;

    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!sceneManager) return;

    BaseScene* scene = sceneManager->GetCurrentScene();
    if (!scene) return;

    const float hitRadius = GetThickness() + 0.75f;
    for (const auto& obj : scene->GetObjects()) {
        Player* player = dynamic_cast<Player*>(obj.get());
        if (!player) continue;

        if (CalcDistancePointToSegment(player->GetWorldPosition(), source, target) > hitRadius) {
            continue;
        }

        Vector3 knockbackDir = player->GetWorldPosition() - source;
        knockbackDir.y = 0.0f;
        if (Math::Length(knockbackDir) < 0.001f) {
            knockbackDir = { 0.0f, 0.0f, 1.0f };
        }
        knockbackDir = Math::Normalize(knockbackDir);

        DamageEvent damageEvent;
        damageEvent.target = player;
        damageEvent.attacker = this;
        damageEvent.damageAmount = GetDamage();
        damageEvent.knockbackVelocity = { knockbackDir.x * 12.0f, 8.0f, knockbackDir.z * 12.0f };
        EventManager::GetInstance()->Dispatch(damageEvent);

        damageCooldownTimer_ = GetDamageInterval();
        return;
    }
}

float GimmickLaserNode::CalcDistancePointToSegment(const Vector3& point, const Vector3& start, const Vector3& end) const {
    const Vector3 segment = end - start;
    const float lengthSq = Math::Dot(segment, segment);
    if (lengthSq <= 0.0001f) {
        return Math::Length(point - start);
    }

    float t = Math::Dot(point - start, segment) / lengthSq;
    t = std::clamp(t, 0.0f, 1.0f);
    const Vector3 closest = start + segment * t;
    return Math::Length(point - closest);
}

Quaternion GimmickLaserNode::MakeYAxisToDirectionQuaternion(const Vector3& direction) const {
    Vector3 to = direction;
    if (Math::Length(to) < 0.001f) {
        return { 0.0f, 0.0f, 0.0f, 1.0f };
    }
    to = Math::Normalize(to);

    const Vector3 from = { 0.0f, 1.0f, 0.0f };
    const float dot = std::clamp(Math::Dot(from, to), -1.0f, 1.0f);

    if (dot > 0.9999f) {
        return { 0.0f, 0.0f, 0.0f, 1.0f };
    }
    if (dot < -0.9999f) {
        return { 1.0f, 0.0f, 0.0f, 0.0f };
    }

    Vector3 axis = Math::Cross(from, to);
    const float s = std::sqrt((1.0f + dot) * 2.0f);
    const float invS = 1.0f / s;
    return { axis.x * invS, axis.y * invS, axis.z * invS, s * 0.5f };
}

float GimmickLaserNode::GetDamage() const {
    return param_.has_value() ? (std::max)(0.0f, param_->speed) : 10.0f;
}

float GimmickLaserNode::GetDamageInterval() const {
    return param_.has_value() ? (std::max)(0.05f, param_->interval) : 0.7f;
}

float GimmickLaserNode::GetThickness() const {
    return param_.has_value() ? (std::max)(0.03f, param_->moveAmount) : 0.25f;
}

std::unique_ptr<Object3d> GimmickLaserNode::Clone() const {
    auto newObj = std::make_unique<GimmickLaserNode>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
