#pragma once
#include "Character.h" // 親クラスをCharacterに変更
#include "engine/io/InputManager.h"   // InputManager をインクルード

// Characterを継承したPlayerクラス
class Player : public Character {
public:
    void Initialize(Object3dCommon* common) override;
    void Update() override;

    /// <summary>
    /// 衝突時に呼び出される関数 (親の関数をオーバーライド)
    /// </summary>
    void OnCollision(Object3d* other) override;

private:
    InputManager* inputManager_ = nullptr;
};