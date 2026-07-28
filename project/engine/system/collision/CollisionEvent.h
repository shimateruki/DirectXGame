#pragma once

#include "CollisionConfig.h"

class Object3d;

// 衝突イベントが接触の開始、継続、終了のどの段階かを表します。
enum class CollisionEventPhase {
    Enter,
    Stay,
    Exit,
};

// Physics WorldからObject3dへ通知する衝突情報です。
// normalはselfをotherから押し戻す向きです。
struct CollisionEvent {
    Object3d* self = nullptr;
    Object3d* other = nullptr;
    CollisionInfo collision;
    CollisionEventPhase phase = CollisionEventPhase::Enter;
    bool isTrigger = false;
};
