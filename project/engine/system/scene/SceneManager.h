#pragma once
#include "AbstractSceneFactory.h"
#include "BaseScene.h"
#include <future>
#include <memory>
#include <string>

class DebugEditor;

/// <summary>
/// 現在シーン、次シーン予約、ロード画面、フェードを伴うシーン切り替えを管理する。
/// </summary>
class SceneManager {
public:
    static SceneManager* GetInstance();

    SceneManager();
    ~SceneManager();

    /// <summary>
    /// シーン生成ファクトリと最初に表示するシーン名を登録する。
    /// </summary>
    void Initialize(AbstractSceneFactory* factory, const std::string& firstSceneName);

    /// <summary>
    /// 現在シーンと非同期ロード中のタスクを終了する。
    /// </summary>
    void Finalize();

    /// <summary>
    /// 現在シーンの更新と、フェード付きシーン切り替えを進める。
    /// </summary>
    void Update(float deltaTime);

    void Draw();
    void DrawUI();
    void DrawShadow();

    /// <summary>
    /// 次シーンインスタンスを直接予約する。主に特殊な内部用途向け。
    /// </summary>
    void SetNextScene(std::unique_ptr<BaseScene> nextScene);

    /// <summary>
    /// シーン名を指定してロード画面経由の切り替えを開始する。
    /// </summary>
    void ChangeScene(const std::string& sceneName);

    std::string LoadLastSceneName();
    void SaveLastSceneName(const std::string& sceneName);
    const std::string& GetCurrentSceneName() const { return currentSceneName_; }

    BaseScene* GetCurrentScene() const;
    void SetDebugEditor(DebugEditor* editor) { debugEditor_ = editor; }
    bool IsPlaying() const { return isPlaying_; }
    void SetIsPlaying(bool isPlaying) { isPlaying_ = isPlaying; }
    bool IsTransitioning() const { return transitionPhase_ != TransitionPhase::Idle; }

private:
    enum class TransitionPhase {
        Idle,
        FadingOutCurrent,
        Loading,
        FadingOutLoading
    };

    void BeginLoadingTransition();
    void StartAsyncSceneCreate();
    bool IsAsyncSceneReady() const;
    void PrepareLoadedSceneOnMainThread();
    void SwapToPreparedScene();
    void SwapToDirectNextScene();
    void SetLoadingProgress(float progress);
    bool IsTransitionBusy() const;

private:
    static SceneManager* instance_;

    std::unique_ptr<BaseScene> currentScene_ = nullptr;
    std::unique_ptr<BaseScene> nextScene_ = nullptr;
    std::unique_ptr<BaseScene> preparedScene_ = nullptr;
    std::future<std::unique_ptr<BaseScene>> loadingFuture_;

    AbstractSceneFactory* sceneFactory_ = nullptr;
    DebugEditor* debugEditor_ = nullptr;

    const std::string kUserConfigPath = "Resources/json/user_config.json";

    bool isPlaying_ = false;
    TransitionPhase transitionPhase_ = TransitionPhase::Idle;
    std::string nextSceneName_ = "";
    std::string currentSceneName_;
    std::string pendingSceneNameForSwap_;
    std::string loadingTargetSceneName_;
    float loadingElapsed_ = 0.0f;
    float minLoadingDisplayTime_ = 0.45f;
    bool preparedSceneInitialized_ = false;
};
