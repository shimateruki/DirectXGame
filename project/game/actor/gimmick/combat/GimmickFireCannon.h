#pragma once
#include "BaseGimmick.h"
#include "game/actor/enemy/combat/AttackTelegraph.h"

// 対象の位置を固定して着弾予告を出し、砲口から炎弾を発射するギミックです。
class GimmickFireCannon : public BaseGimmick {
  public:
    /// 砲台の配置、発射周期、着弾予告を初期化する。
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    /// 標的への旋回と予告・発射・反動を更新する。
    void Update(float deltaTime) override;
    /// 砲台本体と発射演出を描画する。
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    /// 外部トリガーから砲撃開始を要求する。
    void OnTrigger() override;
    /// スイッチに応じて砲撃を有効・無効にする。
    void OnSwitchEvent(bool active) override;

    /// 配置用の砲台複製を作る。
    std::unique_ptr<engine::Object3d> Clone() const override;

  private:
    // 予告、飛行、反動の進行は発射間隔とは別に更新します。
    void UpdateMortar(float deltaTime);
    AttackTelegraph landingWarning_;
    engine::Vector3 lockedTarget_ = {};
    bool targetLocked_ = false;
    float warningTimer_ = 0.0f;
    float flightTimer_ = 0.0f;
    float recoilTimer_ = 0.0f;
    /// Play 中の攻撃更新か判定する。
    bool IsPlaying() const;
    // 砲口と狙い方向を求め、設定値から弾の速度・半径を取得します。
    bool FindFireDirection(engine::Vector3& outDirection) const;
    engine::Vector3 GetForwardDirection() const;
    engine::Vector3 GetMuzzlePosition(const engine::Vector3& direction) const;
    /// 砲口を目標方向へ段階的に向ける。
    void RotateToward(const engine::Vector3& direction, float deltaTime);
    /// 指定方向へ炎弾を生成する。
    void FireProjectile(const engine::Vector3& direction);
    float GetProjectileSpeed() const;
    float GetFireInterval() const;
    float GetProjectileRadius() const;
    float GetTurnSpeedRadians() const;

    engine::Vector3 basePosition_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 baseRotation_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};
    bool initializedForPlay_ = false;
    bool active_ = true;
    float fireTimer_ = 0.0f;
};
