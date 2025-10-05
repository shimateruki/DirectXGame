#pragma once

#include "BaseScene.h"
#include "InputManager.h"

// タイトルシーン
class TitleScene : public BaseScene {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    InputManager* inputManager_ = nullptr; // 入力検知のために保持
};