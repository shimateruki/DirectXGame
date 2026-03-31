#pragma once
#include "Character.h"
#include "InputManager.h"
#include "ParticleSystem.h"
#include "PlayerMover.h"
#include "IAnimationState.h" 
#include "engine/utility/math/Math.h" 
#include <memory>
#include <string>
#include <unordered_map>

class IMoveStrategy; // 前方宣言

// プレイヤーキャラクターの統合制御クラス
class Player : public Character
{
public:
    // ==================================================
    // 基本サイクル
    // ==================================================
    void Initialize(Object3dCommon* common, InputManager* inputManager, ParticleSystem* particleSystem);
    void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;

    // ==================================================
    // 衝突判定
    // ==================================================
    bool OnCollision(Object3d* other) override;

    // ==================================================
    // アニメーション・状態管理 (State Pattern)
    // ==================================================
    void ChangeState(std::unique_ptr<IAnimationState> newState);
    void PlayAnimation(const std::string& animName, bool loop = true);

    // ==================================================
    // 移動制御 (Strategy Pattern)
    // ==================================================
    void SetMoveStrategy(std::unique_ptr<IMoveStrategy> strategy);

    // ==================================================
    // アクセッサ
    // ==================================================
    // --- 物理・姿勢 ---
    Vector3 GetVelocity() const { return velocity_; }
    void SetVelocity(const Vector3& v) { velocity_ = v; }

    Vector3 GetRotation() const { return transform_.rotate; }

    // SetRotation は Euler を受け取り、内部的にクォータニオンを更新するように修正
    void SetRotation(const Vector3& r)
    {
        transform_.rotate = r;
        transform_.quaternion = Math::EulerToQuaternion(r);
        transform_.isQuaternionMaster = true;
    }

    // Y軸のみ更新するユーティリティも、クォータニオンを更新する
    void SetRotationY(float y)
    {
        transform_.rotate.y = y;
        transform_.quaternion = Math::EulerToQuaternion(transform_.rotate);
        transform_.isQuaternionMaster = true;
    }

    // --- 状態フラグ ---
    void SetLockOn(bool isLockingOn) { isLockingOn_ = isLockingOn; }
    bool IsLockingOn() const { return isLockingOn_; }
    void SetIsControlActive(bool isActive) { isControlActive_ = isActive; }

    // --- コンボ：次のクリックで2段目を出すためのフラグ操作 ---
    void SetPendingAttack2(bool pending) { pendingAttack2_ = pending; }
    bool ConsumePendingAttack2() { bool v = pendingAttack2_; pendingAttack2_ = false; return v; }

    // コンボ「時間窓」API
    void StartComboWindow(float duration);
    bool IsComboWindowActive() const;

    // --- 入力バッファ（Run→Attack 遷移での踏み逃がし防止） ---
    void RecordAttackInput(float duration);               // 攻撃入力を短時間バッファする
    void MarkAttackBufferUsedForStateStart();             // そのバッファを「遷移開始で使われた」とマークする
    bool ConsumeBufferedAttackInput();                    // バッファに未使用の入力があれば消費して true を返す

    // --- 各種パラメータ取得 ---
    InputManager* GetInputManager() { return inputManager_; }

    float GetJumpPower() const
    {
        return param_.has_value() ? param_->jumpPower : 10.0f;
    }

    float GetMoveSpeed() const
    {
        return param_.has_value() ? param_->speed : 0.5f;
    }
    // ==================================================
        // 無敵フレーム (Invincibility) 管理
        // ==================================================
    void SetDamageInvincible(bool inv); // ダメージ被弾時のフラグ
    void SetDashInvincible(bool inv);   // 回避ダッシュ時のフラグ

    // どっちか一つでもtrueなら無敵として扱う
    bool IsInvincible() const { return isDamageInvincible_ || isDashInvincible_; }
    float GetHp() const { return param_.has_value() ? param_->hp : 100.0f; }
    float GetMaxHp() const { return param_.has_value() ? param_->maxHp : 100.0f; }
    float GetDeathTimer() const { return deathTimer_; }

    // ==================================================
    // 追加: ジャンプ判定フラグ（落下攻撃を出せるか）
    // ジャンプ状態に入ったときに true にし、着地または落下攻撃開始で false にする
    // ==================================================
    void SetHasJumped(bool v) { hasJumped_ = v; }
    bool HasJumped() const { return hasJumped_; }

private:
    // --- 内部コンポーネント ---
    std::unique_ptr<PlayerMover> mover_ = nullptr;            // 移動処理の委譲先
    std::unique_ptr<IAnimationState> state_ = nullptr;        // 現在のアクション状態

    // --- 外部システム参照 ---
    InputManager* inputManager_ = nullptr;
    ParticleSystem* particleSystem_ = nullptr;

    // --- プレイヤー状態フラグ ---
    bool isLockingOn_ = false;       // 敵をロックオンしているか
    bool isControlActive_ = true;    // 入力を受け付ける状態か（デモシーン等で制限用）

    // コンボ待ちフラグ：Attack1 終了後に次のクリックで Attack2 を出すために使う
    bool pendingAttack2_ = false;

    // --- コンボ時間窓 ---
    float comboWindowTimer_ = 0.0f; // >0 の間、次の攻撃入力は 2 段目に変換される

    // --- 攻撃入力バッファ（Run→Attack の踏み逃がし防止） ---
    bool attackInputBuffered_ = false;               // バッファに入力があるか
    bool attackBufferUsedForStateStart_ = false;    // 「そのバッファが遷移開始で使われた」フラグ
    float attackInputBufferTimer_ = 0.0f;           // バッファの残り時間（秒）

    // --- 無敵関連 ---
    bool isInvincible_ = false;
    Vector4 savedColor_ = { 1.0f, 1.0f, 1.0f, 1.0f }; // 無敵解除時に戻す色

    // 子パーツの色を保存しておくマップ（無敵解除時に復元）
    std::unordered_map<Object3d*, Vector4> childSavedColors_;
    float damageCooldownTimer_ = 0.0f;
    bool isDamageInvincible_ = false;
    bool isDashInvincible_ = false;
    float deathTimer_ = 0.0f;    // 死亡してからの経過時間

    // --- ジャンプ判定フラグ ---
    bool hasJumped_ = false;

    void UpdateColor();
};