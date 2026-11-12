#pragma once

#include "engine/utility/math/Math.h"
#include <cstdint>

class Player;

// クリア後にステージセレクトへ戻った時、ゲートからプレイヤーを飛び出させる専用アニメーション。
// 関連：シーンがゲートと経路を渡し、Playerの復帰動作と姿勢を同期する。
// 主な入口：Start / Update / Stop
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
    // 経路の向きは水平面に限定し、ゼロ入力には前方向を使う。
    engine::Vector3 NormalizeDirection(const engine::Vector3& direction) const;

    // 経路の端を越えないよう、補間率を0〜1に制限する。
    engine::Vector3 Lerp(const engine::Vector3& a, const engine::Vector3& b, float t) const;

    // 四点の三次Bezierでゲートへの曲線経路を作る。
    engine::Vector3 Bezier(const engine::Vector3& p0, const engine::Vector3& p1, const engine::Vector3& p2, const engine::Vector3& p3, float t) const;

    // 演出時間を経路の定義域へ収める。
    float Clamp01(float value) const;

    // 終点近くで減速する三次曲線を適用する。
    float EaseOutCubic(float value) const;

    // 前半を加速、後半を減速させて切替点の速度をつなぐ。
    float EaseInOutCubic(float value) const;

    // normalizedTimeは経路の進行率（0〜1）。Playerの座標と見た目を同じ時刻で更新する。
    void ApplyPose(Player* player, float normalizedTime);

    // 演出中の傾きと移動を消し、経路の終点へ確定させる。
    void ApplyFinishedPose(Player* player, bool restoreControl);

    // 初回だけ属性・マスクを退避して衝突を止める。復帰時はRestoreCollisionと対にする。
    void SuspendCollision(Player* player);

    // 開始前に保存した属性とマスクだけを戻す。
    void RestoreCollision(Player* player);

    Route route_;
    float timer_ = 0.0f;
    bool active_ = false;
    bool finished_ = false;
    bool savedControlActive_ = true; // 演出開始前の操作許可。Stopで復帰する際に使う。
    uint32_t savedCollisionAttribute_ = 0; // 一時停止する前の衝突属性。
    uint32_t savedCollisionMask_ = 0; // 一時停止する前の衝突マスク。
    bool collisionSuspended_ = false; // 衝突を停止しており、まだ復元していないか。
};
