#define NOMINMAX
#include "game/scene/select/GameSelectScene.Internal.h"
#include "Object3d.h"
#include "CollisionManager.h"
#include "CollisionConfig.h"
#include <algorithm>
#include <cmath>

namespace StageSelectDetail {
float SelectClamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }

float SelectEaseOutCubic(float value) {
    value = SelectClamp01(value);
    const float inv = 1.0f - value;
    return 1.0f - inv * inv * inv;
}

Vector3 SelectLookAtRotation(const Vector3& eye, const Vector3& target) {
    Vector3 direction{target.x - eye.x, target.y - eye.y, target.z - eye.z};
    const float lengthSq = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
    if (lengthSq <= 0.000001f) {
        return {0.0f, 0.0f, 0.0f};
    }

    const float invLength = 1.0f / std::sqrt(lengthSq);
    direction.x *= invLength;
    direction.y *= invLength;
    direction.z *= invLength;

    const float pitch = -std::asin(std::clamp(direction.y, -0.98f, 0.98f));
    const float yaw = std::atan2(direction.x, direction.z);
    return {pitch, yaw, 0.0f};
}

Vector3 SelectNormalizeGateDirectionXZ(const Vector3& value, const Vector3& fallback) {
    Vector3 direction{value.x, 0.0f, value.z};
    float lengthSq = direction.x * direction.x + direction.z * direction.z;
    if (lengthSq <= 0.000001f) {
        direction = {fallback.x, 0.0f, fallback.z};
        lengthSq = direction.x * direction.x + direction.z * direction.z;
        if (lengthSq <= 0.000001f) {
            return {0.0f, 0.0f, 1.0f};
        }
    }

    const float invLength = 1.0f / std::sqrt(lengthSq);
    direction.x *= invLength;
    direction.z *= invLength;
    return direction;
}

float SelectResolveGateReturnGroundY(const Vector3& probePosition, const Vector3& gatePos,
                                     const Vector3& stageNodePosition, const Vector3& playerPosition,
                                     const Object3d* ignoredObject) {
    // ゲートの原点や縁は浮いていることがあるため、帰還演出の接地Yにはそのまま使いません。
    // まず着地予定XZから下向きレイを飛ばし、本当に床として登録されている位置を優先します。
    const float gateBasedGroundY = gatePos.y - kGateReturnGroundOffsetFromGateOrigin;
    const float fallbackGroundY = (std::min)({stageNodePosition.y, playerPosition.y, gateBasedGroundY});
    const float rayStartY =
        (std::max)({gatePos.y, stageNodePosition.y, playerPosition.y, fallbackGroundY}) + kGateReturnGroundProbeHeight;
    const Vector3 sampleOffsets[] = {
        {0.0f, 0.0f, 0.0f}, {0.65f, 0.0f, 0.0f}, {-0.65f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.65f}, {0.0f, 0.0f, -0.65f}};

    bool foundGround = false;
    float resolvedGroundY = fallbackGroundY;
    for (const Vector3& offset : sampleOffsets) {
        const Vector3 rayStart = {probePosition.x + offset.x, rayStartY, probePosition.z + offset.z};
        const RaycastHit hit = CollisionManager::GetInstance()->Raycast(rayStart, {0.0f, -1.0f, 0.0f},
                                                                        kGateReturnGroundProbeDistance, CollisionCategories::Get("ground"));
        if (!hit.isHit || hit.hitObject == ignoredObject) {
            continue;
        }

        if (!foundGround || hit.hitPoint.y > resolvedGroundY) {
            resolvedGroundY = hit.hitPoint.y;
            foundGround = true;
        }
    }

    return resolvedGroundY;
}

Vector3 SelectGateEntryDirection(Object3d* gateObject, const Vector3& fallbackDirection) {
    const Vector3 fallback = SelectNormalizeGateDirectionXZ(fallbackDirection, {0.0f, 0.0f, 1.0f});
    if (!gateObject) {
        return fallback;
    }

    Transform* transform = gateObject->GetTransform();
    if (!transform) {
        return fallback;
    }

    const Matrix4x4& world = transform->matWorld;
    Vector3 candidates[] = {SelectNormalizeGateDirectionXZ({world.m[0][0], world.m[0][1], world.m[0][2]}, fallback),
                            SelectNormalizeGateDirectionXZ({world.m[1][0], world.m[1][1], world.m[1][2]}, fallback),
                            SelectNormalizeGateDirectionXZ({world.m[2][0], world.m[2][1], world.m[2][2]}, fallback)};

    Vector3 gateAxis = fallback;
    float bestDot = -1.0f;
    for (const Vector3& candidate : candidates) {
        const float dot = std::fabs(candidate.x * fallback.x + candidate.z * fallback.z);
        if (dot > bestDot) {
            bestDot = dot;
            gateAxis = candidate;
        }
    }

    const float dot = gateAxis.x * fallback.x + gateAxis.z * fallback.z;
    if (dot < 0.0f) {
        gateAxis.x *= -1.0f;
        gateAxis.z *= -1.0f;
    }
    return gateAxis;
}

SelectGateEntryRoute SelectBuildGateEntryRoute(const Vector3& gatePos, const Vector3& direction, float entryY) {
    const Vector3 entryDirection = SelectNormalizeGateDirectionXZ(direction, {0.0f, 0.0f, 1.0f});
    return {{gatePos.x - entryDirection.x * kGateEntrySurfaceOffset, entryY,
             gatePos.z - entryDirection.z * kGateEntrySurfaceOffset},
            {gatePos.x, entryY, gatePos.z},
            {gatePos.x + entryDirection.x * kGateEntryInsideDepth, entryY,
             gatePos.z + entryDirection.z * kGateEntryInsideDepth}};
}

float SelectEaseInOutCubic(float value) {
    value = SelectClamp01(value);
    if (value < 0.5f) {
        return 4.0f * value * value * value;
    }
    const float f = -2.0f * value + 2.0f;
    return 1.0f - (f * f * f) * 0.5f;
}

Vector3 SelectLerpVector3(const Vector3& a, const Vector3& b, float t) {
    t = SelectClamp01(t);
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

Vector3 SelectBezierVector3(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t) {
    t = SelectClamp01(t);
    const float inv = 1.0f - t;
    const float b0 = inv * inv * inv;
    const float b1 = 3.0f * inv * inv * t;
    const float b2 = 3.0f * inv * t * t;
    const float b3 = t * t * t;
    return {p0.x * b0 + p1.x * b1 + p2.x * b2 + p3.x * b3, p0.y * b0 + p1.y * b1 + p2.y * b2 + p3.y * b3,
            p0.z * b0 + p1.z * b1 + p2.z * b2 + p3.z * b3};
}

Vector3 SelectNormalizeXZ(const Vector3& value, const Vector3& fallback) {
    const float lengthSq = value.x * value.x + value.z * value.z;
    if (lengthSq <= 0.0001f) {
        return fallback;
    }
    const float invLength = 1.0f / std::sqrt(lengthSq);
    return {value.x * invLength, 0.0f, value.z * invLength};
}

float SelectEaseOutBack(float value) {
    value = SelectClamp01(value);
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    const float x = value - 1.0f;
    return 1.0f + c3 * x * x * x + c1 * x * x;
}

Vector3 SelectLerp(const Vector3& a, const Vector3& b, float t) {
    t = SelectClamp01(t);
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

Vector4 GetStageIslandColor(int stageIndex, bool unlocked, bool selected, bool unlocking, float pulse) {
    if (!unlocked && !unlocking) {
        return {0.16f, 0.17f, 0.20f, 0.78f};
    }
    if (unlocking) {
        return {0.88f, 0.66f + pulse * 0.18f, 0.28f, 1.0f};
    }
    if (selected) {
        switch (stageIndex) {
        case 0:
            return {0.56f, 0.70f, 0.50f, 1.0f};
        case 1:
            return {0.48f, 0.58f, 0.72f, 1.0f};
        case 2:
            return {0.62f, 0.52f, 0.72f, 1.0f};
        default:
            return {0.54f, 0.64f, 0.58f, 1.0f};
        }
    }
    switch (stageIndex) {
    case 0:
        return {0.46f, 0.60f, 0.44f, 1.0f};
    case 1:
        return {0.38f, 0.48f, 0.62f, 1.0f};
    case 2:
        return {0.52f, 0.44f, 0.64f, 1.0f};
    default:
        return {0.40f, 0.52f, 0.48f, 1.0f};
    }
}

float GetStageIslandEmissive(bool unlocked, bool selected, bool unlocking, float pulse) {
    if (unlocking) {
        return 1.8f + pulse * 1.2f;
    }
    if (!unlocked) {
        return 0.18f;
    }
    return selected ? 1.05f + pulse * 0.45f : 0.72f;
}

Vector4 GetStageGateModelColor(bool unlocked, bool selected, bool cleared, bool unlocking, float pulse) {
    if (!unlocked && !unlocking) {
        return {0.20f, 0.20f, 0.24f, 0.82f};
    }
    if (unlocking) {
        return {1.0f, 0.82f + pulse * 0.10f, 0.28f, 1.0f};
    }
    if (selected) {
        return {1.0f, 0.92f, 0.42f, 1.0f};
    }
    if (cleared) {
        return {0.72f, 1.0f, 0.78f, 1.0f};
    }
    return {0.86f, 0.95f, 1.0f, 1.0f};
}

float GetStageGateModelEmissive(bool unlocked, bool selected, bool cleared, bool unlocking, float pulse) {
    if (unlocking) {
        return 2.7f + pulse * 1.2f;
    }
    if (!unlocked) {
        return 0.25f;
    }
    if (selected) {
        return 1.9f + pulse * 0.55f;
    }
    return cleared ? 1.35f : 1.0f;
}

std::string MakeStageClearCrownName(int stageIndex) {
    return std::string(kStageClearCrownPrefix) + std::to_string(stageIndex);
}

} // namespace StageSelectDetail
