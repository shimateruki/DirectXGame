#pragma once

#include "BaseEnemy.h"

#include <array>
#include <memory>

// 持続する突風で相手を押し流す、吹き飛ばし特化の属性スライムです。
// 関連：BaseEnemyの標的情報を使い、風攻撃の予備動作・発生・後隙を切り替える。
// 主な入口：Initialize / Update
class EnemyWindSlime : public BaseEnemy {
  public:
    /// 風スライムのモデル、当たり判定、攻撃状態を準備する。
    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    /// 突風または空中弾幕の攻撃段階を更新する。
    void Update(float deltaTime) override;

    /// 風球など本体が所有する特殊材質の表示を持つか返す。
    bool HasOwnedSpecialMaterialVisuals() const override;

    /// 本体が所有する特殊材質の表示を指定バッファで描画する。
    void DrawOwnedSpecialMaterialVisuals(uint32_t depthSrvHandle, uint32_t grabSrvHandle) override;

    /// 投げ状態の速度と復帰条件を設定する。
    void BeginThrown(const engine::Vector3& initialVelocity) override;

    /// 配置用の風スライム複製を作る。
    std::unique_ptr<engine::Object3D> Clone() const override;

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

    // 投擲中と復帰中でワールド判定半径を切り替えてから共通物理を進める。
    bool UpdateThrowRecoveryState(float deltaTime);

    // 攻撃の変形が始まる前の配置スケールを一度だけ記録する。
    void EnsureBaseScale();

    void UpdateWildBehavior(float deltaTime, engine::Vector3& velocity);

    // 攻撃中は通常の水平移動を止め、状態ごとの高度・予告・発射だけを更新する。
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

    // 弾を順に可視化し、発射済みの位置は隠して残数を見た目へ反映する。
    void UpdateHeldWindOrbs(float deltaTime, float appearProgress = 1.0f);

    // 攻撃中断・着地時に表示用の弾だけを隠す。
    void HideHeldWindOrbs();

    // 三つの風弾を進行方向に対して左右へ並べ、中央だけ少し高く置く。
    engine::Vector3 ComputeHeldWindOrbPosition(int orbIndex) const;

    // 射程だけでなく向きも確認し、近距離ほど強い外力を標的へ与える。
    void DispatchEnemyBreathPush(const engine::Vector3& direction, float distance);

    // モデルの正面補正を加え、標的方向へ最短角で振り向く。
    void UpdateFacing(const engine::Vector3& direction);

    // ブレス・浮遊・発射反動・着地の姿勢を状態別に作り、通常の跳ね姿勢へ補間する。
    void ApplySlimeAnimation(float deltaTime);

    // 戦闘判定とは独立した VFX と衝突半径を同期します。
    void EmitWindBreathParticles(const engine::Vector3& origin, const engine::Vector3& direction, float range);

    // GPU粒子が未初期化なら演出だけ省き、攻撃処理は続ける。
    void EmitWindPreset(const char* presetName, const engine::Vector3& position);

    // 吹き出す方向と速度倍率をプリセットへ渡し、攻撃ごとに粒子の向きを揃える。
    void EmitDirectedWindPreset(const char* presetName, const engine::Vector3& position, const engine::Vector3& direction,
                                float speedScale);

    // 見た目の伸縮を打ち消す局所半径へ換算し、ワールド上の当たり幅を維持する。
    void SyncWorldCollisionRadius(float worldRadius);

    // 地上行動中は通常の接触半径を使う。
    void SyncGroundCollisionRadius();

    // 投擲中は回転した体を拾いやすい半径へ広げる。
    void SyncThrownCollisionRadius();

    static constexpr int kWindAnimationPhaseCount = 8;

    // 攻撃の進行と予告表示のためのタイマーです。
    AttackState attackState_ = AttackState::Idle;
    float attackStateTimer_ = 0.0f; // 攻撃状態の切替までの残り秒数。
    float attackCooldown_ = 0.75f; // 攻撃の再使用までの残り秒数。
    float recoveryDuration_ = 0.0f;

    float idleTimer_ = 0.0f; // 待機中の見た目に使う経過秒数。
    float groundHopTimer_ = 0.0f;

    float ambientParticleTimer_ = 0.0f; // 周囲の粒子の発生までの残り秒数。
    float breathParticleTimer_ = 0.0f; // ブレス粒子の発生までの残り秒数。
    float breathPushTimer_ = 0.0f; // ブレスの押し出し判定までの残り秒数。

    float lockedTargetDistance_ = 0.0f; // 連射などの開始時に確定する水平距離。

    float volleyGroundY_ = 0.0f; // 連射動作から戻る地面の高さ。
    float volleyHoverY_ = 0.0f;
    float volleyLandingStartY_ = 0.0f;

    float volleyShotTimer_ = 0.0f; // 連射の次弾までの残り秒数。
    float volleyVisualTimer_ = 0.0f; // 連射中の表示までの残り秒数。
    float volleyRecoilTimer_ = 0.0f; // 発射反動までの残り秒数。

    engine::Vector3 lockedAttackDirection_ = {0.0f, 0.0f, 1.0f};
    engine::Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};

    int breathParticleCursor_ = 0;
    int volleyShotCount_ = 0; // 現在の連射で発射済みの弾数。

    bool hasBaseScale_ = false;
    bool warningTriggered_ = false;

    std::string debugPreviewAttackId_;

    std::array<engine::Vector3, 3> volleyImpactCenters_{};
    std::array<std::unique_ptr<engine::Object3D>, 3> heldWindOrbVisuals_;
};
