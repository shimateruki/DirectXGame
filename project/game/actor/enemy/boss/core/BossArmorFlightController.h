#pragma once
#include "engine/utility/math/Math.h"
#include <cstddef>
#include <vector>

class Object3d;

// 装甲の所有権はシーンに残し、射出後の運動と帰還待ち時間だけを管理します。
class BossArmorFlightController {
  public:
    using RestoreOrbit = void (*)(Object3d* block, int originalIndex);
    // 装甲を借用し、飛行リストへ登録する。originalIndex は帰還時の元の並び順。
    void Queue(Object3d* block, const Vector3& rotation, int originalIndex);
    // 飛行・地面待機・帰還を経過秒数で進め、回収時に restoreOrbit を呼ぶ。
    // owner と target も借用し、飛行中は装甲 Object をシーン側で生存させる。
    void Update(Object3d& owner, Object3d* target, std::size_t armorCount, float deltaTime, RestoreOrbit restoreOrbit);
    bool ContainsIndex(std::size_t index) const;
    bool Empty() const { return flyingBlocks_.empty(); }
    // 従来のバリア破壊処理と同じく追跡だけを解除し、ブロックは削除しません。
    void Clear() { flyingBlocks_.clear(); }

  private:
    // シーン所有の装甲を借用し、飛翔・待機・帰還の進行と元の装甲番号を保持する。
    struct FlyingBlock {
        Object3d* block;
        Vector3 velocity;
        Vector3 currentRot;
        // 0:飛翔、1:地面待機、2:帰還、3:回収完了、4:頭上へ装填。
        int mode;
        int originalIndex;
    };
    std::vector<FlyingBlock> flyingBlocks_;
    float returnDelayTimer_ = 0.0f;
};
