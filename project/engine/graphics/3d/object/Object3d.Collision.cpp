#define NOMINMAX
#include "Object3d.h"
#include "DirectXCommon.h"
#include "ModelManager.h"
#include "EffectObject3d.h"
#include "SRVManager.h"
#include "CameraManager.h"
#include "SceneManager.h"
#include "GhostRecorder.h"
#include "CollisionManager.h"
#include <cassert>
#include <algorithm> // min, max
#include <ParticleManager.h>
#include <GPUParticleManager.h>
#include <GPUParticleEmitter.h>
#include <DebugConsole.h>
#include <ProfilerManager.h>
#include <fstream>
#include <filesystem>

namespace {

std::filesystem::path ResolveTerrainCollisionFilePath(const std::string& path) {
    std::filesystem::path filePath(path);
    if (std::filesystem::exists(filePath)) {
        return filePath;
    }

    std::filesystem::path resourcesPath = std::filesystem::path("Resources") / filePath;
    if (std::filesystem::exists(resourcesPath)) {
        return resourcesPath;
    }

    return filePath;
}

bool HasParentInChain(Object3d* parent, const Object3d* child) {
    for (Object3d* current = parent; current != nullptr; current = current->GetParent()) {
        if (current == child) {
            return true;
        }
    }
    return false;
}

void ApplyMatrixToTransform(Transform& transform, const Matrix4x4& matrix) {
    const float epsilon = 0.0001f;

    Vector3 rowX = { matrix.m[0][0], matrix.m[0][1], matrix.m[0][2] };
    Vector3 rowY = { matrix.m[1][0], matrix.m[1][1], matrix.m[1][2] };
    Vector3 rowZ = { matrix.m[2][0], matrix.m[2][1], matrix.m[2][2] };

    transform.scale = {
        std::max(Math::Length(rowX), epsilon),
        std::max(Math::Length(rowY), epsilon),
        std::max(Math::Length(rowZ), epsilon),
    };
    transform.translate = { matrix.m[3][0], matrix.m[3][1], matrix.m[3][2] };

    Matrix4x4 rotateMatrix = Math::MakeIdentity4x4();
    rotateMatrix.m[0][0] = matrix.m[0][0] / transform.scale.x;
    rotateMatrix.m[0][1] = matrix.m[0][1] / transform.scale.x;
    rotateMatrix.m[0][2] = matrix.m[0][2] / transform.scale.x;
    rotateMatrix.m[1][0] = matrix.m[1][0] / transform.scale.y;
    rotateMatrix.m[1][1] = matrix.m[1][1] / transform.scale.y;
    rotateMatrix.m[1][2] = matrix.m[1][2] / transform.scale.y;
    rotateMatrix.m[2][0] = matrix.m[2][0] / transform.scale.z;
    rotateMatrix.m[2][1] = matrix.m[2][1] / transform.scale.z;
    rotateMatrix.m[2][2] = matrix.m[2][2] / transform.scale.z;

    transform.quaternion = Math::MatrixToQuaternion(rotateMatrix);
    transform.rotate = Math::MatrixToEuler(rotateMatrix);
    transform.isQuaternionMaster = true;
}

}

// ========================================================================
// Object3d コリジョン処理
// ------------------------------------------------------------------------
// Collider設定、地形コリジョン読み込み、AABB/OBB取得、衝突チェックを担当する。
// TransformやRenderer本体の更新処理とは分け、当たり判定の責務を追いやすくする。
// ========================================================================
// ========================================================================

void Object3d::SetColliderConfig(const ColliderConfig& config) {
    if (collider_) {
        collider_->SetConfig(config);
        if (isStatic_) CollisionManager::GetInstance()->MarkStaticGridDirty();
    }
}
const Object3d::ColliderConfig& Object3d::GetColliderConfig() const {
    return collider_->GetConfig();
}

void Object3d::SetColliderType(ColliderType type) {
    if (!collider_) return;
    ColliderConfig config = collider_->GetConfig();
    config.type = type;
    collider_->SetConfig(config);
    if (isStatic_) CollisionManager::GetInstance()->MarkStaticGridDirty();
}
ColliderType Object3d::GetColliderType() const {
    return collider_ ? collider_->GetType() : ColliderType::kNone;
}

void Object3d::SetCollisionSize(const Vector3& size) {
    if (!collider_) return;
    ColliderConfig config = collider_->GetConfig();
    config.size = size;
    collider_->SetConfig(config);
    if (isStatic_) CollisionManager::GetInstance()->MarkStaticGridDirty();
}
Vector3 Object3d::GetCollisionSize() const {
    return collider_ ? collider_->GetSize() : Vector3{ 0,0,0 };
}

void Object3d::SetCollisionRadius(float radius) {
    SetCollisionSize({ radius, radius, radius });
}
float Object3d::GetCollisionRadius() const {
    return collider_ ? collider_->GetRadius() : 0.0f;
}

void Object3d::SetCollisionAttribute(uint32_t attribute) {
    if (collider_) {
        collider_->SetAttribute(attribute);
        if (isStatic_) CollisionManager::GetInstance()->MarkStaticGridDirty();
    }
}
uint32_t Object3d::GetCollisionAttribute() const {
    return collider_ ? collider_->GetAttribute() : 0;
}

void Object3d::SetCollisionMask(uint32_t mask) {
    if (collider_) {
        collider_->SetMask(mask);
        if (isStatic_) CollisionManager::GetInstance()->MarkStaticGridDirty();
    }
}
uint32_t Object3d::GetCollisionMask() const {
    return collider_ ? collider_->GetMask() : 0;
}

bool Object3d::LoadTerrainCollisionFromFile(const std::string& path) {
    if (!collider_ || path.empty()) return false;

    std::ifstream file(ResolveTerrainCollisionFilePath(path));
    if (!file) return false;

    json data;
    try {
        file >> data;
    } catch (...) {
        return false;
    }
    if (!data.is_object()) return false;

    TerrainCollisionData terrain;
    terrain.enabled = true;
    terrain.resolution = data.value("resolution", 0);
    terrain.sizeX = data.value("sizeX", 1.0f);
    terrain.sizeZ = data.value("sizeZ", 1.0f);
    terrain.minHeight = data.value("minHeight", 0.0f);
    terrain.maxHeight = data.value("maxHeight", 0.0f);

    if (data.contains("heightSamples") && data["heightSamples"].is_array()) {
        const auto& samples = data["heightSamples"];
        if (!samples.empty() && samples.front().is_array()) {
            for (const auto& row : samples) {
                if (!row.is_array()) continue;
                for (const auto& value : row) {
                    if (value.is_number()) {
                        terrain.heights.push_back(value.get<float>());
                    }
                }
            }
        } else {
            for (const auto& value : samples) {
                if (value.is_number()) {
                    terrain.heights.push_back(value.get<float>());
                }
            }
        }
    }

    const int sampleCount = terrain.resolution + 1;
    if (terrain.resolution <= 0 ||
        static_cast<int>(terrain.heights.size()) < sampleCount * sampleCount) {
        return false;
    }

    SetTerrainCollisionData(terrain, path);
    return true;
}

void Object3d::SetTerrainCollisionData(const TerrainCollisionData& data, const std::string& path) {
    if (!collider_) return;
    collider_->SetTerrainData(data);
    terrainCollisionPath_ = path;
    if (isStatic_) CollisionManager::GetInstance()->MarkStaticGridDirty();
}

const TerrainCollisionData* Object3d::GetTerrainCollisionData() const {
    if (!collider_) return nullptr;
    const TerrainCollisionData& data = collider_->GetTerrainData();
    return data.enabled ? &data : nullptr;
}

AABB Object3d::GetAABB() const {
    return collider_ ? collider_->GetAABB() : AABB{};
}
OBB Object3d::GetOBB() const {
    return collider_ ? collider_->GetOBB() : OBB{};
}

AABB Object3d::GetModelWorldAABB() const {
    Model* model = GetModel();
    if (!model) {
        // モデルがない場合は位置を基準にデフォルトサイズ
        return { {transform_.translate.x - 0.5f, transform_.translate.y - 0.5f, transform_.translate.z - 0.5f},
                 {transform_.translate.x + 0.5f, transform_.translate.y + 0.5f, transform_.translate.z + 0.5f} };
    }

    Vector3 min = model->GetLocalAabbMin();
    Vector3 max = model->GetLocalAabbMax();

    // 8つの頂点をトランスフォーム
    Vector3 corners[8] = {
        {min.x, min.y, min.z},
        {min.x, min.y, max.z},
        {min.x, max.y, min.z},
        {min.x, max.y, max.z},
        {max.x, min.y, min.z},
        {max.x, min.y, max.z},
        {max.x, max.y, min.z},
        {max.x, max.y, max.z}
    };

    AABB worldAABB;
    worldAABB.min = { FLT_MAX, FLT_MAX, FLT_MAX };
    worldAABB.max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

    for (int i = 0; i < 8; ++i) {
        Vector3 worldPos = Math::Transform(corners[i], transform_.matWorld);
        worldAABB.min.x = (std::min)(worldAABB.min.x, worldPos.x);
        worldAABB.min.y = (std::min)(worldAABB.min.y, worldPos.y);
        worldAABB.min.z = (std::min)(worldAABB.min.z, worldPos.z);
        worldAABB.max.x = (std::max)(worldAABB.max.x, worldPos.x);
        worldAABB.max.y = (std::max)(worldAABB.max.y, worldPos.y);
        worldAABB.max.z = (std::max)(worldAABB.max.z, worldPos.z);
    }

    return worldAABB;
}

CollisionInfo Object3d::CheckCollision(Object3d* other) {
    if (!collider_ || !other || !other->GetCollider()) {
        CollisionInfo info;
        info.isColliding = false;
        return info;
    }
    if (GetColliderType() == ColliderType::kNone ||
        other->GetColliderType() == ColliderType::kNone ||
        GetCollisionAttribute() == 0 || other->GetCollisionAttribute() == 0 ||
        GetCollisionMask() == 0 || other->GetCollisionMask() == 0) {
        CollisionInfo info;
        info.isColliding = false;
        return info;
    }
    return collider_->CheckCollision(other->GetCollider());
}

// ========================================================================
// その他 (コピー、保存、レコーダー等)
// ========================================================================

