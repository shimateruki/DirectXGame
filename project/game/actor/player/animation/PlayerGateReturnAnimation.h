#pragma once

#include "engine/utility/math/Math.h"
#include <cstdint>

class Player;

// クリア後にステージセレクトへ戻った時、ゲートからプレイヤーを飛び出させる専用アニメーション。
class PlayerGateReturnAnimation {
  public:
    // ゲート帰還の始点・通過点・終点と基準姿勢。通常移動を再開するまでの専用経路。
    struct Route {
        // 全点はワールド座標。direction は水平面の進行方向で、ゼロ長なら +Z を使う。
        engine::Vector3 start{};
        engine::Vector3 gateCenter{};
        engine::Vector3 end{};
        engine::Vector3 direction{0.0f, 0.0f, 1.0f};
        engine::Vector3 baseScale{2.0f, 2.0f, 2.0f};
    };

    // 操作と衝突判定を一時停止し、指定経路の先頭から演出を始める。player は所有しない。
    void Start(Player* player, const Route& route);
    // 経過秒数で姿勢を進める。完了時は衝突を戻すが、操作の復帰は呼び出し側が決める。
    void Update(Player* player, float deltaTime);
    // 途中終了時にも姿勢と衝突を戻す。restoreControl=true なら開始前の操作状態へ戻す。
    void Stop(Player* player, bool restoreControl);

    bool IsActive() const { return active_; }
    bool IsFinished() const { return finished_; }
    const engine::Vector3& GetEndPosition() const { return route_.end; }

  private:
    engine::Vector3 NormalizeDirection(const engine::Vector3& direction) const;
    engine::Vector3 Lerp(const engine::Vector3& a, const engine::Vector3& b, float t) const;
    engine::Vector3 Bezier(const engine::Vector3& p0, const engine::Vector3& p1, const engine::Vector3& p2, const engine::Vector3& p3, float t) const;
    float Clamp01(float value) const;
    float EaseOutCubic(float value) const;
    float EaseInOutCubic(float value) const;
    void ApplyPose(Player* player, float normalizedTime);
    void ApplyFinishedPose(Player* player, bool restoreControl);
    void SuspendCollision(Player* player);
    void RestoreCollision(Player* player);

    Route route_;
    float timer_ = 0.0f;
    bool active_ = false;
    bool finished_ = false;
    bool savedControlActive_ = true;
    uint32_t savedCollisionAttribute_ = 0;
    uint32_t savedCollisionMask_ = 0;
    bool collisionSuspended_ = false;
};
