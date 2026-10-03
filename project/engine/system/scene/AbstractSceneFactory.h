#pragma once
#include "BaseScene.h"
#include <memory>
#include <string>
#include <vector>

/// <summary>SceneManagerが具体型を知らずにシーン名から生成するための抽象ファクトリ。</summary>
class AbstractSceneFactory {
public:
    virtual ~AbstractSceneFactory() = default;

    /// <summary>sceneNameに対応するシーンを生成して返す。</summary>
    virtual std::unique_ptr<BaseScene> CreateScene(const std::string& sceneName) = 0;

    // EditorのScene切り替えやScene Asset設定へ、登録済み実行クラス名を公開します。
    virtual std::vector<std::string> GetRegisteredSceneNames() const = 0;
};
