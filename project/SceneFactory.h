#pragma once
#include "AbstractSceneFactory.h"
#include <memory> 

/// <summary>
/// このゲーム用のシーン工場（具象クラス）
/// </summary>
class SceneFactory : public AbstractSceneFactory {
public:
    /// <summary>
    /// シーンを生成する
    /// </summary>
    std::unique_ptr<BaseScene> CreateScene(const std::string& sceneName) override;
};