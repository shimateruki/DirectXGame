#pragma once
#include "AbstractSceneFactory.h"
#include <memory>

/// <summary>
/// このゲームで使う具体的なシーンを名前から生成するファクトリ。
/// </summary>
// SceneFactoryは、文字列のシーン名を実際のシーンクラス生成へ変換するファクトリーです。
class SceneFactory : public AbstractSceneFactory {
public:
    /// <summary>
    /// sceneName に対応するシーンインスタンスを生成する。
    /// </summary>
        // 指定されたシーン名に対応するBaseScene派生クラスを生成します。
std::unique_ptr<BaseScene> CreateScene(const std::string& sceneName) override;
};
