#pragma once

#include <string>

// Scene Assetから実行Sceneへ渡す、変更されない初期化情報です。
struct SceneLoadContext {
    std::string sceneAssetId;
    std::string displayName;
    std::string runtimeScene;
    std::string objectLayoutPath;
    std::string spriteLayoutPath;
    std::string controllerName = "DEFAULT";
    std::string bgmPath;
    std::string lightPath;
    std::string cameraPath;
    std::string skyboxPath;

    bool IsSceneAsset() const {
        return !sceneAssetId.empty() && !runtimeScene.empty();
    }
};
