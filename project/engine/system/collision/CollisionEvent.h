#pragma once

#include "CollisionConfig.h"

class Object3d;

/// <summary>接触の開始、継続、終了を区別します。</summary>
enum class CollisionEventPhase {
    Enter,
    Stay,
    Exit,
};

/// <summary>Physics World から Object3d へ通知する衝突情報です。</summary>
struct CollisionEvent {
    Object3d* self = nullptr;
    Object3d* other = nullptr;
    CollisionInfo collision; // normal は self を other から押し戻す向きです。
    CollisionEventPhase phase = CollisionEventPhase::Enter;
    bool isTrigger = false;
};
