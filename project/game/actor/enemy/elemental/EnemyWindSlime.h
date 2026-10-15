#pragma once

#include "BaseEnemy.h"

#include <array>
#include <memory>

// 持続する突風で相手を押し流す、吹き飛ばし特化の属性スライムです。
class EnemyWindSlime : public BaseEnemy {
  public:
    /// 風スライムのモデル、当たり判定、攻撃状態を準備する。
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    /// 突風または空中弾幕の攻撃段階を更新する。
    void Update(float deltaTime) override;
    /// 風球など本体が所有する特殊材質の表示を持つか返す。
    bool HasOwnedSpecialMaterialVisuals() const override;
    /// 本体が所有する特殊材質の表示を指定バッファで描画する。
    void DrawOwnedSpecialMaterialVisuals(uint32_t depthSrvHandle, uint32_t grabSrvHandle) override;
    /// 投げ状態の速度と復帰条件を設定する。
    void BeginThrown(const engine::Vector3& initialVelocity) override;
    /// 配置用の風スライム複製を作る。
    std::unique_ptr<engine::Object3d> Clone() const override;

    // プレビュー対象の攻撃 ID を固定し、進行中の予備動作と再使用待ちをリセットする。
    void SetDebugPreviewAttackId(const std::string& attackId);
    /// デバッグ表示向けに現在の攻撃段階名を返す。
    const char* GetDebugAttackPhaseName() const;
    /// 配置スケールをアニメーションの基準へ反映する。
    void ApplyManagedScale(const engine::Vector3& scale) override;

  private:
    // 突風と空中弾幕の予備動作・攻撃・着地を明示的に分けます。
    enum class AttackState {
        Idle,
        GustWindup,
        GustActive,
        VolleyTakeoff,
        VolleyActive,
        VolleyLanding,
        Recover,
    };

    // 行動不能や投げられた直後の復帰は通常 AI より先に処理します。
    bool UpdateInactiveState(float deltaTime);
    bool UpdateThrowRecoveryState(float deltaTime);
    void EnsureBaseScale();
    void UpdateWildBehavior(float deltaTime, engine::Vector3& velocity);
    void UpdateAttackState(float deltaTime, engine::Vector3& velocity, const engine::Vector3& direction, float distance);
    // 二つの魔法の開始処理。狙い方向は開始時に固定します。
    /// 対象方向を固定して突風の予備動作を開始する。
    void StartGustBreath(const engine::Vector3& direction);
    /// 対象方向と距離を固定して空中弾幕を開始する。
    void StartAerialWindVolley(const engine::Vector3& direction, float distance);
    /// 指定攻撃の回復時間を設定して通常行動への復帰を始める。
    void BeginRecover(const struct EnemyAttackDefinition& attack);
    /// 空中弾幕の着地段階に移る。
    void BeginVolleyLanding(const struct EnemyAttackDefinition& attack);
    /// 空中弾幕の指定番号の風球を発射する。
    void FireAerialWindOrb(int orbIndex);
    /// 風球の着弾予告位置を更新する。
    void UpdateVolleyImpactCenters();
    /// 攻撃進捗に合わせて着弾予告を表示する。
    void ShowVolleyImpactTelegraphs(float progress);
    /// 頭上の障害物を避けるよう弾幕時の浮遊高度を決める。
    float ResolveVolleyHoverY();
    /// 指定高度に留まるための垂直速度を調整する。
    void HoldVolleyAltitude(float height, float deltaTime, engine::Vector3& velocity);
    // 空中弾幕で保持する風球を生成・更新し、攻撃終了後に非表示にします。
    void EnsureHeldWindOrbs();
    void UpdateHeldWindOrbs(float deltaTime, float appearProgress = 1.0f);
    void HideHeldWindOrbs();
    engine::Vector3 ComputeHeldWindOrbPosition(int orbIndex) const;
    void DispatchEnemyBreathPush(const engine::Vector3& direction, float distance);
    void UpdateFacing(const engine::Vector3& direction);
    void ApplySlimeAnimation(float deltaTime);

    // 戦闘判定とは独立した VFX と衝突半径を同期します。
    void EmitWindBreathParticles(const engine::Vector3& origin, const engine::Vector3& direction, float range);
    void EmitWindPreset(const char* presetName, const engine::Vector3& position);
    void EmitDirectedWindPreset(const char* presetName, const engine::Vector3& position, const engine::Vector3& direction,
                                float speedScale);
    void SyncWorldCollisionRadius(float worldRadius);
    void SyncGroundCollisionRadius();
    void SyncThrownCollisionRadius();

    static constexpr int kWindAnimationPhaseCount = 8;

    // 攻撃の進行と予告表示のためのタイマーです。
    AttackState attackState_ = AttackState::Idle;
    float attackStateTimer_ = 0.0f;
    float attackCooldown_ = 0.75f;
    float recoveryDuration_ = 0.0f;
    float idleTimer_ = 0.0f;
    float groundHopTimer_ = 0.0f;
    float ambientParticleTimer_ = 0.0f;
    float breathParticleTimer_ = 0.0f;
    float breathPushTimer_ = 0.0f;
    float lockedTargetDistance_ = 0.0f;
    float volleyGroundY_ = 0.0f;
    float volleyHoverY_ = 0.0f;
    float volleyLandingStartY_ = 0.0f;
    float volleyShotTimer_ = 0.0f;
    float volleyVisualTimer_ = 0.0f;
    float volleyRecoilTimer_ = 0.0f;
    engine::Vector3 lockedAttackDirection_ = {0.0f, 0.0f, 1.0f};
    engine::Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};
    int breathParticleCursor_ = 0;
    int volleyShotCount_ = 0;
    bool hasBaseScale_ = false;
    bool warningTriggered_ = false;
    std::string debugPreviewAttackId_;
    std::array<engine::Vector3, 3> volleyImpactCenters_{};
    std::array<std::unique_ptr<engine::Object3d>, 3> heldWindOrbVisuals_;
};
