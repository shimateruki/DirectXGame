#pragma once

#include "ObjectComponent.h"
#include "engine/system/navigation/NavGrid.h"
#include "CollisionConfig.h"

#include <cstdint>
#include <string_view>
#include <vector>

class Object3d;

// NavAgentComponentは、Physics Queryから局所NavGridを作りA*経路へ移動方向を補正します。
class NavAgentComponent final : public ObjectComponent {
public:
    static constexpr std::string_view kTypeId = "NavAgent";

    std::string_view GetTypeId() const override { return kTypeId; }

    Vector3 CalculateDirection(
        Object3d* owner, const Vector3& currentPosition,
        const Vector3& targetPosition, float deltaTime);
    void InvalidatePath();

    bool IsEnabled() const { return enabled_; }
    void SetEnabled(bool enabled) { enabled_ = enabled; if (!enabled) InvalidatePath(); }
    float GetCellSize() const { return cellSize_; }
    void SetCellSize(float value);
    float GetAgentRadius() const { return agentRadius_; }
    void SetAgentRadius(float value);
    float GetAgentHeight() const { return agentHeight_; }
    void SetAgentHeight(float value);
    float GetSearchPadding() const { return searchPadding_; }
    void SetSearchPadding(float value);
    float GetRepathInterval() const { return repathInterval_; }
    void SetRepathInterval(float value);
    float GetStoppingDistance() const { return stoppingDistance_; }
    void SetStoppingDistance(float value);
    uint32_t GetObstacleMask() const { return obstacleMask_; }
    void SetObstacleMask(uint32_t value) { obstacleMask_ = value; InvalidatePath(); }
    bool AllowsDiagonal() const { return allowDiagonal_; }
    void SetAllowDiagonal(bool value) { allowDiagonal_ = value; InvalidatePath(); }

    const std::vector<Vector3>& GetCurrentPath() const { return currentPath_; }
    std::size_t GetCurrentWaypointIndex() const { return waypointIndex_; }
    bool LastPathSucceeded() const { return lastPathSucceeded_; }

private:
    bool RebuildPath(Object3d* owner, const Vector3& currentPosition, const Vector3& targetPosition);

    bool enabled_ = true;
    float cellSize_ = 1.0f;
    float agentRadius_ = 0.55f;
    float agentHeight_ = 1.5f;
    float searchPadding_ = 5.0f;
    float repathInterval_ = 0.35f;
    float stoppingDistance_ = 0.25f;
    uint32_t obstacleMask_ = kAllSolid;
    bool allowDiagonal_ = true;

    NavGrid grid_;
    std::vector<Vector3> currentPath_;
    std::size_t waypointIndex_ = 0;
    float repathTimer_ = 0.0f;
    Vector3 lastTargetPosition_ = { 0.0f, 0.0f, 0.0f };
    bool hasLastTarget_ = false;
    bool lastPathSucceeded_ = false;
};
