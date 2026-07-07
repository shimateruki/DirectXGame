#pragma once
#include "BaseEnemy.h"

// 空中を旋回し、予兆後に急降下して接触ダメージを狙う敵
// EnemyBatは、浮遊旋回しながらプレイヤーへ急降下する飛行敵です。
class EnemyBat : public BaseEnemy {
public:
        // コウモリ用モデル、Collider、ホーム位置、飛行状態を初期化します。
void Initialize(Object3dCommon* common, const std::string& modelName) override;
        // 旋回、追跡、急降下、復帰、接触判定を更新します。
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

        // 非アクティブ時の共通待機処理を行い、更新継続の可否を返します。
bool UpdateInactiveState(float deltaTime);
    void UpdateTimers(float deltaTime);
        // プレイヤー検知中の移動方向と速度を決定します。
void UpdateTargetBehavior(float deltaTime, Vector3& desired, float& moveSpeed);
    void UpdateWanderBehavior(float deltaTime, Vector3& desired, float& moveSpeed);
    void ApplyMovement(float deltaTime, const Vector3& desired, float moveSpeed);
    void CaptureHomePosition();
    void EnsureAnimation();
    void UpdateFacing(const Vector3& direction);
        // ホーム位置を中心にした旋回目標位置を計算します。
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
