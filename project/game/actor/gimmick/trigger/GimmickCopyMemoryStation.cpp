#define NOMINMAX
#include "GimmickCopyMemoryStation.h"

#include "CollisionConfig.h"
#include "Player.h"
#include "PlayerCopyTypeCatalog.h"
#include "SceneManager.h"
#include "VFXSequencer.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace {
// 3つの見本の間に十分な選択余白を作り、意図しない能力の取得を防ぎます。
constexpr std::array<Vector3, 3> kCoreLocalPositions = {
    Vector3{ -2.45f, 1.48f, -0.32f },
    Vector3{ 0.00f, 1.48f, 0.64f },
    Vector3{ 2.45f, 1.48f, -0.32f },
};
constexpr float kCoreVisualScale = 0.92f;
constexpr float kSelectionCooldown = 0.28f;
constexpr float kPulseDuration = 0.55f;
// 記憶台の子モデルはシーンの特殊マテリアル一覧へ直接登録されないため、
// 通常PBRで確実に描画し、台座と3つの能力見本を一体として読めるようにします。
constexpr int kCorePreviewMaterialType = 0;
constexpr const char* kActivationCue = "copy_memory_station_activate_cue";
}

void GimmickCopyMemoryStation::Initialize(Object3dCommon* common, const std::string& modelName)
{
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("CopyMemoryStation");
    SetName("Gimmick_CopyMemoryStation");
    SetIsVisible(true);
    SetStatic(false);
    SetCastShadow(true);
    SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    SetMaterialType(0);
    SetEmissive(0.50f);
    SetEnableEnvMap(true);
    SetEnvIntensity(0.35f);
    SetMetallic(0.18f);
    SetRoughness(0.34f);
    SetCollisionAttribute(CollisionAttribute::kTrigger);
    SetCollisionMask(CollisionAttribute::kPlayer);

    Object3d::ColliderConfig collider;
    collider.type = ColliderType::kOBB;
    collider.center = { 0.0f, 0.95f, 0.0f };
    collider.size = { 3.1f, 1.6f, 1.85f };
    SetColliderConfig(collider);

    if (!param_.has_value()) {
        param_.emplace();
    }
    param_->gimmickType = "CopyMemoryStation";
    param_->copyMemoryTypeA = "FireSlime";
    param_->copyMemoryTypeB = "ThunderSlime";
    param_->copyMemoryTypeC = "WindSlime";
    param_->copyMemoryActivationRadius = 0.9f;
    param_->copyMemoryUnlimitedDuration = true;
    param_->startActive = true;

    for (size_t index = 0; index < kCoreCount; ++index) {
        coreVisuals_[index] = std::make_unique<Object3d>();
        coreVisuals_[index]->Initialize(common);
        coreVisuals_[index]->SetName("CopyMemoryStation_Core_" + std::to_string(index + 1));
        coreVisuals_[index]->SetClassName("Object");
        coreVisuals_[index]->SetColliderType(ColliderType::kNone);
        coreVisuals_[index]->SetCollisionAttribute(0);
        coreVisuals_[index]->SetCollisionMask(0);
        coreVisuals_[index]->SetMaterialType(kCorePreviewMaterialType);
        // 通常のステージ照明を使い、暗い影に沈まず能力色を読める状態にします。
        coreVisuals_[index]->SetSelectedLighting(2);
        coreVisuals_[index]->SetEnableLighting(true);
        coreVisuals_[index]->SetEnableEnvMap(true);
        coreVisuals_[index]->SetEnvIntensity(0.55f);
        coreVisuals_[index]->SetCastShadow(true);
        coreVisuals_[index]->SetMetallic(0.0f);
        coreVisuals_[index]->SetRoughness(0.46f);
        coreVisuals_[index]->SetEmissive(0.65f);
        displayedTypes_[index].clear();
    }
    UpdateCoreVisuals(0.0f);
}

void GimmickCopyMemoryStation::Update(float deltaTime)
{
    SceneManager* sceneManager = SceneManager::GetInstance();
    const bool isPlaying = sceneManager && sceneManager->IsPlaying();
    if (isPlaying) {
        if (!initializedForPlay_) {
            BeginPlayState();
        }
        selectionCooldown_ = (std::max)(0.0f, selectionCooldown_ - (std::max)(0.0f, deltaTime));
        if (!collisionObserved_) {
            latchedCore_ = -1;
        }
        collisionObserved_ = false;
    }
    else {
        ResetEditorState();
    }

    BaseGimmick::Update(deltaTime);
    UpdateCoreVisuals(deltaTime);
}

void GimmickCopyMemoryStation::Draw(
    ID3D12Resource* pointLightResource,
    ID3D12Resource* spotLightResource)
{
    BaseGimmick::Draw(pointLightResource, spotLightResource);
    if (!GetIsVisible()) {
        return;
    }
    for (const std::unique_ptr<Object3d>& core : coreVisuals_) {
        if (core && core->GetIsVisible()) {
            core->Draw(pointLightResource, spotLightResource);
        }
    }
}

bool GimmickCopyMemoryStation::OnCollision(Object3d* other)
{
    auto* player = dynamic_cast<Player*>(other);
    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!player || !sceneManager || !sceneManager->IsPlaying() || !runtimeEnabled_) {
        return false;
    }

    collisionObserved_ = true;
    const int touchedCore = FindTouchedCore(player->GetWorldPosition());
    if (touchedCore < 0) {
        latchedCore_ = -1;
        return false;
    }
    if (selectionCooldown_ <= 0.0f && touchedCore != latchedCore_) {
        ActivateCore(*player, static_cast<size_t>(touchedCore));
    }
    return false;
}

void GimmickCopyMemoryStation::OnTrigger()
{
    OnSwitchEvent(true);
}

void GimmickCopyMemoryStation::OnSwitchEvent(bool active)
{
    runtimeEnabled_ = active;
    if (!active) {
        latchedCore_ = -1;
        collisionObserved_ = false;
    }
}

std::unique_ptr<Object3d> GimmickCopyMemoryStation::Clone() const
{
    auto clone = std::make_unique<GimmickCopyMemoryStation>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    return clone;
}

void GimmickCopyMemoryStation::BeginPlayState()
{
    initializedForPlay_ = true;
    runtimeEnabled_ = param_.has_value() ? param_->startActive : true;
    selectionCooldown_ = 0.0f;
    latchedCore_ = -1;
    collisionObserved_ = false;
}

void GimmickCopyMemoryStation::ResetEditorState()
{
    initializedForPlay_ = false;
    runtimeEnabled_ = true;
    selectionCooldown_ = 0.0f;
    latchedCore_ = -1;
    collisionObserved_ = false;
}

void GimmickCopyMemoryStation::UpdateCoreVisuals(float deltaTime)
{
    visualTimer_ += (std::max)(0.0f, deltaTime);
    for (size_t index = 0; index < kCoreCount; ++index) {
        const std::string configuredType = GetConfiguredType(index);
        if (displayedTypes_[index] != configuredType) {
            ConfigureCore(index, configuredType);
        }

        Object3d* core = coreVisuals_[index].get();
        if (!core || !core->GetIsVisible()) {
            continue;
        }

        corePulseTimers_[index] = (std::max)(0.0f, corePulseTimers_[index] - (std::max)(0.0f, deltaTime));
        const float phase = visualTimer_ * 2.15f + static_cast<float>(index) * 2.0944f;
        const float bounce = std::sin(phase) * 0.075f;
        Vector3 position = GetCoreWorldPosition(index);
        position.y += bounce * std::abs(GetScale().y);
        core->SetTranslate(position);
        core->SetRotationY(GetRotation().y + visualTimer_ * 0.48f + static_cast<float>(index) * 0.55f);

        const float pulseRate = corePulseTimers_[index] > 0.0f
            ? 1.0f - corePulseTimers_[index] / kPulseDuration
            : 1.0f;
        const float pulse = corePulseTimers_[index] > 0.0f
            ? 1.0f + std::sin(pulseRate * 3.14159265f) * 0.28f
            : 1.0f;
        const Vector3 rootScale = GetScale();
        // 台座だけを横へ広げても見本スライムを横長に潰さないよう、縦・奥行きから均一倍率を作る。
        const float uniformRootScale = (std::max)(
            0.1f,
            (std::abs(rootScale.y) + std::abs(rootScale.z)) * 0.5f);
        const float visualScale = uniformRootScale * kCoreVisualScale;
        core->SetScale({
            visualScale * pulse,
            visualScale / pulse,
            visualScale * pulse,
        });
        core->SetEmissive(corePulseTimers_[index] > 0.0f ? 1.35f : 0.65f);
        core->Update(deltaTime);
        // 見本モデルはObjectManagerへ登録しない内部描画物です。
        // Object3d::Updateは描画定数の確定をObjectManagerへ遅延するため、
        // ここで明示的に確定しないと初期位置へ重なり、カメラへ張り付いて見えます。
        core->UpdateWorldMatrix();
    }
}

void GimmickCopyMemoryStation::ConfigureCore(size_t index, const std::string& enemyType)
{
    displayedTypes_[index] = enemyType;
    Object3d* core = coreVisuals_[index].get();
    const PlayerCopyTypeDescriptor* descriptor = PlayerCopyTypeCatalog::Find(enemyType);
    if (!core || !descriptor) {
        if (core) {
            core->SetIsVisible(false);
        }
        return;
    }

    core->SetModel(std::string(descriptor->modelName));
    core->SetTexture("");
    // 見本自体にも能力色を与え、離れた位置からでも選択結果を判別できるようにする。
    core->SetColor(descriptor->displayColor);
    core->SetMaterialType(kCorePreviewMaterialType);
    core->SetIsVisible(true);
}

Vector3 GimmickCopyMemoryStation::GetCoreWorldPosition(size_t index) const
{
    // ワールド行列で変換し、傾斜面へ置いた場合も3つのコアをソケットへ追従させます。
    return Math::Transform(
        kCoreLocalPositions[index],
        GetWorldMatrix());
}

std::string GimmickCopyMemoryStation::GetConfiguredType(size_t index) const
{
    if (!param_.has_value()) {
        return {};
    }
    switch (index) {
    case 0: return param_->copyMemoryTypeA;
    case 1: return param_->copyMemoryTypeB;
    case 2: return param_->copyMemoryTypeC;
    default: return {};
    }
}

int GimmickCopyMemoryStation::FindTouchedCore(const Vector3& playerPosition) const
{
    const float configuredRadius = param_.has_value() ? param_->copyMemoryActivationRadius : 0.9f;
    const float scaleRadius = (std::max)(std::abs(GetScale().x), std::abs(GetScale().z));
    const float radius = (std::max)(0.1f, configuredRadius) * scaleRadius;
    const float radiusSquared = radius * radius;
    int nearest = -1;
    float nearestDistanceSquared = radiusSquared;
    for (size_t index = 0; index < kCoreCount; ++index) {
        if (!PlayerCopyTypeCatalog::IsSupported(GetConfiguredType(index))) {
            continue;
        }
        const Vector3 corePosition = GetCoreWorldPosition(index);
        const float deltaX = playerPosition.x - corePosition.x;
        const float deltaZ = playerPosition.z - corePosition.z;
        const float distanceSquared = deltaX * deltaX + deltaZ * deltaZ;
        if (distanceSquared <= nearestDistanceSquared) {
            nearestDistanceSquared = distanceSquared;
            nearest = static_cast<int>(index);
        }
    }
    return nearest;
}

void GimmickCopyMemoryStation::ActivateCore(Player& player, size_t index)
{
    if (index >= kCoreCount || player.isDead || player.IsCinematicLocked()) {
        return;
    }

    const std::string enemyType = GetConfiguredType(index);
    const bool unlimitedDuration = !param_.has_value() || param_->copyMemoryUnlimitedDuration;
    if (!player.ApplyStoredCopy(enemyType, unlimitedDuration)) {
        return;
    }

    latchedCore_ = static_cast<int>(index);
    selectionCooldown_ = kSelectionCooldown;
    corePulseTimers_[index] = kPulseDuration;
    VFXSequencer::PlayOneShot(kActivationCue, GetCoreWorldPosition(index), { 1.0f, 1.0f, 1.0f });
}
