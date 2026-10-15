#pragma once

#include "BaseEnemy.h"
#include "game/actor/combat/AttackSequence.h"
#include "game/actor/enemy/combat/ISequencePreviewEnemy.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

class EffectObject3d;
class BossChantVisual;

// 三つの頂点と属性コアを持ち、HPに応じて攻撃属性が変わる中ボススライムです。
class EnemyPrismSlime : public BaseEnemy, public ISequencePreviewEnemy {
  public:
    /// 本体の構築。攻撃状態の実際の初期化は Initialize で行う。
    EnemyPrismSlime();
    /// 所有リソースは各メンバの破棄に委ねる。
    ~EnemyPrismSlime() override;

    /// モデル、三つの頂点、攻撃定義と遭遇状態を初期化する。
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    /// 遭遇状態、属性段階、各攻撃と召喚を進める。
    void Update(float deltaTime) override;
    /// 本体と所有する結晶・詠唱演出を描く。
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    /// 召喚した敵を追加するシーンを登録する。
    void BindSceneSpawnContext(BaseScene* scene) override;
    /// 配置用の中ボス複製を作る。
    std::unique_ptr<engine::Object3d> Clone() const override;
    /// スイッチの有効化に合わせて登場状態を切り替える。
    void OnSwitchEvent(bool active) override;
    bool IsPullImmune() const override { return true; }

    /// <summary>敵攻撃プレビューで指定した技だけを繰り返します。</summary>
    /// <param name="attackId">攻撃プロファイルのID。空文字で通常AIに戻します。</param>
    void SetDebugPreviewAttackId(const std::string& attackId);
    void SetSequencePreviewAttackId(const std::string& attackId) override { SetDebugPreviewAttackId(attackId); }
    /// 攻撃シーケンスが有効な場合、その段階名を返す。
    const char* GetSequenceStateLabel() const override;
    /// 詠唱だけを確認するプレビューへ切り替える。
    void SetDebugPreviewChantOnly(bool enabled) { debugPreviewChantOnly_ = enabled; }

    // 召喚した敵の所有権をシーンへ渡すためのコールバックです。
    void SetSpawnCallback(std::function<void(std::unique_ptr<BaseEnemy>)> callback) {
        spawnCallback_ = std::move(callback);
    }

    /// デバッグ表示向けに現在の攻撃段階名を返す。
    const char* GetDebugAttackPhaseName() const;
    // -2 は詠唱待機、-1 は生成失敗、0 は生成済みだが表示対象なしを表します。
    int GetDebugChantVisibleLayerCount() const;
    /// 配置スケールを結晶と攻撃判定の基準にする。
    void ApplyManagedScale(const engine::Vector3& scale) override;

    // 中ボス戦HUDから、出現演出と実HPを参照します。
    /// 遭遇 HUD を表示すべき状態か返す。
    bool IsEncounterHudActive() const;
    /// HUD に表示する現在 HP を返す。
    float GetEncounterCurrentHp() const;
    /// HUD の分母となる最大 HP を返す。
    float GetEncounterMaximumHp() const;
    /// 登場演出の進行率を HUD 向けに返す。
    float GetEncounterAppearanceProgress() const;
    /// 開発用に中ボスの撃破処理を発火する。
    void TriggerDebugDefeat();

  private:
    enum class ElementPhase {
        Neutral,
        Fire,
        Thunder,
        Wind,
    };

    enum class AttackKind {
        None,
        PrismSpikes,
        CrystalLanceVolley,
        FireFan,
        ThunderChain,
        WindWave,
        SlimeSummon,
    };

    enum class AttackState {
        Idle,
        Windup,
        Active,
        Recovery,
        Reposition,
    };

    // 地面から伸びる結晶一本の表示。完成時のスケールを基準に経過時間で形を変える。
    struct PrismSpikeVisual {
        std::unique_ptr<engine::Object3d> object;
        engine::Vector3 groundPosition{};
        engine::Vector3 fullScale{1.0f, 1.0f, 1.0f};
        float age = 0.0f;
        int index = 0;
    };

    // 結晶槍の予告枠・発射済み状態・飛翔位置をまとめ、表示 Object の寿命を所有する。
    struct CrystalLanceVisual {
        std::unique_ptr<engine::Object3d> object;
        engine::Vector3 position{};
        engine::Vector3 velocity{};
        float age = 0.0f;
        float trailTimer = 0.0f;
        int slotIndex = 0;
        bool launched = false;
    };

    enum class SummonSlimeKind {
        Fire,
        Bomber,
        Pink,
        Thunder,
    };

    // 召喚口の表示と出現させる敵種。spawned により同じ口からの重複生成を防ぐ。
    struct SummonPortalVisual {
        std::unique_ptr<EffectObject3d> effect;
        std::unique_ptr<EffectObject3d> glow;
        engine::Vector3 groundPosition{};
        engine::Vector4 color{1.0f, 1.0f, 1.0f, 1.0f};
        SummonSlimeKind slimeKind = SummonSlimeKind::Pink;
        float age = 0.0f;
        float moteTimer = 0.0f;
        float spawnedAge = 0.0f;
        bool spawned = false;
    };

    // 風渦の移動状態と複数の輪の表示を所有する。方向と表示部品を一緒に更新する。
    struct WindVortexVisual {
        engine::Vector3 origin{};
        engine::Vector3 position{};
        engine::Vector3 direction{};
        float driftTimer = 0.0f;
        std::unique_ptr<EffectObject3d> circle;
        std::unique_ptr<EffectObject3d> circleGlow;
        std::array<std::unique_ptr<EffectObject3d>, 4> outer;
        std::array<std::unique_ptr<EffectObject3d>, 1> inner;
    };

    enum class EncounterState {
        Normal,
        Dormant,
        Appearing,
        Active,
    };

    // 遭遇演出。編集時に配置された座標とスケールは退避し、再生終了時に復元します。
    // 遭遇演出中は通常AIを止め、登場完了までの状態遷移を進めます。
    bool UpdateEncounterState(float deltaTime);
    /// Editor 配置時の位置と大きさを登場演出用に退避する。
    void CaptureEncounterAuthoredState(bool refreshVisualTransform = false);
    /// Play 開始時のスイッチ状態から遭遇段階を設定する。
    void InitializeEncounterState();
    /// Editor に戻るとき配置と見た目を復元する。
    void ResetEncounterStateForEditor();
    /// 休眠状態から登場演出を開始する。
    void BeginEncounterAppearance();
    /// ボスを休眠させ、攻撃表示を隠す。
    void ApplyDormantEncounterState();
    /// 登場演出を終えて戦闘状態へ移る。
    void FinishEncounterAppearance();
    /// スイッチで起動する遭遇制御の対象か返す。
    bool IsEncounterControlled() const;
    /// 現在の登場演出の継続時間を返す。
    float GetEncounterAppearanceDuration() const;
    /// 行動不能中の状態を処理し、通常 AI を止める場合は true を返す。
    bool UpdateInactiveState(float deltaTime);

    // 属性と攻撃状態。velocity は呼び出し元の移動処理へ渡す速度です。
    /// 未設定なら配置スケールを演出と判定の基準として記録する。
    void EnsureBaseScale();
    /// HP に応じて炎・雷・風の属性段階を切り替える。
    void UpdateElementPhase();
    /// 対象距離と攻撃段階から移動速度を決める。
    void UpdateBehavior(float deltaTime, const engine::Vector3& direction, float distance, engine::Vector3& velocity);

    /// <summary>攻撃の対象方向・距離を固定し、予備動作を開始します。</summary>
    /// <param name="kind">開始する技の種類。</param>
    /// <param name="direction">攻撃開始時の標的方向。</param>
    /// <param name="distance">攻撃開始時の標的距離。</param>
    void StartAttack(AttackKind kind, const engine::Vector3& direction, float distance);

    // 予備動作、攻撃、硬直の順に更新し、終了後に再配置へ移ります。
    void UpdateAttack(float deltaTime, const engine::Vector3& direction, float distance);
    /// 予備動作中の予告と詠唱を進める。
    void UpdateWindup(float deltaTime);
    /// 攻撃本体を更新し、終了時に硬直へ移す。
    void UpdateActive(float deltaTime);
    /// 攻撃後の硬直へ切り替える。
    void BeginRecovery();
    /// 現在の技を終了し、次の再配置へ進める。
    void FinishAttack();

    // 次の攻撃を見せるための移動。攻撃中の移動とは分けて管理します。
    /// 次の技を見せるための移動を開始する。
    void BeginReposition();
    /// 再配置中の移動速度を更新する。
    void UpdateReposition(float deltaTime, engine::Vector3& velocity);
    /// 再配置を終えて移動速度を戻す。
    void FinishReposition(engine::Vector3& velocity);

    /// <summary>現在の属性の未使用技から、射程内の技を優先して選びます。</summary>
    /// <param name="targetDistance">標的までの距離。</param>
    /// <returns>今回使用する技。</returns>
    AttackKind ResolveAutomaticAttack(float targetDistance);

    // 一巡後に候補を補充し、プレビュー時は指定技へ切り替えます。
    /// 一巡する攻撃候補を補充する。
    void PrepareAttackCycle();
    /// デバッグ指定中の攻撃種別を解決する。
    AttackKind ResolveDebugAttack() const;
    /// 現在の技に対応する攻撃定義を返す。
    const EnemyAttackDefinition& GetCurrentAttackDefinition() const;
    // 炎属性枠の候補をJSONから選び、開始時に複製します。実行中の編集で弾数を変えません。
    const EnemyAttackDefinition& SelectFireFanDefinition(float distance) const;
    /// 攻撃シーケンスへ渡す対象と術者の状態を構築する。
    AttackSequenceContext MakeSequenceContext();
    /// 内部の攻撃種別を JSON プロファイルの ID へ変換する。
    const char* GetAttackId(AttackKind kind) const;

    // 晶槍の候補位置と発生後の成長・破砕を管理します。
    void PreparePrismSpikePoints();
    /// 候補位置に次の結晶棘を発生させる。
    void SpawnNextPrismSpike();
    /// 結晶棘の成長と破砕を更新する。
    void UpdatePrismSpikeVisuals(float deltaTime);
    /// 破砕した棘に合わせて粒子演出を発生させる。
    void SpawnPrismSpikeShatter(const PrismSpikeVisual& spike);

    // 飛翔する結晶槍と、その足元の魔法陣を更新します。
    void PrepareCrystalLances();
    /// 予告した次の結晶槍を発射する。
    void LaunchNextCrystalLance();
    /// 飛翔中の結晶槍を移動させて着弾を処理する。
    void UpdateCrystalLanceVisuals(float deltaTime);
    /// 結晶槍を囲む魔法陣を攻撃段階に同期する。
    void UpdateCrystalCircleVisual(float deltaTime);

    // 攻撃範囲と術者の詠唱を示す魔法陣です。
    void UpdatePrismSpikeSectorVisual(float deltaTime);
    void UpdateWindSpellVisual(float deltaTime);
    void UpdateChantSigilVisual(float deltaTime);
    /// 結晶槍の着弾地点に破砕演出を出す。
    void SpawnCrystalLanceShatter(const CrystalLanceVisual& lance);
    /// 中断した攻撃の一時表示をまとめて取り除く。
    void ClearPrismAttackVisuals();
    /// 指定番号の結晶棘の表示オブジェクトを作る。
    std::unique_ptr<engine::Object3d> CreatePrismSpikeObject(int index) const;

    /// <summary>晶槍が到達した扇形の範囲に攻撃判定を適用します。</summary>
    /// <param name="reachedDistance">術者から今回の攻撃前線までの距離。</param>
    void ApplyPrismSpellDamage(float reachedDistance);

    // 炎・雷・風の攻撃本体と、発生前に確定する着弾位置です。
    /// 扇状攻撃の次の炎弾を発射する。
    void FireNextFanProjectile();
    /// 確定した次の落雷位置へ攻撃を発生させる。
    void StrikeNextThunderPoint();
    /// 風の竜巻による押し出し判定を適用する。
    void ApplyWindTornado();
    /// 落雷候補位置を攻撃開始前に確定する。
    void PrepareThunderPoints();

    // 召喚陣の生成から敵の引き渡しまでを扱います。
    void PrepareSummonPortals();
    /// 召喚口の出現から消滅までの見た目を更新する。
    void UpdateSummonPortalVisuals(float deltaTime);
    /// 次の召喚口から敵を生成する。
    void SpawnNextSummonedSlime();
    /// 生成した敵の所有権を登録先へ渡す。
    void SpawnSummonedSlime(std::unique_ptr<BaseEnemy> enemy);

    // 魔法陣と地面位置を生成・取得する共通処理です。
    std::unique_ptr<EffectObject3d> CreateSummonPortalEffect(bool glow) const;
    std::unique_ptr<EffectObject3d> CreateCrystalCircleEffect(bool glow) const;
    std::unique_ptr<EffectObject3d> CreateSpellEffect(const char* path) const;
    engine::Vector3 GetChantSigilCenter() const;
    engine::Vector3 FindGroundPoint(const engine::Vector3& samplePosition) const;

    // 顔パーツと本体のアニメーションを同期します。
    /// 属性を示す顔パーツの表示を準備する。
    void InitializeFaceParts();
    std::unique_ptr<engine::Object3d> CreatePart(const std::string& name, const std::string& modelName,
                                         const engine::Vector3& localPosition, const engine::Vector3& localScale, const engine::Vector4& color,
                                         float emissive);
    /// 本体の姿勢へ顔パーツを追従させる。
    void UpdateFaceParts(float deltaTime);
    /// 現在属性の色とパーツ表示を反映する。
    void UpdatePhaseAppearance();
    /// 攻撃段階に応じた本体の伸縮を反映する。
    void ApplySlimeAnimation(float deltaTime);
    /// 指定方向へ本体を向ける。
    void UpdateFacing(const engine::Vector3& direction);

    // JSON プリセットから通常/方向付き粒子を発生させます。
    void EmitPreset(const char* presetName, const engine::Vector3& position);
    void EmitDirectedPreset(const char* presetName, const engine::Vector3& position, const engine::Vector3& direction,
                            float speedScale);
    void SyncCollisionRadius();

    ElementPhase elementPhase_ = ElementPhase::Neutral;
    AttackKind currentAttack_ = AttackKind::None;
    AttackState attackState_ = AttackState::Idle;
    EnemyAttackDefinition selectedFireFan_;
    AttackSequence attackSequence_;
    engine::Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 lockedDirection_ = {0.0f, 0.0f, 1.0f};
    float lockedTargetDistance_ = 0.0f;
    float idleTimer_ = 0.0f;
    float locomotionHopTimer_ = 0.0f;
    float locomotionLandingTimer_ = 0.0f;
    bool locomotionAirborne_ = false;
    float attackTimer_ = 0.0f;
    float attackStateDuration_ = 0.0f;
    float attackCooldown_ = 1.0f;
    float effectTimer_ = 0.0f;
    float actionTimer_ = 0.0f;
    float impactPulseTimer_ = 0.0f;
    engine::Vector3 repositionDestination_{};
    float repositionTimer_ = 0.0f;
    float repositionDuration_ = 0.0f;
    std::uint32_t repositionRandomState_ = 0x51A7C39Du;
    bool repositionJumpPending_ = false;
    int actionIndex_ = 0;
    bool hasBaseScale_ = false;
    bool warningTriggered_ = false;
    bool damageApplied_ = false;
    bool crystalVolleyDamageApplied_ = false;
    std::string debugPreviewAttackId_;
    bool debugPreviewChantOnly_ = false;
    std::array<engine::Vector3, 5> thunderStrikePositions_{};
    engine::Vector3 prismSpellCenter_{};
    engine::Vector3 prismSpellOrigin_{};
    std::array<engine::Vector3, 89> prismSpikePositions_{};
    engine::Vector3 windSpellOrigin_{};
    float windTornadoAge_ = 0.0f;
    std::uint32_t windRandomState_ = 0xC671AE29u;
    std::vector<PrismSpikeVisual> prismSpikeVisuals_;
    std::vector<CrystalLanceVisual> crystalLanceVisuals_;
    std::unique_ptr<EffectObject3d> crystalCircle_;
    std::unique_ptr<EffectObject3d> crystalCircleGlow_;
    std::unique_ptr<EffectObject3d> prismSpikeSector_;
    std::array<std::unique_ptr<EffectObject3d>, 5> prismSpikeSigil_{};
    std::array<WindVortexVisual, 3> windVortices_{};
    std::unique_ptr<BossChantVisual> chantVisual_;
    std::vector<SummonPortalVisual> summonPortalVisuals_;
    std::function<void(std::unique_ptr<BaseEnemy>)> spawnCallback_;
    std::uint32_t summonRandomSerial_ = 0;
    int automaticAttackSerial_ = 0;
    int summonWavesUsed_ = 0;
    std::array<AttackKind, 3> attackCycle_{};
    std::size_t attackCycleCount_ = 0;
    std::size_t attackCycleIndex_ = 0;
    ElementPhase attackCyclePhase_ = ElementPhase::Neutral;
    AttackKind lastAutomaticAttack_ = AttackKind::None;
    std::mt19937 attackRandom_{std::random_device{}()};
    std::array<std::unique_ptr<engine::Object3d>, 2> eyeParts_;
    std::unique_ptr<engine::Object3d> corePart_;
    std::unique_ptr<engine::Object3d> coreFramePart_;
    EncounterState encounterState_ = EncounterState::Normal;
    engine::Vector3 encounterBasePosition_{};
    engine::Vector3 encounterBaseScale_{1.0f, 1.0f, 1.0f};
    engine::Vector4 encounterBaseColor_{1.0f, 1.0f, 1.0f, 1.0f};
    float encounterBaseEmissive_ = 1.08f;
    uint32_t encounterCollisionAttribute_ = 0;
    uint32_t encounterCollisionMask_ = 0;
    int encounterMaterialType_ = 27;
    float encounterTimer_ = 0.0f;
    bool encounterInitializedForPlay_ = false;
    bool encounterAuthoredStateCaptured_ = false;
    bool encounterRequestedActive_ = false;
};
