#include "Collider.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
// XZ断面の分離軸。輪郭と相手の断面の両方の辺を調べ、角の空白も除外する。
bool OverlapOnAxes(const std::vector<engine::Vector3>& a, const std::vector<engine::Vector3>& b,
                   float& minimumDepth, engine::Vector3& outwardNormal) {
    // 両輪郭の辺法線を全て試し、角で接していない領域を矩形判定から除外する。
    const auto testEdges = [&](const std::vector<engine::Vector3>& polygon) {
        for (size_t i = 0; i < polygon.size(); ++i) {
            const engine::Vector3 edge = polygon[(i + 1) % polygon.size()] - polygon[i];
            const float length = std::sqrt(edge.x * edge.x + edge.z * edge.z);
            if (length < 0.00001f) continue;
            const engine::Vector3 axis = { -edge.z / length, 0.0f, edge.x / length };
            float minA = std::numeric_limits<float>::max(), maxA = -minA;
            float minB = std::numeric_limits<float>::max(), maxB = -minB;
            for (const auto& point : a) {
                const float value = point.x * axis.x + point.z * axis.z;
                minA = (std::min)(minA, value); maxA = (std::max)(maxA, value);
            }
            for (const auto& point : b) {
                const float value = point.x * axis.x + point.z * axis.z;
                minB = (std::min)(minB, value); maxB = (std::max)(maxB, value);
            }
            const float overlap = (std::min)(maxA, maxB) - (std::max)(minA, minB);
            if (overlap <= 0.0f) return false;
            if (overlap < minimumDepth) {
                minimumDepth = overlap;
                const float middleA = (minA + maxA) * 0.5f;
                const float middleB = (minB + maxB) * 0.5f;
                outwardNormal = axis * (middleB >= middleA ? 1.0f : -1.0f);
            }
        }
        return true;
    };
    return testEdges(a) && testEdges(b);
}
engine::Vector3 CrossSurface(const engine::Vector3& a, const engine::Vector3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

float DotSurface(const engine::Vector3& a, const engine::Vector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

engine::Vector3 UpwardSurfaceNormal(const engine::Vector3& a, const engine::Vector3& b,
                                    const engine::Vector3& c) {
    engine::Vector3 normal = CrossSurface(b - a, c - a);
    const float length = std::sqrt(DotSurface(normal, normal));
    if (length <= 0.000001f) return {0.0f, 1.0f, 0.0f};
    normal = normal * (1.0f / length);
    return normal.y < 0.0f ? normal * -1.0f : normal;
}
} // namespace

// コンストラクタ
Collider::Collider(Transform* ownerTransform) {
    // 持ち主のTransformを記憶しておく
    transform_ = ownerTransform;
}

engine::Vector3 Collider::GetCollisionScale() const {
    // 見た目だけを伸縮させる物は、判定用の固定スケールを優先する。
    if (!transform_) {
        return { 1.0f, 1.0f, 1.0f };
    }
    return useScaleOverride_ ? scaleOverride_ : transform_->scale;
}

engine::Matrix4x4 Collider::GetCollisionWorldMatrix() const {
    // スケール上書き時も親の座標変換は維持し、表示行列とは別に判定行列を組む。
    if (!transform_ || !useScaleOverride_) {
        return transform_ ? transform_->matWorld : engine::Math::MakeIdentity4x4();
    }

    engine::Math math;
    engine::Quaternion rotation = transform_->quaternion;
    if (!transform_->isQuaternionMaster) {
        rotation = math.EulerToQuaternion(transform_->rotate);
    }

    const engine::Matrix4x4 local = math.Multiply(
        math.Multiply(math.MakeScaleMatrix(scaleOverride_), math.MakeRotateQuaternionMatrix(rotation)),
        math.MakeTranslateMatrix(transform_->translate));
    return transform_->parent ? math.Multiply(local, transform_->parent->matWorld) : local;
}

float Collider::GetRadius() const {
    if (!transform_) return config_.size.x;

    // スケールの最大値を掛ける
    const engine::Vector3 collisionScale = GetCollisionScale();
    float maxScale = (std::max)({
        std::abs(collisionScale.x),
        std::abs(collisionScale.y),
        std::abs(collisionScale.z)
        });
    return config_.size.x * maxScale;
}

engine::Vector3 Collider::GetSphereCenter() const {
    // ローカル中心を判定用行列で変換し、見た目の拡縮と判定のずれを防ぐ。
    if (!transform_) {
        return config_.center;
    }

    engine::Math math;
    engine::Matrix4x4 matTrans = math.MakeTranslateMatrix(config_.center);
    engine::Matrix4x4 matFinal = math.Multiply(matTrans, GetCollisionWorldMatrix());
    return { matFinal.m[3][0], matFinal.m[3][1], matFinal.m[3][2] };
}

OBB Collider::GetOBB() const {
    OBB obb;
    if (!transform_) return obb;

    engine::Math math;

    // 1. コライダーのローカル行列
    engine::Matrix4x4 matRotX = math.MakeRotateXMatrix(config_.rotation.x);
    engine::Matrix4x4 matRotY = math.MakeRotateYMatrix(config_.rotation.y);
    engine::Matrix4x4 matRotZ = math.MakeRotateZMatrix(config_.rotation.z);
    engine::Matrix4x4 matRot = math.Multiply(matRotZ, math.Multiply(matRotX, matRotY));

    engine::Matrix4x4 matTrans = math.MakeTranslateMatrix(config_.center);
    engine::Matrix4x4 matColliderLocal = math.Multiply(matRot, matTrans);

    // 2. 持ち主のワールド行列と合成
    engine::Matrix4x4 matFinal = math.Multiply(matColliderLocal, GetCollisionWorldMatrix());

    // 3. 情報抽出
    obb.center = { matFinal.m[3][0], matFinal.m[3][1], matFinal.m[3][2] };
    obb.orientations[0] = math.Normalize({ matFinal.m[0][0], matFinal.m[0][1], matFinal.m[0][2] });
    obb.orientations[1] = math.Normalize({ matFinal.m[1][0], matFinal.m[1][1], matFinal.m[1][2] });
    obb.orientations[2] = math.Normalize({ matFinal.m[2][0], matFinal.m[2][1], matFinal.m[2][2] });

    const engine::Vector3 collisionScale = GetCollisionScale();
    obb.size = {
        config_.size.x * collisionScale.x,
        config_.size.y * collisionScale.y,
        config_.size.z * collisionScale.z
    };

    return obb;
}

AABB Collider::GetAABB() const {
    if (config_.type == ColliderType::kAABB && transform_) {
        // AABBは所有Objectの見た目の傾きへ追従させません。
        // 親Transformは維持しつつ、ローカル回転だけを除いた行列で中心を求めます。
        engine::Math math;
        const engine::Vector3 collisionScale = GetCollisionScale();
        engine::Matrix4x4 ownerMatrix = math.Multiply(
            math.MakeScaleMatrix(collisionScale),
            math.MakeTranslateMatrix(transform_->translate));
        if (transform_->parent) {
            ownerMatrix = math.Multiply(ownerMatrix, transform_->parent->matWorld);
        }

        const engine::Matrix4x4 centerMatrix = math.Multiply(
            math.MakeTranslateMatrix(config_.center), ownerMatrix);
        const engine::Vector3 center = {
            centerMatrix.m[3][0],
            centerMatrix.m[3][1],
            centerMatrix.m[3][2]
        };
        const engine::Vector3 extent = {
            std::abs(config_.size.x * collisionScale.x),
            std::abs(config_.size.y * collisionScale.y),
            std::abs(config_.size.z * collisionScale.z)
        };
        return { center - extent, center + extent };
    }

    OBB obb = GetOBB();
    engine::Vector3 axisX = obb.orientations[0] * obb.size.x;
    engine::Vector3 axisY = obb.orientations[1] * obb.size.y;
    engine::Vector3 axisZ = obb.orientations[2] * obb.size.z;

    engine::Vector3 corners[8] = {
        obb.center - axisX - axisY - axisZ,
        obb.center + axisX - axisY - axisZ,
        obb.center - axisX + axisY - axisZ,
        obb.center + axisX + axisY - axisZ,
        obb.center - axisX - axisY + axisZ,
        obb.center + axisX - axisY + axisZ,
        obb.center - axisX + axisY + axisZ,
        obb.center + axisX + axisY + axisZ
    };

    engine::Vector3 minPos = corners[0];
    engine::Vector3 maxPos = corners[0];

    for (int i = 1; i < 8; ++i) {
        minPos.x = std::min(minPos.x, corners[i].x);
        minPos.y = std::min(minPos.y, corners[i].y);
        minPos.z = std::min(minPos.z, corners[i].z);

        maxPos.x = std::max(maxPos.x, corners[i].x);
        maxPos.y = std::max(maxPos.y, corners[i].y);
        maxPos.z = std::max(maxPos.z, corners[i].z);
    }

    return { minPos, maxPos };
}

std::vector<engine::Vector3> Collider::GetFootprintWorldVertices(float localY) const {
    // 床の輪郭点を回転・親変換込みのワールド座標へ移し、見た目の縁と揃える。
    std::vector<engine::Vector3> vertices;
    if (config_.footprint.size() < 3 || !transform_ ||
        (config_.type != ColliderType::kAABB && config_.type != ColliderType::kOBB)) return vertices;
    vertices.reserve(config_.footprint.size());
    engine::Math math;
    const engine::Matrix4x4 rotation = math.Multiply(
        math.MakeRotateZMatrix(config_.rotation.z),
        math.Multiply(math.MakeRotateXMatrix(config_.rotation.x), math.MakeRotateYMatrix(config_.rotation.y)));
    const engine::Matrix4x4 localToWorld = math.Multiply(
        math.Multiply(rotation, math.MakeTranslateMatrix(config_.center)), GetCollisionWorldMatrix());
    for (const engine::Vector2& vertex : config_.footprint) {
        vertices.push_back(engine::Math::Transform({ vertex.x, localY, vertex.y }, localToWorld));
    }
    return vertices;
}

bool Collider::ContainsFootprintPoint(const engine::Vector3& worldPoint) const {
    // XZ 断面の辺を同じ向きに回る点だけ内側とし、四角形の余白を除く。
    const auto vertices = GetFootprintWorldVertices(config_.size.y);
    if (vertices.size() < 3) return false;
    int winding = 0;
    for (size_t i = 0; i < vertices.size(); ++i) {
        const engine::Vector3& a = vertices[i];
        const engine::Vector3& b = vertices[(i + 1) % vertices.size()];
        const float cross = (b.x - a.x) * (worldPoint.z - a.z) -
                            (b.z - a.z) * (worldPoint.x - a.x);
        if (std::abs(cross) < 0.0001f) continue;
        const int sign = cross > 0.0f ? 1 : -1;
        if (winding != 0 && sign != winding) return false;
        winding = sign;
    }
    return true;
}

bool Collider::SampleModelSurface(const engine::Vector3& worldPoint, float& outHeight,
                                  engine::Vector3& outNormal) const {
    // 上向き三角形をXZへ投影し、接地位置にある最も高い面を採用する。
    if (!HasModelSurface() || !transform_) return false;
    const engine::Matrix4x4 world = GetCollisionWorldMatrix();
    bool found = false;
    float bestHeight = -std::numeric_limits<float>::max();
    for (const ModelSurfaceTriangle& triangle : modelSurface_) {
        const engine::Vector3 a = engine::Math::Transform(triangle.a, world);
        const engine::Vector3 b = engine::Math::Transform(triangle.b, world);
        const engine::Vector3 c = engine::Math::Transform(triangle.c, world);
        const float denominator = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
        if (std::abs(denominator) < 0.000001f) continue;
        const float u = ((b.z - c.z) * (worldPoint.x - c.x) +
                         (c.x - b.x) * (worldPoint.z - c.z)) / denominator;
        const float v = ((c.z - a.z) * (worldPoint.x - c.x) +
                         (a.x - c.x) * (worldPoint.z - c.z)) / denominator;
        const float w = 1.0f - u - v;
        if (u < -0.0001f || v < -0.0001f || w < -0.0001f) continue;
        const float height = u * a.y + v * b.y + w * c.y;
        if (!found || height > bestHeight) {
            bestHeight = height;
            outNormal = UpwardSurfaceNormal(a, b, c);
            found = true;
        }
    }
    if (found) outHeight = bestHeight;
    return found;
}

bool Collider::RaycastModelSurface(const engine::Vector3& start, const engine::Vector3& direction,
                                   float maxDistance, float& outDistance,
                                   engine::Vector3& outNormal) const {
    // 岩の上面へレイを飛ばし、箱の天面に先に当たる誤判定を避ける。
    if (!HasModelSurface() || !transform_ || maxDistance < 0.0f) return false;
    const engine::Matrix4x4 world = GetCollisionWorldMatrix();
    bool found = false;
    float nearest = maxDistance;
    for (const ModelSurfaceTriangle& triangle : modelSurface_) {
        const engine::Vector3 a = engine::Math::Transform(triangle.a, world);
        const engine::Vector3 b = engine::Math::Transform(triangle.b, world);
        const engine::Vector3 c = engine::Math::Transform(triangle.c, world);
        const engine::Vector3 edge1 = b - a;
        const engine::Vector3 edge2 = c - a;
        const engine::Vector3 p = CrossSurface(direction, edge2);
        const float determinant = DotSurface(edge1, p);
        if (std::abs(determinant) < 0.000001f) continue;
        const float inverse = 1.0f / determinant;
        const engine::Vector3 fromA = start - a;
        const float u = DotSurface(fromA, p) * inverse;
        if (u < 0.0f || u > 1.0f) continue;
        const engine::Vector3 q = CrossSurface(fromA, edge1);
        const float v = DotSurface(direction, q) * inverse;
        if (v < 0.0f || u + v > 1.0f) continue;
        const float distance = DotSurface(edge2, q) * inverse;
        if (distance < 0.0f || distance > nearest) continue;
        nearest = distance;
        outNormal = UpwardSurfaceNormal(a, b, c);
        found = true;
    }
    if (found) outDistance = nearest;
    return found;
}

bool Collider::OverlapsFootprint(const Collider* other, engine::Vector3* normal, float* depth) const {
    if (!other || config_.footprint.size() < 3) return false;
    const std::vector<engine::Vector3> polygon = GetFootprintWorldVertices(config_.size.y);
    if (polygon.size() < 3) return false;

    float minimumDepth = std::numeric_limits<float>::max();
    engine::Vector3 outward = { 0.0f, 0.0f, 0.0f };
    if (other->GetType() == ColliderType::kSphere) {
        const engine::Vector3 center = other->GetSphereCenter();
        const float radius = other->GetRadius();
        // 円と各辺・最も近い頂点を分離軸で調べる。
        std::vector<engine::Vector3> axes;
        axes.reserve(polygon.size() + 1);
        float nearestDistance = std::numeric_limits<float>::max();
        engine::Vector3 nearestVertex{};
        for (size_t i = 0; i < polygon.size(); ++i) {
            const engine::Vector3 edge = polygon[(i + 1) % polygon.size()] - polygon[i];
            const float length = std::sqrt(edge.x * edge.x + edge.z * edge.z);
            if (length > 0.00001f) axes.push_back({ -edge.z / length, 0.0f, edge.x / length });
            const engine::Vector3 delta = center - polygon[i];
            const float distance = delta.x * delta.x + delta.z * delta.z;
            if (distance < nearestDistance) { nearestDistance = distance; nearestVertex = polygon[i]; }
        }
        const engine::Vector3 vertexDelta = center - nearestVertex;
        if (nearestDistance > 0.0000001f) {
            const float length = std::sqrt(nearestDistance);
            axes.push_back({ vertexDelta.x / length, 0.0f, vertexDelta.z / length });
        }
        for (const engine::Vector3& axis : axes) {
            float minimum = std::numeric_limits<float>::max(), maximum = -minimum;
            for (const engine::Vector3& point : polygon) {
                const float projection = point.x * axis.x + point.z * axis.z;
                minimum = (std::min)(minimum, projection); maximum = (std::max)(maximum, projection);
            }
            const float projectedCenter = center.x * axis.x + center.z * axis.z;
            const float overlap = (std::min)(maximum, projectedCenter + radius) -
                                  (std::max)(minimum, projectedCenter - radius);
            if (overlap <= 0.0f) return false;
            if (overlap < minimumDepth) {
                minimumDepth = overlap;
                outward = axis * (projectedCenter >= (minimum + maximum) * 0.5f ? 1.0f : -1.0f);
            }
        }
    } else {
        std::vector<engine::Vector3> section;
        if (other->GetType() == ColliderType::kOBB) {
            const OBB box = other->GetOBB();
            const engine::Vector3 x = box.orientations[0] * box.size.x;
            const engine::Vector3 z = box.orientations[2] * box.size.z;
            section = { box.center - x - z, box.center + x - z,
                        box.center + x + z, box.center - x + z };
        } else {
            const AABB box = other->GetAABB();
            section = { { box.min.x, 0.0f, box.min.z }, { box.max.x, 0.0f, box.min.z },
                        { box.max.x, 0.0f, box.max.z }, { box.min.x, 0.0f, box.max.z } };
        }
        if (!OverlapOnAxes(polygon, section, minimumDepth, outward)) return false;
    }
    if (normal) *normal = outward;
    if (depth) *depth = minimumDepth;
    return true;
}

Ring Collider::GetRing() const {
    Ring ring;
    if (!transform_) return ring;

    engine::Math math;

    // 1. 回転を計算
    engine::Matrix4x4 matRotX = math.MakeRotateXMatrix(config_.rotation.x);
    engine::Matrix4x4 matRotY = math.MakeRotateYMatrix(config_.rotation.y);
    engine::Matrix4x4 matRotZ = math.MakeRotateZMatrix(config_.rotation.z);
    engine::Matrix4x4 matRot = math.Multiply(matRotZ, math.Multiply(matRotX, matRotY));

    // 2. 持ち主のワールド回転と合成
    // matWorld から回転成分だけ抽出するのが理想だが、ここでは簡易的に normal を変換
    engine::Vector3 localNormal = { 0, 1, 0 }; // ローカル空間でのリングの向き (上向き)
    
    // 回転行列を適用
    engine::Matrix4x4 worldRot = math.Multiply(matRot, GetCollisionWorldMatrix());
    // スケールを除去した回転成分だけ取り出すのが正しいが、
    // ここでは normal を変換して正規化する
    ring.normal = math.Normalize({
        localNormal.x * worldRot.m[0][0] + localNormal.y * worldRot.m[1][0] + localNormal.z * worldRot.m[2][0],
        localNormal.x * worldRot.m[0][1] + localNormal.y * worldRot.m[1][1] + localNormal.z * worldRot.m[2][1],
        localNormal.x * worldRot.m[0][2] + localNormal.y * worldRot.m[1][2] + localNormal.z * worldRot.m[2][2]
    });

    // 3. 中心座標 (オフセット加味)
    engine::Matrix4x4 matTrans = math.MakeTranslateMatrix(config_.center);
    engine::Matrix4x4 matFinal = math.Multiply(matTrans, GetCollisionWorldMatrix());
    ring.center = { matFinal.m[3][0], matFinal.m[3][1], matFinal.m[3][2] };

    // 4. サイズ (スケールを反映)
    // X を外径、Z を内径、Y を厚みとして扱う
    const engine::Vector3 collisionScale = GetCollisionScale();
    ring.outerRadius = config_.size.x * std::abs(collisionScale.x);
    ring.innerRadius = config_.size.z * std::abs(collisionScale.z);
    ring.height = config_.size.y * 2.0f * std::abs(collisionScale.y);

    // 外径が内径より小さくならないように調整
    if (ring.outerRadius < ring.innerRadius) {
        std::swap(ring.outerRadius, ring.innerRadius);
    }

    return ring;
}

Cylinder Collider::GetCylinder() const {
    Cylinder cyl;
    if (!transform_) return cyl;
    engine::Math math;

    // 1. 回転
    engine::Matrix4x4 matRotX = math.MakeRotateXMatrix(config_.rotation.x);
    engine::Matrix4x4 matRotY = math.MakeRotateYMatrix(config_.rotation.y);
    engine::Matrix4x4 matRotZ = math.MakeRotateZMatrix(config_.rotation.z);
    engine::Matrix4x4 matRot = math.Multiply(matRotZ, math.Multiply(matRotX, matRotY));

    // 2. 中心 (オフセット加味)
    engine::Matrix4x4 matTrans = math.MakeTranslateMatrix(config_.center);
    engine::Matrix4x4 matFinal = math.Multiply(math.Multiply(matRot, matTrans), GetCollisionWorldMatrix());
    cyl.center = { matFinal.m[3][0], matFinal.m[3][1], matFinal.m[3][2] };

    // 3. 軸 (Y軸を想定)
    engine::Vector3 localAxis = { 0, 1, 0 };
    engine::Matrix4x4 worldRot = math.Multiply(matRot, GetCollisionWorldMatrix());
    cyl.axis = math.Normalize({
        localAxis.x * worldRot.m[0][0] + localAxis.y * worldRot.m[1][0] + localAxis.z * worldRot.m[2][0],
        localAxis.x * worldRot.m[0][1] + localAxis.y * worldRot.m[1][1] + localAxis.z * worldRot.m[2][1],
        localAxis.x * worldRot.m[0][2] + localAxis.y * worldRot.m[1][2] + localAxis.z * worldRot.m[2][2]
    });

    // 4. サイズ
    const engine::Vector3 collisionScale = GetCollisionScale();
    cyl.radius = config_.size.x * std::abs(collisionScale.x);
    cyl.height = config_.size.y * 2.0f * std::abs(collisionScale.y);

    return cyl;
}

bool Collider::SampleTerrain(const Collider* terrain, const engine::Vector3& worldPosition, float& outHeight, engine::Vector3& outNormal) const {
    // ワールド位置を地形の格子へ戻し、補間した高さと法線を衝突側へ渡す。
    if (!terrain || !terrain->transform_) return false;
    const TerrainCollisionData& data = terrain->terrainData_;
    if (!data.enabled || data.resolution <= 0) return false;

    const int sampleCount = data.resolution + 1;
    if (static_cast<int>(data.heights.size()) < sampleCount * sampleCount) return false;

    const engine::Vector3 terrainPos = terrain->transform_->translate;
    const engine::Vector3 terrainScale = terrain->transform_->scale;
    const float scaleX = std::max(0.0001f, std::abs(terrainScale.x));
    const float scaleY = std::max(0.0001f, std::abs(terrainScale.y));
    const float scaleZ = std::max(0.0001f, std::abs(terrainScale.z));
    const float localX = (worldPosition.x - terrainPos.x) / scaleX;
    const float localZ = (worldPosition.z - terrainPos.z) / scaleZ;
    const float u = localX / std::max(0.0001f, data.sizeX) + 0.5f;
    const float v = localZ / std::max(0.0001f, data.sizeZ) + 0.5f;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return false;

    const float fx = u * static_cast<float>(data.resolution);
    const float fz = v * static_cast<float>(data.resolution);
    const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, data.resolution);
    const int z0 = std::clamp(static_cast<int>(std::floor(fz)), 0, data.resolution);
    const int x1 = std::clamp(x0 + 1, 0, data.resolution);
    const int z1 = std::clamp(z0 + 1, 0, data.resolution);
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);

    auto heightAt = [&](int x, int z) -> float {
        return data.heights[static_cast<size_t>(z * sampleCount + x)];
    };
    const float h00 = heightAt(x0, z0);
    const float h10 = heightAt(x1, z0);
    const float h01 = heightAt(x0, z1);
    const float h11 = heightAt(x1, z1);
    const float hx0 = h00 + (h10 - h00) * tx;
    const float hx1 = h01 + (h11 - h01) * tx;
    const float localHeight = hx0 + (hx1 - hx0) * tz;
    outHeight = terrainPos.y + localHeight * scaleY;

    const int xm = std::clamp(x0 - 1, 0, data.resolution);
    const int xp = std::clamp(x0 + 1, 0, data.resolution);
    const int zm = std::clamp(z0 - 1, 0, data.resolution);
    const int zp = std::clamp(z0 + 1, 0, data.resolution);
    const float cellX = std::max(0.0001f, data.sizeX / static_cast<float>(data.resolution) * scaleX);
    const float cellZ = std::max(0.0001f, data.sizeZ / static_cast<float>(data.resolution) * scaleZ);
    const float gradX = ((heightAt(xp, z0) - heightAt(xm, z0)) * scaleY) / cellX;
    const float gradZ = ((heightAt(x0, zp) - heightAt(x0, zm)) * scaleY) / cellZ;
    outNormal = { -gradX, 1.0f, -gradZ };
    const float len = std::sqrt(outNormal.x * outNormal.x + outNormal.y * outNormal.y + outNormal.z * outNormal.z);
    if (len > 0.0001f) {
        outNormal.x /= len;
        outNormal.y /= len;
        outNormal.z /= len;
    } else {
        outNormal = { 0.0f, 1.0f, 0.0f };
    }
    return true;
}

CollisionInfo Collider::CheckSphereTerrainCollision(const engine::Vector3& spherePos, float radius, const Collider* terrain) const {
    // 球の底が地形面を跨いだ場合だけ、法線方向へ押し戻す情報を作る。
    CollisionInfo collision;
    float terrainHeight = 0.0f;
    engine::Vector3 normal = { 0.0f, 1.0f, 0.0f };
    if (!SampleTerrain(terrain, spherePos, terrainHeight, normal)) return collision;

    const float bottom = spherePos.y - radius;
    const float top = spherePos.y + radius;
    if (bottom <= terrainHeight && top >= terrainHeight - radius * 0.25f) {
        collision.isColliding = true;
        collision.normal = normal;
        collision.penetration = terrainHeight - bottom;
    }
    return collision;
}

CollisionInfo Collider::CheckAABBTerrainCollision(const AABB& aabb, const Collider* terrain) const {
    // 中心と四隅を採点し、斜面で最も深く侵入した位置を基準にする。
    CollisionInfo collision;
    const engine::Vector3 points[5] = {
        { (aabb.min.x + aabb.max.x) * 0.5f, aabb.min.y, (aabb.min.z + aabb.max.z) * 0.5f },
        { aabb.min.x, aabb.min.y, aabb.min.z },
        { aabb.max.x, aabb.min.y, aabb.min.z },
        { aabb.min.x, aabb.min.y, aabb.max.z },
        { aabb.max.x, aabb.min.y, aabb.max.z },
    };

    float bestPenetration = 0.0f;
    engine::Vector3 bestNormal = { 0.0f, 1.0f, 0.0f };
    for (const engine::Vector3& point : points) {
        float terrainHeight = 0.0f;
        engine::Vector3 normal = { 0.0f, 1.0f, 0.0f };
        if (!SampleTerrain(terrain, point, terrainHeight, normal)) continue;
        if (aabb.min.y <= terrainHeight && aabb.max.y >= terrainHeight - 0.05f) {
            const float penetration = terrainHeight - aabb.min.y;
            if (penetration > bestPenetration) {
                bestPenetration = penetration;
                bestNormal = normal;
            }
        }
    }

    if (bestPenetration > 0.0f) {
        collision.isColliding = true;
        collision.normal = bestNormal;
        collision.penetration = bestPenetration;
    }
    return collision;
}

CollisionInfo Collider::CheckCollision(const Collider* other) const {
    CollisionInfo collision;
    collision.isColliding = false;

    if (!other || !transform_ || !other->transform_) return collision;

    ColliderType myType = this->GetType();
    ColliderType otherType = other->GetType();
    if (myType == ColliderType::kNone || otherType == ColliderType::kNone) {
        return collision;
    }

    // 自身のワールド座標 
    engine::Vector3 myPos = myType == ColliderType::kSphere ? GetSphereCenter() : transform_->translate;
    engine::Vector3 otherPos = otherType == ColliderType::kSphere ? other->GetSphereCenter() : other->transform_->translate;

    if (myType == ColliderType::kSphere && otherType == ColliderType::kTerrain) {
        collision = CheckSphereTerrainCollision(myPos, this->GetRadius(), other);
    } else if (myType == ColliderType::kTerrain && otherType == ColliderType::kSphere) {
        collision = other->CheckSphereTerrainCollision(otherPos, other->GetRadius(), this);
        collision.normal = collision.normal * -1.0f;
    } else if ((myType == ColliderType::kAABB || myType == ColliderType::kOBB || myType == ColliderType::kCylinder) && otherType == ColliderType::kTerrain) {
        collision = CheckAABBTerrainCollision(this->GetAABB(), other);
    } else if (myType == ColliderType::kTerrain && (otherType == ColliderType::kAABB || otherType == ColliderType::kOBB || otherType == ColliderType::kCylinder)) {
        collision = other->CheckAABBTerrainCollision(other->GetAABB(), this);
        collision.normal = collision.normal * -1.0f;
    }
    // --- 同種形状 ---
    else if (myType == ColliderType::kAABB && otherType == ColliderType::kAABB) {
        collision = CheckAABBCollision(this->GetAABB(), other->GetAABB());
    } else if (myType == ColliderType::kSphere && otherType == ColliderType::kSphere) {
        collision = CheckSphereCollision(
            myPos, this->GetRadius(),
            otherPos, other->GetRadius());
    } else if (myType == ColliderType::kOBB && otherType == ColliderType::kOBB) {
        collision = CheckOBBCollision(this->GetOBB(), other->GetOBB());
    }
    // --- 異種形状 ---
    else if (myType == ColliderType::kSphere && otherType == ColliderType::kAABB) {
        collision = CheckSphereAABBCollision(myPos, this->GetRadius(), other->GetAABB());
    } else if (myType == ColliderType::kAABB && otherType == ColliderType::kSphere) {
        collision = CheckSphereAABBCollision(otherPos, other->GetRadius(), this->GetAABB());
        collision.normal = collision.normal * -1.0f;
    } else if (myType == ColliderType::kSphere && otherType == ColliderType::kOBB) {
        collision = CheckSphereOBBCollision(myPos, this->GetRadius(), other->GetOBB());
    } else if (myType == ColliderType::kOBB && otherType == ColliderType::kSphere) {
        collision = CheckSphereOBBCollision(otherPos, other->GetRadius(), this->GetOBB());
        collision.normal = collision.normal * -1.0f;
    } else if (myType == ColliderType::kAABB && otherType == ColliderType::kOBB) {
        collision = CheckAABBOBBCollision(this->GetAABB(), other->GetOBB());

    } else if (myType == ColliderType::kOBB && otherType == ColliderType::kAABB) {
        collision = CheckAABBOBBCollision(other->GetAABB(), this->GetOBB());
        collision.normal = collision.normal * -1.0f;
    }
    // --- Ring 関連 ---
    else if (myType == ColliderType::kRing && otherType == ColliderType::kSphere) {
        collision = CheckRingSphereCollision(this->GetRing(), otherPos, other->GetRadius());
    } else if (myType == ColliderType::kSphere && otherType == ColliderType::kRing) {
        collision = CheckRingSphereCollision(other->GetRing(), myPos, this->GetRadius());
        collision.normal = collision.normal * -1.0f;
    }
    else if (myType == ColliderType::kRing && (otherType == ColliderType::kOBB || otherType == ColliderType::kAABB)) {
        collision = CheckRingOBBCollision(this->GetRing(), other->GetOBB());
    } else if ((myType == ColliderType::kOBB || myType == ColliderType::kAABB) && otherType == ColliderType::kRing) {
        collision = CheckRingOBBCollision(other->GetRing(), this->GetOBB());
        collision.normal = collision.normal * -1.0f;
    }

    // --- Cylinder 関連 ---
    else if (myType == ColliderType::kCylinder && otherType == ColliderType::kSphere) {
        collision = CheckCylinderSphereCollision(this->GetCylinder(), otherPos, other->GetRadius());
    } else if (myType == ColliderType::kSphere && otherType == ColliderType::kCylinder) {
        collision = CheckCylinderSphereCollision(other->GetCylinder(), myPos, this->GetRadius());
        collision.normal = collision.normal * -1.0f;
    }
    else if (myType == ColliderType::kCylinder && (otherType == ColliderType::kOBB || otherType == ColliderType::kAABB)) {
        collision = CheckCylinderOBBCollision(this->GetCylinder(), other->GetOBB());
    } else if ((myType == ColliderType::kOBB || myType == ColliderType::kAABB) && otherType == ColliderType::kCylinder) {
        collision = CheckCylinderOBBCollision(other->GetCylinder(), this->GetOBB());
        collision.normal = collision.normal * -1.0f;
    }

    if (collision.isColliding && (myType == ColliderType::kAABB || myType == ColliderType::kOBB) &&
        config_.footprint.size() >= 3) {
        engine::Vector3 horizontalNormal;
        float horizontalDepth = 0.0f;
        if (!OverlapsFootprint(other, &horizontalNormal, &horizontalDepth)) {
            return {};
        }
        if (horizontalDepth < collision.penetration) {
            collision.normal = horizontalNormal * -1.0f;
            collision.penetration = horizontalDepth;
        }
    }
    if (collision.isColliding && (otherType == ColliderType::kAABB || otherType == ColliderType::kOBB) &&
        other->config_.footprint.size() >= 3) {
        engine::Vector3 horizontalNormal;
        float horizontalDepth = 0.0f;
        if (!other->OverlapsFootprint(this, &horizontalNormal, &horizontalDepth)) {
            return {};
        }
        if (horizontalDepth < collision.penetration) {
            collision.normal = horizontalNormal;
            collision.penetration = horizontalDepth;
        }
    }
    if (collision.isColliding) {
        // 側面は従来形状で受け、上からの接触だけをモデルの実際の高さへ合わせる。
        const Collider* surface = HasModelSurface() ? this : (other->HasModelSurface() ? other : nullptr);
        const Collider* mover = surface == this ? other : this;
        if (surface && mover != surface &&
            (mover->GetType() == ColliderType::kSphere || mover->GetType() == ColliderType::kAABB ||
             mover->GetType() == ColliderType::kOBB || mover->GetType() == ColliderType::kCylinder)) {
            const AABB moverBounds = mover->GetAABB();
            const engine::Vector3 center = mover->GetType() == ColliderType::kSphere
                ? mover->GetSphereCenter() : (moverBounds.min + moverBounds.max) * 0.5f;
            const float bottom = mover->GetType() == ColliderType::kSphere
                ? center.y - mover->GetRadius() : moverBounds.min.y;
            float surfaceHeight = 0.0f;
            engine::Vector3 surfaceNormal{};
            if (surface->SampleModelSurface(center, surfaceHeight, surfaceNormal) &&
                center.y >= surfaceHeight - 0.12f) {
                if (bottom > surfaceHeight) {
                    collision = {};
                } else {
                    collision.normal = surface == other ? surfaceNormal : surfaceNormal * -1.0f;
                    collision.penetration = surfaceHeight - bottom;
                }
            }
        }
    }
    return collision;
}
