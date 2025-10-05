#include "SceneFactory.h"
#include "TitleScene.h"
#include "GamePlayScene.h"

BaseScene* SceneFactory::CreateScene(const std::string& sceneName) {
    BaseScene* newScene = nullptr;

    if (sceneName == "TITLE") {
        newScene = new TitleScene();
    } else if (sceneName == "GAMEPLAY") {
        newScene = new GamePlayScene();
    }
    // 他のシーンを追加する場合は、ここにelse ifを追記する

    return newScene;
}