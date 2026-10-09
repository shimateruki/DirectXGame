#pragma once

#include "BaseEnemy.h"
#include "game/actor/combat/AttackSequence.h"

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
class EnemyPrismSlime : public BaseEnemy {
  public:
    EnemyPrismSlime();
    ~EnemyPrismSlime() override;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    std::unique_ptr<Object3d> Clone() const override;
    void OnSwitchEvent(bool active) override;
    bool IsPullImmune() const override { return true; }

    /// <summary>敵攻撃プレビューで指定した技だけを繰り返します。</summary>
    /// <param name="attackId">攻撃プロファイルのID。空文字で通常AIに戻します。</param>
    void SetDebugPreviewAttackId(const std::string& attackId);
    const char* GetSequenceStateLabel() const override;
    void SetDebugPreviewChantOnly(bool enabled) { debugPreviewChantOnly_ = enabled; }

    // 召喚した敵の所有権をシーンへ渡すためのコールバックです。
    void SetSpawnCallback(std::function<void(std::unique_ptr<BaseEnemy>)> callback) {
        spawnCallback_ = std::move(callback);
    }

    const char* GetDebugAttackPhaseName() const;
    // -2 は詠唱待機、-1 は生成失敗、0 は生成済みだが表示対象なしを表します。
    int GetDebugChantVisibleLayerCount() const;
    void ApplyManagedScale(const Vector3& scale) override;

    // 中ボス戦HUDから、出現演出と実HPを参照します。
    bool IsEncounterHudActive() const;
    float GetEncounterCurrentHp() const;
    float GetEncounterMaximumHp() const;
    float GetEncounterAppearanceProgress() const;
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
        std::unique_ptr<Object3d> object;
        Vector3 groundPosition{};
        Vector3 fullScale{1.0f, 1.0f, 1.0f};
        float age = 0.0f;
        int index = 0;
    };

    // 結晶槍の予告枠・発射済み状態・飛翔位置をまとめ、表示 Object の寿命を所有する。
    struct CrystalLanceVisual {
        std::unique_ptr<Object3d> object;
        Vector3 position{};
        Vector3 velocity{};
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
        Vector3 groundPosition{};
        Vector4 color{1.0f, 1.0f, 1.0f, 1.0f};
        SummonSlimeKind slimeKind = SummonSlimeKind::Pink;
        float age = 0.0f;
        float moteTimer = 0.0f;
        float spawnedAge = 0.0f;
        bool spawned = false;
    };

    // 風渦の移動状態と複数の輪の表示を所有する。方向と表示部品を一緒に更新する。
    struct WindVortexVisual {
        Vector3 origin{};
        Vector3 position{};
        Vector3 direction{};
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
    void CaptureEncounterAuthoredState(bool refreshVisualTransform = false);
    void InitializeEncounterState();
    void ResetEncounterStateForEditor();
    void BeginEncounterAppearance();
    void ApplyDormantEncounterState();
    void FinishEncounterAppearance();
    bool IsEncounterControlled() const;
    float GetEncounterAppearanceDuration() const;
    bool UpdateInactiveState(float deltaTime);

    // 属性と攻撃状態。velocity は呼び出し元の移動処理へ渡す速度です。
    void EnsureBaseScale();
    void UpdateElementPhase();
    void UpdateBehavior(float deltaTime, const Vector3& direction, float distance, Vector3& velocity);

    /// <summary>攻撃の対象方向・距離を固定し、予備動作を開始します。</summary>
    /// <param name="kind">開始する技の種類。</param>
    /// <param name="direction">攻撃開始時の標的方向。</param>
    /// <param name="distance">攻撃開始時の標的距離。</param>
    void StartAttack(AttackKind kind, const Vector3& direction, float distance);

    // 予備動作、攻撃、硬直の順に更新し、終了後に再配置へ移ります。
    void UpdateAttack(float deltaTime, const Vector3& direction, float distance);
    void UpdateWindup(float deltaTime);
    void UpdateActive(float deltaTime);
    void BeginRecovery();
    void FinishAttack();

    // 次の攻撃を見せるための移動。攻撃中の移動とは分けて管理します。
    void BeginReposition();
    void UpdateReposition(float deltaTime, Vector3& velocity);
    void FinishReposition(Vector3& velocity);

    /// <summary>現在の属性の未使用技から、射程内の技を優先して選びます。</summary>
    /// <param name="targetDistance">標的までの距離。</param>
    /// <returns>今回使用する技。</returns>
    AttackKind ResolveAutomaticAttack(float targetDistance);

    // 一巡後に候補を補充し、プレビュー時は指定技へ切り替えます。
    void PrepareAttackCycle();
    AttackKind ResolveDebugAttack() const;
    const EnemyAttackDefinition& GetCurrentAttackDefinition() const;
    // 炎属性枠の候補をJSONから選び、開始時に複製します。実行中の編集で弾数を変えません。
    const EnemyAttackDefinition& SelectFireFanDefinition(float distance) const;
    AttackSequenceContext MakeSequenceContext();
    const char* GetAttackId(AttackKind kind) const;

    // 晶槍の候補位置と発生後の成長・破砕を管理します。
    void PreparePrismSpikePoints();
    void SpawnNextPrismSpike();
    void UpdatePrismSpikeVisuals(float deltaTime);
    void SpawnPrismSpikeShatter(const PrismSpikeVisual& spike);

    // 飛翔する結晶槍と、その足元の魔法陣を更新します。
    void PrepareCrystalLances();
    void LaunchNextCrystalLance();
    void UpdateCrystalLanceVisuals(float deltaTime);
    void UpdateCrystalCircleVisual(float deltaTime);

    // 攻撃範囲と術者の詠唱を示す魔法陣です。
    void UpdatePrismSpikeSectorVisual(float deltaTime);
    void UpdateWindSpellVisual(float deltaTime);
    void UpdateChantSigilVisual(float deltaTime);
    void SpawnCrystalLanceShatter(const CrystalLanceVisual& lance);
    void ClearPrismAttackVisuals();
    std::unique_ptr<Object3d> CreatePrismSpikeObject(int index) const;

    /// <summary>晶槍が到達した扇形の範囲に攻撃判定を適用します。</summary>
    /// <param name="reachedDistance">術者から今回の攻撃前線までの距離。</param>
    void ApplyPrismSpellDamage(float reachedDistance);

    // 炎・雷・風の攻撃本体と、発生前に確定する着弾位置です。
    void FireNextFanProjectile();
    void StrikeNextThunderPoint();
    void ApplyWindTornado();
    void PrepareThunderPoints();

    // 召喚陣の生成から敵の引き渡しまでを扱います。
    void PrepareSummonPortals();
    void UpdateSummonPortalVisuals(float deltaTime);
    void SpawnNextSummonedSlime();
    void SpawnSummonedSlime(std::unique_ptr<BaseEnemy> enemy);

    // 魔法陣と地面位置を生成・取得する共通処理です。
    std::unique_ptr<EffectObject3d> CreateSummonPortalEffect(bool glow) const;
    std::unique_ptr<EffectObject3d> CreateCrystalCircleEffect(bool glow) const;
    std::unique_ptr<EffectObject3d> CreateSpellEffect(const char* path) const;
    Vector3 GetChantSigilCenter() const;
    Vector3 FindGroundPoint(const Vector3& samplePosition) const;

    // 顔パーツと本体のアニメーションを同期します。
    void InitializeFaceParts();
    std::unique_ptr<Object3d> CreatePart(const std::string& name, const std::string& modelName,
                                         const Vector3& localPosition, const Vector3& localScale, const Vector4& color,
                                         float emissive);
    void UpdateFaceParts(float deltaTime);
    void UpdatePhaseAppearance();
    void ApplySlimeAnimation(float deltaTime);
    void UpdateFacing(const Vector3& direction);

    // JSON プリセットから通常/方向付き粒子を発生させます。
    void EmitPreset(const char* presetName, const Vector3& position);
    void EmitDirectedPreset(const char* presetName, const Vector3& position, const Vector3& direction,
                            float speedScale);
    void SyncCollisionRadius();

    ElementPhase elementPhase_ = ElementPhase::Neutral;
    AttackKind currentAttack_ = AttackKind::None;
    AttackState attackState_ = AttackState::Idle;
    EnemyAttackDefinition selectedFireFan_;
    AttackSequence attackSequence_;
    Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};
    Vector3 lockedDirection_ = {0.0f, 0.0f, 1.0f};
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
    Vector3 repositionDestination_{};
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
    std::array<Vector3, 5> thunderStrikePositions_{};
    Vector3 prismSpellCenter_{};
    Vector3 prismSpellOrigin_{};
    std::array<Vector3, 89> prismSpikePositions_{};
    Vector3 windSpellOrigin_{};
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
    std::array<std::unique_ptr<Object3d>, 2> eyeParts_;
    std::unique_ptr<Object3d> corePart_;
    std::unique_ptr<Object3d> coreFramePart_;
    EncounterState encounterState_ = EncounterState::Normal;
    Vector3 encounterBasePosition_{};
    Vector3 encounterBaseScale_{1.0f, 1.0f, 1.0f};
    Vector4 encounterBaseColor_{1.0f, 1.0f, 1.0f, 1.0f};
    float encounterBaseEmissive_ = 1.08f;
    uint32_t encounterCollisionAttribute_ = 0;
    uint32_t encounterCollisionMask_ = 0;
    int encounterMaterialType_ = 27;
    float encounterTimer_ = 0.0f;
    bool encounterInitializedForPlay_ = false;
    bool encounterAuthoredStateCaptured_ = false;
    bool encounterRequestedActive_ = false;
};
