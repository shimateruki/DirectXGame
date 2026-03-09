#pragma once
#include <memory>
#include <string>
#include "BaseScene.h"
#include "AbstractSceneFactory.h"


class DebugEditor;

/// <summary>
/// シーンを管理するクラス
/// </summary>
class SceneManager {
public:

    static SceneManager* GetInstance();

    SceneManager();


    /// <summary>
    /// デストラクタ
    /// </summary>
    ~SceneManager();

    /// <summary>
    /// 初期化 
    /// </summary>;
    void Initialize(AbstractSceneFactory* factory, const std::string& firstSceneName);

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// 更新
    /// </summary>
    void Update(float deltaTime);

    /// <summary>
    /// 描画 (現在のシーンの描画)
    /// </summary>
    void Draw();

    /// <summary>
    /// 次のシーンを予約する
    /// </summary>
    /// <param name="nextScene">次に切り替えるシーンのインスタンス</param>
    void SetNextScene(std::unique_ptr<BaseScene> nextScene);

    /// <summary>
    /// ファクトリーを使って、名前で次のシーンを予約する
    /// </summary>
    void ChangeScene(const std::string& sceneName);

    std::string LoadLastSceneName();
    void SaveLastSceneName(const std::string& sceneName);

    // <summary>
    /// 現在のシーンのポインタを取得する (Editor用)
    /// </summary>
    BaseScene* GetCurrentScene() const;
    void SetDebugEditor(DebugEditor* editor) {
        debugEditor_ = editor;
    }
    void DrawUI();
    bool IsPlaying() const { return isPlaying_; }
    void SetIsPlaying(bool isPlaying) { isPlaying_ = isPlaying; }
    void DrawShadow();
private:
    static SceneManager* instance_;
    std::unique_ptr<BaseScene> currentScene_ = nullptr;
    std::unique_ptr<BaseScene> nextScene_ = nullptr;
    AbstractSceneFactory* sceneFactory_ = nullptr; // ファクトリーのポインタ
    DebugEditor* debugEditor_ = nullptr;
    const std::string kUserConfigPath = "Resources/json/user_config.json";
    bool isPlaying_ = false;
};