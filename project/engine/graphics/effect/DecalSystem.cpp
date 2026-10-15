#include "DecalSystem.h"

#include "BaseScene.h"
#include "CollisionManager.h"
#include "ModelManager.h"
#include "Object3d.h"

#include <algorithm>
#include <cmath>

namespace {
// 面法線がほぼゼロでもデカールの基底を作れるよう、上向きの既定法線を返します。
engine::Vector3 SafeNormal(const engine::Vector3& value) {
    const float length = engine::Math::Length(value);
    if (length <= 0.0001f) return { 0.0f, 1.0f, 0.0f };
    return value * (1.0f / length);
}

engine::Quaternion BuildSurfaceQuaternion(const engine::Vector3& normal, float rotationRadians) {
    // Planeのローカル-Zを面法線へ合わせ、面上の回転も含む直交基底を作る。
    const engine::Vector3 zAxis = -SafeNormal(normal);
    const engine::Vector3 helper = std::abs(engine::Math::Dot(zAxis, { 0.0f, 1.0f, 0.0f })) > 0.98f
        ? engine::Vector3{ 1.0f, 0.0f, 0.0f }
        : engine::Vector3{ 0.0f, 1.0f, 0.0f };
    engine::Vector3 xAxis = SafeNormal(engine::Math::Cross(helper, zAxis));
    engine::Vector3 yAxis = SafeNormal(engine::Math::Cross(zAxis, xAxis));

    // 接地面の法線は固定し、面内のX・Y軸だけを指定角度で回す。
    const float cosine = std::cos(rotationRadians);
    const float sine = std::sin(rotationRadians);
    const engine::Vector3 rotatedX = xAxis * cosine + yAxis * sine;
    const engine::Vector3 rotatedY = yAxis * cosine - xAxis * sine;

    engine::Matrix4x4 rotation = engine::Math::MakeIdentity4x4();
    rotation.m[0][0] = rotatedX.x;
    rotation.m[0][1] = rotatedX.y;
    rotation.m[0][2] = rotatedX.z;
    rotation.m[1][0] = rotatedY.x;
    rotation.m[1][1] = rotatedY.y;
    rotation.m[1][2] = rotatedY.z;
    rotation.m[2][0] = zAxis.x;
    rotation.m[2][1] = zAxis.y;
    rotation.m[2][2] = zAxis.z;
    return engine::Math::MatrixToQuaternion(rotation);
}
}

std::unique_ptr<engine::Object3d> DecalSystem::Create(
    Object3dCommon* common,
    const engine::Vector3& surfacePoint,
    const engine::Vector3& surfaceNormal,
    const DecalSpawnDesc& desc) {
    if (!common) return nullptr;

    // Decalは通常のObject3dをPlaneとして生成し、照明・影・衝突を無効にする。
    auto decal = std::make_unique<engine::Object3d>();
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

    engine::Object3d::ColliderConfig collider = decal->GetColliderConfig();
    collider.type = ColliderType::kNone;
    decal->SetColliderConfig(collider);

    // 尺寸や時間を有効範囲へ揃え、再生状態を初期化してから面に置く。
    engine::Object3d::DecalSettings settings;
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

engine::Object3d* DecalSystem::Spawn(
    BaseScene& scene,
    const engine::Vector3& surfacePoint,
    const engine::Vector3& surfaceNormal,
    const DecalSpawnDesc& desc) {
    std::unique_ptr<engine::Object3d> decal = Create(
        scene.GetObject3dCommon(), surfacePoint, surfaceNormal, desc);
    if (!decal) return nullptr;
    // 所有権をSceneへ移したあと、実際にSceneへ保持されたときだけ非所有ポインタを返す。
    engine::Object3d* result = decal.get();
    scene.AddObject(std::move(decal));
    for (const std::unique_ptr<engine::Object3d>& ownedObject : scene.GetObjects()) {
        if (ownedObject.get() == result) {
            return result;
        }
    }
    return nullptr;
}

engine::Object3d* DecalSystem::SpawnFromRaycast(
    BaseScene& scene,
    const engine::Vector3& rayStart,
    const engine::Vector3& rayDirection,
    float maxDistance,
    uint32_t collisionMask,
    const DecalSpawnDesc& desc) {
    if (maxDistance <= 0.0f || engine::Math::Length(rayDirection) <= 0.0001f) return nullptr;
    const RaycastHit hit = CollisionManager::GetInstance()->Raycast(
        rayStart, SafeNormal(rayDirection), maxDistance, collisionMask);
    if (!hit.isHit) return nullptr;
    return Spawn(scene, hit.hitPoint, hit.normal, desc);
}

void DecalSystem::AlignToSurface(
    engine::Object3d& object,
    const engine::Vector3& surfacePoint,
    const engine::Vector3& surfaceNormal,
    float rotationRadians,
    float depthOffset) {
    // Z-fightingを避けるため法線方向へ少し浮かせ、Planeの寸法と向きを接地面に揃える。
    const engine::Vector3 normal = SafeNormal(surfaceNormal);
    engine::Object3d::DecalSettings settings = object.GetDecalSettings();
    settings.depthOffset = (std::max)(depthOffset, 0.0001f);
    object.SetDecalSettings(settings);
    object.SetTranslate(surfacePoint + normal * settings.depthOffset);
    object.SetScale({ settings.size.x * 0.5f, settings.size.y * 0.5f, 1.0f });
    const engine::Quaternion rotation = BuildSurfaceQuaternion(normal, rotationRadians);
    object.SetQuaternion(rotation);
    object.SetEulerAngles(engine::Math::MatrixToEuler(engine::Math::MakeRotateQuaternionMatrix(rotation)));
    object.UpdateLocalMatrix();
    object.UpdateWorldMatrix();
}

void engine::Object3d::SetDecalSettings(const DecalSettings& settings) {
    // デカールの寸法と演出時間を安全な範囲へ丸め、無効化時は経過時間を戻す。
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

void engine::Object3d::RestartDecalPlayback() {
    decalElapsedTime_ = 0.0f;
    decalAuthoredAlpha_ = GetColor().w;
    if (decalSettings_.enabled) {
        SetIsVisible(true);
        isDead = false;
    }
}
