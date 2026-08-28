#pragma once

#include "BaseEnemy.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class EffectObject3d;

// 三つの頂点と属性コアを持ち、HPに応じて攻撃属性が変わる中ボススライムです。
class EnemyPrismSlime : public BaseEnemy {
public:
    ~EnemyPrismSlime() override;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    std::unique_ptr<Object3d> Clone() const override;
    void OnSwitchEvent(bool active) override;
    bool IsPullImmune() const override { return true; }

    // 敵攻撃プレビューで選択中の技だけを繰り返し確認します。
    void SetDebugPreviewAttackId(const std::string& attackId);
    void SetSpawnCallback(std::function<void(std::unique_ptr<BaseEnemy>)> callback) {
        spawnCallback_ = std::move(callback);
    }
    const char* GetDebugAttackPhaseName() const;
    void ApplyManagedScale(const Vector3& scale) override;

    // 中ボス戦HUDから、出現演出と実HPを安全に参照します。
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

    struct PrismSpikeVisual {
        std::unique_ptr<Object3d> object;
        Vector3 groundPosition{};
        Vector3 fullScale{ 1.0f, 1.0f, 1.0f };
        float age = 0.0f;
    };

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

    struct SummonPortalVisual {
        std::unique_ptr<EffectObject3d> effect;
        Vector3 groundPosition{};
        Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        SummonSlimeKind slimeKind = SummonSlimeKind::Pink;
        float age = 0.0f;
        float spawnedAge = 0.0f;
        bool spawned = false;
    };

    enum class EncounterState {
        Normal,
        Dormant,
        Appearing,
        Active,
    };

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
    void EnsureBaseScale();
    void UpdateElementPhase();
    void UpdateBehavior(float deltaTime, const Vector3& direction, float distance, Vector3& velocity);
    void StartAttack(AttackKind kind, const Vector3& direction, float distance);
    void UpdateAttack(float deltaTime, const Vector3& direction, float distance);
    void UpdateWindup(float deltaTime);
    void UpdateActive(float deltaTime, float targetDistance);
    void BeginRecovery();
    void FinishAttack();
    void BeginReposition();
    void UpdateReposition(float deltaTime, Vector3& velocity);
    void FinishReposition(Vector3& velocity);

    AttackKind ResolveAutomaticAttack() const;
    AttackKind ResolveDebugAttack() const;
    const EnemyAttackDefinition& GetCurrentAttackDefinition() const;
    const char* GetAttackId(AttackKind kind) const;

    void PreparePrismSpikePoints();
    void SpawnNextPrismSpike();
    void UpdatePrismSpikeVisuals(float deltaTime);
    void SpawnPrismSpikeShatter(const PrismSpikeVisual& spike);
    void PrepareCrystalLances();
    void LaunchNextCrystalLance();
    void UpdateCrystalLanceVisuals(float deltaTime);
    void SpawnCrystalLanceShatter(const CrystalLanceVisual& lance);
    void ClearPrismAttackVisuals();
    std::unique_ptr<Object3d> CreatePrismSpikeObject(int index) const;
    void ApplyPrismSpellDamage();
    void FireNextFanProjectile();
    void StrikeNextThunderPoint();
    void ApplyWindWave(float targetDistance);
    void PrepareThunderPoints();
    void PrepareSummonPortals();
    void UpdateSummonPortalVisuals(float deltaTime);
    void SpawnNextSummonedSlime();
    void SpawnSummonedSlime(std::unique_ptr<BaseEnemy> enemy);
    std::unique_ptr<EffectObject3d> CreateSummonPortalEffect() const;
    Vector3 FindGroundPoint(const Vector3& samplePosition) const;

    void InitializeFaceParts();
    std::unique_ptr<Object3d> CreatePart(
        const std::string& name,
        const std::string& modelName,
        const Vector3& localPosition,
        const Vector3& localScale,
        const Vector4& color,
        float emissive);
    void UpdateFaceParts(float deltaTime);
    void UpdatePhaseAppearance();
    void ApplySlimeAnimation(float deltaTime);
    void UpdateFacing(const Vector3& direction);
    void EmitPreset(const char* presetName, const Vector3& position);
    void EmitDirectedPreset(const char* presetName, const Vector3& position, const Vector3& direction, float speedScale);
    void SyncCollisionRadius();

    ElementPhase elementPhase_ = ElementPhase::Neutral;
    AttackKind currentAttack_ = AttackKind::None;
    AttackState attackState_ = AttackState::Idle;
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 lockedDirection_ = { 0.0f, 0.0f, 1.0f };
    float lockedTargetDistance_ = 0.0f;
    float idleTimer_ = 0.0f;
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
    std::array<Vector3, 5> thunderStrikePositions_{};
    Vector3 prismSpellCenter_{};
    std::array<Vector3, 7> prismSpikePositions_{};
    std::vector<PrismSpikeVisual> prismSpikeVisuals_;
    std::vector<CrystalLanceVisual> crystalLanceVisuals_;
    std::vector<SummonPortalVisual> summonPortalVisuals_;
    std::function<void(std::unique_ptr<BaseEnemy>)> spawnCallback_;
    std::uint32_t summonRandomSerial_ = 0;
    int automaticAttackSerial_ = 0;
    int summonWavesUsed_ = 0;
    std::array<std::unique_ptr<Object3d>, 2> eyeParts_;
    std::unique_ptr<Object3d> corePart_;
    std::unique_ptr<Object3d> coreFramePart_;
    EncounterState encounterState_ = EncounterState::Normal;
    Vector3 encounterBasePosition_{};
    Vector3 encounterBaseScale_{ 1.0f, 1.0f, 1.0f };
    Vector4 encounterBaseColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
    float encounterBaseEmissive_ = 1.08f;
    uint32_t encounterCollisionAttribute_ = 0;
    uint32_t encounterCollisionMask_ = 0;
    int encounterMaterialType_ = 27;
    float encounterTimer_ = 0.0f;
    bool encounterInitializedForPlay_ = false;
    bool encounterAuthoredStateCaptured_ = false;
    bool encounterRequestedActive_ = false;
};
