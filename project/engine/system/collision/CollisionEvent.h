#pragma once

#include "CollisionConfig.h"

namespace engine { class Object3D; }

/// <summary>接触の開始、継続、終了を区別します。</summary>
enum class CollisionEventPhase {
    Enter,
    Stay,
    Exit,
};

/// <summary>Physics World から Object3D へ通知する衝突情報です。</summary>
// 関連：CollisionManager（生成）、Object3D（通知先）
struct CollisionEvent {
    // 通知対象と相手は衝突管理中の借用参照。イベントを跨いで所有権として保持しない。
    engine::Object3D* self = nullptr;
    engine::Object3D* other = nullptr;
    CollisionInfo collision; // normal は self を other から押し戻す向きです。
    CollisionEventPhase phase = CollisionEventPhase::Enter;
    bool isTrigger = false;
};
