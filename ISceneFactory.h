#pragma once

#include "BaseScene.h"
#include <string>

// �V�[���𐶐�����t�@�N�g���̃C���^�[�t�F�[�X
class ISceneFactory {
public:
    virtual ~ISceneFactory() = default;

    /// <summary>
    /// �V�[������
    /// </summary>
    /// <param name="sceneName">�V�[����</param>
    /// <returns>�������ꂽ�V�[��</returns>
    virtual BaseScene* CreateScene(const std::string& sceneName) = 0;
};