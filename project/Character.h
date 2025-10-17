#pragma once
#include "engine/3d/Object3d.h"
#include "engine/io/InputManager.h"

/// <summary>
/// 物理的な振る舞いを持つキャラクターの基底クラス
/// </summary>
class Character : public Object3d {
public:
    void Initialize(Object3dCommon* common) override;
    void Update() override;

    // 衝突応答の基本処理（押し戻し）を実装
    void OnCollision(Object3d* other) override;


};