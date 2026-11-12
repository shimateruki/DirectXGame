#pragma once
#include "AbstractSceneFactory.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// 担当：シーン名と生成関数を登録し、指定されたBaseScene派生型を作る。
// 関連：AbstractSceneFactory（契約）、SceneManager（生成要求）
// 主な入口：RegisterScene / CreateScene / GetRegisteredSceneNames
class SceneFactory : public AbstractSceneFactory {
public:
    using SceneCreator = std::function<std::unique_ptr<BaseScene>()>;

    /// <summary>空の登録表を持つシーンファクトリを作る。</summary>
    SceneFactory() = default;

    /// <summary>名前に対応するシーンインスタンスを生成する。未登録ならnullptr。</summary>
    std::unique_ptr<BaseScene> CreateScene(const std::string& sceneName) override;

    /// <summary>登録順にシーン名を返す。</summary>
    std::vector<std::string> GetRegisteredSceneNames() const override;

    /// <summary>実行時シーン名と生成関数を登録し、Editorの候補にも加える。</summary>
    /// <returns>名前と生成関数が有効ならtrue。同名の場合は生成関数を置き換える。</returns>
    bool RegisterScene(const std::string& sceneName, SceneCreator creator);

private:
    std::unordered_map<std::string, SceneCreator> creators_;
    std::vector<std::string> registrationOrder_;
};
