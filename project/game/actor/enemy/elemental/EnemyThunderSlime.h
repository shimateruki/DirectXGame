#pragma once
#include "BaseEnemy.h"
#include "EffectObject3D.h"
#include <array>
#include <memory>

// EnemyThunderSlimeは、チャージ放電、雷オーラ、待機火花を使う属性スライム敵です。
// 関連：BaseEnemyの標的と予告を使い、放電の段階と雷の表示物を更新する。
// 主な入口：Initialize / Update
class EnemyThunderSlime : public BaseEnemy {
  public:
    /// 所有する雷オーラの表示を解放する。
    ~EnemyThunderSlime() override;

    // 雷スライム用モデル、Collider、雷演出の初期状態を準備します。
    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    // 徘徊、追跡、チャージ、放電、雷オーラを更新します。
    void Update(float deltaTime) override;

    // 本体と雷オーラをライト情報付きで描画します。
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;

    /// 投げ状態の速度を設定し、雷攻撃を中断する。
    void BeginThrown(const engine::Vector3& initialVelocity) override;

    /// 配置用の雷スライム複製を作る。
    std::unique_ptr<engine::Object3D> Clone() const override;

    // 攻撃候補を限定し、進行中のチャージと再使用待ちをリセットする。
    void SetDebugPreviewAttackId(const std::string& attackId);

    /// デバッグ表示向けに放電攻撃の段階名を返す。
    const char* GetDebugAttackPhaseName() const;

    /// 配置スケールを待機・放電演出の基準へ反映する。
    void ApplyManagedScale(const engine::Vector3& scale) override {
        baseScale_ = scale;
        hasBaseScale_ = true;
        SetScale(scale);
    }

  private:
    // 放電攻撃、常時オーラ、投げ状態の当たり判定調整を分けて管理する
    /// 行動不能中の更新を処理し、通常行動を止める場合は true を返す。
    bool UpdateInactiveState(float deltaTime);

    /// 投げ復帰中の更新を処理し、通常行動を止める場合は true を返す。
    bool UpdateThrowRecoveryState(float deltaTime);

    /// 未設定なら現在の配置スケールを記録する。
    void EnsureBaseScale();

    // 攻撃後のつぶれと次の攻撃までの待ち時間を独立して進める。
    void UpdateWildTimers(float deltaTime);

    // 連続落雷中は新しい攻撃を選ばず、固定した方向への落雷を完了させる。
    void UpdateWildBehavior(float deltaTime, engine::Vector3& velocity);

    // 近距離放電・中距離落雷・追跡・巡回を射程と待機時間で振り分ける。
    void UpdateCombatBehavior(float deltaTime, engine::Vector3& velocity, const engine::Vector3& direction, float distance);

    /// 野生時の落雷攻撃に向けて狙い方向を固定する。
    void StartWildLightning(const engine::Vector3& direction);

    /// 落雷のチャージ、予告、連続着弾を進める。
    void UpdateWildLightning(float deltaTime);

    /// 指定地点に落雷の表示と判定を発生させる。
    void SpawnWildLightningStrike(const engine::Vector3& groundPosition);

    /// 落雷地点の範囲内へダメージを通知する。
    void DispatchWildLightningDamage(const engine::Vector3& groundPosition);

    /// 野生時落雷の進行状態を初期状態へ戻す。
    void ResetWildLightning();

    // 検知範囲外では共通巡回速度を使い、移動方向へモデルを向ける。
    void UpdateWanderBehavior(float deltaTime, engine::Vector3& velocity);

    // 詠唱中は足踏みを止め、移動中だけ跳ねる速度と地上判定半径を反映する。
    void ApplyGroundMovementAndAnimation(float deltaTime, engine::Vector3& velocity);

    // モデルの正面補正を加え、最短角で標的へ向ける。
    void UpdateFacing(const engine::Vector3& direction);

    // 放電攻撃前のチャージ状態へ移行します。
    void StartCharge();

    // 残り時間で円形予兆と電弧を強め、終了時に一度だけ放電する。
    void UpdateCharge(float deltaTime, const engine::Vector3& direction);

    // チャージ完了後に雷範囲攻撃を発生させます。
    void ReleaseShock(const engine::Vector3& direction);

    // 高さ方向の差を軽く見て球状範囲を判定し、中心に近いほど押し出しを強める。
    void DispatchShockDamage(const engine::Vector3& center, const engine::Vector3& direction, float radius, float damage);

    /// 待機中に発生する小さな火花を更新する。
    void UpdateIdleSpark(float deltaTime);

    /// 雷オーラの描画対象を作成する。
    void InitializeAuraEffect();

    // 雷オーラの形状、色、表示状態を本体に同期します。
    void UpdateAuraEffect(float deltaTime);

    /// 雷オーラを非表示にする。
    void HideAuraEffect();

    /// 体の大きさから雷オーラの中心と縦横の径を計算する。
    void CalculateAuraShape(engine::Vector3& center, float& horizontalDiameter, float& verticalDiameter) const;

    // 電弧を楕円周上に分散し、個数と位相で待機・詠唱・放電を使い分ける。
    void EmitOuterThunderEffect(const char* presetName, int count, float phaseOffset = 0.0f);

    // 落雷の発生が近づくほど電弧を中心・上方へ集める。
    void EmitLineChargeEffect(const char* presetName, float progress);

    // GPU粒子が未初期化のプレビューでも本体の攻撃は継続する。
    void EmitThunderPreset(const char* presetName, const engine::Vector3& position);

    // 上から地形を探して雷を接地させ、地形がなければ指定高さを使う。
    engine::Vector3 FindStrikeGround(const engine::Vector3& samplePosition, engine::Object3D* ignoredObject, float fallbackHeight) const;

    // 柱状の雷は着地点を基準に上へ伸ばし、床の閃光と焦げ跡は地面へ置く。
    void SpawnLightningStrikeVisual(const engine::Vector3& groundPosition, const engine::Vector3& direction);

    // 地面との重なりを避ける小さな高さ補正を加えて詠唱模様を置く。
    void SpawnChargeGroundEffect(const engine::Vector3& groundPosition, float scale = 1.0f);

    // 放電・落雷・反動で異なる目標姿勢を作り、通常時は跳ねる変形へ戻す。
    void ApplySlimeAnimation(float deltaTime);

    // 見た目のスケール変化に左右されないワールド半径になるよう局所半径へ換算する。
    void SyncWorldCollisionRadius(float worldRadius);

    // 地上の通常行動では小さい接触半径を使う。
    void SyncGroundCollisionRadius();

    // 投擲時は回転しても当たりやすい半径に切り替える。
    void SyncThrownCollisionRadius();

    float attackCooldown_ = 1.15f;     // 野生時の放電再使用待ち。
    float chargeTimer_ = 0.0f;         // 放電前のタメ時間。
    float chargeParticleTimer_ = 0.0f; // タメ中の火花発生間隔。
    float shockSquashTimer_ = 0.0f;    // 放電後の横広がり演出。
    float idleTimer_ = 0.0f;           // 待機アニメーション用。
    float groundHopTimer_ = 0.0f;      // 通常移動中の小ホップ周期。
    float idleSparkTimer_ = 0.0f;      // 常時火花の発生間隔。

    enum class WildLightningState {
        Idle,
        Charging,
        Striking,
    };

    WildLightningState wildLightningState_ = WildLightningState::Idle;

    std::array<engine::Vector3, 5> wildStrikePositions_{};
    engine::Vector3 wildLightningDirection_ = {0.0f, 0.0f, 1.0f};

    float wildLightningTimer_ = 0.0f; // 暴走雷の段階切替までの残り秒数。
    float wildLightningEffectTimer_ = 0.0f; // 暴走雷の表示更新までの残り秒数。

    int wildStrikeIndex_ = 0; // 暴走中の次の落雷位置を選ぶ番号。
    bool wildLightningHitTarget_ = false; // 同じ暴走攻撃で対象に繰り返し命中させないための印。

    engine::Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 lastShockDirection_ = {0.0f, 0.0f, 1.0f};

    std::unique_ptr<EffectObject3D> auraEffect_;

    bool hasBaseScale_ = false;
    bool isCharging_ = false;
    bool chargeWarningTriggered_ = false;

    std::string debugPreviewAttackId_;
};
