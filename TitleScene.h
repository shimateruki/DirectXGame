#pragma once

#include "BaseScene.h"
#include "InputManager.h"

// �^�C�g���V�[��
class TitleScene : public BaseScene {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    InputManager* inputManager_ = nullptr; // ���͌��m�̂��߂ɕێ�
};