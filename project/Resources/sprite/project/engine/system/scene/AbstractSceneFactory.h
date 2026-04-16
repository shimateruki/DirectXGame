#pragma once
#include "BaseScene.h" 
#include <string>
#include <memory> 

/// <summary>
/// シーン工場（抽象クラス）
/// </summary>
class AbstractSceneFactory {
public:
    virtual ~AbstractSceneFactory() = default;

    /// <summary>
    /// シーンを生成する
    /// </summary>
    virtual std::unique_ptr<BaseScene> CreateScene(const std::string& sceneName) = 0;
};