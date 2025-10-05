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
    // ���̃V�[����ǉ�����ꍇ�́A������else if��ǋL����

    return newScene;
}