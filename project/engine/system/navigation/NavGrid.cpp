#include "NavGrid.h"
#include "engine/system/collision/CollisionManager.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace {
struct OpenNode {
    int index = -1;
    float score = 0.0f;
};

struct OpenNodeGreater {
    bool operator()(const OpenNode& lhs, const OpenNode& rhs) const {
        return lhs.score > rhs.score;
    }
};

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

bool NavGrid::Build(const NavGridBuildSettings& sourceSettings, const PhysicsQueryFilter& filter) {
    settings_ = sourceSettings;
    settings_.cellSize = (std::max)(0.1f, settings_.cellSize);
    settings_.agentRadius = (std::max)(0.05f, settings_.agentRadius);
    settings_.agentHeight = (std::max)(settings_.agentRadius * 2.0f, settings_.agentHeight);
    settings_.maxCellsPerAxis = (std::clamp)(settings_.maxCellsPerAxis, 4, 512);

    if (settings_.boundsMin.x > settings_.boundsMax.x) std::swap(settings_.boundsMin.x, settings_.boundsMax.x);
    if (settings_.boundsMin.z > settings_.boundsMax.z) std::swap(settings_.boundsMin.z, settings_.boundsMax.z);

    const float spanX = (std::max)(settings_.cellSize, settings_.boundsMax.x - settings_.boundsMin.x);
    const float spanZ = (std::max)(settings_.cellSize, settings_.boundsMax.z - settings_.boundsMin.z);
    width_ = (std::clamp)(static_cast<int>(std::ceil(spanX / settings_.cellSize)) + 1, 1, settings_.maxCellsPerAxis);
    depth_ = (std::clamp)(static_cast<int>(std::ceil(spanZ / settings_.cellSize)) + 1, 1, settings_.maxCellsPerAxis);
    blocked_.assign(static_cast<std::size_t>(width_ * depth_), 0);

    CollisionManager* physics = CollisionManager::GetInstance();
    const float blockingHeight = settings_.planeY + (std::max)(0.15f, settings_.agentRadius * 0.30f);

    // 範囲全体を一度だけPhysics Queryし、得られた障害物AABBをGridへラスタライズします。
    // セルごとのQueryを避けることで、再探索時のスパイクを抑えます。
    const AABB queryBounds = {
        { settings_.boundsMin.x, settings_.planeY - 0.05f, settings_.boundsMin.z },
        { settings_.boundsMax.x, settings_.planeY + settings_.agentHeight, settings_.boundsMax.z },
    };
    const auto obstacles = physics->OverlapAABB(queryBounds, filter);
    for (const PhysicsOverlapHit& hit : obstacles) {
        if (!hit.object) continue;
        AABB obstacle = hit.object->GetAABB();
        // 足元より低い床面は障害物にせず、進行を遮る高さの形状だけを採用します。
        if (obstacle.max.y <= blockingHeight ||
            obstacle.min.y >= settings_.planeY + settings_.agentHeight) {
            continue;
        }

        obstacle.min.x -= settings_.agentRadius;
        obstacle.min.z -= settings_.agentRadius;
        obstacle.max.x += settings_.agentRadius;
        obstacle.max.z += settings_.agentRadius;
        const int minX = (std::clamp)(
            static_cast<int>(std::floor((obstacle.min.x - settings_.boundsMin.x) / settings_.cellSize)),
            0, width_ - 1);
        const int maxX = (std::clamp)(
            static_cast<int>(std::ceil((obstacle.max.x - settings_.boundsMin.x) / settings_.cellSize)),
            0, width_ - 1);
        const int minZ = (std::clamp)(
            static_cast<int>(std::floor((obstacle.min.z - settings_.boundsMin.z) / settings_.cellSize)),
            0, depth_ - 1);
        const int maxZ = (std::clamp)(
            static_cast<int>(std::ceil((obstacle.max.z - settings_.boundsMin.z) / settings_.cellSize)),
            0, depth_ - 1);
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                blocked_[static_cast<std::size_t>(ToIndex(x, z))] = 1u;
            }
        }
    }
    return IsBuilt();
}

bool NavGrid::IsBlocked(int x, int z) const {
    if (x < 0 || z < 0 || x >= width_ || z >= depth_) return true;
    return blocked_[static_cast<std::size_t>(ToIndex(x, z))] != 0;
}

Vector3 NavGrid::GetCellCenter(int x, int z) const {
    return {
        settings_.boundsMin.x + static_cast<float>(x) * settings_.cellSize,
        settings_.planeY,
        settings_.boundsMin.z + static_cast<float>(z) * settings_.cellSize,
    };
}

bool NavGrid::WorldToCell(const Vector3& position, int& outX, int& outZ) const {
    if (!IsBuilt()) return false;
    outX = static_cast<int>(std::round((position.x - settings_.boundsMin.x) / settings_.cellSize));
    outZ = static_cast<int>(std::round((position.z - settings_.boundsMin.z) / settings_.cellSize));
    outX = (std::clamp)(outX, 0, width_ - 1);
    outZ = (std::clamp)(outZ, 0, depth_ - 1);
    return true;
}

bool NavGrid::FindNearestWalkable(int sourceX, int sourceZ, int& outX, int& outZ) const {
    if (!IsBlocked(sourceX, sourceZ)) {
        outX = sourceX;
        outZ = sourceZ;
        return true;
    }

    const int maxRadius = (std::max)(width_, depth_);
    for (int radius = 1; radius < maxRadius; ++radius) {
        for (int z = sourceZ - radius; z <= sourceZ + radius; ++z) {
            for (int x = sourceX - radius; x <= sourceX + radius; ++x) {
                if (std::abs(x - sourceX) != radius && std::abs(z - sourceZ) != radius) continue;
                if (!IsBlocked(x, z)) {
                    outX = x;
                    outZ = z;
                    return true;
                }
            }
        }
    }
    return false;
}

bool NavGrid::FindPath(const Vector3& start, const Vector3& goal, std::vector<Vector3>& outPath) const {
    outPath.clear();
    int startX = 0;
    int startZ = 0;
    int goalX = 0;
    int goalZ = 0;
    if (!WorldToCell(start, startX, startZ) || !WorldToCell(goal, goalX, goalZ)) return false;
    const bool exactGoalIsWalkable = !IsBlocked(goalX, goalZ);
    if (!FindNearestWalkable(startX, startZ, startX, startZ) ||
        !FindNearestWalkable(goalX, goalZ, goalX, goalZ)) return false;

    const int startIndex = ToIndex(startX, startZ);
    const int goalIndex = ToIndex(goalX, goalZ);
    const int cellCount = width_ * depth_;
    std::vector<float> cost(static_cast<std::size_t>(cellCount), (std::numeric_limits<float>::max)());
    std::vector<int> parent(static_cast<std::size_t>(cellCount), -1);
    std::vector<std::uint8_t> closed(static_cast<std::size_t>(cellCount), 0);
    std::priority_queue<OpenNode, std::vector<OpenNode>, OpenNodeGreater> open;

    const auto heuristic = [goalX, goalZ](int x, int z) {
        const int dx = std::abs(goalX - x);
        const int dz = std::abs(goalZ - z);
        const int diagonal = (std::min)(dx, dz);
        return static_cast<float>(dx + dz - diagonal * 2) + static_cast<float>(diagonal) * 1.41421356f;
    };

    cost[static_cast<std::size_t>(startIndex)] = 0.0f;
    open.push({ startIndex, heuristic(startX, startZ) });
    const int directions[8][2] = {
        { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
        { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 },
    };
    const int directionCount = settings_.allowDiagonal ? 8 : 4;

    while (!open.empty()) {
        const int index = open.top().index;
        open.pop();
        if (closed[static_cast<std::size_t>(index)] != 0) continue;
        closed[static_cast<std::size_t>(index)] = 1;
        if (index == goalIndex) break;

        const int x = index % width_;
        const int z = index / width_;
        for (int directionIndex = 0; directionIndex < directionCount; ++directionIndex) {
            const int dx = directions[directionIndex][0];
            const int dz = directions[directionIndex][1];
            const int nextX = x + dx;
            const int nextZ = z + dz;
            if (IsBlocked(nextX, nextZ)) continue;
            if (dx != 0 && dz != 0 && (IsBlocked(x + dx, z) || IsBlocked(x, z + dz))) continue;

            const int nextIndex = ToIndex(nextX, nextZ);
            const float stepCost = dx != 0 && dz != 0 ? 1.41421356f : 1.0f;
            const float nextCost = cost[static_cast<std::size_t>(index)] + stepCost;
            if (nextCost >= cost[static_cast<std::size_t>(nextIndex)]) continue;
            cost[static_cast<std::size_t>(nextIndex)] = nextCost;
            parent[static_cast<std::size_t>(nextIndex)] = index;
            open.push({ nextIndex, nextCost + heuristic(nextX, nextZ) });
        }
    }

    if (startIndex != goalIndex && parent[static_cast<std::size_t>(goalIndex)] < 0) return false;
    for (int index = goalIndex; index >= 0; index = parent[static_cast<std::size_t>(index)]) {
        outPath.push_back(GetCellCenter(index % width_, index / width_));
        if (index == startIndex) break;
    }
    std::reverse(outPath.begin(), outPath.end());
    SimplifyPath(outPath);
    if (exactGoalIsWalkable &&
        (outPath.empty() || PlanarDistanceSquared(outPath.back(), goal) > 0.0001f)) {
        Vector3 exactGoal = goal;
        exactGoal.y = settings_.planeY;
        outPath.push_back(exactGoal);
    }
    return true;
}

void NavGrid::SimplifyPath(std::vector<Vector3>& path) const {
    if (path.size() < 3) return;
    std::vector<Vector3> simplified;
    simplified.reserve(path.size());
    simplified.push_back(path.front());
    for (std::size_t index = 1; index + 1 < path.size(); ++index) {
        const Vector3 previousDirection = NormalizePlanar(path[index] - path[index - 1]);
        const Vector3 nextDirection = NormalizePlanar(path[index + 1] - path[index]);
        if (std::abs(previousDirection.x - nextDirection.x) > 0.01f ||
            std::abs(previousDirection.z - nextDirection.z) > 0.01f) {
            simplified.push_back(path[index]);
        }
    }
    simplified.push_back(path.back());
    path = std::move(simplified);
}
