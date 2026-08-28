#include "DecalSystem.h"

#include "BaseScene.h"
#include "CollisionManager.h"
#include "ModelManager.h"
#include "Object3d.h"

#include <algorithm>
#include <cmath>

namespace {
Vector3 SafeNormal(const Vector3& value) {
    const float length = Math::Length(value);
    if (length <= 0.0001f) return { 0.0f, 1.0f, 0.0f };
    return value * (1.0f / length);
}

Quaternion BuildSurfaceQuaternion(const Vector3& normal, float rotationRadians) {
    // The plane primitive faces local -Z. Build an orthonormal frame whose -Z follows the surface normal.
    const Vector3 zAxis = -SafeNormal(normal);
    const Vector3 helper = std::abs(Math::Dot(zAxis, { 0.0f, 1.0f, 0.0f })) > 0.98f
        ? Vector3{ 1.0f, 0.0f, 0.0f }
        : Vector3{ 0.0f, 1.0f, 0.0f };
    Vector3 xAxis = SafeNormal(Math::Cross(helper, zAxis));
    Vector3 yAxis = SafeNormal(Math::Cross(zAxis, xAxis));

    const float cosine = std::cos(rotationRadians);
    const float sine = std::sin(rotationRadians);
    const Vector3 rotatedX = xAxis * cosine + yAxis * sine;
    const Vector3 rotatedY = yAxis * cosine - xAxis * sine;

    Matrix4x4 rotation = Math::MakeIdentity4x4();
    rotation.m[0][0] = rotatedX.x;
    rotation.m[0][1] = rotatedX.y;
    rotation.m[0][2] = rotatedX.z;
    rotation.m[1][0] = rotatedY.x;
    rotation.m[1][1] = rotatedY.y;
    rotation.m[1][2] = rotatedY.z;
    rotation.m[2][0] = zAxis.x;
    rotation.m[2][1] = zAxis.y;
    rotation.m[2][2] = zAxis.z;
    return Math::MatrixToQuaternion(rotation);
}
}

std::unique_ptr<Object3d> DecalSystem::Create(
    Object3dCommon* common,
    const Vector3& surfacePoint,
    const Vector3& surfaceNormal,
    const DecalSpawnDesc& desc) {
    if (!common) return nullptr;

    auto decal = std::make_unique<Object3d>();
    decal->Initialize(common);
    ModelManager::GetInstance()->LoadModel("Primitives/plane");
    decal->SetModel("Primitives/plane");
    decal->SetClassName("Decal");
    decal->SetName(desc.name.empty() ? "Decal" : desc.name);
    decal->SetTexture(desc.texturePath);
    decal->SetColor(desc.color);
    decal->SetBlendMode(static_cast<BlendMode>(std::clamp(desc.blendMode, 0, 5)));
    decal->SetMaterialType(28);
    decal->SetEnableLighting(false);
    decal->SetEnableNormalMap(false);
    decal->SetEnableEnvMap(false);
    decal->SetEmissive(desc.emissive);
    decal->SetCastShadow(false);
    decal->SetCollisionAttribute(0);
    decal->SetCollisionMask(0);

    Object3d::ColliderConfig collider = decal->GetColliderConfig();
    collider.type = ColliderType::kNone;
    decal->SetColliderConfig(collider);

    Object3d::DecalSettings settings;
    settings.enabled = true;
    settings.size = {
        (std::max)(desc.size.x, 0.01f),
        (std::max)(desc.size.y, 0.01f)
    };
    settings.depthOffset = (std::max)(desc.depthOffset, 0.0001f);
    settings.lifetime = (std::max)(desc.lifetime, 0.0f);
    settings.fadeIn = (std::max)(desc.fadeIn, 0.0f);
    settings.fadeOut = (std::max)(desc.fadeOut, 0.0f);
    settings.transient = desc.transient;
    decal->SetDecalSettings(settings);
    decal->RestartDecalPlayback();
    AlignToSurface(*decal, surfacePoint, surfaceNormal, desc.rotationRadians, settings.depthOffset);
    return decal;
}

Object3d* DecalSystem::Spawn(
    BaseScene& scene,
    const Vector3& surfacePoint,
    const Vector3& surfaceNormal,
    const DecalSpawnDesc& desc) {
    std::unique_ptr<Object3d> decal = Create(
        scene.GetObject3dCommon(), surfacePoint, surfaceNormal, desc);
    if (!decal) return nullptr;
    Object3d* result = decal.get();
    scene.AddObject(std::move(decal));
    for (const std::unique_ptr<Object3d>& ownedObject : scene.GetObjects()) {
        if (ownedObject.get() == result) {
            return result;
        }
    }
    return nullptr;
}

Object3d* DecalSystem::SpawnFromRaycast(
    BaseScene& scene,
    const Vector3& rayStart,
    const Vector3& rayDirection,
    float maxDistance,
    uint32_t collisionMask,
    const DecalSpawnDesc& desc) {
    if (maxDistance <= 0.0f || Math::Length(rayDirection) <= 0.0001f) return nullptr;
    const RaycastHit hit = CollisionManager::GetInstance()->Raycast(
        rayStart, SafeNormal(rayDirection), maxDistance, collisionMask);
    if (!hit.isHit) return nullptr;
    return Spawn(scene, hit.hitPoint, hit.normal, desc);
}

void DecalSystem::AlignToSurface(
    Object3d& object,
    const Vector3& surfacePoint,
    const Vector3& surfaceNormal,
    float rotationRadians,
    float depthOffset) {
    const Vector3 normal = SafeNormal(surfaceNormal);
    Object3d::DecalSettings settings = object.GetDecalSettings();
    settings.depthOffset = (std::max)(depthOffset, 0.0001f);
    object.SetDecalSettings(settings);
    object.SetTranslate(surfacePoint + normal * settings.depthOffset);
    object.SetScale({ settings.size.x * 0.5f, settings.size.y * 0.5f, 1.0f });
    Transform* transform = object.GetTransform();
    transform->quaternion = BuildSurfaceQuaternion(normal, rotationRadians);
    transform->rotate = Math::MatrixToEuler(Math::MakeRotateQuaternionMatrix(transform->quaternion));
    transform->isQuaternionMaster = true;
    object.UpdateLocalMatrix();
    object.UpdateWorldMatrix();
}

void Object3d::SetDecalSettings(const DecalSettings& settings) {
    decalSettings_ = settings;
    decalSettings_.size.x = (std::max)(decalSettings_.size.x, 0.01f);
    decalSettings_.size.y = (std::max)(decalSettings_.size.y, 0.01f);
    decalSettings_.depthOffset = (std::max)(decalSettings_.depthOffset, 0.0001f);
    decalSettings_.lifetime = (std::max)(decalSettings_.lifetime, 0.0f);
    decalSettings_.fadeIn = (std::max)(decalSettings_.fadeIn, 0.0f);
    decalSettings_.fadeOut = (std::max)(decalSettings_.fadeOut, 0.0f);
    if (!decalSettings_.enabled) {
        decalElapsedTime_ = 0.0f;
    }
}

void Object3d::RestartDecalPlayback() {
    decalElapsedTime_ = 0.0f;
    decalAuthoredAlpha_ = GetColor().w;
    if (decalSettings_.enabled) {
        SetIsVisible(true);
        isDead = false;
    }
}
