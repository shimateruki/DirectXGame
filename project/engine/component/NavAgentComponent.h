#pragma once

#include "ObjectComponent.h"
#include "engine/system/navigation/NavGrid.h"
#include "CollisionConfig.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace engine { class Object3D; }

// 担当：周辺障害物から局所グリッドを作り、目標へ進む方向を求める。
// 関連：Object3D（探索の基準）、NavGrid（経路探索）、CollisionManager（障害物照会）
// 主な入口：CalculateDirection / InvalidatePath
class NavAgentComponent final : public ObjectComponent {
public:
    static constexpr std::string_view kTypeId = "NavAgent"; // 保存と実行時の型判定に使う固定ID。

    /// <summary>コンポーネントの識別名を返す。</summary>
    std::string_view GetTypeId() const override { return kTypeId; }

    /// <summary>現在地から目標へ向かう経路を使い、次に進む方向を求める。</summary>
    /// <param name="owner">障害物検索の基準となる所有オブジェクト。</param>
    /// <param name="currentPosition">現在のワールド座標。</param>
    /// <param name="targetPosition">目標のワールド座標。</param>
    /// <param name="deltaTime">前回更新からの経過秒数。</param>
    /// <returns>XZ 平面の単位方向。到着時はゼロ、無効時や探索失敗時は目標への直接方向。</returns>
    /// <remarks>速度や位置は変更しない。返した方向の先が安全とは限らず、移動側で衝突を解決する。</remarks>
    engine::Vector3 CalculateDirection(
        engine::Object3D* owner, const engine::Vector3& currentPosition,
        const engine::Vector3& targetPosition, float deltaTime);

    /// <summary>保持中の経路を破棄し、次回の計算で再探索する。</summary>
    void InvalidatePath();

    /// <summary>経路補正が有効かを返す。</summary>
    bool IsEnabled() const { return enabled_; }

    /// <summary>経路補正を切り替える。無効化時は経路を破棄する。</summary>
    void SetEnabled(bool enabled) { enabled_ = enabled; if (!enabled) InvalidatePath(); }

    /// <summary>局所グリッドのセル幅を返す。</summary>
    float GetCellSize() const { return cellSize_; }

    /// <summary>局所グリッドのセル幅を設定する。</summary>
    void SetCellSize(float value);

    /// <summary>障害物から確保するエージェントの半径を返す。</summary>
    float GetAgentRadius() const { return agentRadius_; }

    /// <summary>障害物から確保するエージェントの半径を設定する。</summary>
    void SetAgentRadius(float value);

    /// <summary>通過可否に使うエージェントの高さを返す。</summary>
    float GetAgentHeight() const { return agentHeight_; }

    /// <summary>通過可否に使うエージェントの高さを設定する。</summary>
    void SetAgentHeight(float value);

    /// <summary>探索範囲に足す余白を返す。</summary>
    float GetSearchPadding() const { return searchPadding_; }

    /// <summary>探索範囲に足す余白を設定する。</summary>
    void SetSearchPadding(float value);

    /// <summary>経路を再探索する間隔を返す。</summary>
    float GetRepathInterval() const { return repathInterval_; }

    /// <summary>経路を再探索する間隔を設定する。</summary>
    void SetRepathInterval(float value);

    /// <summary>目標到着と判定する距離を返す。</summary>
    float GetStoppingDistance() const { return stoppingDistance_; }

    /// <summary>目標到着と判定する距離を設定する。</summary>
    void SetStoppingDistance(float value);

    /// <summary>障害物として調べる衝突属性マスクを返す。</summary>
    uint32_t GetObstacleMask() const { return obstacleMask_; }

    /// <summary>障害物の衝突属性マスクを変更し、経路を再探索する。</summary>
    void SetObstacleMask(uint32_t value) { obstacleMask_ = value; InvalidatePath(); }

    /// <summary>斜め移動を許す設定かを返す。</summary>
    bool AllowsDiagonal() const { return allowDiagonal_; }

    /// <summary>斜め移動の可否を変更し、経路を再探索する。</summary>
    void SetAllowDiagonal(bool value) { allowDiagonal_ = value; InvalidatePath(); }

    /// <summary>現在の経路を返す。</summary>
    const std::vector<engine::Vector3>& GetCurrentPath() const { return currentPath_; }

    /// <summary>現在追跡しているウェイポイント番号を返す。</summary>
    std::size_t GetCurrentWaypointIndex() const { return waypointIndex_; }

    /// <summary>直近の経路探索が成功したかを返す。</summary>
    bool LastPathSucceeded() const { return lastPathSucceeded_; }

private:
    // 障害物を再取得し、現在地から目標までの局所経路を作り直す。
    bool RebuildPath(engine::Object3D* owner, const engine::Vector3& currentPosition, const engine::Vector3& targetPosition);

    bool enabled_ = true;
    float cellSize_ = 1.0f; // 探索格子の一辺の長さ。
    float agentRadius_ = 0.55f; // 障害物から確保する水平半径。
    float agentHeight_ = 1.5f; // 通過に必要な高さ。
    float searchPadding_ = 5.0f; // 探索範囲へ足す余白。
    // 再探索の待機秒数。保持中の経路を使う間もCalculateDirectionへ経過時間を渡す。
    float repathInterval_ = 0.35f;
    float stoppingDistance_ = 0.25f; // 目標への到着とみなす距離。
    uint32_t obstacleMask_ = CollisionCategories::Get("ground");
    bool allowDiagonal_ = true; // 斜め隣のセルへ移動できるか。

    // 再探索間隔中は前回のグリッドと経路を再利用する。
    NavGrid grid_;
    std::vector<engine::Vector3> currentPath_; // 計算済みのワールド経路点。
    std::size_t waypointIndex_ = 0; // 次に目指す経路点の番号。
    float repathTimer_ = 0.0f; // 再探索までの待機秒数。
    engine::Vector3 lastTargetPosition_ = engine::Vector3::ZERO; // 前回探索した目標位置。
    bool hasLastTarget_ = false; // 前回の目標位置が有効か。
    bool lastPathSucceeded_ = false; // 直近の経路探索が成功したか。
};
