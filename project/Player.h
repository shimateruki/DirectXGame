#pragma once
#include "Character.h" // 親クラスをCharacterに変更

// Characterを継承したPlayerクラス
class Player : public Character {
public:
    // Player独自の初期化や更新は、必要ならここでオーバーライド
     void Initialize(Object3dCommon* common) override;
     void Update() override;

    /// <summary>
    /// 衝突時に呼び出される関数 (親の関数をオーバーライド)
    /// </summary>
    void OnCollision(Object3d* other) override;
protected:
    InputManager* inputManager_ = nullptr;

};