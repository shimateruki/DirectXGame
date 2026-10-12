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
    // 相対パスの地形判定はResources配下も探し、旧データの保存形式を受け入れる。
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
    // 親の祖先をたどり、循環する親子付けを拒否するための判定を返す。
    for (Object3d* current = parent; current != nullptr; current = current->GetParent()) {
        if (current == child) {
            return true;
        }
    }
    return false;
}

void ApplyMatrixToTransform(Transform& transform, const Matrix4x4& matrix) {
    // 行列を移動・倍率・回転へ分離し、親の変更後も見た目の姿勢を維持する。
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
    // 静的オブジェクトの形状変更は空間グリッドの再構築を要求する。
    if (collider_) {
        collider_->SetConfig(config);
        if (isStatic_) CollisionManager::GetInstance()->MarkStaticGridDirty();
    }
}

const Object3d::ColliderConfig& Object3d::GetColliderConfig() const {
    // Collider所有の設定を参照で返すため、呼び出し中はColliderが有効であることが前提。
    return collider_->GetConfig();
}

void Object3d::SetColliderType(ColliderType type) {
    // 形状種別の変更後は静的衝突グリッドを無効化し、古い登録位置を使わせない。
    if (!collider_) return;
    ColliderConfig config = collider_->GetConfig();
    config.type = type;
    collider_->SetConfig(config);
    if (isStatic_) CollisionManager::GetInstance()->MarkStaticGridDirty();
}

ColliderType Object3d::GetColliderType() const {
    // Colliderが無い場合は衝突しない形状として扱う。
    return collider_ ? collider_->GetType() : ColliderType::kNone;
}

void Object3d::SetCollisionSize(const Vector3& size) {
    // 大きさを変更した静的物体は衝突グリッドへ再登録する。
    if (!collider_) return;
    ColliderConfig config = collider_->GetConfig();
    config.size = size;
    collider_->SetConfig(config);
    if (isStatic_) CollisionManager::GetInstance()->MarkStaticGridDirty();
}

Vector3 Object3d::GetCollisionSize() const {
    // Collider未作成時は各軸0の範囲を返す。
    return collider_ ? collider_->GetSize() : Vector3{ 0,0,0 };
}

void Object3d::SetCollisionRadius(float radius) {
    // 半径を各軸へ渡すため、球など等方的な判定の大きさに使う。
    SetCollisionSize({ radius, radius, radius });
}

float Object3d::GetCollisionRadius() const {
    // Colliderが無い場合は有効な半径も無い。
    return collider_ ? collider_->GetRadius() : 0.0f;
}

void Object3d::SetCollisionAttribute(uint32_t attribute) {
    // 衝突カテゴリ変更は静的グリッド内の検索対象を変えるため、再構築を要求する。
    if (collider_) {
        collider_->SetAttribute(attribute);
        if (isStatic_) CollisionManager::GetInstance()->MarkStaticGridDirty();
    }
}

uint32_t Object3d::GetCollisionAttribute() const {
    // Collider未作成時のカテゴリ0は衝突対象外を示す。
    return collider_ ? collider_->GetAttribute() : 0;
}

void Object3d::SetCollisionMask(uint32_t mask) {
    // 衝突相手の選別条件が変わったら静的グリッドを更新する。
    if (collider_) {
        collider_->SetMask(mask);
        if (isStatic_) CollisionManager::GetInstance()->MarkStaticGridDirty();
    }
}

uint32_t Object3d::GetCollisionMask() const {
    // Colliderが無い場合は相手との接触を全て無効にする。
    return collider_ ? collider_->GetMask() : 0;
}

bool Object3d::LoadTerrainCollisionFromFile(const std::string& path) {
    // 高さ配列が解像度に足りないJSONは採用せず、既存の地形判定を保持する。
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
    // 判定本体と再読込用パスを同時に保存し、静的グリッドへ変更を通知する。
    if (!collider_) return;
    collider_->SetTerrainData(data);
    terrainCollisionPath_ = path;
    if (isStatic_) CollisionManager::GetInstance()->MarkStaticGridDirty();
}

const TerrainCollisionData* Object3d::GetTerrainCollisionData() const {
    // 地形判定が無効ならデータ参照を返さず、通常Colliderと区別する。
    if (!collider_) return nullptr;
    const TerrainCollisionData& data = collider_->GetTerrainData();
    return data.enabled ? &data : nullptr;
}

AABB Object3d::GetAABB() const {
    // Colliderが無ければ空の軸平行範囲を返す。
    return collider_ ? collider_->GetAABB() : AABB{};
}

OBB Object3d::GetOBB() const {
    // Colliderが無ければ空の回転付き範囲を返す。
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
    // 双方に有効な形状・カテゴリ・マスクがある場合だけ狭域判定へ進む。
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

