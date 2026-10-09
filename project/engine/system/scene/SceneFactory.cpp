#include "SceneFactory.h"
#include "game/scene/gameplay/GamePlayScene.h"
#include "game/scene/select/GameSelectScene.h"
#include "game/scene/title/TitleScene.h"
#include "game/scene/settings/SettingsScene.h"
#include"game/scene/gameover/GameOverScene.h"
#include"game/scene/clear/GameClearScene.h"
#include"game/scene/preview/PreviewScene.h"
#include "game/scene/editor/SceneAssetEditorScene.h"
#include "game/scene/tutorial/TutorialScene.h"
#include <utility>

// 文字列キーは SceneManager の遷移先と対応します。登録順はエディタの一覧表示にも使います。
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

// 同名登録は生成関数だけ差し替え、一覧上の位置は変えません。
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
