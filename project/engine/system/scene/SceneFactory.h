#pragma once
#include "AbstractSceneFactory.h"
#include <memory>

/// <summary>
/// このゲームで使う具体的なシーンを名前から生成するファクトリ。
/// </summary>
class SceneFactory : public AbstractSceneFactory {
public:
    /// <summary>
    /// sceneName に対応するシーンインスタンスを生成する。
    /// </summary>
    std::unique_ptr<BaseScene> CreateScene(const std::string& sceneName) override;
};
