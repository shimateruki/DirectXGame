#pragma once

#include "BaseEnemy.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

// ステージ3の最終ボスです。3形態で王冠エネルギーを使った大規模攻撃を行います。
class EnemyFalseKingSlime final : public BaseEnemy {
public:
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
        Vector3 groundPosition{};
        float age = 0.0f;
        float delay = 0.0f;
        float flightDuration = 0.72f;
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

    void EnsureBaseScale();
    void UpdateBehavior(float deltaTime, const Vector3& targetDirection, float targetDistance, Vector3& velocity);
    void StartAttack(AttackKind kind, const Vector3& targetDirection);
    void UpdateAttack(float deltaTime, const Vector3& targetDirection, float targetDistance, Vector3& velocity);
    void UpdateWindup(float deltaTime, const Vector3& targetDirection);
    void BeginActive();
    void UpdateActive(float deltaTime, const Vector3& targetDirection, Vector3& velocity);
    void BeginRecovery();
    void FinishAttack();
    AttackKind ResolveAutomaticAttack(float targetDistance) const;
    AttackKind ResolveDebugAttack() const;
    const char* GetAttackId(AttackKind kind) const;
    const EnemyAttackDefinition& GetCurrentAttackDefinition() const;

    int ResolveDesiredBattlePhase() const;
    void BeginPhaseShift(int nextPhase);
    void UpdatePhaseShift(float deltaTime, Vector3& velocity);

    void PrepareLanceTargets(bool dominionPattern);
    void SpawnNextCrownLance(bool dominionPattern = false);
    void SpawnCrownLance(const Vector3& groundPosition, float delay);
    void UpdateCrownLances(float deltaTime);
    void SpawnRoyalWaves();
    void UpdateRoyalWaves(float deltaTime);
    void BeginRush();
    void BeginRushStep(int step);
    void UpdateRush(float deltaTime, const Vector3& targetDirection, Vector3& velocity);
    void SpawnRoyalBeams(bool dominionPattern);
    void UpdateRoyalBeams(float deltaTime);
    void UpdateDominion(float deltaTime);

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
    float dominionLanceTimer_ = 0.0f;
    float ambientSparkTimer_ = 0.0f;
    int actionIndex_ = 0;
    int automaticAttackSerial_ = 0;
    int battlePhase_ = 1;
    int pendingBattlePhase_ = 1;
    int rushStep_ = 0;
    bool rushDamageApplied_ = false;
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
};
