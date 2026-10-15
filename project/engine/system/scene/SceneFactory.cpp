#include "SceneFactory.h"
#include <utility>

std::unique_ptr<BaseScene> SceneFactory::CreateScene(const std::string& sceneName) {
    // 未登録キーを別のシーンへ置き換えず、呼び出し側に失敗を返す。
    const auto found = creators_.find(sceneName);
    if (found == creators_.end() || !found->second) {
        return nullptr;
    }
    return found->second();
}

std::vector<std::string> SceneFactory::GetRegisteredSceneNames() const {
    // エディター一覧は登録順を使い、マップの内部順序には依存しない。
    return registrationOrder_;
}

// 同名登録は生成関数だけ差し替え、一覧上の位置は変えません。
bool SceneFactory::RegisterScene(const std::string& sceneName, SceneCreator creator) {
    // 同名シーンの生成処理は差し替えられるが、一覧上の登録順は増やさない。
    if (sceneName.empty() || !creator) {
        return false;
    }
    const bool isNewRegistration = creators_.find(sceneName) == creators_.end();
    creators_[sceneName] = std::move(creator);
    if (isNewRegistration) {
        registrationOrder_.push_back(sceneName);
    }
    return true;
}
