#pragma once

#include "BaseEnemy.h"

#include <array>
#include <memory>
#include <random>
#include <string>
#include <vector>

class EffectObject3d;
class BossChantVisual;

// ステージ3の最終ボスです。3形態で王冠エネルギーを使った大規模攻撃を行います。
class EnemyFalseKingSlime final : public BaseEnemy {
public:
    EnemyFalseKingSlime();
    ~EnemyFalseKingSlime() override;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    void DrawForCamera(
        Camera* camera,
        ID3D12Resource* pointLightResource,
        ID3D12Resource* spotLightResource,
        int previewBufferIndex = 0) override;
    void OnSwitchEvent(bool active) override;
    std::unique_ptr<Object3d> Clone() const override;
    void ApplyManagedScale(const Vector3& scale) override;
    bool IsPullImmune() const override { return true; }

    bool HasOwnedSpecialMaterialVisuals() const override;
    void DrawOwnedSpecialMaterialVisuals(uint32_t depthSrvHandle, uint32_t grabSrvHandle) override;
    void DrawOwnedSpecialMaterialVisualsForCamera(
        Camera* camera,
        uint32_t depthSrvHandle,
        uint32_t grabSrvHandle,
        int previewBufferIndex = 0) override;

    // 敵攻撃プレビューで指定した攻撃だけを繰り返します。
    void SetDebugPreviewAttackId(const std::string& attackId);
    const char* GetDebugAttackPhaseName() const;
    const char* GetDebugAttackBodyName() const;
    size_t GetDebugVisibleAttackVisualCount() const;

    bool IsEncounterHudActive() const;
    float GetEncounterCurrentHp() const;
    float GetEncounterMaximumHp() const;
    float GetEncounterAppearanceProgress() const;
    int GetBattlePhase() const { return battlePhase_; }
    float GetPhaseTransitionProgress() const;
    void TriggerDebugDefeat();
    bool CanTriggerDebugPhaseTwoTransition() const;
    bool CanTriggerDebugCrownInterlude() const;
    bool CanTriggerDebugFinalDefeat() const;
    void TriggerDebugPhaseTwoTransition();
    void TriggerDebugCrownInterlude();
    // 登場カットが終わるまでは攻撃せず、身体の登場動作を最後まで見せます。
    void SetIntroPresentationActive(bool active) { introPresentationActive_ = active; }
    // 通常ダメージで形態を飛ばさないため、HPを書き換える前に呼びます。
    float FilterFinaleDamage(float damage);
    void OpenCrownArmor();
    void BeginFinalArena(const Vector3& position);
    void AbortCrownFinale();
    bool IsCrownInterlude() const { return crownInterlude_; }
    int GetBrokenSealCount() const { return brokenSeals_; }
    void DrawFinaleEditor();

private:
    enum class EncounterState {
        Normal,
        Dormant,
        Appearing,
        Active,
    };

    enum class AttackKind {
        None,
        CrownLanceRain,
        RoyalShockwave,
        KingRush,
        RoyalCross,
        CrownDominion,
        UmbraFlame,
        AstralReaver,
        NightfallLances,
    };

    enum class AttackState {
        Idle,
        Windup,
        Active,
        Recovery,
        PhaseShift,
    };

    struct CrownLance {
        std::unique_ptr<Object3d> visual;
        std::unique_ptr<Object3d> aura;
        std::unique_ptr<Object3d> portal;
        Vector3 groundPosition{};
        Vector3 portalPosition{};
        Vector3 flightDirection{ 0.0f, -1.0f, 0.0f };
        float age = 0.0f;
        float delay = 0.0f;
        float portalOpenDuration = 0.24f;
        float readyDuration = 0.22f;
        float flightDuration = 0.54f;
        float portalCloseDuration = 0.24f;
        float impactHoldDuration = 0.10f;
        float dissolveDuration = 0.42f;
        float particleTimer = 0.0f;
        bool impactPlayed = false;
        bool damageApplied = false;
    };

    struct RoyalWave {
        std::unique_ptr<Object3d> visual;
        std::unique_ptr<Object3d> glow;
        Vector3 center{};
        float age = 0.0f;
        float delay = 0.0f;
        float lifetime = 1.05f;
        float startRadius = 1.0f;
        float endRadius = 28.0f;
        float previousRadius = 1.0f;
        float particleTimer = 0.0f;
        bool alternateColor = false;
        bool hitTarget = false;
    };

    struct RoyalBeam {
        std::unique_ptr<Object3d> visual;
        std::unique_ptr<Object3d> glow;
        Vector3 origin{};
        float age = 0.0f;
        float delay = 0.0f;
        float lifetime = 3.0f;
        float angle = 0.0f;
        float angularSpeed = 0.0f;
        float length = 31.0f;
        float width = 1.5f;
        float damageTimer = 0.0f;
    };

    struct UmbraFlamePortal {
        std::unique_ptr<EffectObject3d> sigil;
        std::unique_ptr<Object3d> flame;
        Vector3 groundPosition{};
        float delay = 0.0f;
        float age = 0.0f;
        float activeAge = 0.0f;
        float particleTimer = 0.0f;
        float damageTimer = 0.0f;
    };

    struct AstralReaverOrb {
        std::unique_ptr<EffectObject3d> halo;
        std::unique_ptr<EffectObject3d> blade;
        std::unique_ptr<EffectObject3d> arc;
        std::unique_ptr<EffectObject3d> core;
        Vector3 origin{};
        Vector3 destination{};
        float launchDelay = 0.0f;
        float flightDuration = 0.68f;
        float trailTimer = 0.0f;
    };

    struct NightfallLancesVisual {
        // 一本の大槍が着地した後、エネルギーを収束させて範囲攻撃を放ちます。
        std::unique_ptr<EffectObject3d> targetSigil;
        std::unique_ptr<EffectObject3d> targetRing;
        std::unique_ptr<EffectObject3d> innerRing;
        std::unique_ptr<EffectObject3d> spearCore;
        std::unique_ptr<EffectObject3d> spearGlow;
        std::array<std::unique_ptr<EffectObject3d>, 3> barbs;
        std::unique_ptr<EffectObject3d> impactRing;
        std::unique_ptr<EffectObject3d> impactCore;
        std::unique_ptr<EffectObject3d> blastRing;
        std::unique_ptr<EffectObject3d> blastShell;
        float activeAge = 0.0f;
        float particleTimer = 0.0f;
        float dissolveParticleTimer = 0.0f;
        bool damageApplied = false;
    };

    // 第2形態の主槍を囲む小槍。着弾時刻をずらして回避経路を作ります。
    struct SealNightfallSatellite {
        std::unique_ptr<EffectObject3d> sigil;
        std::unique_ptr<EffectObject3d> core;
        std::unique_ptr<EffectObject3d> glow;
        Vector3 ground{};
        float impactAge = 0.0f;
        bool damageApplied = false;
    };

    void EnsureBaseScale();
    void UpdateBehavior(float deltaTime, const Vector3& targetDirection, float targetDistance, Vector3& velocity);
    void StartAttack(AttackKind kind, const Vector3& targetDirection);
    void UpdateAttack(float deltaTime, const Vector3& targetDirection, float targetDistance, Vector3& velocity);
    void UpdateWindup(float deltaTime, const Vector3& targetDirection);
    void BeginActive();
    void UpdateActive(float deltaTime, const Vector3& targetDirection, Vector3& velocity);
    void BeginRecovery();
    void FinishAttack();
    // 現在の形態の未使用技から、射程内の技を優先して選びます。
    AttackKind ResolveAutomaticAttack(float targetDistance);
    void PrepareAttackCycle();
    AttackKind ResolveDebugAttack() const;
    const char* GetAttackId(AttackKind kind) const;
    const EnemyAttackDefinition& GetCurrentAttackDefinition() const;

    int ResolveDesiredBattlePhase() const;
    void BeginPhaseShift(int nextPhase);
    void UpdatePhaseShift(float deltaTime, Vector3& velocity);

    void PrepareLanceTargets(bool dominionPattern);
    void SpawnNextCrownLance(bool dominionPattern = false);
    void SpawnCrownLance(const Vector3& groundPosition, float delay, float readyDuration = 0.22f);
    void UpdateCrownLances(float deltaTime);
    void SpawnRoyalWaves();
    void UpdateRoyalWaves(float deltaTime);
    void BeginShockwaveJump();
    void UpdateShockwaveJump(float deltaTime, Vector3& velocity);
    void FinishShockwaveLanding();
    void BeginRush();
    void BeginRushStep(int step);
    void UpdateRush(float deltaTime, const Vector3& targetDirection, Vector3& velocity);
    void SpawnRoyalBeams(bool dominionPattern);
    void UpdateRoyalBeams(float deltaTime);
    void UpdateDominion(float deltaTime);
    void PrepareUmbraFlames();
    void UpdateUmbraFlames(float deltaTime, bool active);
    void UpdateUmbraFlameNetwork(float deltaTime, bool active);
    void UpdateSpellChant(float deltaTime);
    void PrepareAstralReaver();
    void UpdateAstralReaver(float deltaTime, bool active);
    void PrepareNightfallLances();
    void UpdateNightfallLances(float deltaTime, bool active);
    void PrepareSealNightfallSatellites();
    void UpdateSealNightfallSatellites(float deltaTime, bool active);

    std::unique_ptr<Object3d> CreateRoyalVisual(
        const std::string& name,
        const std::string& model,
        int materialType,
        const Vector4& color,
        float emissive) const;
    std::unique_ptr<Object3d> CreateRoyalMesh(
        const std::string& name,
        const std::string& model,
        const Vector4& color,
        float emissive) const;
    Vector3 FindGroundPoint(const Vector3& samplePosition) const;
    void DamageTargetAt(
        const Vector3& center,
        float radius,
        float verticalTolerance,
        float damage,
        DamageType damageType,
        const Vector3& knockback);
    void DamageTargetOnBeam(RoyalBeam& beam, float deltaTime);
    void UpdateFacing(const Vector3& direction, float follow = 0.12f);
    void ApplyBossAnimation(float deltaTime);
    void UpdateRoyalGlow(float deltaTime);
    void EmitPreset(const char* presetName, const Vector3& position) const;
    void EmitDirectedPreset(
        const char* presetName,
        const Vector3& position,
        const Vector3& direction,
        float speedScale) const;
    void ClearTransientVisuals();

    bool UpdateEncounterState(float deltaTime);
    void CaptureAuthoredState(bool force = false);
    void InitializeEncounterState();
    void ResetForEditor();
    void BeginAppearance();
    void ApplyDormantState();
    void FinishAppearance();
    void SyncCollisionBounds();
    bool IsEncounterControlled() const;
    float GetAppearanceDuration() const;

    AttackKind currentAttack_ = AttackKind::None;
    AttackState attackState_ = AttackState::Idle;
    Vector3 baseScale_{ 1.0f, 1.0f, 1.0f };
    Vector3 lockedDirection_{ 0.0f, 0.0f, 1.0f };
    Vector3 lockedTargetPosition_{};
    Vector3 attackStartPosition_{};
    Vector3 attackEndPosition_{};
    std::array<Vector3, 18> lanceTargets_{};
    std::vector<CrownLance> crownLances_;
    std::vector<RoyalWave> royalWaves_;
    std::vector<RoyalBeam> royalBeams_;
    std::vector<UmbraFlamePortal> umbraFlames_;
    std::unique_ptr<EffectObject3d> umbraFlameNetwork_;
    std::unique_ptr<BossChantVisual> spellChant_;
    bool umbraFlameFanHit_ = false;
    std::array<AstralReaverOrb, 3> astralReaverOrbs_;
    std::array<std::unique_ptr<EffectObject3d>, 3> astralReaverImpactRings_;
    std::unique_ptr<EffectObject3d> astralReaverTargetRing_;
    std::unique_ptr<EffectObject3d> astralReaverImpactCore_;
    float astralReaverElapsed_ = 0.0f;
    float astralReaverImpactAge_ = -1.0f;
    bool astralReaverDamageApplied_ = false;
    NightfallLancesVisual nightfallLances_;
    std::array<SealNightfallSatellite, 3> sealNightfallSatellites_;
    std::unique_ptr<Object3d> shockwaveChargeVisual_;
    std::unique_ptr<Object3d> rushWingsVisual_;
    std::unique_ptr<Object3d> dominionSigilVisual_;

    float attackTimer_ = 0.0f;
    float attackStateDuration_ = 0.0f;
    float attackCooldown_ = 1.2f;
    float actionTimer_ = 0.0f;
    float effectTimer_ = 0.0f;
    float phaseTransitionTimer_ = 0.0f;
    float phaseTransitionDuration_ = 1.55f;
    float rushStepTimer_ = 0.0f;
    float rushStepDuration_ = 0.82f;
    float shockwaveJumpTimer_ = 0.0f;
    float shockwaveJumpDuration_ = 0.92f;
    float shockwaveJumpHeight_ = 6.4f;
    float dominionLanceTimer_ = 0.0f;
    float ambientSparkTimer_ = 0.0f;
    int actionIndex_ = 0;
    int lanceSpawnSerial_ = 0;
    int automaticAttackSerial_ = 0;
    // 一巡するまで技を再選択しないシャッフル袋です。
    std::array<AttackKind, 8> attackCycle_{};
    std::size_t attackCycleCount_ = 0;
    std::size_t attackCycleIndex_ = 0;
    int attackCyclePhase_ = 0;
    AttackKind lastAutomaticAttack_ = AttackKind::None;
    std::mt19937 attackRandom_{ std::random_device{}() };
    int battlePhase_ = 1;
    int pendingBattlePhase_ = 1;
    int rushStep_ = 0;
    bool rushDamageApplied_ = false;
    bool shockwaveLanded_ = false;
    bool warningTriggered_ = false;
    bool hasBaseScale_ = false;
    std::string debugPreviewAttackId_;

    EncounterState encounterState_ = EncounterState::Normal;
    Vector3 authoredPosition_{};
    Vector3 authoredScale_{ 1.0f, 1.0f, 1.0f };
    Vector3 authoredRotation_{};
    Vector4 authoredColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
    uint32_t authoredCollisionAttribute_ = 0;
    uint32_t authoredCollisionMask_ = 0;
    int authoredMaterialType_ = 0;
    float authoredEmissive_ = 1.0f;
    float encounterTimer_ = 0.0f;
    float idleTimer_ = 0.0f;
    bool initializedForPlay_ = false;
    bool authoredStateCaptured_ = false;
    bool activationRequested_ = false;
    struct CrownSeal {
        std::unique_ptr<Object3d> crystal;
        std::unique_ptr<Object3d> ring;
        std::unique_ptr<Object3d> link;
        std::unique_ptr<Object3d> gem;
        Vector3 position{};
        bool broken = false;
        float breakAge = 0.0f;
    };
    std::array<CrownSeal, 3> crownSeals_;
    int brokenSeals_ = 0;
    float sealAttackTimer_ = 0.0f;
    float sealPressureDamage_ = 0.0f;
    int sealAttackSerial_ = 0;
    enum class SealState { Reveal, Aim, Locked, Attack, Recover, Broken };
    SealState sealState_ = SealState::Reveal;
    float sealStateTimer_ = 0.0f;
    float sealVfxTimer_ = 0.0f;
    int sealAttackStage_ = 0;
    bool sealImpactPlayed_ = false;
    float armorOpenTimer_ = 0.0f;
    bool crownInterlude_ = false;
    bool finalArenaStarted_ = false;
    bool sealTargetsLocked_ = false;
    bool pendingDebugSeals_ = false;
    bool introPresentationActive_ = false;
    bool pendingDebugInterlude_ = false;
    Vector3 sealLockedTarget_{};
    std::unique_ptr<EffectObject3d> crownShieldRing_;
    std::unique_ptr<EffectObject3d> crownShieldShell_;
    void InitializeCrownSeals();
    void UpdateCrownSeals(float deltaTime);
    void TryBreakCrownSeal(const Vector3& impact, int attackStage = 0);
    void ResetCrownFinale();
};
