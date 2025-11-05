#include "SceneFactory.h"
#include "GamePlayScene.h" 
#include "TitleScene.h"    

std::unique_ptr<BaseScene> SceneFactory::CreateScene(const std::string& sceneName) {

    // 次のシーンを生成
    std::unique_ptr<BaseScene> newScene = nullptr;

    if (sceneName == "TITLE") {
        newScene = std::make_unique<TitleScene>();
    } else if (sceneName == "GAMEPLAY") {
        newScene = std::make_unique<GamePlayScene>();
    }
  

    return newScene;
}