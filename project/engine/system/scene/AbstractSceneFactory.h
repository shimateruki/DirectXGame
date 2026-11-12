#pragma once
#include "BaseScene.h"
#include <memory>
#include <string>
#include <vector>

// 担当：シーン名からBaseSceneを作る契約を定義する。
// 関連：SceneManager（利用）、SceneFactory（登録表による実装）
// 主な入口：CreateScene / GetRegisteredSceneNames
class AbstractSceneFactory {
public:
    virtual ~AbstractSceneFactory() = default;

    /// <summary>sceneNameに対応するシーンを生成して返す。</summary>
    virtual std::unique_ptr<BaseScene> CreateScene(const std::string& sceneName) = 0;

    // EditorのScene切り替えやScene Asset設定へ、登録済み実行クラス名を公開します。
    virtual std::vector<std::string> GetRegisteredSceneNames() const = 0;
};
