#pragma once
#include "Character.h" 
#include "InputManager.h"  
// Characterを継承したPlayerクラス
class Player : public Character {
public:
    void Initialize(Object3dCommon* common, InputManager* inputManager);
    void Update() override;
    void Draw() override;
    

    /// <summary>
    /// 衝突時に呼び出される関数 
    /// </summary>
    bool OnCollision(Object3d* other) override;

private:
    InputManager* inputManager_ = nullptr;
};