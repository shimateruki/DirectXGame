#pragma once

#include "engine/utility/math/Math.h"

#include <cstdint>
#include <vector>

struct PhysicsQueryFilter;

// NavGridをPhysics Queryから構築するための設定です。
struct NavGridBuildSettings {
    Vector3 boundsMin = { -10.0f, 0.0f, -10.0f };
    Vector3 boundsMax = { 10.0f, 0.0f, 10.0f };
    float planeY = 0.0f;
    float cellSize = 1.0f;
    float agentRadius = 0.5f;
    float agentHeight = 1.5f;
    bool allowDiagonal = true;
    int maxCellsPerAxis = 128;
};

// Physics World上の障害物を2D Gridへ落とし込み、A*経路を返します。
class NavGrid {
public:
    bool Build(const NavGridBuildSettings& settings, const PhysicsQueryFilter& filter);
    bool FindPath(const Vector3& start, const Vector3& goal, std::vector<Vector3>& outPath) const;

    bool IsBuilt() const { return width_ > 0 && depth_ > 0 && !blocked_.empty(); }
    bool IsBlocked(int x, int z) const;
    Vector3 GetCellCenter(int x, int z) const;
    int GetWidth() const { return width_; }
    int GetDepth() const { return depth_; }
    const NavGridBuildSettings& GetSettings() const { return settings_; }

private:
    int ToIndex(int x, int z) const { return z * width_ + x; }
    bool WorldToCell(const Vector3& position, int& outX, int& outZ) const;
    bool FindNearestWalkable(int sourceX, int sourceZ, int& outX, int& outZ) const;
    void SimplifyPath(std::vector<Vector3>& path) const;

    NavGridBuildSettings settings_;
    int width_ = 0;
    int depth_ = 0;
    std::vector<std::uint8_t> blocked_;
};
