#include "engine/3d/CollisionConfig.h"
#include "engine/base/Math.h"
#include <algorithm> // std::min, std::max
#include <cmath>     // std::abs

// --- ヘルパー関数群 (実装) ---

// [1] AABB vs AABB 判定
CollisionInfo CheckAABBCollision(const AABB& a, const AABB& b) {
    CollisionInfo info;
    float overlapX = std::min(a.max.x, b.max.x) - std::max(a.min.x, b.min.x);
    float overlapY = std::min(a.max.y, b.max.y) - std::max(a.min.y, b.min.y);
    float overlapZ = std::min(a.max.z, b.max.z) - std::max(a.min.z, b.min.z);
    if (overlapX < 0 || overlapY < 0 || overlapZ < 0) {
        info.isColliding = false;
        return info;
    }
    info.isColliding = true;
    if (overlapY < overlapX && overlapY < overlapZ) {
        info.penetration = overlapY;
        Vector3 aCenter = (a.min + a.max) * 0.5f;
        Vector3 bCenter = (b.min + b.max) * 0.5f;
        info.normal = (aCenter.y < bCenter.y) ? Vector3{0, -1, 0} : Vector3{0, 1, 0};
    } else if (overlapX < overlapZ) {
        info.penetration = overlapX;
        Vector3 aCenter = (a.min + a.max) * 0.5f;
        Vector3 bCenter = (b.min + b.max) * 0.5f;
        info.normal = (aCenter.x < bCenter.x) ? Vector3{-1, 0, 0} : Vector3{1, 0, 0};
    } else {
        info.penetration = overlapZ;
        Vector3 aCenter = (a.min + a.max) * 0.5f;
        Vector3 bCenter = (b.min + b.max) * 0.5f;
        info.normal = (aCenter.z < bCenter.z) ? Vector3{0, 0, -1} : Vector3{0, 0, 1};
    }
    return info;
}

// [2] Sphere vs Sphere 判定
CollisionInfo CheckSphereCollision(
    const Vector3& posA, float rA, const Vector3& posB, float rB) {
    CollisionInfo info;
    Math math;
    Vector3 vecAtoB = posB - posA;
    float distance = math.Length(vecAtoB);
    float totalRadius = rA + rB;
    float penetration = totalRadius - distance;

    if (penetration > 0) {
        info.isColliding = true;
        info.penetration = penetration;
        if (distance > 0.001f) {
            info.normal = (vecAtoB / distance) * -1.0f;
        } else {
            info.normal = {1.0f, 0, 0};
        }
    } else {
        info.isColliding = false;
    }
    return info;
}

// [3] Sphere vs AABB 判定
CollisionInfo CheckSphereAABBCollision(
    const Vector3& spherePos, float sphereRadius, const AABB& aabb) {
    CollisionInfo info;
    Math math;

    Vector3 closestPoint = {
        math.Clamp(spherePos.x, aabb.min.x, aabb.max.x),
        math.Clamp(spherePos.y, aabb.min.y, aabb.max.y),
        math.Clamp(spherePos.z, aabb.min.z, aabb.max.z)
    };
    float distanceSq = math.Length(spherePos - closestPoint);
    
    if (distanceSq < (sphereRadius * sphereRadius)) {
        info.isColliding = true;
        if (distanceSq > 0.001f) {
            float distance = std::sqrt(distanceSq);
            info.penetration = sphereRadius - distance;
            info.normal = (spherePos - closestPoint) / distance;
        } else {
            Vector3 aabbCenter = (aabb.min + aabb.max) * 0.5f;
            Vector3 vecToCenter = aabbCenter - spherePos;
            if (math.Length(vecToCenter) < 0.001f) {
                info.normal = {1, 0, 0};
            } else {
                info.normal = math.Normalize(vecToCenter) * -1.0f;
            }
            info.penetration = sphereRadius;
        }
    } else {
        info.isColliding = false;
    }
    return info;
}