#include "SceneFactory.h"
#include "GamePlayScene.h" 
#include "GameSelectScene.h"
#include "TitleScene.h"    
#include"GameOverScene.h"
#include"GameClearScene.h"
#include"PreviewScene.h"

std::unique_ptr<BaseScene> SceneFactory::CreateScene(const std::string& sceneName) {

    // 次のシーンを生成
    std::unique_ptr<BaseScene> newScene = nullptr;

    if (sceneName == "TITLE") {
        newScene = std::make_unique<TitleScene>();
    } else if (sceneName == "GAMEPLAY") {
        newScene = std::make_unique<GamePlayScene>();
    } else if (sceneName == "SELECT") {
        newScene = std::make_unique<GameSelectScene>();
    }
    else if (sceneName == "GAMEOVER") {
        newScene = std::make_unique<GameOverScene>();
    }
    else if (sceneName == "GAMECLEAR") {
        newScene = std::make_unique<GameClearScene>();
	}
    else if (sceneName == "PREVIEW") {
        newScene = std::make_unique<PreviewScene>();
    }
  

    return newScene;
}