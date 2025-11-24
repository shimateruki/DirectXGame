#pragma once
#include "Character.h" 
#include "InputManager.h"  
#include "IMoveStrategy.h" 
#include "ParticleSystem.h"
#include <memory>          

class Player : public Character {
public:
    void Initialize(Object3dCommon* common, InputManager* inputManager, ParticleSystem* particleSystem);
    void Update(float deltaTime) override;
    void Draw() override;
    

    /// <summary>
    /// 衝突時に呼び出される関数 
    /// </summary>
    bool OnCollision(Object3d* other) override;
    /// <summary>
    /// このプレイヤーの移動戦略（アルゴリズム）を設定する
    /// </summary>
    void SetMoveStrategy(std::unique_ptr<IMoveStrategy> strategy) {
        moveStrategy_ = std::move(strategy);
    }

 
    /// <summary>
    /// ロックオン状態を設定する 
    /// </summary>
    void SetLockOn(bool isLockingOn) { isLockingOn_ = isLockingOn; }
    bool IsLockingOn() const { return isLockingOn_; }
    InputManager* GetInputManager() { return inputManager_; }

private:
    InputManager* inputManager_ = nullptr;
    std::unique_ptr<IMoveStrategy> moveStrategy_ = nullptr;
    bool isLockingOn_ = false;
    ParticleSystem* particleSystem_ = nullptr;
    bool wasGrounded_ = false;
};