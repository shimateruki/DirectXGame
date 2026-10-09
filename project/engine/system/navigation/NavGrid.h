#pragma once

#include "engine/utility/math/Math.h"

#include <cstdint>
#include <vector>

struct PhysicsQueryFilter;

/// <summary>物理障害物から 2D 経路探索グリッドを作るための設定です。</summary>
struct NavGridBuildSettings {
    // XZ の探索領域と、移動面の高さ。Y の境界値はグリッド寸法に使いません。
    Vector3 boundsMin = { -10.0f, 0.0f, -10.0f };
    Vector3 boundsMax = { 10.0f, 0.0f, 10.0f };
    float planeY = 0.0f;

    // 障害物は agentRadius だけ膨張させ、agentHeight の範囲で高さを判定します。
    float cellSize = 1.0f;
    float agentRadius = 0.5f;
    float agentHeight = 1.5f;
    bool allowDiagonal = true;
    int maxCellsPerAxis = 128; // 広い領域でのメモリ使用量を制限します。
};

/// <summary>物理障害物を XZ グリッドに投影し、A* で経路を求めます。</summary>
class NavGrid {
public:
    /// <summary>指定領域の障害物を収集し、通行可能セルを構築します。</summary>
    /// <param name="settings">探索領域、セル寸法、エージェント寸法。</param>
    /// <param name="filter">障害物を収集する物理クエリのフィルター。</param>
    /// <returns>グリッドを構築できた場合は true。</returns>
    bool Build(const NavGridBuildSettings& settings, const PhysicsQueryFilter& filter);

    /// <summary>通行可能な開始・目標セルを選び、曲がり角だけを残した経路を返します。</summary>
    /// <param name="start">開始位置。XZ のみをセル選択に使います。</param>
    /// <param name="goal">目標位置。通行不可なら近くの通行可能セルへ移します。</param>
    /// <param name="outPath">成功時の経路。失敗時は空になります。</param>
    /// <returns>経路が見つかった場合は true。</returns>
    /// <remarks>範囲外の入力は端のセルへ丸める。高さは planeY 固定で、上下階や坂の経路は解かない。</remarks>
    bool FindPath(const Vector3& start, const Vector3& goal, std::vector<Vector3>& outPath) const;

    /// <summary>使用できるグリッドが構築済みか返します。</summary>
    bool IsBuilt() const { return width_ > 0 && depth_ > 0 && !blocked_.empty(); }

    /// <summary>セルが障害物またはグリッド外か返します。</summary>
    bool IsBlocked(int x, int z) const;

    /// <summary>セルのワールド座標を移動面上で返します。</summary>
    Vector3 GetCellCenter(int x, int z) const;

    /// <summary>グリッドの X 方向のセル数を返します。</summary>
    int GetWidth() const { return width_; }

    /// <summary>グリッドの Z 方向のセル数を返します。</summary>
    int GetDepth() const { return depth_; }

    /// <summary>構築時に正規化した設定を返します。</summary>
    const NavGridBuildSettings& GetSettings() const { return settings_; }

private:
    // XZ 座標を blocked_ の一次元インデックスに変換します。
    int ToIndex(int x, int z) const { return z * width_ + x; }

    // ワールド位置を最寄りのセルへ丸め、グリッド端に収めます。
    bool WorldToCell(const Vector3& position, int& outX, int& outZ) const;

    // 指定セルが塞がっている場合、周囲を正方形状に広げて最初の空きセルを返します。
    // 同じ輪の中で距離順には並べないため、ユークリッド距離での最寄り点を保証しません。
    bool FindNearestWalkable(int sourceX, int sourceZ, int& outX, int& outZ) const;

    // 直線上の中間セルを取り除き、曲がり角を残します。
    void SimplifyPath(std::vector<Vector3>& path) const;

    NavGridBuildSettings settings_;
    int width_ = 0;
    int depth_ = 0;
    std::vector<std::uint8_t> blocked_; // 0 は通行可能、1 は障害物。
};
