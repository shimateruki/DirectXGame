#pragma once
#include "BaseEnemy.h"
#include <functional>
#include <memory>

// 距離を保ちながら爆弾を生成して投げる、ボム使いの敵。
class EnemyBomber : public BaseEnemy {
  public:
    /// 投擲敵のモデル、当たり判定、爆弾表示を準備する。
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    /// 間合いを保ちながら投擲状態と移動を進める。
    void Update(float deltaTime) override;
    /// 本体と手元に保持する爆弾の見た目を描画する。
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    /// 持ち上げ状態に合わせて行動と爆弾表示を切り替える。
    void SetCarried(bool isCarried) override;
    /// 爆弾生成先として現在のシーンを記録する。
    void BindSceneSpawnContext(BaseScene* scene) override;
    /// 指定スケールを待機・投擲アニメーションの基準にする。
    void ApplyManagedScale(const engine::Vector3& scale) override {
        baseScale_ = scale;
        hasBaseScale_ = true;
        SetScale(scale);
    }

    // 生成した爆弾の所有権を受け取るコールバックです。
    // 未登録時は現在シーンへ直接追加するため、生成先を制御する場合に登録します。
    void SetSpawnCallback(std::function<void(std::unique_ptr<BaseEnemy>)> callback) { spawnCallback_ = callback; }

  private:
    // 溜め、跳躍投擲、着地までの攻撃段階です。
    enum class ThrowState { Idle, Windup, LeapThrow, Landing };

    // 移動制限と敵の射程を先に評価し、投擲状態を進めます。
    bool UpdateBlockedState(float deltaTime);
    void EnsureBaseScaleForAnimation();
    void UpdateThrowTimers(float deltaTime);
    void UpdateCombatMovement(float deltaTime, const engine::Vector3& direction, float distance, bool inRange);
    void UpdateThrowState(float deltaTime, bool inRange);
    void ResetThrowStateAfterTargetLost();
    bool IsTargetInRange(float* outDistance = nullptr, engine::Vector3* outDirection = nullptr) const;
    void UpdateFacing(const engine::Vector3& direction);
    void UpdateFootwork(float deltaTime, const engine::Vector3& direction, float distance, bool inRange);
    void ApplySlimeAnimation(float deltaTime);
    // 手元の爆弾は予備動作の表示用で、実際の爆弾生成とは分けます。
    void UpdateHeldBombVisual(float deltaTime);
    void EnsureHeldBombVisual();
    void HideHeldBombVisual();
    engine::Vector3 ComputeHeldBombPosition(float progress) const;
    float GetThrowProgress() const;
    float GetThrowLeapProgress() const;
    /// 爆弾を構える予備動作を開始する。
    void BeginThrow();
    /// 投擲時の跳躍と回転演出を開始する。
    void BeginThrowLeap();
    /// 跳躍中に爆弾の放出時刻を評価する。
    void UpdateThrowLeap(float deltaTime);
    /// 着地回復へ切り替える。
    void BeginThrowLanding();
    /// 爆弾オブジェクトを生成して放出する。
    void ThrowBomb();
    /// コールバックまたはシーンへ生成した爆弾の所有権を渡す。
    void SpawnBombObject(std::unique_ptr<BaseEnemy> bomb);

    // 次の投擲までの待機時間と、現在の投擲アニメーションの進行です。
    float throwTimer_ = 0.0f;
    float throwInterval_ = 2.45f;
    float windupTimer_ = 0.0f;
    float throwWindup_ = 1.05f;
    float initialThrowDelay_ = 0.55f;
    float footworkTimer_ = 0.0f;
    float footworkDirection_ = 1.0f;
    float idleTimer_ = 0.0f;
    ThrowState throwState_ = ThrowState::Idle;
    float throwRecoilTimer_ = 0.0f;
    float throwRecoverPoseTimer_ = 0.0f;
    float throwLeapTimer_ = 0.0f;
    float throwLandingTimer_ = 0.0f;
    float throwLeapBaseYaw_ = 0.0f;
    float throwSpinDirection_ = 1.0f;
    bool throwBombReleased_ = false;
    std::function<void(std::unique_ptr<BaseEnemy>)> spawnCallback_ = nullptr;
    Object3dCommon* common_ = nullptr;
    engine::Vector3 baseScale_ = {2.0f, 2.0f, 2.0f};
    bool hasBaseScale_ = false;
    std::unique_ptr<engine::Object3d> heldBombVisual_ = nullptr;
};
