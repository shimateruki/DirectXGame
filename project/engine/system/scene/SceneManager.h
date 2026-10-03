#pragma once
#include "AbstractSceneFactory.h"
#include "BaseScene.h"
#include "SceneLoadContext.h"
#include "ScenePreloader.h"
#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

class DebugEditor;

/// <summary>現在シーンの更新描画、非同期ロード、ロード画面とフェード付き遷移を管理する。</summary>
class SceneManager {
public:
    /// <summary>エンジン全体で共有するシーン管理インスタンスを返す。</summary>
    static SceneManager* GetInstance();

    SceneManager();
    ~SceneManager();

    /// <summary>シーン生成ファクトリを登録し、最初のシーンを作って初期化する。</summary>
    void Initialize(AbstractSceneFactory* factory, const std::string& firstSceneName);

    /// <summary>現在シーンを終了し、ロード中のタスクとリソースを片付ける。</summary>
    void Finalize();

    /// <summary>現在シーンの更新と、フェード付きシーン遷移を進める。</summary>
    void Update(float deltaTime);

    /// <summary>現在シーンまたはローディングシーンを描画する。</summary>
    void Draw();

    /// <summary>現在シーンのUI描画を委譲する。</summary>
    void DrawUI();

    /// <summary>現在シーンの影マップ描画を委譲する。</summary>
    void DrawShadow();

    /// <summary>
    /// 次シーンインスタンスを直接予約する。主に特殊な内部用途向け。
    /// </summary>
    void SetNextScene(std::unique_ptr<BaseScene> nextScene);

    /// <summary>シーン名を指定し、ロード画面を挟む遷移を開始する。</summary>
    void ChangeScene(const std::string& sceneName);

    // 死亡復帰など、ロード画面を介さず暗転中に直接シーンを作り直します。
    // 復帰先がアイリスを開始する場合だけ destinationHandlesReveal を true にします。
    void ChangeSceneWithoutLoading(const std::string& sceneName, bool destinationHandlesReveal = false);

    // 呼び出し元でフェードアウトを完了済みの場合に、暗転を再開せず遷移します。
    void ChangeSceneAfterFade(const std::string& sceneName);

    /// <summary>現在のシーンを再読み込みする。</summary>
    bool ReloadCurrentScene();

    /// <summary>ファクトリに登録済みのシーン名を返す。</summary>
    std::vector<std::string> GetRegisteredSceneNames() const;

    /// <summary>指定名のシーンを生成できるか返す。</summary>
    bool IsSceneRegistered(const std::string& sceneName) const;

    /// <summary>ユーザー設定から最後に使用したシーン名を読み込む。</summary>
    std::string LoadLastSceneName();

    /// <summary>最後に使用したシーン名をユーザー設定へ保存する。</summary>
    void SaveLastSceneName(const std::string& sceneName);
    const std::string& GetCurrentSceneName() const { return currentSceneName_; }

    /// <summary>現在のシーンを返す。未設定ならnullptr。</summary>
    BaseScene* GetCurrentScene() const;

    // Sceneインスタンスが差し替わるたびに増加します。ポインタのアドレス再利用による誤判定を防ぎます。
    uint64_t GetSceneGeneration() const { return sceneGeneration_; }
    void SetDebugEditor(DebugEditor* editor) { debugEditor_ = editor; }
    bool IsPlaying() const { return isPlaying_; }
    void SetIsPlaying(bool isPlaying) { isPlaying_ = isPlaying; }
    bool IsTransitioning() const { return transitionPhase_ != TransitionPhase::Idle; }
    bool IsSceneInitializingAsync() const {
        return sceneInitializationStarted_ && !sceneInitializationFinished_;
    }

    // 以下はEditor用Scene Assetの切り替え。実行時シーン型とは別にレイアウトを管理する。
    bool OpenSceneAsset(
        const std::string& objectLayoutPath,
        const std::string& spriteLayoutPath,
        const std::string& runtimeSceneName);
    bool OpenSceneAsset(const SceneLoadContext& context);
    bool OpenEditorSceneAsset(const std::string& objectLayoutPath, const std::string& spriteLayoutPath);
    void SetEditorSceneAssetPaths(const std::string& objectLayoutPath, const std::string& spriteLayoutPath);
    const std::string& GetEditorSceneAssetObjectPath() const { return activeSceneLoadContext_.objectLayoutPath; }
    const std::string& GetEditorSceneAssetSpritePath() const { return activeSceneLoadContext_.spriteLayoutPath; }
    const std::string& GetActiveSceneAssetRuntimeScene() const { return activeSceneLoadContext_.runtimeScene; }
    const SceneLoadContext& GetActiveSceneLoadContext() const { return activeSceneLoadContext_; }
    bool HasActiveSceneAsset() const { return activeSceneLoadContext_.IsSceneAsset(); }

    /// <summary>現在のScene Asset指定を解除する。</summary>
    void ClearActiveSceneAsset();

private:
    /// <summary>暗転、ロード、切替で進むシーン遷移の内部段階。</summary>
    enum class TransitionPhase {
        Idle,
        FadingOutCurrent,
        Loading,
        FadingOutLoading
    };

    // ロード画面を開始し、次シーンの生成・素材準備・メインスレッド反映を順に進める。
    void BeginLoadingTransition();
    void QueueSceneChange(const std::string& sceneName, bool startFadeOut, bool skipLoading = false, bool destinationHandlesReveal = false);
    void StartAsyncSceneCreate();
    bool IsAsyncSceneReady() const;
    void PrepareLoadedSceneOnMainThread();
    void StartPreparedAssetsAsync();
    float CalculateLoadingProgress() const;

    // 準備済みシーンを現在シーンへ差し替え、遷移状態を更新する。
    void SwapToPreparedScene();
    void SwapToDirectNextScene();
    void SetLoadingProgress(float progress);
    bool IsTransitionBusy() const;

private:
    static SceneManager* instance_;

    std::unique_ptr<BaseScene> currentScene_ = nullptr;
    std::unique_ptr<BaseScene> nextScene_ = nullptr;
    std::unique_ptr<BaseScene> preparedScene_ = nullptr;
    std::future<std::shared_ptr<ScenePreloadData>> loadingFuture_;
    std::future<void> assetCreationFuture_;
    std::shared_ptr<ScenePreloadData> preparedLoadData_;
    std::shared_ptr<ScenePreloadProgress> preloadProgress_;

    AbstractSceneFactory* sceneFactory_ = nullptr;
    DebugEditor* debugEditor_ = nullptr;

    const std::string kUserConfigPath = "Resources/json/user_config.json";

    bool isPlaying_ = false;
    uint64_t sceneGeneration_ = 0;
    TransitionPhase transitionPhase_ = TransitionPhase::Idle;
    std::string nextSceneName_ = "";
    std::string currentSceneName_;
    std::string pendingSceneNameForSwap_;
    std::string loadingTargetSceneName_;
    float loadingElapsed_ = 0.0f;
    float minLoadingDisplayTime_ = 0.45f;
    bool preparedSceneInitialized_ = false;
    std::atomic<std::size_t> preparedTextureIndex_ = 0;
    std::atomic<std::size_t> preparedModelIndex_ = 0;
    bool asyncUploadBatchStarted_ = false;
    bool asyncUploadBatchSubmitted_ = false;
    bool assetCreationStarted_ = false;
    bool assetCreationFinished_ = false;
    bool preparedAssetsReady_ = false;
    bool sceneInitializationStarted_ = false;
    bool sceneInitializationFinished_ = false;
    SceneLoadContext activeSceneLoadContext_;
    bool preserveSceneAssetForNextChange_ = false;
    bool directTransitionPending_ = false;
    bool directDestinationHandlesReveal_ = false;
};
