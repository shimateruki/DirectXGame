#pragma once
#include "BaseGimmick.h"
#include "game/actor/enemy/combat/AttackTelegraph.h"

// 担当：設定された方向・周期で砲撃し、着弾予告と砲台の反動を更新します。
// 関連：BaseGimmickは配置、BulletManagerは弾の生成・更新、CollisionManagerは本体の接触判定を担当します。
// 調整項目はInitializeのProperty登録からEditorへ公開する。
// 主な入口：Initialize / Update / Draw / OnTrigger / OnSwitchEvent
class GimmickFireCannon : public BaseGimmick {
  public:
    // 表示名とは独立した、既存シーンの保存IDと生成モデルです。
    static constexpr const char* kTypeId = "FireCannon";
    static constexpr const char* kModelPath = "Primitives/cube";

    ~GimmickFireCannon() override;
    /// 砲台の配置、発射周期、着弾予告を初期化する。
    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    /// 標的への旋回と予告・発射・反動を更新する。
    void Update(float deltaTime) override;

    /// 砲台本体と発射演出を描画する。
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;

    /// 外部トリガーから砲撃開始を要求する。
    void OnTrigger() override;

    /// スイッチに応じて砲撃を有効・無効にする。
    void OnSwitchEvent(bool active) override;

    /// 配置用の砲台複製を作る。
    std::unique_ptr<engine::Object3D> Clone() const override;

    // 直進砲台の対地形接触だけを省き、保存用の衝突設定は維持します。
    uint32_t GetContactCollisionMask() const override;

  private:
    // 予告、飛行、反動の進行は発射間隔とは別に更新します。
    void UpdateMortar(float deltaTime);

    // 大型砲弾はモデルの砲口側へ重力なしで発射します。
    void FireBombard();

    // 保存しない実行時状態。停止・モード変更のフレームで通常の接触条件へ戻します。
    bool suppressBombardTerrainContacts_ = false;

    // startはワールド発射位置、forwardは正規化したワールド方向です。
    // 弾・爆風の経路と起動余裕にPlayerが入ればtrue。余裕0以下・Player不在時も発射を許可します。
    bool IsBombardPathRelevant(const engine::Vector3& start, const engine::Vector3& forward) const;

    // 描画専用の姿勢なので、配置・当たり判定・発射方向は変えません。
    void UpdateBombardPose();
    float GetBombardRecoveryTime() const;
    float bombardAnticipationTime_ = 0.20f;
    float bombardRecoilStrength_ = 0.22f;
    float bombardRecoveryTime_ = 0.42f;

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

    float GetProjectileLifetime() const;

    float GetTurnSpeedRadians() const;

    engine::Vector3 basePosition_ = engine::Vector3::ZERO;
    engine::Vector3 baseRotation_ = engine::Vector3::ZERO;
    engine::Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};
    bool initializedForPlay_ = false;
    bool previewActive_ = false;
    bool active_ = true;
    float fireTimer_ = 0.0f;
};
