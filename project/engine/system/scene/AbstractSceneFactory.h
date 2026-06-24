#pragma once
#include "BaseScene.h"
#include <memory>
#include <string>

/// <summary>
/// SceneManager が具体的なシーンクラスを知らずに生成するための抽象ファクトリ。
/// </summary>
class AbstractSceneFactory {
public:
    virtual ~AbstractSceneFactory() = default;

    /// <summary>
    /// sceneName に対応するシーンを生成する。
    /// </summary>
    virtual std::unique_ptr<BaseScene> CreateScene(const std::string& sceneName) = 0;
};
