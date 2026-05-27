#pragma once
#include "Object3d.h"
#include <vector>

/// <summary>
/// マップを構成するブロックアクター
/// ボスが吸収して操ることができる
/// </summary>
class MapBlock : public Object3d {
public:
    // ==========================================
    // 全MapBlockを管理する共有名簿
    // ==========================================
    static std::vector<MapBlock*> s_activeBlocks;

    void Initialize(Object3dCommon* common) override;
    void Update(float deltaTime) override;
    ~MapBlock();

    /// <summary>
    /// ボスに吸収された時の処理
    /// </summary>
    void OnAbsorbed();
    void StartBreak(const Vector3& impulse = { 0.0f, 8.0f, 0.0f });
    bool IsBreaking() const { return breakState_ != BreakState::None; }

private:
    enum class BreakState {
        None,
        Falling,
        Rolling,
        Landed,
    };

    bool isAbsorbed_ = false;
    BreakState breakState_ = BreakState::None;
    Vector3 breakVelocity_ = { 0.0f, 0.0f, 0.0f };
    Vector3 breakAngularVelocity_ = { 0.0f, 0.0f, 0.0f };
    float breakTimer_ = 0.0f;
    float rollingTimer_ = 0.0f;
    float landedTimer_ = 0.0f;
    float breakSparkTimer_ = 0.0f;
    float breakGroundY_ = 0.0f;
    float breakStartScaleY_ = 1.0f;
    Vector3 breakStartScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 breakLandedScale_ = { 1.0f, 1.0f, 1.0f };
    Vector4 breakBaseColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };

    void UpdateBreak(float deltaTime);

    // ==========================================
    // 自分が密かに持っておくレーザー
    // ==========================================
    std::unique_ptr<Object3d> laserBeam_;
};
