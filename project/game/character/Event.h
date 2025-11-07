#pragma once 
#include"engine/base/Math.h"
class Object3d;

/// <summary>
/// プレイヤーが何かに衝突したときに発行されるイベント
/// </summary>
struct PlayerHitEvent {
    Object3d* hitObject = nullptr; // 衝突した相手のオブジェクト
    Vector3 normal = { 0,0,0 };
};

