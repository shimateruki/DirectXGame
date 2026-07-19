#include "SceneFactory.h"
#include "GamePlayScene.h" 
#include "GameSelectScene.h"
#include "TitleScene.h"    
#include "SettingsScene.h"
#include"GameOverScene.h"
#include"GameClearScene.h"
#include"PreviewScene.h"
#include "SceneAssetEditorScene.h"
#include "TutorialScene.h"
#include <utility>

SceneFactory::SceneFactory() {
    RegisterScene("TITLE", [] { return std::make_unique<TitleScene>(); });
    RegisterScene("SETTING", [] { return std::make_unique<SettingsScene>(); });
    RegisterScene("GAMEPLAY", [] { return std::make_unique<GamePlayScene>(); });
    RegisterScene("SELECT", [] { return std::make_unique<GameSelectScene>(); });
    RegisterScene("GAMEOVER", [] { return std::make_unique<GameOverScene>(); });
    RegisterScene("GAMECLEAR", [] { return std::make_unique<GameClearScene>(); });
    RegisterScene("PREVIEW", [] { return std::make_unique<PreviewScene>(); });
    RegisterScene("TUTORIAL", [] { return std::make_unique<TutorialScene>(); });
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
