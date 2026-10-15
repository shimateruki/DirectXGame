#pragma once

#include "engine/utility/math/Math.h"
#include "engine/animation/AnimatorController.h"
#include "engine/animation/BodyAnimationClip.h"

#include <string>
#include <unordered_map>

class Player;

// 通常移動とコピー能力の姿勢を合成し、Player のスライムモデルへ適用します。
class PlayerSlimeAnimator {
  public:
    // 基本姿勢。コピー技の一時姿勢は AbilityPose で重ねます。
    enum class Mode { Idle, Run, Jump, Dash, AimHook, ShootHook, PullEnemy, Carry, GateReturn, Disabled };

    enum class AbilityPose {
        None,
        Guard,
        BlazeDash,
        SlowFall,
    };

    /// <summary>基準スケールと前フレームの姿勢を初期化します。</summary>
    /// <param name="player">設定時に現在姿勢を同期する対象。省略できます。</param>
    void Reset(const engine::Vector3& baseScale, Player* player = nullptr);
    void SetMode(Mode mode);
    Mode GetMode() const { return mode_; }
    // ワールド座標の進行方向。ゼロ長の場合は前方向を使う。
    void SetMotionDirection(const engine::Vector3& direction);
    // 引き寄せ演出の向きと進行率。進行率は 0～1 に丸める。
    void SetPullDirection(const engine::Vector3& direction);
    void SetPullProgress(float progress);
    // 溜め率と姿勢の強さは 0～1 に丸める。
    void SetJumpCharge(float chargeRate);
    void SetAbilityPose(AbilityPose pose, float strength = 1.0f);
    // 指定スケールへの短い反動を重ねる。duration の単位は秒。
    void TriggerImpulse(const engine::Vector3& scale, float duration);
    // Animation Workbenchで編集できる能力モーションを、通常移動とは別レイヤーで再生します。
    /// <summary>通常移動と別レイヤーで能力モーションを再生します。</summary>
    /// <param name="clipName">Animation Workbench で編集したクリップ名</param>
    /// <returns>クリップが見つかり、再生を開始できたか</returns>
    bool PlayAbilityMotion(const std::string& clipName, bool loop = false, float playbackSpeed = 1.0f,
                           float blendInDuration = 0.06f, float blendOutDuration = 0.10f);
    // 指定秒数で能力モーションをフェードアウトする。即時解除は ClearAbilityMotion を使う。
    void StopAbilityMotion(float blendOutDuration = 0.08f);
    // 再生状態を即時破棄する。player を渡した場合は描画用の変形も戻す。
    void ClearAbilityMotion(Player* player = nullptr);
    void Update(Player* player, float deltaTime);
    // 編集済み Controller と BodyClip を再読込する。失敗時は旧データを保持せず未読込状態にする。
    bool ReloadController();
    const AnimatorControllerAsset* GetControllerAsset() const {
        return controllerLoaded_ ? &controllerAsset_ : nullptr;
    }
    // エディターがゲーム本編の現在StateとParameterを確認するための参照です。
    AnimatorControllerRuntime* GetControllerRuntime() { return controllerLoaded_ ? &controllerRuntime_ : nullptr; }

  private:
    // 状態に応じたスケールと回転を計算し、作成済みクリップを優先して適用します。
    engine::Vector3 NormalizeOrForward(const engine::Vector3& direction) const;
    engine::Vector3 BuildModeScale(Player* player, float deltaTime) const;
    engine::Vector3 BuildModeRotation(Player* player) const;
    bool TryBuildAuthoredBodyPose(Player* player, engine::Vector3& scaleOut, engine::Vector3& rotationOut,
                                  engine::Vector3& visualOffsetOut) const;
    void UpdateAbilityMotion(float deltaTime, engine::Vector3& visualScale, engine::Vector3& visualRotation, engine::Vector3& visualOffset,
                             engine::Vector3& visualShear, float& shearPivotY);
    void ApplyAbilityPose(engine::Vector3& scale, engine::Vector3& rotation) const;
    void ReloadBodyClips();
    static const char* GetStateName(Mode mode);

    // 通常移動、着地反動、能力モーションの時間は独立して進めます。
    Mode mode_ = Mode::Idle;
    float modeTimer_ = 0.0f;
    float jumpChargeRate_ = 0.0f;
    float pullProgress_ = 0.0f;
    AbilityPose abilityPose_ = AbilityPose::None;
    float abilityPoseStrength_ = 0.0f;
    float impulseTimer_ = 0.0f;
    float impulseDuration_ = 0.0f;
    float modeTransitionTimer_ = 0.0f;
    bool modeTransitionCapturePending_ = false;
    engine::Vector3 modeTransitionStartScale_ = {2.0f, 2.0f, 2.0f};
    engine::Vector3 modeTransitionStartRotation_ = {0.0f, 0.0f, 0.0f};
    float jumpTakeoffStretchTimer_ = 0.0f;
    float landingHoldTimer_ = 0.0f;
    float landingSquashTimer_ = 0.0f;
    float landingReboundTimer_ = 0.0f;
    float landingImpactRate_ = 1.0f;
    float previousVelocityY_ = 0.0f;
    bool wasGrounded_ = true;
    engine::Vector3 baseScale_ = {2.0f, 2.0f, 2.0f};
    engine::Vector3 motionDirection_ = {0.0f, 0.0f, 1.0f};
    engine::Vector3 pullDirection_ = {0.0f, 0.0f, 1.0f};
    engine::Vector3 impulseScale_ = {2.0f, 2.0f, 2.0f};
    // 読み込んだ Controller と BodyClip。ファイル再読み込み時に差し替えます。
    AnimatorControllerAsset controllerAsset_;
    AnimatorControllerRuntime controllerRuntime_;
    std::unordered_map<std::string, BodyAnimationClip> bodyClips_;
    BodyAnimationClip abilityMotionClip_;
    std::string abilityMotionName_;
    std::string abilityMotionClipAssetName_; // Stateの参照先変更後に同名攻撃を再読込するための実ファイル名。
    float abilityMotionTimer_ = 0.0f;
    float abilityMotionPlaybackSpeed_ = 1.0f;
    float abilityMotionBlendInDuration_ = 0.06f;
    float abilityMotionBlendOutDuration_ = 0.10f;
    float abilityMotionStopTimer_ = 0.0f;
    float abilityMotionStopDuration_ = 0.0f;
    bool abilityMotionActive_ = false;
    bool abilityMotionLoop_ = false;
    bool abilityMotionStopping_ = false;
    bool controllerLoaded_ = false;
    // LocomotionMode(Int) が定義されたAssetでは遷移条件をJSON側で評価します。
    bool useModeTransitions_ = false;
};
