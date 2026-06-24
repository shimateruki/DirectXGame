#pragma once
#include "BaseEnemy.h"

// 大きく跳ねて着地衝撃波を出し、フックで小型スライムへ分裂する大型敵
class EnemyGiantSlime : public BaseEnemy {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    std::unique_ptr<Object3d> Clone() const override;
    void BeginHookSplitPull(const Vector3& hookOwnerPos);
    bool UpdateHookSplitPull(float deltaTime, const Vector3& hookOwnerPos, class ParticleSystem* particleSystem);
    void CancelHookSplitPull();
    float GetHookSplitProgress() const;
    bool HasSplit() const { return hasSplit_; }

private:
    // ジャンプ攻撃、着地衝撃波、フック分裂を役割ごとに分ける
    void LaunchJump(const Vector3& direction, float distance);
    void DispatchLandingShockwave();
    void ApplySlimeAnimation(float deltaTime);
    void SplitIntoSmallSlimes(class ParticleSystem* particleSystem);

    float jumpTimer_ = 0.0f;          // 次の跳躍までの待ち時間。
    float landingPulseTimer_ = 0.0f;  // 着地時の潰れ演出。
    float idleTimer_ = 0.0f;          // 待機呼吸アニメーション用。
    float hookSplitPullTimer_ = 0.0f; // フック分裂の進行度。
    bool isJumpingAttack_ = false;    // 攻撃ジャンプ中か。
    bool isHookSplitPulled_ = false;  // フックで引っ張られているか。
    bool hasSplit_ = false;           // 分裂済みか。
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 hookSplitBasePosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 hookSplitBaseScale_ = { 1.0f, 1.0f, 1.0f };
    bool hasBaseScale_ = false;
};
