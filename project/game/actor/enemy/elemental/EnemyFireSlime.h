#pragma once
#include "BaseEnemy.h"
#include "game/actor/combat/AttackSequence.h"
#include "game/actor/enemy/combat/EnemyAttackAction.h"
#include "game/actor/enemy/combat/ISequencePreviewEnemy.h"

// 近距離では炎ブレス、遠距離では火球で攻撃し、頭上の炎で属性を示すスライム敵です。
class EnemyFireSlime : public BaseEnemy, public ISequencePreviewEnemy {
  public:
    // 炎スライム用モデル、Collider、初期能力値、炎エフェクトを準備します。
    void Initialize(engine::Object3dCommon* common, const std::string& modelName) override;

    // 徘徊、追跡、炎攻撃、投げ復帰、炎見た目を更新します。
    void Update(float deltaTime) override;

    /// 投げられた直後の速度と攻撃中断状態を設定する。
    void BeginThrown(const engine::Vector3& initialVelocity) override;

    /// 配置に使う炎スライムの複製を作る。
    std::unique_ptr<engine::Object3d> Clone() const override;

    /// プレビューで再生する攻撃 ID を指定する。
    void SetSequencePreviewAttackId(const std::string& id) override { previewAttackId_ = id; }

    /// 火球シーケンスが有効な場合、その段階名を返す。
    const char* GetSequenceStateLabel() const override {
        return selectedFireball_.sequence.enabled ? fireballSequence_.GetStateLabel() : nullptr;
    }

    /// 配置スケールを通常アニメーションの基準として保持する。
    void ApplyManagedScale(const engine::Vector3& scale) override {
        baseScale_ = scale;
        hasBaseScale_ = true;
        SetScale(scale);
    }

  private:
    // 攻撃本体、火の演出、スライムの伸縮を分けて管理する
    /// 行動不能中の更新を処理し、通常行動を止める場合は true を返す。
    bool UpdateInactiveState(float deltaTime);

    /// 投げ復帰中の更新を処理し、通常行動を止める場合は true を返す。
    bool UpdateThrowRecoveryState(float deltaTime);

    /// 未設定なら現在の配置スケールを記録する。
    void EnsureBaseScale();

    // 行動間隔と攻撃中の時間は負値を残さず、次の行動選択へ渡す。
    void UpdateWildTimers(float deltaTime);

    void UpdateWildBehavior(float deltaTime, engine::Vector3& velocity);

    // 編集済みシーケンスを優先し、使わない攻撃だけ従来の距離別行動へ戻す。
    void UpdateCombatBehavior(float deltaTime, engine::Vector3& velocity, const engine::Vector3& direction, float distance);

    // 索敵外は攻撃速度より遅い巡回速度で動き、進行方向へ体を向ける。
    void UpdateWanderBehavior(float deltaTime, engine::Vector3& velocity);

    // 詠唱中の跳ね動作は止め、飛び道具の発射姿勢と移動を競合させない。
    void ApplyGroundMovementAndAnimation(float deltaTime, engine::Vector3& velocity);

    // 停止中の微小ベクトルで向きを跳ねさせず、モデルの前方軸だけ補正する。
    void UpdateFacing(const engine::Vector3& direction);

    // 炎ブレス攻撃の予兆と再生状態を開始します。
    void StartBreath();

    // 攻撃判定より先に扇形の予告を見せ、判定発生後は予告を消す。
    void UpdateBreath(float deltaTime, const engine::Vector3& direction, float distance);

    /// 火球の予備動作または攻撃シーケンスを開始する。
    void StartFireballWindup(const engine::Vector3& direction, float distance);

    /// 予備動作の時間を進め、発射段階へ移す。
    void UpdateFireballWindup(float deltaTime, engine::Vector3& velocity, const engine::Vector3& direction, float distance);

    // ブレス範囲内の対象へダメージを通知します。
    void DispatchBreathDamage(const engine::Vector3& direction, float distance);

    /// 指定方向へ火球攻撃を発射する。
    void FireFireball(const engine::Vector3& direction, float distance);

    /// 攻撃シーケンスへ渡す対象と発射位置をまとめる。
    AttackSequenceContext MakeSequenceContext(const engine::Vector3& direction, float distance);

    /// 距離とプレビュー指定に合う攻撃定義を選ぶ。
    const EnemyAttackDefinition* SelectSequenceAttack(float distance) const;

    EnemyAttackDefinition selectedFireball_;
    EnemyAttackAction fireballSequence_;
    std::string previewAttackId_;

    // 頭上の炎は AI と別の更新経路で維持し、待機中も動かす。
    void UpdateHeadFlame(float deltaTime);

    /// 頭上の炎表示がなければ作成する。
    void EnsureHeadFlameVisual();

    // 頭上の炎エフェクトを本体位置と状態へ同期します。
    void UpdateHeadFlameVisual(float deltaTime);

    /// 頭上の炎表示をシーンから取り除くよう要求する。
    void RequestRemoveHeadFlameVisual();

    /// ブレス中に使う炎表示を必要数用意する。
    void EnsureBreathFlameVisuals();

    /// 吐息方向に沿って炎表示の位置と大きさを更新する。
    void UpdateBreathFlameVisuals(const engine::Vector3& direction, float deltaTime);

    /// 攻撃終了時にブレス用の炎表示を隠す。
    void HideBreathFlameVisuals();

    /// ブレス用の炎表示をシーンから取り除くよう要求する。
    void RequestRemoveBreathFlameVisuals();

    // 放出口を順に前へ進め、メッシュの炎と同じ距離帯に火の粉を散らす。
    void EmitBreathParticles(const engine::Vector3& origin, const engine::Vector3& direction);

    // 未初期化の GPU パーティクル管理には要求を送らない。
    void EmitFirePreset(const char* presetName, const engine::Vector3& position);

    // 火の粉の流れる向きを指定できる場合だけ方向付きプリセットを使う。
    void EmitDirectedFirePreset(const char* presetName, const engine::Vector3& position, const engine::Vector3& direction,
                                float speedScale = 1.0f);

    void ApplySlimeAnimation(float deltaTime);

    // 見た目の伸縮でワールド空間の当たり半径が変わらないよう逆スケールする。
    void SyncWorldCollisionRadius(float worldRadius);

    // 地上では小さめの本体判定へ戻す。
    void SyncGroundCollisionRadius();

    // 投擲時は接触を拾いやすい広い判定を使う。
    void SyncThrownCollisionRadius();

    float attackCooldown_ = 0.65f;     // 野生時の攻撃再使用待ち。
    float breathTimer_ = 0.0f;         // ブレス持続時間。
    float fireballWindupTimer_ = 0.0f; // 火球を撃つ前に見せる溜め時間。

    float breathParticleTimer_ = 0.0f; // ブレス粒子の発生間隔。
    float breathEmberTimer_ = 0.0f;    // ブレスの火の粉発生間隔。

    float headFlameParticleTimer_ = 0.0f;
    float headEmberParticleTimer_ = 0.0f;

    float attackTimer_ = 0.0f;    // 攻撃中の伸縮演出。
    float idleTimer_ = 0.0f;      // 待機呼吸アニメーション用。
    float groundHopTimer_ = 0.0f; // 通常移動中の小ホップ周期。

    float pendingFireballDistance_ = 0.0f;
    engine::Vector3 pendingFireballDirection_ = {0.0f, 0.0f, 1.0f};

    engine::Vector3 smoothedFlameVelocity_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};

    engine::Object3d* headFlameVisual_ = nullptr;
    engine::Object3d* breathFlameVisuals_[7] = {};

    int breathParticleCursor_ = 0;
    int headFlameParticleCursor_ = 0;

    bool hasBaseScale_ = false;
    bool breathDamageDone_ = false;
    bool breathWarningTriggered_ = false;
    bool fireballWarningTriggered_ = false;
    bool fireballAimLocked_ = false;

    bool headFlameRemoveRequested_ = false;
    bool breathFlameRemoveRequested_ = false;
};
