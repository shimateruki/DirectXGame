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
#include <vector>

class IMoveStrategy; // 前方宣言

// プレイヤーの残像の1フレーム分を管理する構造体
struct GhostTrail {
    std::vector<Object3d*> parts;
    std::vector<Matrix4x4> baseMatrices;
    float alpha = 1.0f;
    float life = 1.0f;
    float maxLife = 1.0f;
};

// プレイヤーの攻撃パラメータ（JSON保存・ImGui調整用）
struct PlayerAttackParams {
    float damageCombo1 = 10.0f;
    float damageCombo2 = 15.0f;
    float damageCombo3 = 30.0f;
    float damagePlunge = 20.0f;

    // JSON変換用
    void ToJson(json& j) const {
        j["damageCombo1"] = damageCombo1;
        j["damageCombo2"] = damageCombo2;
        j["damageCombo3"] = damageCombo3;
        j["damagePlunge"] = damagePlunge;
    }
    void FromJson(const json& j) {
        if (j.contains("damageCombo1")) damageCombo1 = j["damageCombo1"];
        if (j.contains("damageCombo2")) damageCombo2 = j["damageCombo2"];
        if (j.contains("damageCombo3")) damageCombo3 = j["damageCombo3"];
        if (j.contains("damagePlunge")) damagePlunge = j["damagePlunge"];
    }
};

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
    IAnimationState* GetState() const { return state_.get(); }
    void PlayAnimation(const std::string& animName, bool loop = true);

    // ==================================================
    // 移動制御 (Strategy Pattern)
    // ==================================================
    void SetMoveStrategy(std::unique_ptr<IMoveStrategy> strategy);

    // ==================================================
    // 残像（Ghost Trail）関連
    // ==================================================
    void CreateGhostTrail();

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
    void SetIsControlActive(bool isActive) { isControlActive_ = isControlLocked_ ? false : isActive; }
    void SetControlLock(bool isLocked)
    {
        isControlLocked_ = isLocked;
        if (isLocked) {
            isControlActive_ = false;
        }
    }
    void SetIsPhysicsActive(bool active) { isPhysicsActive_ = active; }
    bool GetIsControlActive() const { return isControlActive_; }
    bool IsPhysicsActive() const { return isPhysicsActive_; }

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
    bool IsDeathSequenceActive() const { return param_.has_value() && param_->hp <= 0.0f; }
    bool IsInvincible() const { return isDamageInvincible_ || isDashInvincible_ || IsDeathSequenceActive(); }
    float GetHp() const { return param_.has_value() ? param_->hp : 100.0f; }
    float GetMaxHp() const { return param_.has_value() ? param_->maxHp : 100.0f; }
    float GetDeathTimer() const { return deathTimer_; }
    void SetForceLockOnTarget(Object3d* target) { forceLockOnTarget_ = target; }
    Object3d* GetForceLockOnTarget() const { return forceLockOnTarget_; }

    // --- 回避（ダッシュ）のクールタイム割合を取得 (1.0 = 準備完了) ---
    float GetDashCooldownRatio() const;

    // --- 攻撃方向の保存（吹き飛ばし用） ---
    void SetAttackDirection(const Vector3& dir) { attackDirection_ = dir; }
    Vector3 GetAttackDirection() const { return attackDirection_; }

    // --- 攻撃パラメータ管理 ---
    PlayerAttackParams& GetAttackParams() { return attackParams_; }
    void LoadAttackParams();
    void SaveAttackParams();

    // ロックオンの強制解除要求
    void RequestClearLockOn() { requestClearLockOn_ = true; }
    bool ConsumeClearLockOnRequest() {
        bool r = requestClearLockOn_;
        requestClearLockOn_ = false;
        return r;
    }

    // --- SE用オーディオハンドルアクセッサ ---
    uint32_t GetSEAvoidHandle() const { return seAvoidHandle_; }
    uint32_t GetSEJumpHandle() const { return seJumpHandle_; }
    uint32_t GetSEMoveHandle() const { return seMoveHandle_; }
    uint32_t GetSESwingMiss1Handle() const { return seSwingMiss1Handle_; }
    uint32_t GetSESwingMiss2Handle() const { return seSwingMiss2Handle_; }
    uint32_t GetSESwordHandle() const { return seSwordHandle_; }
    uint32_t GetSEDownAttack1Handle() const { return seDownAttack1Handle_; }
    uint32_t GetSEDownAttack2Handle() const { return seDownAttack2Handle_; }


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
    bool isPhysicsActive_ = true;    // 物理演算・移動計算を行うか（座標固定用）
    bool isControlLocked_ = false;

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
    bool hasSavedInvincibleColors_ = false;
    float damageCooldownTimer_ = 0.0f;
    bool isDamageInvincible_ = false;
    bool isDashInvincible_ = false;
    float deathTimer_ = 0.0f;    // 死亡してからの経過時間
    Object3d* forceLockOnTarget_ = nullptr; // 強制ロックオン対象
    bool requestClearLockOn_ = false;       // ロックオン解除要求フラグ
    Vector3 attackDirection_ = { 0,0,1 };   // 攻撃開始時の向き
    PlayerAttackParams attackParams_;       // 攻撃力などのパラメータ
    
    // --- SE用オーディオハンドル ---
    uint32_t seAvoidHandle_ = 0;
    uint32_t seJumpHandle_ = 0;
    uint32_t seMoveHandle_ = 0;
    uint32_t seSwingMiss1Handle_ = 0;
    uint32_t seSwingMiss2Handle_ = 0;
    uint32_t seSwordHandle_ = 0;
    uint32_t seDownAttack1Handle_ = 0;
    uint32_t seDownAttack2Handle_ = 0;
    uint32_t seDamageHandle_ = 0;

    void UpdateColor();

public:
    // --- 残像関連 ---
    std::vector<GhostTrail> ghostTrails_;
    float ghostTimer_ = 0.0f;

    std::vector<std::unique_ptr<Object3d>> ghostPool_;
    int ghostPoolIndex_ = 0;
};
