#pragma once
#include "game/actor/enemy/combat/EnemyAttackSelection.h"

#include "BaseEnemy.h"
#include "engine/animation/BodyAnimationClip.h"
#include "game/actor/enemy/animation/BossGroundLocomotion.h"

#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

class BossChantVisual;
class EffectObject3D;

/// <summary>ステージ2中ボス。攻撃AI、溶岩系攻撃、登場演出を統括します。</summary>
// 関連：BaseEnemyの戦闘基盤とBossChantVisualの詠唱表示を使い、地形型の攻撃を進める。
// 主な入口：Initialize / Update
class EnemyMagmaSlime final : public BaseEnemy {
  public:
    /// 本体の構築。攻撃状態の実際の初期化は Initialize で行う。
    EnemyMagmaSlime();

    /// 所有リソースは各メンバの破棄に委ねる。
    ~EnemyMagmaSlime() override;

    /// モデル、当たり判定、登場演出と攻撃定義を初期化する。
    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    /// 遭遇状態と攻撃 AI、溶岩弾や溶岩床を更新する。
    void Update(float deltaTime) override;

    /// 本体と所有する通常描画の演出を描く。
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;

    void DrawOwnedTransparentVisuals(ID3D12Resource* pointLightResource,
                                     ID3D12Resource* spotLightResource) override;

    void DrawForCamera(Camera* camera, ID3D12Resource* pointLightResource,
                       ID3D12Resource* spotLightResource, int previewBufferIndex = 0) override;

    void DrawOwnedTransparentVisualsForCamera(Camera* camera, ID3D12Resource* pointLightResource,
                                              ID3D12Resource* spotLightResource,
                                              int previewBufferIndex = 0) override;

    /// スイッチの有効化に合わせて登場状態を切り替える。
    void OnSwitchEvent(bool active) override;

    /// 配置用の中ボス複製を作る。
    std::unique_ptr<engine::Object3D> Clone() const override;

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
        // 飛行中の表示を所有する。
        std::unique_ptr<engine::Object3D> visual;
        // 発射位置・制御点・着弾点は、二次ベジェ曲線に使うワールド座標。
        engine::Vector3 start{};
        engine::Vector3 control{};
        engine::Vector3 target{};
        // 発射してからの経過秒数。
        float age = 0.0f;
        // 発射から着弾までの秒数。ageとの比で軌道上の位置を求める。
        float flightDuration = 0.8f;
        // 表示に適用する回転角（ラジアン）。
        float spin = 0.0f;
        // 終点に着いた弾の着弾処理を一度に制限する。
        bool impacted = false;
    };

    // 着弾後に残る溶岩床。damageTimer は継続ダメージの次回判定までの秒数です。
    struct MagmaPool {
        std::unique_ptr<engine::Object3D> visual;
        // 溶岩床の中心のワールド座標。
        engine::Vector3 position{};
        // 生成してからの経過秒数。
        float age = 0.0f;
        // 床が消えるまでの秒数。
        float lifetime = 3.4f;
        // 水平面でのダメージ範囲の半径。
        float radius = 2.0f;
        float damageTimer = 0.0f;
    };

    // 火山プレスから広がる波。previousRadius は高速フレームでの判定抜けを防ぎます。
    struct MagmaRing {
        std::unique_ptr<engine::Object3D> visual;
        engine::Vector3 position{};
        float age = 0.0f;
        // 衝撃波が広がり始めるまでの待ち時間（秒）。
        float delay = 0.0f;
        // 広がり始めてから消えるまでの秒数。
        float lifetime = 0.72f;
        float startRadius = 1.0f;
        float endRadius = 8.0f;
        // 前フレームの半径。今回の半径との間を調べてすり抜けを防ぐ。
        float previousRadius = 1.0f;
        // この波で標的にダメージを与えたか。
        bool hitTarget = false;
    };

    // 間欠泉の予告、噴出、消滅を個別に管理します。
    struct MagmaPillar {
        std::unique_ptr<engine::Object3D> visual;
        // 柱の根元に使うワールド座標。
        engine::Vector3 position{};
        float age = 0.0f;
        // 生成から噴出を開始するまでの秒数。
        float delay = 0.0f;
        // 噴出開始後に表示を維持する秒数。
        float lifetime = 0.92f;
        float radius = 1.4f;
        float height = 7.0f;
        // 柱の継続粒子を出すまでの残り秒数。
        float particleTimer = 0.0f;
        // 柱を発生させた技のID。発生後のダメージ設定を引くときに使う。
        std::string attackId;
        // 初回の噴出粒子を発生済みか。
        bool burstSpawned = false;
        // この柱でダメージを適用済みか。
        bool damageApplied = false;
    };

    // 螺旋溶岩波の進行方向、軌道、溶岩床生成状態です。
    struct MagmaSurge {
        std::unique_ptr<engine::Object3D> visual;
        // 波の上端に重ねる表示。
        std::unique_ptr<engine::Object3D> crestVisual;

        // 溶岩波の進行を計算する起点。
        engine::Vector3 origin{};
        // 曲がりを加える前の基準方向。
        engine::Vector3 direction{0.0f, 0.0f, 1.0f};

        float age = 0.0f;
        // この波が動き始めるまでの秒数。
        float delay = 0.0f;
        // 動き始めてから消えるまでの秒数。
        float lifetime = 1.18f;

        float maxDistance = 13.0f;
        float radius = 1.1f;

        // 次の軌跡粒子までの残り秒数。
        float trailTimer = 0.0f;
        // 進行に応じて基準方向に加える角度の最大量（ラジアン）。
        float curveAmount = 0.0f;

        // この波が着地点に溶岩床を残すか。
        bool leavesPool = false;
        bool hitTarget = false;
        // この波から溶岩床を生成済みか。
        bool poolSpawned = false;
    };

    // 地面を進むマグマ魔法の一区画です。delay で順番に噴出させます。
    struct MagmaFloodCell {
        // 地面を覆う溶岩面の表示。
        std::unique_ptr<engine::Object3D> surface;
        // 溶岩面の立ち上がりに重ねる表示。
        std::unique_ptr<engine::Object3D> crest;
        // 洪水セルのワールド座標。
        engine::Vector3 position{};
        // 飛沫を出す方向の基準。
        engine::Vector3 direction{0.0f, 0.0f, 1.0f};
        float delay = 0.0f;
        // 生成からの経過秒数。delayを引いて出現後の時間を求める。
        float age = 0.0f;
        // 出現後の寿命（秒）。
        float lifetime = 2.2f;
        float radius = 1.4f;
        // 出現時の飛沫を出したか。
        bool burstSpawned = false;
        // 出現時にこのセルが溶岩床を残すか。
        bool leavesPool = false;
    };

    // AIと攻撃フェーズ。攻撃時間・射程は EnemyAttackProfile の設定を基準にします。
    /// 未設定なら配置スケールを演出と判定の基準として記録する。
    void EnsureBaseScale();

    /// <summary>標的距離と現在フェーズから、移動速度と攻撃開始を決めます。</summary>
    void UpdateBehavior(float deltaTime, const engine::Vector3& targetDirection, float targetDistance, engine::Vector3& velocity);
    void UpdateLocomotion(float deltaTime, const engine::Vector3& targetDirection, engine::Vector3& velocity);

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

    /// 本編とプレビューが共有する攻撃種別・IDの対応表を返す。
    std::span<const EnemyAttackSelection::Entry<AttackKind>> GetAttackEntries() const;

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

    // 噴出遅延と攻撃 ID を柱ごとに保持し、着地衝撃と通常噴火で威力を分ける。
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
    std::unique_ptr<engine::Object3D> CreateMagmaVisual(const std::string& name, const std::string& model, int materialType,
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

    /// Editorで編集する火山プレスの姿勢を攻撃フェーズの実時刻へ同期する。
    void UpdateSlamBodyPose();

    /// 指定方向へ本体を向ける。
    void UpdateFacing(const engine::Vector3& direction);

    /// 本体の熱色と発光を攻撃段階へ同期する。
    void UpdateBodyHeat(float deltaTime);

    // GPU粒子が利用できる場合だけ指定プリセットを発生させる。
    void EmitPreset(const char* presetName, const engine::Vector3& position) const;

    // 発射方向と速度倍率を持つ粒子だけを、GPU粒子管理器が有効な場合に放出する。
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
    // 攻撃の種類。技ごとの設定や更新処理を選ぶ。
    AttackKind currentAttack_ = AttackKind::None;
    // 現在の技が予備動作・実行・硬直のどの段階にいるか。
    AttackState attackState_ = AttackState::Idle;

    engine::Vector3 baseScale_{1.0f, 1.0f, 1.0f}; // 攻撃・被弾の変形を重ねる前の基準倍率。

    engine::Vector3 lockedDirection_{0.0f, 0.0f, 1.0f}; // 攻撃の狙いを確定した水平単位方向。
    // 技が狙いを固定したときの標的のワールド座標。
    engine::Vector3 lockedTargetPosition_{};

    // 突進やプレスの軌道を計算する起点のワールド座標。
    engine::Vector3 attackStartPosition_{};
    // 突進の到達点やプレスの着地点に使うワールド座標。
    engine::Vector3 attackEndPosition_{};

    // 各弾の着弾点と各噴火柱の位置。攻撃開始時にワールド座標で決める。
    std::array<engine::Vector3, 15> mortarTargets_{};
    std::array<engine::Vector3, 9> eruptionTargets_{};

    // 飛行中の溶岩弾。着弾後は更新ループで取り除く。
    std::vector<MagmaBlob> magmaBlobs_;
    // 地面に残る溶岩床。弾とは別の寿命とダメージ間隔で更新する。
    std::vector<MagmaPool> magmaPools_;
    // 発生待ちと拡張中の衝撃波を保持する。
    std::vector<MagmaRing> magmaRings_;
    // 遅延発生する噴火柱。生成元の技が終わった後も各柱の寿命まで更新する。
    std::vector<MagmaPillar> magmaPillars_;
    // 螺旋状に広がる溶岩波を保持する。
    std::vector<MagmaSurge> magmaSurges_;
    // 洪水を構成するセル。区画ごとに発生と消滅を進める。
    std::vector<MagmaFloodCell> magmaFloodCells_;

    // 突進中に本体周辺へ配置する炎の表示。
    std::array<std::unique_ptr<engine::Object3D>, 5> rushFlameVisuals_{};

    // 炎柱本体の表示。外周3本と中央1本を技の進行に合わせて更新する。
    std::array<std::unique_ptr<engine::Object3D>, 4> fireColumnVisuals_{};
    // 外周3箇所と中央の魔法陣。炎柱本体とは別に更新する。
    std::array<std::unique_ptr<EffectObject3D>, 4> fireColumnSigils_{};
    // 外周から中央へ伸びる3本の連結演出。
    std::array<std::unique_ptr<EffectObject3D>, 3> fireColumnLinks_{};

    // 中央の主柱を置くワールド座標。
    engine::Vector3 fireColumnCenter_{};
    // 順に発生する外周3本のワールド座標。
    std::array<engine::Vector3, 3> fireColumnOuterPositions_{};

    // 術式の詠唱中に本体を中心として再生する魔法陣や粒子。
    std::unique_ptr<BossChantVisual> chantVisual_;

    // 火山プレス中に本体へ適用するボディアニメーション。
    BodyAnimationClip slamBodyClip_;
    // 火山プレスのボディアニメーションで表示に渡す姿勢行列。
    std::shared_ptr<engine::Matrix4x4> slamVisualPose_;
    bool scriptedMovementThisFrame_ = false; // 技の処理で座標を直接動かしたフレームはCharacterMotorの移動を止める。

    // タイマーはすべて秒単位です。attackTimer_ は現在フェーズの残り時間です。
    // ボスの経過秒数。見た目の揺れや攻撃パターンの位相に使う。
    float idleTimer_ = 0.0f;
    // 接地移動の状態。方向転換や歩行の進行をフレーム間で保持する。
    BossGroundLocomotion locomotion_{};
    // 歩数に応じた演出の連番。左右交互の切り替えに使う。
    std::uint32_t locomotionStepSerial_ = 0;

    float attackTimer_ = 0.0f; // 攻撃の段階切替までの残り秒数。
    float attackStateDuration_ = 0.0f; // 現在の攻撃段階の全長（秒）。残り時間の正規化に使う。
    float attackCooldown_ = 1.0f; // 攻撃の再使用までの残り秒数。

    float actionTimer_ = 0.0f; // 技内の次の発生処理までの残り秒数。
    float effectTimer_ = 0.0f; // 次の粒子・表示更新までの残り秒数。

    float ambientEmberTimer_ = 0.0f; // 周囲の火の粉の発生までの残り秒数。
    float landingPulseTimer_ = 0.0f; // 着地の変形までの残り秒数。

    // 溶岩弾の着弾パターンを回転させる基準角（ラジアン）。
    float mortarPatternPhase_ = 0.0f;
    // 主柱の次のダメージ判定までの残り秒数。
    float fireColumnDamageTimer_ = 0.0f;
    // 洪水セル全体で共有するダメージ間隔の残り秒数。
    float floodDamageTimer_ = 0.0f;

    int actionIndex_ = 0; // 技内で次に発生させる弾・着弾点の番号。
    int automaticAttackSerial_ = 0; // 自動選択した技の通算番号。攻撃周期をまたいで進む。
    // 一巡でAIが選ぶ技の並び。候補を使い切ると組み直す。
    std::array<AttackKind, 7> attackCycle_{};
    // 配列の末尾に達したら、次の自動攻撃選択で周期を作り直す。
    std::size_t attackCycleIndex_ = attackCycle_.size();
    // 最後にAIが選んだ技。次の候補を決めるときに連続使用を避けるため参照する。
    AttackKind lastAutomaticAttack_ = AttackKind::None;
    // AIの攻撃候補を並べ替えるための乱数生成器。
    std::mt19937 attackRandom_{std::random_device{}()};
    bool hasBaseScale_ = false; // モデルの基準倍率を一度取得したか。
    bool warningTriggered_ = false; // 現在の技で予告の強調を一度だけ発生させるための印。
    // 現在の技で一度だけ適用するダメージを処理済みか。
    bool attackDamageApplied_ = false;
    // 突進終端の衝撃を発生済みか。
    bool rushImpactSpawned_ = false;
    // プレス着地時の衝撃を発生済みか。着地後の接地状態にも使う。
    bool slamImpactSpawned_ = false;
    // デバッグプレビューから指定された技のID。
    std::string debugPreviewAttackId_;

    // Editorで指定した登場前の状態。Playの開始・停止で安全に復元します。
    // 待機・登場中・戦闘中のどの段階か。通常AIを実行するかの判定にも使う。
    EncounterState encounterState_ = EncounterState::Normal;

    engine::Vector3 encounterBasePosition_{}; // Editorで設定された登場前の位置。Playの退避・復帰に使う。
    engine::Vector3 encounterBaseScale_{1.0f, 1.0f, 1.0f}; // 登場演出で縮める前の配置倍率。
    // 登場時の回転演出を重ねる基準。演出終了時や退避解除時に復元する。
    engine::Vector3 encounterBaseRotation_{};
    // 登場中に色を変える前の色。演出終了やEditor復帰時の復元に使う。
    engine::Vector4 encounterBaseColor_{1.0f, 1.0f, 1.0f, 1.0f};
    // 登場中の発光変化を加える前の基準値。
    float encounterBaseEmissive_ = 1.18f;

    uint32_t encounterCollisionAttribute_ = 0; // 登場待機中に無効化する前の衝突属性。
    uint32_t encounterCollisionMask_ = 0; // 登場待機中に無効化する前の衝突マスク。

    // 登場演出で材質を切り替える前に退避した材質番号。
    int encounterMaterialType_ = 0;

    // 登場を開始してからの経過秒数。演出時間で割って進捗を求める。
    float encounterTimer_ = 0.0f;

    // 現在のPlayで遭遇状態を初期化済みか。Play終了時に解除する。
    bool encounterInitializedForPlay_ = false;
    // Editorの配置状態を退避済みか。演出中の値での再退避を防ぐ。
    bool encounterAuthoredStateCaptured_ = false;
    // 外部から指定された遭遇の起動状態。Play初期化時にも参照する。
    bool encounterRequestedActive_ = false;
};
