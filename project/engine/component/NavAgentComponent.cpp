#include "NavAgentComponent.h"

#include "Object3d.h"
#include "engine/system/collision/CollisionManager.h"

#include <algorithm>
#include <cmath>

namespace {
float PlanarDistanceSquared(const Vector3& lhs, const Vector3& rhs) {
    const float dx = lhs.x - rhs.x;
    const float dz = lhs.z - rhs.z;
    return dx * dx + dz * dz;
}

Vector3 NormalizePlanar(const Vector3& value) {
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.000001f) return { 0.0f, 0.0f, 0.0f };
    return { value.x / length, 0.0f, value.z / length };
}
}

void NavAgentComponent::SetCellSize(float value) {
    cellSize_ = (std::clamp)(value, 0.2f, 10.0f);
    InvalidatePath();
}

void NavAgentComponent::SetAgentRadius(float value) {
    agentRadius_ = (std::clamp)(value, 0.05f, 10.0f);
    agentHeight_ = (std::max)(agentHeight_, agentRadius_ * 2.0f);
    InvalidatePath();
}

void NavAgentComponent::SetAgentHeight(float value) {
    agentHeight_ = (std::clamp)(value, agentRadius_ * 2.0f, 20.0f);
    InvalidatePath();
}

void NavAgentComponent::SetSearchPadding(float value) {
    searchPadding_ = (std::clamp)(value, 1.0f, 50.0f);
    InvalidatePath();
}

void NavAgentComponent::SetRepathInterval(float value) {
    repathInterval_ = (std::clamp)(value, 0.05f, 5.0f);
}

void NavAgentComponent::SetStoppingDistance(float value) {
    stoppingDistance_ = (std::clamp)(value, 0.0f, 10.0f);
}

void NavAgentComponent::InvalidatePath() {
    currentPath_.clear();
    waypointIndex_ = 0;
    repathTimer_ = 0.0f;
    hasLastTarget_ = false;
    lastPathSucceeded_ = false;
}

Vector3 NavAgentComponent::CalculateDirection(
    Object3d* owner, const Vector3& currentPosition,
    const Vector3& targetPosition, float deltaTime) {
    Vector3 direct = targetPosition - currentPosition;
    direct.y = 0.0f;
    if (!enabled_ || !owner) return NormalizePlanar(direct);

    if (PlanarDistanceSquared(currentPosition, targetPosition) <= stoppingDistance_ * stoppingDistance_) {
        return { 0.0f, 0.0f, 0.0f };
    }

    repathTimer_ -= (std::max)(0.0f, deltaTime);
    const bool targetMoved = !hasLastTarget_ ||
        PlanarDistanceSquared(lastTargetPosition_, targetPosition) > cellSize_ * cellSize_ * 0.25f;
    if (currentPath_.empty() || targetMoved || repathTimer_ <= 0.0f) {
        RebuildPath(owner, currentPosition, targetPosition);
    }

    const float reachDistance = (std::max)(stoppingDistance_, cellSize_ * 0.35f);
    while (waypointIndex_ < currentPath_.size() &&
        PlanarDistanceSquared(currentPosition, currentPath_[waypointIndex_]) <= reachDistance * reachDistance) {
        ++waypointIndex_;
    }

    if (waypointIndex_ >= currentPath_.size()) return NormalizePlanar(direct);
    return NormalizePlanar(currentPath_[waypointIndex_] - currentPosition);
}

bool NavAgentComponent::RebuildPath(
    Object3d* owner, const Vector3& currentPosition, const Vector3& targetPosition) {
    const float padding = (std::max)(searchPadding_, cellSize_ * 2.0f);
    NavGridBuildSettings settings;
    settings.boundsMin = {
        (std::min)(currentPosition.x, targetPosition.x) - padding,
        currentPosition.y,
        (std::min)(currentPosition.z, targetPosition.z) - padding,
    };
    settings.boundsMax = {
        (std::max)(currentPosition.x, targetPosition.x) + padding,
        currentPosition.y,
        (std::max)(currentPosition.z, targetPosition.z) + padding,
    };
    settings.planeY = currentPosition.y;
    settings.cellSize = cellSize_;
    settings.agentRadius = agentRadius_;
    settings.agentHeight = agentHeight_;
    settings.allowDiagonal = allowDiagonal_;
    settings.maxCellsPerAxis = 64;

    PhysicsQueryFilter filter;
    filter.mask = obstacleMask_;
    filter.ignoredObject = owner;
    filter.ignoreDescendants = true;
    filter.includeTriggers = false;

    currentPath_.clear();
    waypointIndex_ = 0;
    lastPathSucceeded_ = grid_.Build(settings, filter) &&
        grid_.FindPath(currentPosition, targetPosition, currentPath_);
    repathTimer_ = repathInterval_;
    lastTargetPosition_ = targetPosition;
    hasLastTarget_ = true;
    return lastPathSucceeded_;
}
