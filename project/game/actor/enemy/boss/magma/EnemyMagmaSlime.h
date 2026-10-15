#pragma once

#include "BaseEnemy.h"

#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

class BossChantVisual;
class EffectObject3d;

/// <summary>ステージ2中ボス。攻撃AI、溶岩系攻撃、登場演出を統括します。</summary>
class EnemyMagmaSlime final : public BaseEnemy {
  public:
    /// 本体の構築。攻撃状態の実際の初期化は Initialize で行う。
    EnemyMagmaSlime();
    /// 所有リソースは各メンバの破棄に委ねる。
    ~EnemyMagmaSlime() override;

    /// モデル、当たり判定、登場演出と攻撃定義を初期化する。
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    /// 遭遇状態と攻撃 AI、溶岩弾や溶岩床を更新する。
    void Update(float deltaTime) override;
    /// 本体と所有する通常描画の演出を描く。
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    /// スイッチの有効化に合わせて登場状態を切り替える。
    void OnSwitchEvent(bool active) override;
    /// 配置用の中ボス複製を作る。
    std::unique_ptr<engine::Object3d> Clone() const override;
    /// 配置スケールを攻撃判定と登場演出の基準にする。
    void ApplyManagedScale(const engine::Vector3& scale) override;
    bool IsPullImmune() const override { return true; }

    /// 特殊材質で描く溶岩表示を所有するか返す。
    bool HasOwnedSpecialMaterialVisuals() const override;
    /// 所有する特殊材質の溶岩表示を指定バッファで描く。
    void DrawOwnedSpecialMaterialVisuals(uint32_t depthSrvHandle, uint32_t grabSrvHandle) override;

    /// <summary>指定した技だけを再生する敵攻撃プレビューへ切り替えます。</summary>
    /// <param name="attackId">敵攻撃プロファイル中の攻撃ID。空文字は通常AIです。</param>
    void SetDebugPreviewAttackId(const std::string& attackId);
    /// <summary>プレビューの現在フェーズ名を返します。</summary>
    const char* GetDebugAttackPhaseName() const;

    // 中ボスHUDが参照する遭遇状態とHPです。
    /// 遭遇 HUD を表示すべき状態か返す。
    bool IsEncounterHudActive() const;
    /// HUD に表示する現在 HP を返す。
    float GetEncounterCurrentHp() const;
    /// HUD の分母となる最大 HP を返す。
    float GetEncounterMaximumHp() const;
    /// 登場演出の進行率を HUD 向けに返す。
    float GetEncounterAppearanceProgress() const;

  private:
    enum class EncounterState {
        Normal,
        Dormant,
        Appearing,
        Active,
    };

    enum class AttackKind {
        None,
        MagmaMortar,
        LavaRush,
        VolcanicSlam,
        EruptionField,
        LavaSpiral,
        FireColumn,
        MagmaFlood,
    };

    enum class AttackState {
        Idle,
        Windup,
        Active,
        Recovery,
    };

    // 放物線を飛ぶマグマ弾の、着弾までの位置と時間を保持します。
    struct MagmaBlob {
        std::unique_ptr<engine::Object3d> visual;
        engine::Vector3 start{};
        engine::Vector3 control{};
        engine::Vector3 target{};
        float age = 0.0f;
        float flightDuration = 0.8f;
        float spin = 0.0f;
        bool impacted = false;
    };

    // 着弾後に残る溶岩床。damageTimer は継続ダメージの次回判定までの秒数です。
    struct MagmaPool {
        std::unique_ptr<engine::Object3d> visual;
        engine::Vector3 position{};
        float age = 0.0f;
        float lifetime = 3.4f;
        float radius = 2.0f;
        float damageTimer = 0.0f;
    };

    // 火山プレスから広がる波。previousRadius は高速フレームでの判定抜けを防ぎます。
    struct MagmaRing {
        std::unique_ptr<engine::Object3d> visual;
        engine::Vector3 position{};
        float age = 0.0f;
        float delay = 0.0f;
        float lifetime = 0.72f;
        float startRadius = 1.0f;
        float endRadius = 8.0f;
        float previousRadius = 1.0f;
        bool hitTarget = false;
    };

    // 間欠泉の予告、噴出、消滅を個別に管理します。
    struct MagmaPillar {
        std::unique_ptr<engine::Object3d> visual;
        engine::Vector3 position{};
        float age = 0.0f;
        float delay = 0.0f;
        float lifetime = 0.92f;
        float radius = 1.4f;
        float height = 7.0f;
        float particleTimer = 0.0f;
        std::string attackId;
        bool burstSpawned = false;
        bool damageApplied = false;
    };

    // 螺旋溶岩波の進行方向、軌道、溶岩床生成状態です。
    struct MagmaSurge {
        std::unique_ptr<engine::Object3d> visual;
        std::unique_ptr<engine::Object3d> crestVisual;
        engine::Vector3 origin{};
        engine::Vector3 direction{0.0f, 0.0f, 1.0f};
        float age = 0.0f;
        float delay = 0.0f;
        float lifetime = 1.18f;
        float maxDistance = 13.0f;
        float radius = 1.1f;
        float trailTimer = 0.0f;
        float curveAmount = 0.0f;
        bool leavesPool = false;
        bool hitTarget = false;
        bool poolSpawned = false;
    };

    // 地面を進むマグマ魔法の一区画です。delay で順番に噴出させます。
    struct MagmaFloodCell {
        std::unique_ptr<engine::Object3d> surface;
        std::unique_ptr<engine::Object3d> crest;
        engine::Vector3 position{};
        engine::Vector3 direction{0.0f, 0.0f, 1.0f};
        float delay = 0.0f;
        float age = 0.0f;
        float lifetime = 2.2f;
        float radius = 1.4f;
        bool burstSpawned = false;
        bool leavesPool = false;
    };

    // AIと攻撃フェーズ。攻撃時間・射程は EnemyAttackProfile の設定を基準にします。
    /// 未設定なら配置スケールを演出と判定の基準として記録する。
    void EnsureBaseScale();
    /// <summary>標的距離と現在フェーズから、移動速度と攻撃開始を決めます。</summary>
    void UpdateBehavior(float deltaTime, const engine::Vector3& targetDirection, float targetDistance, engine::Vector3& velocity);
    /// <summary>攻撃状態と予備動作を初期化します。</summary>
    void StartAttack(AttackKind kind, const engine::Vector3& targetDirection);
    /// <summary>予備動作、攻撃、硬直の更新先を切り替えます。</summary>
    void UpdateAttack(float deltaTime, const engine::Vector3& targetDirection, float targetDistance, engine::Vector3& velocity);
    /// <summary>魔法陣や攻撃予告を更新します。</summary>
    void UpdateWindup(float deltaTime, const engine::Vector3& targetDirection);
    /// <summary>予備動作完了時に各技の攻撃本体を開始します。</summary>
    void BeginActive();
    /// <summary>進行中の攻撃を更新し、終了時に硬直へ移します。</summary>
    void UpdateActive(float deltaTime, float targetDistance, engine::Vector3& velocity);
    /// 攻撃後の硬直へ移る。
    void BeginRecovery();
    /// 現在の技を終了して次の行動を選べる状態へ戻す。
    void FinishAttack();

    // 全技を一巡するまで重複させず、射程内の技を優先して選びます。
    AttackKind ResolveAutomaticAttack(float targetDistance);
    /// 一巡する攻撃候補を補充する。
    void PrepareAttackCycle();
    /// デバッグ指定中の攻撃種別を解決する。
    AttackKind ResolveDebugAttack() const;
    /// 内部の攻撃種別を JSON プロファイルの ID へ変換する。
    const char* GetAttackId(AttackKind kind) const;
    /// 現在の技に対応する攻撃定義を返す。
    const EnemyAttackDefinition& GetCurrentAttackDefinition() const;

    // マグマ弾と着弾床。ターゲット位置を確定してから弾を逐次発射します。
    void PrepareMortarTargets();
    /// 確定済みの目標へ次のマグマ弾を発射する。
    void LaunchNextMagmaBlob();
    /// 飛翔中のマグマ弾を進めて着弾を処理する。
    void UpdateMagmaBlobs(float deltaTime);
    /// 弾の着弾位置に判定と溶岩床を発生させる。
    void ImpactMagmaBlob(MagmaBlob& blob);
    /// 指定位置に一定時間残る溶岩床を作る。
    void SpawnMagmaPool(const engine::Vector3& position, float radius, float lifetime);
    /// 溶岩床の寿命と継続ダメージを更新する。
    void UpdateMagmaPools(float deltaTime);
    // 突進とその炎の残像は同じ進行率から更新します。
    void BeginRush();
    /// 突進中の移動速度と攻撃判定を更新する。
    void UpdateRush(float deltaTime, float targetDistance, engine::Vector3& velocity);
    /// 突進を終え、残像表示を片付ける。
    void FinishRush();
    /// 突進時の炎表示を必要数用意する。
    void EnsureRushFlameVisuals();
    /// 突進の進行率から炎表示の位置と大きさを更新する。
    void UpdateRushFlameVisuals(const engine::Vector3& position, float progress, float deltaTime);
    /// 突進用の炎表示を取り除く。
    void ClearRushFlameVisuals();
    // 火山プレスは着地を一度だけ判定し、波を別オブジェクトとして広げます。
    void BeginVolcanicSlam();
    /// 火山プレスの落下と着地時刻を進める。
    void UpdateVolcanicSlam(float deltaTime, engine::Vector3& velocity);
    /// 着地地点の衝撃と波を一度だけ発生させる。
    void ImpactVolcanicSlam();
    /// 着地点から広がる溶岩の輪を作る。
    void SpawnSlamRings(const engine::Vector3& position);
    /// 輪の拡大と接触判定を更新する。
    void UpdateMagmaRings(float deltaTime);
    // 間欠泉の候補位置を地面へ投影してから、時間差で柱を生成します。
    void PrepareEruptionTargets();
    /// 確定した候補地点へ次の間欠泉を配置する。
    void SpawnNextMagmaPillar();
    void SpawnMagmaPillar(const engine::Vector3& position, float radius, float height, float lifetime, float delay,
                          const char* attackId);
    /// 間欠泉の予告、噴出、終了を進める。
    void UpdateMagmaPillars(float deltaTime);
    // 螺旋溶岩波は複数の進行体を使い、通過した位置に溶岩床を残します。
    void BeginLavaSpiral();
    /// 螺旋溶岩波の移動と通過後の床生成を進める。
    void UpdateMagmaSurges(float deltaTime);
    // 三柱の炎魔法。各魔法陣と柱の順序を同じターゲット配列で管理します。
    void PrepareFireColumnTargets();
    /// 炎柱の候補位置に予告用魔法陣を作る。
    void CreateFireColumnSigils();
    /// 魔法陣の表示を攻撃段階へ同期する。
    void UpdateFireColumnSigils(float deltaTime);
    /// 炎柱の連続発生を開始する。
    void BeginFireColumn();
    /// 各炎柱の発生間隔と寿命を更新する。
    void UpdateFireColumn(float deltaTime);
    // マグマ流は複数セルを順番に出し、最後のセルまで攻撃を維持します。
    void BeginMagmaFlood();
    /// セルを順に発生させてマグマ流を進める。
    void UpdateMagmaFlood(float deltaTime);
    /// 詠唱陣を現在の予備動作へ同期する。
    void UpdateChantVisual(float deltaTime);

    /// <summary>攻撃用モデルを作り、描画材質・色・発光を設定します。</summary>
    std::unique_ptr<engine::Object3d> CreateMagmaVisual(const std::string& name, const std::string& model, int materialType,
                                                const engine::Vector4& color, float emissive) const;
    /// 指定位置の下にある地面上の座標を探す。
    engine::Vector3 FindGroundPoint(const engine::Vector3& samplePosition) const;
    /// <summary>指定範囲の標的へダメージと燃焼を適用します。</summary>
    /// <param name="center">地面上の判定中心。</param>
    /// <param name="radius">水平面の判定半径。</param>
    /// <param name="burnDuration">燃焼の継続秒数。0 は燃焼なしです。</param>
    void DamageTargetAt(const engine::Vector3& center, float radius, float damage, const engine::Vector3& knockback, float burnDuration,
                        float burnTickInterval, float burnTickDamage);
    /// 攻撃状態に応じた本体の伸縮を反映する。
    void ApplySlimeAnimation(float deltaTime);
    /// 指定方向へ本体を向ける。
    void UpdateFacing(const engine::Vector3& direction);
    /// 本体の熱色と発光を攻撃段階へ同期する。
    void UpdateBodyHeat(float deltaTime);
    void EmitPreset(const char* presetName, const engine::Vector3& position) const;
    void EmitDirectedPreset(const char* presetName, const engine::Vector3& position, const engine::Vector3& direction,
                            float speedScale) const;
    /// <summary>見た目のスケールを本体の衝突半径へ反映します。</summary>
    void SyncCollisionRadius();
    /// <summary>中断・形態変更時に残った攻撃オブジェクトを破棄します。</summary>
    void ClearTransientVisuals();

    // 登場から戦闘開始までの状態を管理します。Editor配置値は別に保持します。
    /// 休眠から登場完了までを進め、通常 AI を止める間は true を返す。
    bool UpdateEncounterState(float deltaTime);
    /// Editor 配置の位置と大きさを遭遇演出前に退避する。
    void CaptureEncounterAuthoredState(bool refreshVisualTransform = false);
    /// Play 開始時のスイッチ状態から遭遇段階を設定する。
    void InitializeEncounterState();
    /// Editor に戻るとき配置と見た目を復元する。
    void ResetEncounterStateForEditor();
    /// 休眠状態から登場演出を開始する。
    void BeginEncounterAppearance();
    /// ボスを休眠させ、戦闘用表示を隠す。
    void ApplyDormantEncounterState();
    /// 登場演出を終えて戦闘状態へ移る。
    void FinishEncounterAppearance();
    /// スイッチで起動する遭遇制御の対象か返す。
    bool IsEncounterControlled() const;
    /// 現在の登場演出の継続時間を返す。
    float GetEncounterAppearanceDuration() const;

    // 攻撃中の位置・弾・演出オブジェクトです。
    AttackKind currentAttack_ = AttackKind::None;
    AttackState attackState_ = AttackState::Idle;
    engine::Vector3 baseScale_{1.0f, 1.0f, 1.0f};
    engine::Vector3 lockedDirection_{0.0f, 0.0f, 1.0f};
    engine::Vector3 lockedTargetPosition_{};
    engine::Vector3 attackStartPosition_{};
    engine::Vector3 attackEndPosition_{};
    std::array<engine::Vector3, 15> mortarTargets_{};
    std::array<engine::Vector3, 9> eruptionTargets_{};
    std::vector<MagmaBlob> magmaBlobs_;
    std::vector<MagmaPool> magmaPools_;
    std::vector<MagmaRing> magmaRings_;
    std::vector<MagmaPillar> magmaPillars_;
    std::vector<MagmaSurge> magmaSurges_;
    std::vector<MagmaFloodCell> magmaFloodCells_;
    std::array<std::unique_ptr<engine::Object3d>, 5> rushFlameVisuals_{};
    std::array<std::unique_ptr<engine::Object3d>, 4> fireColumnVisuals_{};
    std::array<std::unique_ptr<EffectObject3d>, 4> fireColumnSigils_{};
    std::array<std::unique_ptr<EffectObject3d>, 3> fireColumnLinks_{};
    engine::Vector3 fireColumnCenter_{};
    std::array<engine::Vector3, 3> fireColumnOuterPositions_{};
    std::unique_ptr<BossChantVisual> chantVisual_;

    // タイマーはすべて秒単位です。attackTimer_ は現在フェーズの残り時間です。
    float idleTimer_ = 0.0f;
    float locomotionHopTimer_ = 0.0f;
    float locomotionLandingTimer_ = 0.0f;
    bool locomotionAirborne_ = false;
    float attackTimer_ = 0.0f;
    float attackStateDuration_ = 0.0f;
    float attackCooldown_ = 1.0f;
    float actionTimer_ = 0.0f;
    float effectTimer_ = 0.0f;
    float ambientEmberTimer_ = 0.0f;
    float landingPulseTimer_ = 0.0f;
    float mortarPatternPhase_ = 0.0f;
    float fireColumnDamageTimer_ = 0.0f;
    float floodDamageTimer_ = 0.0f;
    int actionIndex_ = 0;
    int automaticAttackSerial_ = 0;
    std::array<AttackKind, 7> attackCycle_{};
    std::size_t attackCycleIndex_ = attackCycle_.size();
    AttackKind lastAutomaticAttack_ = AttackKind::None;
    std::mt19937 attackRandom_{std::random_device{}()};
    bool hasBaseScale_ = false;
    bool warningTriggered_ = false;
    bool attackDamageApplied_ = false;
    bool rushImpactSpawned_ = false;
    bool slamImpactSpawned_ = false;
    std::string debugPreviewAttackId_;

    // Editorで指定した登場前の状態。Playの開始・停止で安全に復元します。
    EncounterState encounterState_ = EncounterState::Normal;
    engine::Vector3 encounterBasePosition_{};
    engine::Vector3 encounterBaseScale_{1.0f, 1.0f, 1.0f};
    engine::Vector3 encounterBaseRotation_{};
    engine::Vector4 encounterBaseColor_{1.0f, 1.0f, 1.0f, 1.0f};
    float encounterBaseEmissive_ = 1.18f;
    uint32_t encounterCollisionAttribute_ = 0;
    uint32_t encounterCollisionMask_ = 0;
    int encounterMaterialType_ = 0;
    float encounterTimer_ = 0.0f;
    bool encounterInitializedForPlay_ = false;
    bool encounterAuthoredStateCaptured_ = false;
    bool encounterRequestedActive_ = false;
};
