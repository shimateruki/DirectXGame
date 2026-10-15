#pragma once
#include "Character.h"
#include "game/actor/player/combat/PlayerAttackInputBuffer.h"
#include "EffectObject3d.h"
#include "IAnimationState.h"
#include "InputManager.h"
#include "ParticleSystem.h"
#include "game/actor/player/animation/PlayerGateReturnAnimation.h"
#include "game/actor/player/movement/PlayerMover.h"
#include "game/actor/player/combat/PlayerBaseCombatController.h"
#include "game/actor/player/copy/PlayerCopyAbilityController.h"
#include "game/actor/player/animation/PlayerSlimeAnimator.h"
#include "engine/utility/math/Math.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class IMoveStrategy; // 前方宣言
namespace engine { class SpriteCommon; }
class BaseEnemy;

/// <summary>プレイヤーの入力、移動、戦闘状態、コピー能力を統合します。</summary>
class Player : public Character {
  public:
    enum class EnemyMorphType {
        None = 0,
        Slime = 1,
        Bomber = 2,
        // 値3～6は廃止したコピー能力とのリプレイ互換用に予約します。
        FireSlime = 7,
        ThunderSlime = 8,
        WindSlime = 9,
        Crown = 10
    };

    // ==================================================
    // 基本サイクル
    // ==================================================
    /// <summary>プレイヤーが保持する状態、演出、補助オブジェクトを破棄します。</summary>
    ~Player() override;
    /// <summary>モデルと入力・粒子への参照を設定してプレイヤーを初期化します。</summary>
    /// <param name="common">3D描画の共通基盤。</param>
    /// <param name="inputManager">操作入力の参照元。</param>
    /// <param name="particleSystem">プレイヤー演出に使う粒子システム。</param>
    /// <param name="spriteCommon">UIスプライトの描画基盤。省略可能です。</param>
    void Initialize(Object3dCommon* common, InputManager* inputManager, ParticleSystem* particleSystem,
                    engine::SpriteCommon* spriteCommon = nullptr);
    /// <summary>入力、移動、戦闘状態、コピー能力と演出を一フレーム進めます。</summary>
    /// <param name="deltaTime">前フレームからの経過秒数。</param>
    void Update(float deltaTime) override;
    /// <summary>メインカメラ向けに本体と所有する表示物を描画します。</summary>
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    /// <summary>指定カメラ向けにプレイヤーの表示物を描画します。</summary>
    /// <param name="previewBufferIndex">プレビュー用の描画バッファ番号。</param>
    void DrawForCamera(Camera* camera, ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource,
                       int previewBufferIndex = 0) override;
    /// <summary>指定カメラ向けに特殊マテリアルの描画パスを実行します。</summary>
    void DrawSpecialMaterialForCamera(Camera* camera, uint32_t depthSrvHandle, uint32_t grabSrvHandle,
                                      int previewBufferIndex = 0) override;
    /// <summary>プレイヤーが追加の特殊マテリアル表示物を所有するか返します。</summary>
    bool HasOwnedSpecialMaterialVisuals() const override;
    /// <summary>メインカメラ向けに所有する特殊マテリアル表示物を描画します。</summary>
    void DrawOwnedSpecialMaterialVisuals(uint32_t depthSrvHandle, uint32_t grabSrvHandle) override;
    /// <summary>指定カメラ向けに所有する特殊マテリアル表示物を描画します。</summary>
    void DrawOwnedSpecialMaterialVisualsForCamera(Camera* camera, uint32_t depthSrvHandle, uint32_t grabSrvHandle,
                                                  int previewBufferIndex = 0) override;
    /// <summary>プレイヤー専用のUIを描画します。</summary>
    void DrawUI();
    /// <summary>管理対象の本体スケールを変更し、現在の姿に反映します。</summary>
    void ApplyManagedScale(const engine::Vector3& scale) override;

    // ==================================================
    // 衝突判定
    // ==================================================
    /// <summary>他のオブジェクトとの接触を処理します。</summary>
    /// <returns>この接触を処理した場合は true。</returns>
    bool OnCollision(engine::Object3d* other) override;

    // ==================================================
    // アニメーション・状態管理 (State Pattern)
    // ==================================================
    /// <summary>現在の状態を終了し、新しいアニメーション状態へ移ります。</summary>
    /// <param name="newState">所有権を受け取る次の状態。</param>
    void ChangeState(std::unique_ptr<IAnimationState> newState);
    /// <summary>指定名の本体アニメーションを再生します。</summary>
    /// <param name="loop">末尾から繰り返す場合は true。</param>
    void PlayAnimation(const std::string& animName, bool loop = true);
    /// <summary>スライム用Animator Controllerを再読み込みします。</summary>
    /// <returns>再読み込みに成功した場合は true。</returns>
    bool ReloadSlimeAnimatorController() { return slimeAnimator_.ReloadController(); }
    /// <summary>現在読み込まれているスライム用Controllerアセットを返します。</summary>
    const AnimatorControllerAsset* GetSlimeAnimatorControllerAsset() const {
        return slimeAnimator_.GetControllerAsset();
    }
    /// <summary>再生状態を変更するスライム用Controllerランタイムを返します。</summary>
    AnimatorControllerRuntime* GetSlimeAnimatorControllerRuntime() {
        return slimeAnimator_.GetControllerRuntime();
    }
    /// <summary>移動や待機など、スライムの基本姿勢モードを設定します。</summary>
    void SetSlimeAnimationMode(PlayerSlimeAnimator::Mode mode);
    /// <summary>スライム姿勢の向きを設定します。</summary>
    void SetSlimeAnimationDirection(const engine::Vector3& direction);
    /// <summary>吸い込み姿勢が向く方向を設定します。</summary>
    void SetSlimePullDirection(const engine::Vector3& direction);
    /// <summary>吸い込み姿勢の進行率を設定します。</summary>
    void SetSlimePullProgress(float progress);
    /// <summary>ジャンプの溜め姿勢を設定します。</summary>
    void SetSlimeJumpCharge(float chargeRate);
    /// <summary>コピー能力の姿勢と強さを設定します。</summary>
    void SetSlimeAbilityPose(PlayerSlimeAnimator::AbilityPose pose, float strength = 1.0f);
    /// <summary>指定時間だけスライム本体にスケールの反動を加えます。</summary>
    void TriggerSlimeImpulse(const engine::Vector3& scale, float duration);
    /// <summary>スライム本体に能力モーションを再生します。</summary>
    /// <param name="clipName">Animator内のクリップ名。</param>
    /// <param name="loop">末尾で繰り返す場合は true。</param>
    /// <param name="playbackSpeed">再生速度の倍率。</param>
    /// <param name="blendInDuration">開始時のブレンド秒数。</param>
    /// <param name="blendOutDuration">終了時のブレンド秒数。</param>
    bool PlaySlimeAbilityMotion(const std::string& clipName, bool loop = false, float playbackSpeed = 1.0f,
                                float blendInDuration = 0.06f, float blendOutDuration = 0.10f);
    /// <summary>能力モーションを指定時間で通常姿勢へ戻します。</summary>
    void StopSlimeAbilityMotion(float blendOutDuration = 0.08f);
    /// <summary>能力モーションの再生状態を即座に消去します。</summary>
    void ClearSlimeAbilityMotion();
    /// <summary>次回の更新に限ってスライム姿勢のモードと向きを上書きします。</summary>
    void ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode mode, const engine::Vector3& direction);
    /// <summary>指定経路に沿うゲート帰還演出を開始します。</summary>
    void StartGateReturnAnimation(const PlayerGateReturnAnimation::Route& route);
    /// <summary>ゲート帰還演出が進行中か返します。</summary>
    bool IsGateReturnAnimationActive() const;
    /// <summary>ゲート帰還演出が終点に達したか返します。</summary>
    bool IsGateReturnAnimationFinished() const;
    /// <summary>ゲート帰還演出を止めます。</summary>
    /// <param name="restoreControl">元の操作状態を復元する場合は true。</param>
    void StopGateReturnAnimation(bool restoreControl);
    /// <summary>ムービー中の操作をロックし、必要ならコピー能力を保持します。</summary>
    /// <param name="preserveCopyAbility">true のとき、ムービー開始時にコピー能力を解除しません。</param>
    void BeginCinematicLock(bool preserveCopyAbility = false);
    /// <summary>入力・衝突を止めた演出中に、通常の吸い込み姿勢だけを評価します。</summary>
    void StepCinematicPullVisual(float deltaTime, const engine::Vector3& direction, float progress);
    /// <summary>演出中の吸い込み姿勢の一時指定を解除します。</summary>
    void EndCinematicPullVisual();
    /// <summary>演出用ロックを解除し、必要なら操作状態を復元します。</summary>
    void EndCinematicLock(bool restoreControl);
    /// <summary>ムービーなどによりプレイヤー操作がロックされているか返します。</summary>
    bool IsCinematicLocked() const { return isCinematicLocked_; }

    // ==================================================
    // 移動制御 (Strategy Pattern)
    // ==================================================
    /// <summary>移動戦略の所有権を受け取り、以後の移動計算に使用します。</summary>
    void SetMoveStrategy(std::unique_ptr<IMoveStrategy> strategy);
    /// <summary>ダッシュパネルの速度・旋回補正を指定時間だけ適用します。</summary>
    /// <param name="duration">効果時間（秒）。</param>
    /// <param name="speedMultiplier">移動速度の倍率。</param>
    /// <param name="turnMultiplier">旋回速度の倍率。</param>
    void ApplyDashPanelBoost(float duration, float speedMultiplier, float turnMultiplier);
    /// <summary>衝撃ゲートを壊せる通常攻撃またはコピー突進の最中か返します。</summary>
    bool IsImpactBreakActive() const;
    /// <summary>ピンクスライム固有のバウンド落下中か返します。</summary>
    /// <remarks>通常攻撃では反応しない能力専用ギミックの判定に使用します。</remarks>
    bool IsPinkBounceSlamImpactActive() const;

    // ==================================================
    // アクセス
    // ==================================================
    // --- 物理・姿勢 ---
    /// <summary>現在の移動速度を取得・設定します。</summary>
    void SetVelocity(const engine::Vector3& v) { velocity_ = v; }
    engine::Vector3 GetVelocity() const { return velocity_; }
    /// <summary>落下復帰時の位置を取得・設定します。</summary>
    void SetRespawnPosition(const engine::Vector3& pos) { respawnPosition_ = pos; }
    engine::Vector3 GetRespawnPosition() const { return respawnPosition_; }
    /// <summary>チェックポイントを有効にし、復帰位置と対象名を保存します。</summary>
    void ActivateCheckpoint(const engine::Vector3& position, const std::string& checkpointObjectName = {});
    /// <summary>敵を生成せず、保存済みのコピー能力を直接適用します。</summary>
    /// <param name="enemyType">コピー元の敵種別。</param>
    /// <param name="unlimitedDuration">時間制限を設けない場合は true。</param>
    bool ApplyStoredCopy(const std::string& enemyType, bool unlimitedDuration = true);
    /// <summary>隔離エディターの能力プレビューに一フレーム分の入力を渡します。</summary>
    /// <remarks>通常のプレイヤーには適用しません。</remarks>
    void SetCopyPreviewInput(const PlayerCopyAbilityInput& input) {
        if (IsEditorInternal()) {
            copyPreviewActive_ = true;
            copyPreviewInput_ = input;
        }
    }
    /// <summary>隔離プレビュー中のコピー能力を解除します。</summary>
    void ReleaseCopyPreview() {
        if (IsEditorInternal())
            BeginEnemyMorphRelease(false);
    }
    /// <summary>隔離プレビュー中の地面の高さを設定します。</summary>
    void SetCopyPreviewGroundHeight(float height) {
        if (IsEditorInternal())
            copyPreviewGroundHeight_ = height;
    }
    /// <summary>隔離プレビューが所有する一時オブジェクトを追加します。</summary>
    /// <param name="object">所有権を移すオブジェクト。</param>
    void AddCopyPreviewObject(std::unique_ptr<engine::Object3d> object) {
        if (IsEditorInternal())
            copyPreviewObjects_.push_back(std::move(object));
    }
    /// <summary>隔離プレビューが所有する一時オブジェクトを更新します。</summary>
    void StepCopyPreviewObjects(float deltaTime, float groundY);
    /// <summary>現在のコピー能力状態をエディター表示用の短い文字列で返します。</summary>
    const char* GetCopyAbilityStateLabel() const {
        return copyAbilityController_ ? copyAbilityController_->GetStateLabel() : "未コピー";
    }
    /// <summary>隔離プレビューで再生する攻撃IDを指定します。</summary>
    void SetCopyPreviewAttackId(const std::string& id) {
        if (IsEditorInternal() && copyAbilityController_) copyAbilityController_->SetPreviewAttackId(id);
    }
    /// <summary>隔離Actorの能力セッションを保存前の設定で準備します。</summary>
    void SetCopyPreviewProfile(const EnemyAttackProfile& profile) {
        if (IsEditorInternal() && copyAbilityController_) {
            copyAbilityController_->Cancel(*this);
            copyAbilityController_->Activate(static_cast<int>(enemyMorphType_), profile);
        }
    }
    /// <summary>王冠能力を解除します。</summary>
    void ClearCrownPower();

    /// <summary>Object3dが保持するEuler回転を返します。</summary>
    engine::Vector3 GetRotation() const { return engine::Object3d::GetRotation(); }
    /// <summary>表示モデル固有のY回転補正を返します。</summary>
    float GetVisualYawOffset() const;
    /// <summary>移動の基準になるY回転角をラジアンで返します。</summary>
    float GetMoveYaw() const;
    /// <summary>現在の前方向をワールド座標で返します。</summary>
    engine::Vector3 GetForwardDirection() const;
    /// <summary>通常移動と同じカメラ基準で、現在押している移動方向を返します。</summary>
    /// <remarks>操作制限中にも読めるため、攻撃中の方向転換に使用できます。</remarks>
    engine::Vector3 GetMoveInputDirection();
    /// <summary>水平面での進行向きをY回転のラジアン値で指定します。</summary>
    void SetMoveYaw(float yaw);

    /// <summary>Euler回転を設定し、内部のクォータニオンも同期します。</summary>
    void SetRotation(const engine::Vector3& r) {
        SetEulerAngles(r);
        SetQuaternion(engine::Math::EulerToQuaternion(r));
    }

    /// <summary>Y軸回転だけを更新し、内部のクォータニオンも同期します。</summary>
    void SetRotationY(float y) {
        engine::Vector3 rotation = GetRotation();
        rotation.y = y;
        SetRotation(rotation);
    }

    // --- 状態フラグ ---
    /// <summary>ロックオン状態を取得・設定します。</summary>
    void SetLockOn(bool isLockingOn) { isLockingOn_ = isLockingOn; }
    bool IsLockingOn() const { return isLockingOn_; }
    /// <summary>ロックオンする位置とその有効性を設定します。</summary>
    /// <remarks>敵のポインターは保持せず、位置だけを受け取ります。</remarks>
    void SetLockOnAimPoint(const engine::Vector3& point, bool valid) {
        lockOnAimPoint_ = point;
        lockOnAimValid_ = valid;
    }
    /// <summary>有効なロックオン位置があるか返します。</summary>
    bool HasLockOnAimPoint() const { return isLockingOn_ && lockOnAimValid_; }
    /// <summary>ロックオン位置を考慮した攻撃方向を返します。</summary>
    engine::Vector3 GetLockOnAttackDirection() const;
    /// <summary>プレイヤー入力の受付状態を取得・設定します。</summary>
    void SetIsControlActive(bool active) { isControlActive_ = active; }
    bool IsControlActive() const { return isControlActive_; }
    /// <summary>チュートリアル中の被害防止を有効または無効にします。</summary>
    void SetTutorialSafetyEnabled(bool enabled);
    /// <summary>チュートリアル中の被害防止が有効か返します。</summary>
    bool IsTutorialSafetyEnabled() const { return tutorialSafetyEnabled_; }
    /// <summary>チュートリアル中に保持した敵を投げる・吸収する操作の許可を設定します。</summary>
    void SetTutorialCarryActionPermissions(bool allowThrow, bool allowAbsorb);
    /// <summary>チュートリアル用の保持アクション状態を初期状態に戻します。</summary>
    void ResetTutorialCarryActionState();

    /// <summary>今回のプレイ中のジャンプ回数を取得・加算・リセットします。</summary>
    uint32_t GetJumpCount() const { return jumpCount_; }
    void IncrementJumpCount() { jumpCount_++; }
    void ResetJumpCount() { jumpCount_ = 0; }

    /// <summary>コンボ受付中の次の攻撃予約を設定します。</summary>
    void SetPendingAttack2(bool pending) { pendingAttack2_ = pending; }
    /// <summary>次の攻撃予約を読み取り、予約を消去します。</summary>
    /// <returns>消去前に予約があった場合は true。</returns>
    bool ConsumePendingAttack2() {
        bool v = pendingAttack2_;
        pendingAttack2_ = false;
        return v;
    }

    /// <summary>指定秒数のコンボ入力受付を開始します。</summary>
    void StartComboWindow(float duration);
    /// <summary>コンボ入力の受付時間内か返します。</summary>
    bool IsComboWindowActive() const;

    /// <summary>RunからAttackへ移る間の入力を指定秒数だけ記録します。</summary>
    void RecordAttackInput(float duration);
    /// <summary>状態開始時に攻撃入力バッファを使用したことを記録します。</summary>
    void MarkAttackBufferUsedForStateStart();
    /// <summary>有効な攻撃入力を一度だけ取り出します。</summary>
    /// <returns>未消費の攻撃入力があった場合は true。</returns>
    bool ConsumeBufferedAttackInput();

    // --- 外部システム参照 ---
    /// <summary>所有権を持たない入力管理と粒子システムへの参照を返します。</summary>
    InputManager* GetInputManager() { return inputManager_; }
    ParticleSystem* GetParticleSystem() { return particleSystem_; }

    /// <summary>現在のプレイヤーパラメータからジャンプ力を返します。</summary>
    float GetJumpPower() const { return param_.has_value() ? param_->jumpPower : 24.0f; }

    /// <summary>現在のプレイヤーパラメータから移動速度を返します。</summary>
    float GetMoveSpeed() const { return param_.has_value() ? param_->speed : 27.7f; }

    // ==================================================
    // 無敵フレーム (Invincibility) 管理
    // ==================================================
    /// <summary>被弾後・ダッシュ中・ガード中の無敵状態をそれぞれ設定します。</summary>
    void SetDamageInvincible(bool inv);
    void SetDashInvincible(bool inv);
    void SetGuardInvincible(bool inv);
    /// <summary>能力回避用に攻撃判定を伴わない無敵時間を開始します。</summary>
    void StartEvasionInvincibility(float duration);
    /// <summary>被弾演出とノックバック、被弾後の無敵時間を開始します。</summary>
    /// <param name="knockbackDirection">吹き飛ばす方向。</param>
    /// <param name="invincibleDuration">無敵時間（秒）。</param>
    void StartDamageFeedback(const engine::Vector3& knockbackDirection, float invincibleDuration = 1.0f);
    /// <summary>感電演出を開始し、終了後に被弾無敵時間を適用します。</summary>
    void StartElectricShockFeedback(float duration = 0.78f, float invincibleDuration = 1.0f);

    /// <summary>チュートリアル保護または各無敵状態のいずれかが有効か返します。</summary>
    bool IsInvincible() const {
        return tutorialSafetyEnabled_ || isDamageInvincible_ || isDashInvincible_ || isEvasionInvincible_ ||
               isGuardInvincible_;
    }
    /// <summary>現在のHPを返します。未設定時は既定値を使用します。</summary>
    float GetHp() const { return param_.has_value() ? param_->hp : 100.0f; }
    /// <summary>乗車型床の水平速度を記録し、ジャンプ時に一部を引き継ぎます。</summary>
    void SetPlatformMotion(const engine::Vector3& velocity);
    /// <summary>最大HPを返します。未設定時は既定値を使用します。</summary>
    float GetMaxHp() const { return param_.has_value() ? param_->maxHp : 100.0f; }
    /// <summary>チェックポイントなどで現在のHPを最大値まで回復します。</summary>
    void RestoreHpToFull();
    /// <summary>死亡してからの経過秒数を返します。</summary>
    float GetDeathTimer() const { return deathTimer_; }

    /// <summary>頭上に保持する敵を設定します。所有権は受け取りません。</summary>
    void SetCarriedEnemy(engine::Object3d* enemy);
    /// <summary>保持中の敵を離し、必要なら元の姿勢を復元します。</summary>
    void ReleaseCarriedEnemy(bool restorePose = true);
    /// <summary>保持中の敵への参照を返します。所有権は移しません。</summary>
    engine::Object3d* GetCarriedEnemy() const { return carriedEnemy_; }
    /// <summary>現在、敵のコピー能力に変身しているか返します。</summary>
    bool IsEnemyMorphed() const { return isEnemyMorphed_; }
    /// <summary>現在の変身先の敵種別を返します。</summary>
    EnemyMorphType GetEnemyMorphType() const { return enemyMorphType_; }
    /// <summary>現在の変身に時間制限があるか返します。</summary>
    bool HasEnemyMorphTimeLimit() const { return isEnemyMorphed_ && enemyMorphHasTimeLimit_; }
    /// <summary>ピンクスライムの姿に変身しているか返します。</summary>
    bool IsPinkSlimeMorphed() const;
    /// <summary>現在のコピー能力の残り時間を0～1の比率で返します。</summary>
    /// <remarks>時間無制限の変身は1、変身していない場合は0を返します。</remarks>
    float GetEnemyMorphRate() const;
    /// <summary>コピー能力の解除演出中か返します。</summary>
    bool IsEnemyMorphReleasing() const { return enemyMorphReleaseActive_; }
    /// <summary>コピー能力の解除演出の進行率を返します。</summary>
    float GetEnemyMorphReleaseRate() const;

#ifdef USE_IMGUI
    /// <summary>デバッグUIから、指定した敵の能力へ直接切り替えます。</summary>
    void DebugForceEnemyMorph(BaseEnemy* enemy);
    /// <summary>デバッグUIから現在の変身を解除します。</summary>
    void DebugClearEnemyMorph();
    /// <summary>デバッグ移動の前に、座標や操作を上書きする一時状態を解除します。</summary>
    void DebugPrepareForTeleport();
#endif

  protected:
    /// <summary>プレイヤー固有の実行状態をリプレイ用JSONへ保存します。</summary>
    void CaptureReplayCustomState(json& state) const override;
    /// <summary>リプレイ用JSONからプレイヤー固有の実行状態を戻します。</summary>
    void RestoreReplayCustomState(const json& state) override;

  private:
    // --- 内部コンポーネント ---
    std::unique_ptr<PlayerMover> mover_ = nullptr;     // 移動処理の委譲先。
    std::unique_ptr<IAnimationState> state_ = nullptr; // 現在のアクション状態。
    PlayerSlimeAnimator slimeAnimator_;
    PlayerGateReturnAnimation gateReturnAnimation_;
    std::unique_ptr<PlayerBaseCombatController> baseCombatController_;
    std::unique_ptr<PlayerCopyAbilityController> copyAbilityController_;
    bool copyPreviewActive_ = false;
    float copyPreviewGroundHeight_ = 0.0f;
    std::unique_ptr<engine::Object3d> copyAdornment_;
    /// <summary>現在のコピー能力に付随する表示物を更新します。</summary>
    void UpdateCopyAdornment();
    PlayerCopyAbilityInput copyPreviewInput_{};
    std::vector<std::unique_ptr<engine::Object3d>> copyPreviewObjects_;
    engine::Vector3 managedBaseScale_ = {2.0f, 2.0f, 2.0f};

    // --- 外部システム参照 ---
    InputManager* inputManager_ = nullptr;
    ParticleSystem* particleSystem_ = nullptr;

    // --- プレイヤー状態フラグ ---
    bool isLockingOn_ = false; // 敵をロックオンしているか。
    engine::Vector3 lockOnAimPoint_{};
    bool lockOnAimValid_ = false;
    bool isControlActive_ = true; // 入力を受け付ける状態か。
    bool forcedSlimeAnimationModeActive_ = false;
    PlayerSlimeAnimator::Mode forcedSlimeAnimationMode_ = PlayerSlimeAnimator::Mode::Idle;
    engine::Vector3 forcedSlimeAnimationDirection_{0.0f, 0.0f, 1.0f};
    bool isCinematicLocked_ = false;
    bool cinematicSavedControlActive_ = true;
    uint32_t cinematicSavedCollisionAttribute_ = 0;
    uint32_t cinematicSavedCollisionMask_ = 0;
    bool tutorialSafetyEnabled_ = false;
    bool tutorialCarryThrowEnabled_ = true;
    bool tutorialCarryAbsorbEnabled_ = true;


    // 攻撃1終了後に次のクリックで攻撃2へつなげるための予約フラグ
    bool pendingAttack2_ = false;

    // コンボ予約・入力消費の状態は専用クラスだけが変更します。
    PlayerAttackInputBuffer attackInputBuffer_;

    // --- 無敵関連 ---
    bool isInvincible_ = false;
    engine::Vector4 savedColor_ = {1.0f, 1.0f, 1.0f, 1.0f}; // 無敵解除時に戻す色。

    // 子パーツの色を保存して、無敵解除時に復元する
    std::unordered_map<engine::Object3d*, engine::Vector4> childSavedColors_;
    float damageCooldownTimer_ = 0.0f;
    bool isDamageInvincible_ = false;
    bool isDashInvincible_ = false;
    bool isGuardInvincible_ = false;
    float evasionInvincibleTimer_ = 0.0f;
    bool isEvasionInvincible_ = false;
    float invincibleBlinkTimer_ = 0.0f;
    bool damageBlinkVisibilityApplied_ = false;
    bool damageBlinkBodyVisible_ = true;
    std::vector<bool> damageBlinkChildVisible_;
    float deathTimer_ = 0.0f; // 死亡してからの経過時間。
    /// <summary>被弾無敵中の本体と子パーツの点滅を更新します。</summary>
    void UpdateDamageInvincibleBlinkVisibility();
    /// <summary>点滅前の本体と子パーツの表示状態を復元します。</summary>
    void RestoreDamageBlinkVisibility();
    /// <summary>感電中の停止、エフェクト、経過時間を更新します。</summary>
    void UpdateElectricShockFeedback(float deltaTime);
    /// <summary>感電演出を終了し、停止していた操作と姿勢を戻します。</summary>
    void EndElectricShockFeedback();
    /// <summary>感電を示すオーラ表示物を準備します。</summary>
    void InitializeElectricShockAuraEffect();
    /// <summary>感電オーラの位置と形状を更新します。</summary>
    void UpdateElectricShockAuraEffect(float deltaTime);
    /// <summary>感電オーラを非表示にします。</summary>
    void HideElectricShockAuraEffect();
    /// <summary>本体姿勢から感電オーラの中心と縦横の直径を算出します。</summary>
    void CalculateElectricShockAuraShape(engine::Vector3& center, float& horizontalDiameter, float& verticalDiameter) const;
    /// <summary>無敵状態に応じて本体と子パーツの色を更新します。</summary>
    void UpdateColor();

    // 落下復帰用
    engine::Vector3 respawnPosition_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 baseRotation_ = {0.0f, 0.0f, 0.0f}; // 初期回転。モデル姿勢補正にも使う。
    bool isFirstUpdate_ = true;
    bool checkpointActivated_ = false;
    std::string checkpointObjectName_;
    std::string checkpointMorphEnemyType_;

    // フックマーカー
    std::unique_ptr<engine::Object3d> hookMarker_;

    // 現在頭に乗せている敵と、狙い先の情報
    engine::Object3d* carriedEnemy_ = nullptr;
    engine::Vector3 carriedEnemyBaseScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 carriedEnemyBaseRotation_ = {0.0f, 0.0f, 0.0f};
    engine::Quaternion carriedEnemyBaseQuaternion_ = {0.0f, 0.0f, 0.0f, 1.0f};
    bool carriedEnemyBaseQuaternionMaster_ = true;
    bool hasCarriedEnemyBaseScale_ = false;
    engine::Object3d* aimTargetObject_ = nullptr;

    EnemyMorphType enemyMorphType_ = EnemyMorphType::None;
    BaseEnemy* enemyMorphSource_ = nullptr;
    bool isEnemyMorphed_ = false;
    bool enemyMorphHasTimeLimit_ = true;
    float enemyMorphTimer_ = 0.0f;
    float enemyMorphDuration_ = 5.0f;
    float enemyMorphEffectTimer_ = 0.0f;
    float enemyMorphVisualTimer_ = 0.0f;
    bool enemyMorphReleaseActive_ = false;
    bool enemyMorphReleaseBurstStarted_ = false;
    bool enemyMorphReleaseExpired_ = false;
    float enemyMorphReleaseTimer_ = 0.0f;
    float enemyMorphReleaseDuration_ = 0.42f;
    float enemyMorphReleaseBurstTime_ = 0.11f;
    float enemyMorphReleaseParticleTimer_ = 0.0f;
    engine::Vector3 enemyMorphReleasePlayerScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 enemyMorphReleaseVisualScale_ = {0.5f, 0.5f, 0.5f};
    engine::Vector3 enemyMorphReleaseStartPosition_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 enemyMorphReleaseDirection_ = {0.0f, 0.0f, -1.0f};
    engine::Vector4 enemyMorphReleaseTint_ = {1.0f, 1.0f, 1.0f, 1.0f};
    std::unique_ptr<engine::Object3d> enemyMorphReleaseVisual_;
    float electricShockFeedbackTimer_ = 0.0f;
    float electricShockFeedbackEmitTimer_ = 0.0f;
    float electricShockFeedbackTotalDuration_ = 0.0f;
    float electricShockPendingInvincibleDuration_ = 0.0f;
    bool electricShockControlLocked_ = false;
    bool electricShockWasControlActive_ = true;
    engine::Vector3 electricShockLockedPosition_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 electricShockBaseScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 electricShockBaseRotation_ = {0.0f, 0.0f, 0.0f};
    std::unique_ptr<EffectObject3d> electricShockAuraEffect_;
    std::string savedMorphModelName_;
    std::string savedMorphTexturePath_;
    std::string savedMorphAnimName_;
    bool savedMorphAnimLoop_ = true;
    engine::Vector4 savedMorphColor_ = {1.0f, 1.0f, 1.0f, 1.0f};
    engine::Vector3 savedMorphScale_ = {1.0f, 1.0f, 1.0f};
    int savedMorphMaterialType_ = 0;
    float savedMorphEmissive_ = 1.0f;
    engine::Vector4 enemyMorphTint_ = {1.0f, 1.0f, 1.0f, 1.0f};
    std::vector<bool> savedMorphChildVisible_;
    bool savedMorphSourceVisible_ = true;
    BaseEnemy* pendingAbsorbEnemy_ = nullptr;
    EnemyMorphType pendingAbsorbType_ = EnemyMorphType::None;
    engine::Vector3 pendingAbsorbEnemyBaseScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 pendingAbsorbEnemyStartPos_ = {0.0f, 0.0f, 0.0f};
    engine::Vector4 pendingAbsorbTint_ = {1.0f, 1.0f, 1.0f, 1.0f};
    engine::Vector4 pendingAbsorbPlayerBaseColor_ = {1.0f, 1.0f, 1.0f, 1.0f};
    float absorbEffectTimer_ = 0.0f;
    float absorbEffectEmitTimer_ = 0.0f;
    bool absorbEffectActive_ = false;

    // コピー能力の開始、更新、解除。元の姿と敵モデルの表示状態を復元します。
    /// <summary>対象の敵からコピー能力を取得し、変身の開始処理を行います。</summary>
    void StartEnemyMorph(BaseEnemy* enemy);
    /// <summary>変身前のモデル、色、姿勢とコピー元の表示状態を保存します。</summary>
    void SaveEnemyMorphBaseAppearance(bool sourceVisible);
    /// <summary>進行方向を適用して変身開始を確定します。</summary>
    void CompleteEnemyMorphStart(float moveYaw);
    /// <summary>変身開始時の粒子と光の演出を発生させます。</summary>
    void EmitEnemyMorphStartEffects();
    /// <summary>コピー能力の残り時間と変身中の表示を更新します。</summary>
    void UpdateEnemyMorph(float deltaTime);
    /// <summary>コピー能力の解除演出を開始します。</summary>
    /// <param name="expired">時間切れによる解除の場合は true。</param>
    void BeginEnemyMorphRelease(bool expired);
    /// <summary>解除演出を進め、完了時に元の姿へ戻します。</summary>
    void UpdateEnemyMorphRelease(float deltaTime);
    /// <summary>解除演出の瞬間的な粒子を一度だけ発生させます。</summary>
    void EmitEnemyMorphReleaseBurst();
    /// <summary>保存していたプレイヤーとコピー元の見た目を復元します。</summary>
    void RestoreEnemyMorphAppearance();
    /// <summary>変身解除演出の一時状態を初期化します。</summary>
    void ResetEnemyMorphRelease();
    /// <summary>進行中の変身を取り消し、保存済みの姿を復元します。</summary>
    void CancelEnemyMorph();
    /// <summary>指定した敵に吸い込み演出が必要か判定します。</summary>
    bool ShouldPlayAbsorbEffect(BaseEnemy* enemy) const;
    /// <summary>対象の可視状態を退避し、吸い込み演出を開始します。</summary>
    void BeginAbsorbEffect(BaseEnemy* enemy);
    /// <summary>吸い込み対象の移動と演出時間を更新します。</summary>
    void UpdateAbsorbEffect(float deltaTime);
    /// <summary>吸い込み演出を完了させ、コピー能力の取得へ進めます。</summary>
    void FinishAbsorbEffect();
    /// <summary>吸い込み演出を取り消し、必要なら対象の表示を復元します。</summary>
    void CancelAbsorbEffect(bool restoreEnemy);
    /// <summary>変身モデル固有のY回転補正を返します。</summary>
    float GetEnemyMorphModelYawOffset() const;
    /// <summary>敵の種別名をリプレイ互換の変身種別へ変換します。</summary>
    EnemyMorphType ResolveEnemyMorphType(const std::string& enemyType) const;
    /// <summary>変身種別に対応する色を返します。</summary>
    engine::Vector4 GetEnemyMorphTint(EnemyMorphType type) const;
    /// <summary>現在の変身種別に対応する敵種別名を返します。</summary>
    std::string GetEnemyTypeForCurrentMorph() const;
    /// <summary>チェックポイント復帰用に現在のコピー能力を保存します。</summary>
    void RefreshCheckpointMorphSnapshot();

  public:
    /// <summary>フック位置を示す補助オブジェクトを返します。所有権は移しません。</summary>
    engine::Object3d* GetHookMarker() const { return hookMarker_.get(); }

  private:
    // ギミック同期用
    uint32_t jumpCount_ = 0;
};
