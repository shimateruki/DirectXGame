#pragma once

#include "BaseScene.h"
#include <string>

// シーンを生成するファクトリのインターフェース
class ISceneFactory {
public:
    virtual ~ISceneFactory() = default;

    /// <summary>
    /// シーン生成
    /// </summary>
    /// <param name="sceneName">シーン名</param>
    /// <returns>生成されたシーン</returns>
    virtual BaseScene* CreateScene(const std::string& sceneName) = 0;
};