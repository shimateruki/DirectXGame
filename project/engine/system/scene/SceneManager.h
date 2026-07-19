#pragma once
#include "AbstractSceneFactory.h"
#include "BaseScene.h"
#include "SceneLoadContext.h"
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

class DebugEditor;

/// <summary>
/// 現在シーン、次シーン予約、ロード画面、フェードを伴うシーン切り替えを管理する。
/// </summary>
// SceneManagerは、現在シーンの更新描画、非同期ロード、ローディング表示、シーン切り替えを管理します。
class SceneManager {
public:
        // エンジン全体で共有するシーン管理インスタンスを取得します。
static SceneManager* GetInstance();

    SceneManager();
    ~SceneManager();

    /// <summary>
    /// シーン生成ファクトリと最初に表示するシーン名を登録する。
    /// </summary>
        // シーン生成ファクトリーを受け取り、最初のシーンを作成して初期化します。
void Initialize(AbstractSceneFactory* factory, const std::string& firstSceneName);

    /// <summary>
    /// 現在シーンと非同期ロード中のタスクを終了する。
    /// </summary>
        // 現在シーンを終了し、ロード中リソースを片付けます。
void Finalize();

    /// <summary>
    /// 現在シーンの更新と、フェード付きシーン切り替えを進める。
    /// </summary>
        // 現在シーンまたはローディング遷移の状態を更新します。
void Update(float deltaTime);

        // 現在シーンまたはローディングシーンの描画を行います。
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
        // シーン名を指定して次のシーンへの遷移を開始します。
void ChangeScene(const std::string& sceneName);

    bool ReloadCurrentScene();
    std::vector<std::string> GetRegisteredSceneNames() const;
    bool IsSceneRegistered(const std::string& sceneName) const;

    std::string LoadLastSceneName();
    void SaveLastSceneName(const std::string& sceneName);
    const std::string& GetCurrentSceneName() const { return currentSceneName_; }

    BaseScene* GetCurrentScene() const;
    // Sceneインスタンスが差し替わるたびに増加します。ポインタのアドレス再利用による誤判定を防ぎます。
    uint64_t GetSceneGeneration() const { return sceneGeneration_; }
    void SetDebugEditor(DebugEditor* editor) { debugEditor_ = editor; }
    bool IsPlaying() const { return isPlaying_; }
    void SetIsPlaying(bool isPlaying) { isPlaying_ = isPlaying; }
    bool IsTransitioning() const { return transitionPhase_ != TransitionPhase::Idle; }

    // Editor専用SceneへScene Assetを読み込みます。ゲーム用Sceneクラスの種類とは分離して扱います。
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
    void ClearActiveSceneAsset();

private:
        // 非同期ロード中のシーン切り替えフェーズを表します。
enum class TransitionPhase {
        Idle,
        FadingOutCurrent,
        Loading,
        FadingOutLoading
    };

        // ローディング画面を表示し、次シーン読み込みへの準備を始めます。
void BeginLoadingTransition();
        // 別スレッドで次シーン生成を開始します。
void StartAsyncSceneCreate();
    bool IsAsyncSceneReady() const;
    void PrepareLoadedSceneOnMainThread();
        // 準備完了した次シーンを現在シーンとして差し替えます。
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
    uint64_t sceneGeneration_ = 0;
    TransitionPhase transitionPhase_ = TransitionPhase::Idle;
    std::string nextSceneName_ = "";
    std::string currentSceneName_;
    std::string pendingSceneNameForSwap_;
    std::string loadingTargetSceneName_;
    float loadingElapsed_ = 0.0f;
    float minLoadingDisplayTime_ = 0.45f;
    bool preparedSceneInitialized_ = false;
    SceneLoadContext activeSceneLoadContext_;
    bool preserveSceneAssetForNextChange_ = false;
};
