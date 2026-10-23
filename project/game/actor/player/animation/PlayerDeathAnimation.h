#pragma once

#include "engine/utility/math/Math.h"
#include <vector>

class Player;

// プレイヤー死亡時の見た目だけを担当するアニメーション制御です。
// 状態遷移、残機表示、暗転は PlayerStateDead 側に残し、ここでは
// 「ヒット停止 -> ふくらみ -> 破裂 -> 飛沫」の流れに集中します。
class PlayerDeathAnimation {
  public:
    // 表示と接触の元状態を退避し、死亡演出を途中で止めても復元できるようにする。
    void Start(Player* player, bool finalDeath);

    // 破裂前の予兆と破裂後のフェードを、同じタイマー上で進めます。
    void Update(Player* player, float deltaTime);

    // 復活時に死亡演出が変更した表示状態を戻します。
    void RestoreVisual(Player* player);

    bool IsReadyForFade() const;

    bool IsFinished() const;

  private:
    // ヒットストップから膨張へ移る間の形状を、共通の演出時計で決める。
    void ApplyPreBurstPose(Player* player);

    // 一時死亡と最終死亡で粒子数と色を変え、身体が消える瞬間を強調する。
    void EmitBurst(Player* player);

    // 足元の低い位置だけに少量の粒子を出し、飛散後の余韻を作る。
    void EmitPuddle(Player* player);

    // 親だけでなく子の表示も消し、装飾が残るのを防ぐ。
    void HidePlayerBody(Player* player);

    float timer_ = 0.0f;
    float puddleEmitTimer_ = 0.0f;

    bool active_ = false;
    bool finalDeath_ = false;
    bool burstEmitted_ = false;

    bool baseVisible_ = true;

    bool collisionStored_ = false;
    uint32_t baseCollisionAttribute_ = 0;
    uint32_t baseCollisionMask_ = 0xFFFFFFFF;

    engine::Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 baseRotation_ = {0.0f, 0.0f, 0.0f};

    std::vector<bool> childVisible_;
};
