#pragma once

#include "ISceneFactory.h"

// ISceneFactoryを継承した、具体的なシーンファクトリ
class SceneFactory : public ISceneFactory {
public:
    /// <summary>
    /// シーン生成
    /// </summary>
    /// <param name="sceneName">シーン名</param>
    /// <returns>生成されたシーン</returns>
    BaseScene* CreateScene(const std::string& sceneName) override;
};