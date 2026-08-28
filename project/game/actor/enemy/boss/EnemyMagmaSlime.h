#pragma once

#include "BaseEnemy.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// マグマ弾幕、灼熱突進、火山プレス、追尾間欠泉、螺旋溶岩波を使い分けるステージ2中ボスです。
class EnemyMagmaSlime final : public BaseEnemy {
public:
    ~EnemyMagmaSlime() override;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    void OnSwitchEvent(bool active) override;
    std::unique_ptr<Object3d> Clone() const override;
    void ApplyManagedScale(const Vector3& scale) override;
    bool IsPullImmune() const override { return true; }

    bool HasOwnedSpecialMaterialVisuals() const override;
    void DrawOwnedSpecialMaterialVisuals(uint32_t depthSrvHandle, uint32_t grabSrvHandle) override;

    // 敵攻撃プレビューで指定した攻撃だけを繰り返します。
    void SetDebugPreviewAttackId(const std::string& attackId);
    const char* GetDebugAttackPhaseName() const;

    // 中ボスHUDから、遭遇状態と実HPを参照します。
    bool IsEncounterHudActive() const;
    float GetEncounterCurrentHp() const;
    float GetEncounterMaximumHp() const;
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
    };

    enum class AttackState {
        Idle,
        Windup,
        Active,
        Recovery,
    };

    struct MagmaBlob {
        std::unique_ptr<Object3d> visual;
        Vector3 start{};
        Vector3 control{};
        Vector3 target{};
        float age = 0.0f;
        float flightDuration = 0.8f;
        float spin = 0.0f;
        bool impacted = false;
    };

    struct MagmaPool {
        std::unique_ptr<Object3d> visual;
        Vector3 position{};
        float age = 0.0f;
        float lifetime = 3.4f;
        float radius = 2.0f;
        float damageTimer = 0.0f;
    };

    struct MagmaRing {
        std::unique_ptr<Object3d> visual;
        Vector3 position{};
        float age = 0.0f;
        float delay = 0.0f;
        float lifetime = 0.72f;
        float startRadius = 1.0f;
        float endRadius = 8.0f;
        float previousRadius = 1.0f;
        bool hitTarget = false;
    };

    struct MagmaPillar {
        std::unique_ptr<Object3d> visual;
        Vector3 position{};
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

    struct MagmaSurge {
        std::unique_ptr<Object3d> visual;
        std::unique_ptr<Object3d> crestVisual;
        Vector3 origin{};
        Vector3 direction{ 0.0f, 0.0f, 1.0f };
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

    void EnsureBaseScale();
    void UpdateBehavior(float deltaTime, const Vector3& targetDirection, float targetDistance, Vector3& velocity);
    void StartAttack(AttackKind kind, const Vector3& targetDirection);
    void UpdateAttack(float deltaTime, const Vector3& targetDirection, float targetDistance, Vector3& velocity);
    void UpdateWindup(float deltaTime, const Vector3& targetDirection);
    void BeginActive();
    void UpdateActive(float deltaTime, float targetDistance, Vector3& velocity);
    void BeginRecovery();
    void FinishAttack();

    AttackKind ResolveAutomaticAttack(float targetDistance) const;
    AttackKind ResolveDebugAttack() const;
    const char* GetAttackId(AttackKind kind) const;
    const EnemyAttackDefinition& GetCurrentAttackDefinition() const;

    void PrepareMortarTargets();
    void LaunchNextMagmaBlob();
    void UpdateMagmaBlobs(float deltaTime);
    void ImpactMagmaBlob(MagmaBlob& blob);
    void SpawnMagmaPool(const Vector3& position, float radius, float lifetime);
    void UpdateMagmaPools(float deltaTime);
    void BeginRush();
    void UpdateRush(float deltaTime, float targetDistance, Vector3& velocity);
    void FinishRush();
    void EnsureRushFlameVisuals();
    void UpdateRushFlameVisuals(const Vector3& position, float progress, float deltaTime);
    void ClearRushFlameVisuals();
    void BeginVolcanicSlam();
    void UpdateVolcanicSlam(float deltaTime, Vector3& velocity);
    void ImpactVolcanicSlam();
    void SpawnSlamRings(const Vector3& position);
    void UpdateMagmaRings(float deltaTime);
    void PrepareEruptionTargets();
    void SpawnNextMagmaPillar();
    void SpawnMagmaPillar(
        const Vector3& position,
        float radius,
        float height,
        float lifetime,
        float delay,
        const char* attackId);
    void UpdateMagmaPillars(float deltaTime);
    void BeginLavaSpiral();
    void UpdateMagmaSurges(float deltaTime);

    std::unique_ptr<Object3d> CreateMagmaVisual(
        const std::string& name,
        const std::string& model,
        int materialType,
        const Vector4& color,
        float emissive) const;
    Vector3 FindGroundPoint(const Vector3& samplePosition) const;
    void DamageTargetAt(
        const Vector3& center,
        float radius,
        float damage,
        const Vector3& knockback,
        float burnDuration,
        float burnTickInterval,
        float burnTickDamage);
    void ApplySlimeAnimation(float deltaTime);
    void UpdateFacing(const Vector3& direction);
    void UpdateBodyHeat(float deltaTime);
    void EmitPreset(const char* presetName, const Vector3& position) const;
    void EmitDirectedPreset(
        const char* presetName,
        const Vector3& position,
        const Vector3& direction,
        float speedScale) const;
    void SyncCollisionRadius();
    void ClearTransientVisuals();
    bool UpdateEncounterState(float deltaTime);
    void CaptureEncounterAuthoredState(bool refreshVisualTransform = false);
    void InitializeEncounterState();
    void ResetEncounterStateForEditor();
    void BeginEncounterAppearance();
    void ApplyDormantEncounterState();
    void FinishEncounterAppearance();
    bool IsEncounterControlled() const;
    float GetEncounterAppearanceDuration() const;

    AttackKind currentAttack_ = AttackKind::None;
    AttackState attackState_ = AttackState::Idle;
    Vector3 baseScale_{ 1.0f, 1.0f, 1.0f };
    Vector3 lockedDirection_{ 0.0f, 0.0f, 1.0f };
    Vector3 lockedTargetPosition_{};
    Vector3 attackStartPosition_{};
    Vector3 attackEndPosition_{};
    std::array<Vector3, 15> mortarTargets_{};
    std::array<Vector3, 9> eruptionTargets_{};
    std::vector<MagmaBlob> magmaBlobs_;
    std::vector<MagmaPool> magmaPools_;
    std::vector<MagmaRing> magmaRings_;
    std::vector<MagmaPillar> magmaPillars_;
    std::vector<MagmaSurge> magmaSurges_;
    std::array<std::unique_ptr<Object3d>, 5> rushFlameVisuals_{};

    float idleTimer_ = 0.0f;
    float attackTimer_ = 0.0f;
    float attackStateDuration_ = 0.0f;
    float attackCooldown_ = 1.0f;
    float actionTimer_ = 0.0f;
    float effectTimer_ = 0.0f;
    float ambientEmberTimer_ = 0.0f;
    float landingPulseTimer_ = 0.0f;
    float mortarPatternPhase_ = 0.0f;
    int actionIndex_ = 0;
    int automaticAttackSerial_ = 0;
    bool hasBaseScale_ = false;
    bool warningTriggered_ = false;
    bool attackDamageApplied_ = false;
    bool rushImpactSpawned_ = false;
    bool slamImpactSpawned_ = false;
    std::string debugPreviewAttackId_;

    EncounterState encounterState_ = EncounterState::Normal;
    Vector3 encounterBasePosition_{};
    Vector3 encounterBaseScale_{ 1.0f, 1.0f, 1.0f };
    Vector3 encounterBaseRotation_{};
    Vector4 encounterBaseColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
    float encounterBaseEmissive_ = 1.18f;
    uint32_t encounterCollisionAttribute_ = 0;
    uint32_t encounterCollisionMask_ = 0;
    int encounterMaterialType_ = 0;
    float encounterTimer_ = 0.0f;
    bool encounterInitializedForPlay_ = false;
    bool encounterAuthoredStateCaptured_ = false;
    bool encounterRequestedActive_ = false;
};
