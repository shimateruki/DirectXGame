#pragma once

#include <string>

/// <summary>Scene Asset から実行シーンへ渡す初期化情報です。</summary>
struct SceneLoadContext {
    // シーンの識別子、表示名、実行クラスを分けて保持します。
    std::string sceneAssetId;
    std::string displayName;
    std::string runtimeScene;

    // シーン固有のレイアウトと参照アセット。空文字列は指定なしです。
    std::string objectLayoutPath;
    std::string spriteLayoutPath;
    std::string controllerName = "DEFAULT";
    std::string bgmPath;
    std::string lightPath;
    std::string cameraPath;
    std::string skyboxPath;

    /// <summary>Scene Asset と実行クラスの両方が指定されているか返します。</summary>
    bool IsSceneAsset() const {
        return !sceneAssetId.empty() && !runtimeScene.empty();
    }
};
