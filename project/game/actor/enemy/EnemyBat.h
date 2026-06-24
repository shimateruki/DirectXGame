#pragma once
#include "BaseEnemy.h"

// 空中を旋回し、予兆後に急降下して接触ダメージを狙う敵
class EnemyBat : public BaseEnemy {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    std::unique_ptr<Object3d> Clone() const override;

private:
    // 旋回、予兆、急降下、復帰の4状態で動きを管理する
    enum class BatState {
        Orbit,
        Telegraph,
        Dive,
        Recover
    };

    void CaptureHomePosition();
    void EnsureAnimation();
    void UpdateFacing(const Vector3& direction);
    Vector3 CalcOrbitPosition() const;
    void SetPlayerContactEnabled(bool enabled);

    Vector3 homePosition_ = { 0.0f, 0.0f, 0.0f }; // 旋回の中心になる初期位置。
    Vector3 diveTarget_ = { 0.0f, 0.0f, 0.0f };   // 急降下時に狙う地点。
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    BatState state_ = BatState::Orbit;
    float hoverTimer_ = 0.0f;     // 浮遊アニメーション用。
    float stateTimer_ = 0.0f;     // 現在ステートの経過時間。
    float diveCooldown_ = 2.0f;   // 次の急降下までの待ち時間。
    bool hasHomePosition_ = false;
};
