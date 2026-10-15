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

    /// <summary>シーン遷移の初期状態を作る。</summary>
    SceneManager();
    /// <summary>保留中の非同期処理を終了し、保持するシーンを破棄する。</summary>
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

    /// <summary>ロード画面を挟まず、暗転中にシーンを直接作り直す。</summary>
    /// <param name="destinationHandlesReveal">遷移先がアイリスなどの明転を担当する場合はtrue。</param>
    void ChangeSceneWithoutLoading(const std::string& sceneName, bool destinationHandlesReveal = false);

    /// <summary>呼び出し元がフェードアウトを終えた状態から遷移を開始する。</summary>
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
    /// <summary>現在の実行時シーン名を返す。未設定なら空文字列。</summary>
    const std::string& GetCurrentSceneName() const { return currentSceneName_; }

    /// <summary>現在のシーンを返す。未設定ならnullptr。</summary>
    BaseScene* GetCurrentScene() const;

    /// <summary>シーンインスタンスの差し替えごとに増える世代番号を返す。</summary>
    /// <remarks>ポインターのアドレスが再利用されてもシーン変更を判定できる。</remarks>
    uint64_t GetSceneGeneration() const { return sceneGeneration_; }
    /// <summary>シーン管理と連携するEditorを登録する。所有権は受け取らない。</summary>
    void SetDebugEditor(DebugEditor* editor) { debugEditor_ = editor; }
    /// <summary>ゲームを再生中か取得・設定する。</summary>
    bool IsPlaying() const { return isPlaying_; }
    void SetIsPlaying(bool isPlaying) { isPlaying_ = isPlaying; }
    /// <summary>シーン遷移のいずれかの段階を実行中か返す。</summary>
    bool IsTransitioning() const { return transitionPhase_ != TransitionPhase::Idle; }
    /// <summary>シーン初期化を非同期で進めているか返す。</summary>
    bool IsSceneInitializingAsync() const {
        return sceneInitializationStarted_ && !sceneInitializationFinished_;
    }

    /// <summary>Editorの3D・Spriteレイアウトと実行時シーン名を組にして開く。</summary>
    /// <returns>再生中・遷移中・未登録のシーン名ならfalse。</returns>
    bool OpenSceneAsset(
        const std::string& objectLayoutPath,
        const std::string& spriteLayoutPath,
        const std::string& runtimeSceneName);
    /// <summary>SceneLoadContextに指定されたScene Assetを開く。</summary>
    /// <returns>指定を受理して遷移を始めた場合はtrue。</returns>
    bool OpenSceneAsset(const SceneLoadContext& context);
    /// <summary>実行時シーン名をSCENE_EDITORに固定してレイアウトを開く。</summary>
    bool OpenEditorSceneAsset(const std::string& objectLayoutPath, const std::string& spriteLayoutPath);
    /// <summary>編集対象のパスだけを更新する。シーンの再読込と保存はしない。</summary>
    void SetEditorSceneAssetPaths(const std::string& objectLayoutPath, const std::string& spriteLayoutPath);
    /// <summary>現在のScene Assetの3D配置パスを返す。</summary>
    const std::string& GetEditorSceneAssetObjectPath() const { return activeSceneLoadContext_.objectLayoutPath; }
    /// <summary>現在のScene AssetのSprite配置パスを返す。</summary>
    const std::string& GetEditorSceneAssetSpritePath() const { return activeSceneLoadContext_.spriteLayoutPath; }
    /// <summary>現在のScene Assetに結び付く実行時シーン名を返す。</summary>
    const std::string& GetActiveSceneAssetRuntimeScene() const { return activeSceneLoadContext_.runtimeScene; }
    /// <summary>現在のScene Asset読み込み設定を返す。</summary>
    const SceneLoadContext& GetActiveSceneLoadContext() const { return activeSceneLoadContext_; }
    /// <summary>開いているScene Assetがあるか返す。</summary>
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

    /// <summary>ロード画面を開始し、次シーンの非同期準備へ移る。</summary>
    void BeginLoadingTransition();
    /// <summary>遷移先とフェード・ロードの方式を内部キューへ登録する。</summary>
    void QueueSceneChange(const std::string& sceneName, bool startFadeOut, bool skipLoading = false, bool destinationHandlesReveal = false);
    /// <summary>次シーンの生成を非同期タスクに依頼する。</summary>
    void StartAsyncSceneCreate();
    /// <summary>非同期のシーン生成が完了したか返す。</summary>
    bool IsAsyncSceneReady() const;
    /// <summary>生成済みシーンをメインスレッドで初期化する。</summary>
    void PrepareLoadedSceneOnMainThread();
    /// <summary>事前抽出した素材の読み込みを非同期で開始する。</summary>
    void StartPreparedAssetsAsync();
    /// <summary>シーン生成と素材準備を合わせたロード進捗を返す。</summary>
    float CalculateLoadingProgress() const;

    /// <summary>準備済みシーンを現在シーンへ差し替える。</summary>
    void SwapToPreparedScene();
    /// <summary>ロード画面を経ない次シーンを現在シーンへ差し替える。</summary>
    void SwapToDirectNextScene();
    /// <summary>ロード画面に表示する進捗値を更新する。</summary>
    void SetLoadingProgress(float progress);
    /// <summary>ロード・フェードを含む遷移が進行中か返す。</summary>
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
