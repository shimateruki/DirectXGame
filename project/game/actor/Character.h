#pragma once
#include "InputManager.h"
#include "Object3D.h"
#include "CharacterMotor.h"

/// <summary>
/// 速度、重力、接地判定など、物理的に動くキャラクターの基底クラス。
/// </summary>
// Characterは、重力、速度、接地判定、物理衝突補正を持つ移動キャラクターの基底クラスです。
// 関連：Object3dの表示・衝突情報を使い、移動の衝突補正はCharacterMotorへ任せる。
// 主な入口：Update / OnCollision / SimulateMotorStep
class Character : public engine::Object3D {
  public:
    /// <summary>衝突相手から受ける押し戻しなどの物理補正を適用する。</summary>
    /// <returns>衝突を処理した場合はtrue。</returns>
    bool OnCollision(engine::Object3D* other) override;

    /// <summary>
    /// 速度と重力を反映して座標を更新する。
    /// </summary>
    // 速度、重力、接地状態を反映してTransformを更新します。
    void Update(float deltaTime) override;

    /// <summary>ライト情報を使ってキャラクター本体を描画する。</summary>
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;

    /// <summary>現在の移動速度を取得・設定する。</summary>
    engine::Vector3 GetVelocity() const { return velocity_; }

    void SetVelocity(const engine::Vector3& v) { velocity_ = v; }

    /// <summary>速度の指定軸だけを更新する。他軸は維持する。</summary>
    float SetVelocityX(float value) { return velocity_.x = value; }
    float SetVelocityY(float value) { return velocity_.y = value; }
    float SetVelocityZ(float value) { return velocity_.z = value; }

    /// <summary>ダメージ状態へ移らず、指定時間だけ移動へ外力を重ねる。</summary>
    void ApplyExternalImpulse(const engine::Vector3& velocity, float duration = 0.16f);

    /// <summary>最後の移動更新での接地状態を取得・設定する。</summary>
    bool IsGrounded() const { return isGrounded_; }

    void SetGrounded(bool grounded) { isGrounded_ = grounded; }

    /// <summary>衝突と移動を計算するCharacterMotorを可変または読み取り専用で返す。</summary>
    CharacterMotor& GetCharacterMotor() { return characterMotor_; }

    const CharacterMotor& GetCharacterMotor() const { return characterMotor_; }

    /// <summary>
    /// このキャラクターに適用する重力を設定する。
    /// </summary>
    void SetGravity(float gravity);

    /// <summary>
    /// 最大落下速度を設定する。
    /// </summary>
    void SetMaxFallSpeed(float maxFallSpeed);

    /// <summary>衝突情報に基づいて押し戻しと速度補正を適用する。</summary>
    void ApplyPhysicsCollision(const CollisionInfo& info, uint32_t attribute);

    /// <summary>Character自身の配置と移動状態を複製する。</summary>
    /// <returns>派生型から呼ばれた場合はnullptr。派生側でCloneを実装する。</returns>
    std::unique_ptr<engine::Object3D> Clone() const override;

  protected:
    /// <summary>速度や接地などCharacter固有の状態をリプレイ用JSONへ書く。</summary>
    void CaptureReplayCustomState(json& state) const override;

    /// <summary>CharacterMotorの設定を現在のキャラクターパラメータへ同期する。</summary>
    void SyncCharacterMotorSettings();

    /// <summary>演出が座標を直接制御するフレームでは重力とMotor移動を止める。</summary>
    /// <summary>速度・接地状態を所有したまま、一回分の重力と衝突移動を進める。</summary>
    void SimulateMotorStep(engine::Vector3& position, float gravity, float maxFallSpeed, float deltaTime);

    void SetMotorSuspended(bool suspended) { motorSuspended_ = suspended; }

    /// <summary>リプレイ用JSONからCharacter固有の状態を戻す。</summary>
    void RestoreReplayCustomState(const json& state) override;

private:
    engine::Vector3 velocity_ = engine::Vector3::ZERO; // 現在速度。
    bool isGrounded_ = false;               // 最後の更新で接地しているか。
    bool motorSuspended_ = false;            // 演出が座標を直接決めるフレームだけ有効。

private:
    engine::Vector3 externalImpulseVelocity_ = engine::Vector3::ZERO;
    float externalImpulseTimer_ = 0.0f;
    float externalImpulseDuration_ = 0.0f;
    CharacterMotor characterMotor_;
    bool externalImpulseVerticalPending_ = false;
};
