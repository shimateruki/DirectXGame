#pragma once 
class Object3d;

/// <summary>
/// プレイヤーが何かに衝突したときに発行されるイベント
/// </summary>
struct PlayerHitEvent {
    Object3d* hitObject = nullptr; // 衝突した相手のオブジェクト
};

