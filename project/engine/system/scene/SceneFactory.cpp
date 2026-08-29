#include "SceneFactory.h"
#include "GameOverScene.h"
#include "GameScene.h"
#include "SceneAssetEditorScene.h"
#include "TitleScene.h"
#include <utility>

SceneFactory::SceneFactory() {
    RegisterScene("TITLE", [] { return std::make_unique<TitleScene>(); });
    RegisterScene("GAME", [] { return std::make_unique<GameScene>(); });
    RegisterScene("GAME_OVER", [] { return std::make_unique<GameOverScene>(); });
    RegisterScene("SCENE_EDITOR", [] { return std::make_unique<SceneAssetEditorScene>(); });
}

std::unique_ptr<BaseScene> SceneFactory::CreateScene(const std::string& sceneName) {
    const auto found = creators_.find(sceneName);
    if (found == creators_.end() || !found->second) {
        return nullptr;
    }
    return found->second();
}

std::vector<std::string> SceneFactory::GetRegisteredSceneNames() const {
    return registrationOrder_;
}

bool SceneFactory::RegisterScene(const std::string& sceneName, SceneCreator creator) {
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
