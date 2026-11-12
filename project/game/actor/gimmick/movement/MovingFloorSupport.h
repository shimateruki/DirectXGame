#pragma once

// 動く床の上面接触と乗員の追従を共通化する。床ごとの移動経路や発火条件は各ギミックが管理する。
#include "Character.h"
#include "GimmickMovingFloor.h"
#include <cmath>

// 原点ではなく当たり判定の足元で乗車を判定する。
// 前フレーム末尾の押し戻しは行列へ未反映の場合があるため、先に更新する。
inline bool IsStandingOnMovingFloor(engine::Object3D& floor, Character& actor) {
    // 上昇中や死亡中は足元が重なっても床に乗っている扱いにしない。
    if (actor.IsDead() || !actor.IsGrounded() || actor.GetVelocity().y > 0.1f)
        return false;
    floor.UpdateLocalMatrix();
    floor.UpdateWorldMatrix(false);
    actor.UpdateLocalMatrix();
    actor.UpdateWorldMatrix(false);
    const auto deck = floor.GetAabb();
    const auto body = actor.GetAabb();
    const float gap = body.min.y - deck.max.y;
    if (gap < -0.2f || gap > 0.2f || body.max.x <= deck.min.x + 0.02f ||
        body.min.x >= deck.max.x - 0.02f || body.max.z <= deck.min.z + 0.02f ||
        body.min.z >= deck.max.z - 0.02f) return false;
    const Collider* floorCollider = floor.GetCollider();

    // 多角形フットプリントがある床はAABB通過後に実形状でも確認する。
    if (floorCollider && floorCollider->GetConfig().footprint.size() >= 3) {
        return floorCollider->OverlapsFootprint(actor.GetCollider());
    }
    return true;
}
