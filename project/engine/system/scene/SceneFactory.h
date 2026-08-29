#pragma once
#include "AbstractSceneFactory.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/// 文字列のシーン名を、対応するBaseScene派生クラスへ変換するファクトリーです。
class SceneFactory : public AbstractSceneFactory {
public:
    using SceneCreator = std::function<std::unique_ptr<BaseScene>()>;

    SceneFactory();

    /// 指定されたシーン名に対応するBaseScene派生クラスを生成します。
    std::unique_ptr<BaseScene> CreateScene(const std::string& sceneName) override;
    std::vector<std::string> GetRegisteredSceneNames() const override;

    // 新しいBaseScene派生クラスはここへ登録すると、Editorの候補へ自動反映されます。
    bool RegisterScene(const std::string& sceneName, SceneCreator creator);

private:
    std::unordered_map<std::string, SceneCreator> creators_;
    std::vector<std::string> registrationOrder_;
};
