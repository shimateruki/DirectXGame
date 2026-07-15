#pragma once
#include "Character.h"
#include "EffectObject3d.h"
#include "IAnimationState.h"
#include "InputManager.h"
#include "ParticleSystem.h"
#include "PlayerGateReturnAnimation.h"
#include "PlayerMover.h"
#include "PlayerSlimeAnimator.h"
#include "engine/utility/math/Math.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class IMoveStrategy; // 前方宣言
class SpriteCommon;
class BaseEnemy;

// プレイヤーキャラクターの入力、移動、状態、敵変身をまとめて扱うクラス
class Player : public Character
{
public:
    // ==================================================
    // 基本サイクル
    // ==================================================
    void Initialize(Object3dCommon* common, InputManager* inputManager, ParticleSystem* particleSystem, SpriteCommon* spriteCommon = nullptr);
    void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    void DrawUI(); // プレイヤー専用 UI の描画。

    // ==================================================
    // 衝突判定
    // ==================================================
    bool OnCollision(Object3d* other) override;

    // ==================================================
    // アニメーション・状態管理 (State Pattern)
    // ==================================================
    void ChangeState(std::unique_ptr<IAnimationState> newState);
    void PlayAnimation(const std::string& animName, bool loop = true);
    void SetSlimeAnimationMode(PlayerSlimeAnimator::Mode mode);
    void SetSlimeAnimationDirection(const Vector3& direction);
    void SetSlimePullDirection(const Vector3& direction);
    void SetSlimePullProgress(float progress);
    void SetSlimeJumpCharge(float chargeRate);
    void TriggerSlimeImpulse(const Vector3& scale, float duration);
    void ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode mode, const Vector3& direction);
    void StartGateReturnAnimation(const PlayerGateReturnAnimation::Route& route);
    bool IsGateReturnAnimationActive() const;
    bool IsGateReturnAnimationFinished() const;
    void StopGateReturnAnimation(bool restoreControl);
    void BeginCinematicLock();
    void EndCinematicLock(bool restoreControl);
    bool IsCinematicLocked() const { return isCinematicLocked_; }

    // ==================================================
    // 移動制御 (Strategy Pattern)
    // ==================================================
    void SetMoveStrategy(std::unique_ptr<IMoveStrategy> strategy);
    void ApplyDashPanelBoost(float duration, float speedMultiplier, float turnMultiplier);
    void ApplyIceSurface(float duration, float friction, float steering);

    // ==================================================
    // アクセス
    // ==================================================
    // --- 物理・姿勢 ---
    Vector3 GetVelocity() const { return velocity_; }
    void SetVelocity(const Vector3& v) { velocity_ = v; }
    void SetRespawnPosition(const Vector3& pos) { respawnPosition_ = pos; }
    Vector3 GetRespawnPosition() const { return respawnPosition_; }

    Vector3 GetRotation() const { return transform_.rotate; }
    float GetVisualYawOffset() const;
    float GetMoveYaw() const;
    Vector3 GetForwardDirection() const;
    void SetMoveYaw(float yaw);

    // Euler 回転を設定し、内部のクォータニオンも同期する
    void SetRotation(const Vector3& r)
    {
        transform_.rotate = r;
        transform_.quaternion = Math::EulerToQuaternion(r);
        transform_.isQuaternionMaster = true;
    }

    // Y 軸だけを更新し、クォータニオンも同期する
    void SetRotationY(float y)
    {
        transform_.rotate.y = y;
        transform_.quaternion = Math::EulerToQuaternion(transform_.rotate);
        transform_.isQuaternionMaster = true;
    }

    // --- 状態フラグ ---
    void SetLockOn(bool isLockingOn) { isLockingOn_ = isLockingOn; }
    bool IsLockingOn() const { return isLockingOn_; }
    void SetIsControlActive(bool active) { isControlActive_ = active; }
    bool IsControlActive() const { return isControlActive_; }

    uint32_t GetJumpCount() const { return jumpCount_; }
    void IncrementJumpCount() { jumpCount_++; }
    void ResetJumpCount() { jumpCount_ = 0; }

    // コンボ受付中に次の攻撃を予約する
    void SetPendingAttack2(bool pending) { pendingAttack2_ = pending; }
    bool ConsumePendingAttack2() { bool v = pendingAttack2_; pendingAttack2_ = false; return v; }

    // コンボ受付時間の管理
    void StartComboWindow(float duration);
    bool IsComboWindowActive() const;

    // Run から Attack へ遷移する時の入力抜けを防ぐ短時間バッファ
    void RecordAttackInput(float duration);
    void MarkAttackBufferUsedForStateStart();
    bool ConsumeBufferedAttackInput();

    // --- 外部システム参照 ---
    InputManager* GetInputManager() { return inputManager_; }
    ParticleSystem* GetParticleSystem() { return particleSystem_; }

    float GetJumpPower() const
    {
        return param_.has_value() ? param_->jumpPower : 24.0f;
    }

    float GetMoveSpeed() const
    {
        return param_.has_value() ? param_->speed : 27.7f;
    }

    // ==================================================
    // 無敵フレーム (Invincibility) 管理
    // ==================================================
    void SetDamageInvincible(bool inv); // ダメージ被弾後の無敵フラグ。
    void SetDashInvincible(bool inv);   // 回避ダッシュ中の無敵フラグ。
    void StartElectricShockFeedback(float duration = 0.78f, float invincibleDuration = 1.0f);

    // どちらか一方でも有効なら、ダメージを受けない状態として扱う
    bool IsInvincible() const { return isDamageInvincible_ || isDashInvincible_; }
    float GetHp() const { return param_.has_value() ? param_->hp : 100.0f; }
    float GetMaxHp() const { return param_.has_value() ? param_->maxHp : 100.0f; }
    float GetDeathTimer() const { return deathTimer_; }

    void SetCarriedEnemy(Object3d* enemy);
    void ReleaseCarriedEnemy(bool restorePose = true);
    Object3d* GetCarriedEnemy() const { return carriedEnemy_; }
    bool IsEnemyMorphed() const { return isEnemyMorphed_; }
    bool HasEnemyMorphTimeLimit() const { return isEnemyMorphed_ && enemyMorphHasTimeLimit_; }
    bool IsPinkSlimeMorphed() const;
    float GetEnemyMorphRate() const;

private:
    // --- 内部コンポーネント ---
    std::unique_ptr<PlayerMover> mover_ = nullptr;     // 移動処理の委譲先。
    std::unique_ptr<IAnimationState> state_ = nullptr; // 現在のアクション状態。
    PlayerSlimeAnimator slimeAnimator_;
    PlayerGateReturnAnimation gateReturnAnimation_;

    // --- 外部システム参照 ---
    InputManager* inputManager_ = nullptr;
    ParticleSystem* particleSystem_ = nullptr;

    // --- プレイヤー状態フラグ ---
    bool isLockingOn_ = false;       // 敵をロックオンしているか。
    bool isControlActive_ = true;    // 入力を受け付ける状態か。
    bool forcedSlimeAnimationModeActive_ = false;
    PlayerSlimeAnimator::Mode forcedSlimeAnimationMode_ = PlayerSlimeAnimator::Mode::Idle;
    Vector3 forcedSlimeAnimationDirection_{ 0.0f, 0.0f, 1.0f };
    bool isCinematicLocked_ = false;
    bool cinematicSavedControlActive_ = true;
    uint32_t cinematicSavedCollisionAttribute_ = 0;
    uint32_t cinematicSavedCollisionMask_ = 0;

    // 攻撃1終了後に次のクリックで攻撃2へつなげるための予約フラグ
    bool pendingAttack2_ = false;

    // --- コンボ受付時間 ---
    float comboWindowTimer_ = 0.0f; // >0 の間、次の攻撃入力を 2 段目へ変換する。

    // --- 攻撃入力バッファ ---
    bool attackInputBuffered_ = false;              // バッファに攻撃入力があるか。
    bool attackBufferUsedForStateStart_ = false;    // そのバッファを状態遷移開始で使ったか。
    float attackInputBufferTimer_ = 0.0f;           // バッファの残り時間。

    // --- 無敵関連 ---
    bool isInvincible_ = false;
    Vector4 savedColor_ = { 1.0f, 1.0f, 1.0f, 1.0f }; // 無敵解除時に戻す色。

    // 子パーツの色を保存して、無敵解除時に復元する
    std::unordered_map<Object3d*, Vector4> childSavedColors_;
    float damageCooldownTimer_ = 0.0f;
    bool isDamageInvincible_ = false;
    bool isDashInvincible_ = false;
    float invincibleBlinkTimer_ = 0.0f;
    bool damageBlinkVisibilityApplied_ = false;
    bool damageBlinkBodyVisible_ = true;
    std::vector<bool> damageBlinkChildVisible_;
    float deathTimer_ = 0.0f; // 死亡してからの経過時間。
    void UpdateDamageInvincibleBlinkVisibility();
    void RestoreDamageBlinkVisibility();
    void UpdateElectricShockFeedback(float deltaTime);
    void EndElectricShockFeedback();
    void InitializeElectricShockAuraEffect();
    void UpdateElectricShockAuraEffect(float deltaTime);
    void HideElectricShockAuraEffect();
    void CalculateElectricShockAuraShape(Vector3& center, float& horizontalDiameter, float& verticalDiameter) const;
    void UpdateColor();

    // 落下復帰用
    Vector3 respawnPosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 baseRotation_ = { 0.0f, 0.0f, 0.0f }; // 初期回転。モデル姿勢補正にも使う。
    bool isFirstUpdate_ = true;

    // フックマーカー
    std::unique_ptr<Object3d> hookMarker_;

    // 現在頭に乗せている敵と、狙い先の情報
    Object3d* carriedEnemy_ = nullptr;
    Vector3 carriedEnemyBaseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 carriedEnemyBaseRotation_ = { 0.0f, 0.0f, 0.0f };
    Quaternion carriedEnemyBaseQuaternion_ = { 0.0f, 0.0f, 0.0f, 1.0f };
    bool carriedEnemyBaseQuaternionMaster_ = true;
    bool hasCarriedEnemyBaseScale_ = false;
    Object3d* aimTargetObject_ = nullptr;
    float carryGlideEffectTimer_ = 0.0f;

    enum class EnemyMorphType {
        None,
        Slime,
        Bomber,
        Bat,
        BeamDrone,
        Mushroom,
        GiantSlime,
        FireSlime,
        ThunderSlime
    };

    EnemyMorphType enemyMorphType_ = EnemyMorphType::None;
    BaseEnemy* enemyMorphSource_ = nullptr;
    bool isEnemyMorphed_ = false;
    bool enemyMorphHasTimeLimit_ = true;
    float enemyMorphTimer_ = 0.0f;
    float enemyMorphDuration_ = 5.0f;
    float enemyMorphEffectTimer_ = 0.0f;
    float enemyMorphVisualTimer_ = 0.0f;
    float electricShockFeedbackTimer_ = 0.0f;
    float electricShockFeedbackEmitTimer_ = 0.0f;
    float electricShockFeedbackTotalDuration_ = 0.0f;
    float electricShockPendingInvincibleDuration_ = 0.0f;
    bool electricShockControlLocked_ = false;
    bool electricShockWasControlActive_ = true;
    Vector3 electricShockLockedPosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 electricShockBaseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 electricShockBaseRotation_ = { 0.0f, 0.0f, 0.0f };
    std::unique_ptr<EffectObject3d> electricShockAuraEffect_;
    std::string savedMorphModelName_;
    std::string savedMorphTexturePath_;
    std::string savedMorphAnimName_;
    bool savedMorphAnimLoop_ = true;
    Vector4 savedMorphColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector3 savedMorphScale_ = { 1.0f, 1.0f, 1.0f };
    int savedMorphMaterialType_ = 0;
    float savedMorphEmissive_ = 1.0f;
    Vector4 enemyMorphTint_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    std::vector<bool> savedMorphChildVisible_;
    bool savedMorphSourceVisible_ = true;
    BaseEnemy* pendingAbsorbEnemy_ = nullptr;
    EnemyMorphType pendingAbsorbType_ = EnemyMorphType::None;
    Vector3 pendingAbsorbEnemyBaseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 pendingAbsorbEnemyStartPos_ = { 0.0f, 0.0f, 0.0f };
    Vector4 pendingAbsorbTint_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 pendingAbsorbPlayerBaseColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    float absorbEffectTimer_ = 0.0f;
    float absorbEffectEmitTimer_ = 0.0f;
    bool absorbEffectActive_ = false;

    void StartEnemyMorph(BaseEnemy* enemy);
    void UpdateEnemyMorph(float deltaTime);
    void CancelEnemyMorph();
    bool ShouldPlayAbsorbEffect(BaseEnemy* enemy) const;
    void BeginAbsorbEffect(BaseEnemy* enemy);
    void UpdateAbsorbEffect(float deltaTime);
    void FinishAbsorbEffect();
    void CancelAbsorbEffect(bool restoreEnemy);
    float GetEnemyMorphModelYawOffset() const;
    EnemyMorphType ResolveEnemyMorphType(const std::string& enemyType) const;
    Vector4 GetEnemyMorphTint(EnemyMorphType type) const;

public:
    Object3d* GetHookMarker() const { return hookMarker_.get(); }

private:
    // ギミック同期用
    uint32_t jumpCount_ = 0;
};
