#pragma once

#include "ISceneFactory.h"

// ISceneFactory���p�������A��̓I�ȃV�[���t�@�N�g��
class SceneFactory : public ISceneFactory {
public:
    /// <summary>
    /// �V�[������
    /// </summary>
    /// <param name="sceneName">�V�[����</param>
    /// <returns>�������ꂽ�V�[��</returns>
    BaseScene* CreateScene(const std::string& sceneName) override;
};