#pragma once
#include "AbstractSceneFactory.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/// <summary>登録されたシーン名を対応するBaseScene派生クラスの生成関数へ結び付ける。</summary>
class SceneFactory : public AbstractSceneFactory {
public:
    using SceneCreator = std::function<std::unique_ptr<BaseScene>()>;

    SceneFactory();

    /// <summary>名前に対応するシーンインスタンスを生成する。未登録ならnullptr。</summary>
    std::unique_ptr<BaseScene> CreateScene(const std::string& sceneName) override;

    /// <summary>登録順にシーン名を返す。</summary>
    std::vector<std::string> GetRegisteredSceneNames() const override;

    // 新しいBaseScene派生クラスはここへ登録すると、Editorの候補へ自動反映されます。
    bool RegisterScene(const std::string& sceneName, SceneCreator creator);

private:
    std::unordered_map<std::string, SceneCreator> creators_;
    std::vector<std::string> registrationOrder_;
};
