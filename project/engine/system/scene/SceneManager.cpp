#include "SceneManager.h"

#include "DirectXCommon.h"
#include "LoadingScene.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "engine/graphics/postprocess/Fade.h"
#ifdef USE_IMGUI
#include "CameraEditor.h"
#endif
#include "json.hpp"

#include <cassert>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <exception>
#include <utility>
#include <Windows.h>

using json = nlohmann::json;

SceneManager* SceneManager::instance_ = nullptr;

SceneManager* SceneManager::GetInstance() {
    return instance_;
}

SceneManager::SceneManager() {
    instance_ = this;
}

SceneManager::~SceneManager() {
    Finalize();
}

void SceneManager::Initialize(AbstractSceneFactory* factory, const std::string& firstSceneName) {
    sceneFactory_ = factory;
    currentSceneName_ = firstSceneName;
    pendingSceneNameForSwap_.clear();
    loadingTargetSceneName_.clear();
    loadingElapsed_ = 0.0f;

    assert(sceneFactory_ && "SceneFactory is not set in SceneManager.");
    assert(IsSceneRegistered(firstSceneName) && "Initial scene is not registered in SceneFactory.");
    currentScene_ = sceneFactory_->CreateScene(firstSceneName);
    assert(currentScene_ && "Initial scene creation failed.");
    if (!currentScene_) {
        currentSceneName_.clear();
        transitionPhase_ = TransitionPhase::Idle;
        return;
    }

    ++sceneGeneration_;
    currentScene_->SetSceneManager(this);
    currentScene_->SetSceneLoadContext(activeSceneLoadContext_);
    if (debugEditor_) {
        currentScene_->SetDebugEditor(debugEditor_);
    }

    // 初回起動ではローディングSceneを生成せず、最初のSceneを直接初期化します。
    // これによりロードUIの初期化と最低表示時間を省き、ロード画面はScene遷移時だけ表示します。
    TextureManager* textureManager = TextureManager::GetInstance();
    const bool initialUploadBatch = textureManager->BeginAsyncUploadBatch();
    textureManager->SetAsyncUploadRecording(initialUploadBatch);
    try {
        currentScene_->Initialize();
    }
    catch (...) {
        textureManager->SetAsyncUploadRecording(false);
        if (initialUploadBatch) {
            textureManager->SubmitAsyncUploadBatch();
            textureManager->WaitForAsyncUploadBatch();
        }
        currentScene_.reset();
        currentSceneName_.clear();
        throw;
    }
    textureManager->SetAsyncUploadRecording(false);
    if (initialUploadBatch) {
        textureManager->SubmitAsyncUploadBatch();
        textureManager->WaitForAsyncUploadBatch();
    }

    currentScene_->OnActivated();
    transitionPhase_ = TransitionPhase::Idle;
}

void SceneManager::Finalize() {
    if (assetCreationFuture_.valid()) {
        assetCreationFuture_.wait();
        try {
            assetCreationFuture_.get();
        }
        catch (...) {
        }
    }
    TextureManager::GetInstance()->WaitForAsyncUploadBatch();
    if (loadingFuture_.valid()) {
        loadingFuture_.wait();
        try {
            preparedLoadData_ = loadingFuture_.get();
        }
        catch (...) {
            preparedLoadData_.reset();
        }
    }

    DirectXCommon::GetInstance()->WaitForGPUAndReset();

    if (currentScene_) {
        currentScene_->Finalize();
        currentScene_.reset();
    }
    ++sceneGeneration_;

    if (preparedScene_ && preparedSceneInitialized_) {
        preparedScene_->Finalize();
    }
    preparedScene_.reset();
    preparedLoadData_.reset();
    preloadProgress_.reset();
    preparedSceneInitialized_ = false;
    preparedTextureIndex_ = 0;
    preparedModelIndex_ = 0;

    nextScene_.reset();
    asyncUploadBatchStarted_ = false;
    asyncUploadBatchSubmitted_ = false;
    assetCreationStarted_ = false;
    assetCreationFinished_ = false;
    preparedAssetsReady_ = false;
    sceneInitializationStarted_ = false;
    sceneInitializationFinished_ = false;
    transitionPhase_ = TransitionPhase::Idle;
}

void SceneManager::Update(float deltaTime) {
    const float effectiveDeltaTime = deltaTime > 0.0f ? deltaTime : (1.0f / 60.0f);

    switch (transitionPhase_) {
    case TransitionPhase::FadingOutCurrent:
        if (Fade::GetInstance()->GetStatus() == Fade::Status::FadeOut && !Fade::GetInstance()->IsFinished()) {
            return;
        }
        BeginLoadingTransition();
        return;

    case TransitionPhase::Loading: {
        loadingElapsed_ += effectiveDeltaTime;
        if (currentScene_) {
            currentScene_->Update(effectiveDeltaTime);
        }

        const bool asyncReady = preparedLoadData_ || preparedSceneInitialized_ || IsAsyncSceneReady();

        if (asyncReady && !preparedSceneInitialized_) {
            PrepareLoadedSceneOnMainThread();
        }

        SetLoadingProgress(CalculateLoadingProgress());

        if (loadingElapsed_ >= minLoadingDisplayTime_ &&
            preparedSceneInitialized_ &&
            Fade::GetInstance()->GetStatus() != Fade::Status::FadeIn) {
            SetLoadingProgress(1.0f);
            Fade::GetInstance()->StartFadeOut(0.35f);
            transitionPhase_ = TransitionPhase::FadingOutLoading;
        }
        return;
    }

    case TransitionPhase::FadingOutLoading:
        if (currentScene_) {
            currentScene_->Update(effectiveDeltaTime);
        }
        SetLoadingProgress(1.0f);

        if (Fade::GetInstance()->GetStatus() == Fade::Status::FadeOut && !Fade::GetInstance()->IsFinished()) {
            return;
        }
        PrepareLoadedSceneOnMainThread();
        SwapToPreparedScene();
        return;

    case TransitionPhase::Idle:
    default:
        break;
    }

    if (nextScene_ != nullptr) {
        SwapToDirectNextScene();
    }

    if (currentScene_) {
        currentScene_->Update(deltaTime);
    }
}

void SceneManager::Draw() {
    if (currentScene_) {
        currentScene_->Draw();
    }
}

void SceneManager::DrawUI() {
    if (currentScene_) {
        currentScene_->DrawUI();
    }
}

void SceneManager::DrawShadow() {
    if (currentScene_) {
        currentScene_->DrawShadow();
    }
}

void SceneManager::SetNextScene(std::unique_ptr<BaseScene> nextScene) {
    nextScene_ = std::move(nextScene);
}

BaseScene* SceneManager::GetCurrentScene() const {
    return currentScene_.get();
}

void SceneManager::ChangeScene(const std::string& sceneName) {
    QueueSceneChange(sceneName, true);
}

void SceneManager::ChangeSceneAfterFade(const std::string& sceneName) {
    QueueSceneChange(sceneName, false);
}

void SceneManager::QueueSceneChange(const std::string& sceneName, bool startFadeOut) {
    if (sceneFactory_ == nullptr) {
        assert(false && "SceneFactory is not set in SceneManager.");
        return;
    }

    if (IsTransitionBusy()) {
        return;
    }

    if (!IsSceneRegistered(sceneName)) {
        assert(false && "Requested scene is not registered in SceneFactory.");
        return;
    }

    if (!preserveSceneAssetForNextChange_) {
        ClearActiveSceneAsset();
    }
    preserveSceneAssetForNextChange_ = false;

    nextSceneName_ = sceneName;
    pendingSceneNameForSwap_ = sceneName;
    transitionPhase_ = TransitionPhase::FadingOutCurrent;
    if (startFadeOut) {
        Fade::GetInstance()->StartFadeOut(1.0f);
    }

#ifdef USE_IMGUI
    // Editor専用Sceneは起動時のゲームSceneとして復元しません。
    if (sceneName != "SCENE_EDITOR") {
        SaveLastSceneName(sceneName);
    }
#endif
}

bool SceneManager::ReloadCurrentScene() {
    if (currentSceneName_.empty() || IsTransitionBusy()) {
        return false;
    }

    preserveSceneAssetForNextChange_ = HasActiveSceneAsset();
    ChangeScene(currentSceneName_);
    return true;
}

std::vector<std::string> SceneManager::GetRegisteredSceneNames() const {
    return sceneFactory_ ? sceneFactory_->GetRegisteredSceneNames() : std::vector<std::string>{};
}

bool SceneManager::IsSceneRegistered(const std::string& sceneName) const {
    const std::vector<std::string> sceneNames = GetRegisteredSceneNames();
    return std::find(sceneNames.begin(), sceneNames.end(), sceneName) != sceneNames.end();
}

bool SceneManager::OpenSceneAsset(
    const std::string& objectLayoutPath,
    const std::string& spriteLayoutPath,
    const std::string& runtimeSceneName) {
    SceneLoadContext context;
    context.sceneAssetId = objectLayoutPath;
    context.runtimeScene = runtimeSceneName;
    context.objectLayoutPath = objectLayoutPath;
    context.spriteLayoutPath = spriteLayoutPath;
    return OpenSceneAsset(context);
}

bool SceneManager::OpenSceneAsset(const SceneLoadContext& context) {
    if (!context.IsSceneAsset() || context.objectLayoutPath.empty() ||
        isPlaying_ || IsTransitionBusy() || !IsSceneRegistered(context.runtimeScene)) {
        return false;
    }

    activeSceneLoadContext_ = context;
    preserveSceneAssetForNextChange_ = true;
    ChangeScene(context.runtimeScene);
    return true;
}

bool SceneManager::OpenEditorSceneAsset(
    const std::string& objectLayoutPath,
    const std::string& spriteLayoutPath) {
    return OpenSceneAsset(objectLayoutPath, spriteLayoutPath, "SCENE_EDITOR");
}

void SceneManager::SetEditorSceneAssetPaths(
    const std::string& objectLayoutPath,
    const std::string& spriteLayoutPath) {
    activeSceneLoadContext_.objectLayoutPath = objectLayoutPath;
    activeSceneLoadContext_.spriteLayoutPath = spriteLayoutPath;
    std::string sceneAssetId = objectLayoutPath;
    const size_t slash = sceneAssetId.find_last_of("/\\");
    if (slash != std::string::npos) {
        sceneAssetId = sceneAssetId.substr(slash + 1);
    }
    if (sceneAssetId.size() >= 5 && sceneAssetId.substr(sceneAssetId.size() - 5) == ".json") {
        sceneAssetId.resize(sceneAssetId.size() - 5);
    }
    activeSceneLoadContext_.sceneAssetId = sceneAssetId;
}

void SceneManager::ClearActiveSceneAsset() {
    activeSceneLoadContext_ = {};
}

void SceneManager::BeginLoadingTransition() {
    TextureManager::GetInstance()->WaitForAsyncUploadBatch();
    DirectXCommon::GetInstance()->WaitForGPUAndReset();

    if (currentScene_) {
        currentScene_->Finalize();
        currentScene_.reset();
    }

    preparedScene_.reset();
    preparedLoadData_.reset();
    preloadProgress_.reset();
    preparedSceneInitialized_ = false;
    preparedTextureIndex_ = 0;
    preparedModelIndex_ = 0;
    asyncUploadBatchStarted_ = false;
    asyncUploadBatchSubmitted_ = false;
    assetCreationStarted_ = false;
    assetCreationFinished_ = false;
    preparedAssetsReady_ = false;
    sceneInitializationStarted_ = false;
    sceneInitializationFinished_ = false;

    loadingTargetSceneName_ = nextSceneName_;
    nextSceneName_.clear();
    loadingElapsed_ = 0.0f;

    currentScene_ = std::make_unique<LoadingScene>();
    ++sceneGeneration_;
#ifdef USE_IMGUI
    CameraEditor::GetInstance()->InvalidatePreviewForSceneChange();
#endif
    currentScene_->SetSceneManager(this);
    if (debugEditor_) {
        currentScene_->SetDebugEditor(debugEditor_);
    }
    currentScene_->Initialize();
    SetLoadingProgress(0.08f);

    StartAsyncSceneCreate();
    Fade::GetInstance()->StartFadeIn(0.25f);
    transitionPhase_ = TransitionPhase::Loading;
}

void SceneManager::StartAsyncSceneCreate() {
    assert(sceneFactory_);
    const std::string targetName = loadingTargetSceneName_;
    preparedScene_ = sceneFactory_->CreateScene(targetName);
    assert(preparedScene_ && "Scene creation failed before async preload.");
    if (!preparedScene_) {
        return;
    }

    preparedScene_->SetSceneManager(this);
    preparedScene_->SetSceneLoadContext(activeSceneLoadContext_);
    if (debugEditor_) {
        preparedScene_->SetDebugEditor(debugEditor_);
    }

    SceneLoadManifest manifest = preparedScene_->BuildAsyncLoadManifest();
    if (!activeSceneLoadContext_.bgmPath.empty()) {
        manifest.AddAudio(activeSceneLoadContext_.bgmPath);
    }
    preloadProgress_ = std::make_shared<ScenePreloadProgress>();
    const std::shared_ptr<ScenePreloadProgress> progress = preloadProgress_;

    loadingFuture_ = std::async(std::launch::async, [manifest, progress, targetName]() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        try {
            return ScenePreloader::Prepare(manifest, progress);
        }
        catch (const std::exception& exception) {
            auto failed = std::make_shared<ScenePreloadData>();
            failed->warnings.push_back(
                "Scene preload failed for " + targetName + ": " + exception.what());
            if (progress) {
                progress->Finish();
            }
            return failed;
        }
        catch (...) {
            auto failed = std::make_shared<ScenePreloadData>();
            failed->warnings.push_back("Scene preload failed for " + targetName + ".");
            if (progress) {
                progress->Finish();
            }
            return failed;
        }
    });
}

bool SceneManager::IsAsyncSceneReady() const {
    if (!loadingFuture_.valid()) {
        return false;
    }

    return loadingFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

void SceneManager::PrepareLoadedSceneOnMainThread() {
    if (preparedSceneInitialized_) {
        return;
    }

    if (!preparedLoadData_ && loadingFuture_.valid()) {
        if (!IsAsyncSceneReady()) {
            return;
        }
        preparedLoadData_ = loadingFuture_.get();
        if (!preparedLoadData_) {
            preparedLoadData_ = std::make_shared<ScenePreloadData>();
        }
        for (const std::string& warning : preparedLoadData_->warnings) {
            OutputDebugStringA((warning + "\n").c_str());
        }
        if (preparedScene_) {
            preparedScene_->SetPreparedLoadData(preparedLoadData_);
        }
        asyncUploadBatchStarted_ = TextureManager::GetInstance()->BeginAsyncUploadBatch();
    }

    assert(preparedScene_ && "Scene creation failed during loading transition.");
    if (!preparedScene_) {
        transitionPhase_ = TransitionPhase::Idle;
        return;
    }

    if (!preparedAssetsReady_) {
        if (!assetCreationStarted_) {
            StartPreparedAssetsAsync();
            return;
        }
        if (!assetCreationFinished_) {
            if (!assetCreationFuture_.valid() ||
                assetCreationFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                return;
            }
            try {
                assetCreationFuture_.get();
            }
            catch (const std::exception& exception) {
                OutputDebugStringA((std::string("Asset creation failed: ") + exception.what() + "\n").c_str());
                assert(false && "Asset creation failed.");
            }
            catch (...) {
                OutputDebugStringA("Asset creation failed with an unknown exception.\n");
                assert(false && "Asset creation failed.");
            }
            assetCreationFinished_ = true;
        }

        if (preparedLoadData_ &&
            (preparedTextureIndex_.load(std::memory_order_relaxed) < preparedLoadData_->textures.size() ||
             preparedModelIndex_.load(std::memory_order_relaxed) < preparedLoadData_->modelNames.size())) {
            return;
        }

        if (asyncUploadBatchStarted_ && !asyncUploadBatchSubmitted_) {
            asyncUploadBatchSubmitted_ = TextureManager::GetInstance()->SubmitAsyncUploadBatch();
            if (!asyncUploadBatchSubmitted_) {
                return;
            }
        }
        if (asyncUploadBatchSubmitted_ &&
            !TextureManager::GetInstance()->PollAsyncUploadBatch()) {
            return;
        }

        preparedAssetsReady_ = true;
        asyncUploadBatchStarted_ = false;
        asyncUploadBatchSubmitted_ = false;
    }

    if (!sceneInitializationStarted_) {
        preparedScene_->SetSceneManager(this);
        preparedScene_->SetSceneLoadContext(activeSceneLoadContext_);
        if (debugEditor_) {
            preparedScene_->SetDebugEditor(debugEditor_);
        }

        // Scene::Initialize() は CameraManager / LightManager / GPU描画管理など、
        // メインスレッド専用の共有状態を更新します。アセットの読み込み準備だけを
        // 非同期にし、Scene本体の構築は描画と競合しないこのスレッドで確定します。
        asyncUploadBatchStarted_ = TextureManager::GetInstance()->BeginAsyncUploadBatch();
        sceneInitializationStarted_ = true;
        preparedScene_->BeginLoadingInitialize();
    }

    if (!sceneInitializationFinished_) {
        TextureManager::GetInstance()->SetAsyncUploadRecording(asyncUploadBatchStarted_);
        try {
            sceneInitializationFinished_ = preparedScene_->InitializeLoadingStep();
        }
        catch (...) {
            TextureManager::GetInstance()->SetAsyncUploadRecording(false);
            throw;
        }
        TextureManager::GetInstance()->SetAsyncUploadRecording(false);
        if (!sceneInitializationFinished_) {
            return;
        }
    }

    if (asyncUploadBatchStarted_ && !asyncUploadBatchSubmitted_) {
        asyncUploadBatchSubmitted_ = TextureManager::GetInstance()->SubmitAsyncUploadBatch();
        if (!asyncUploadBatchSubmitted_) {
            return;
        }
    }
    if (asyncUploadBatchSubmitted_ &&
        !TextureManager::GetInstance()->PollAsyncUploadBatch()) {
        return;
    }

    preparedScene_->SetPreparedLoadData(nullptr);
    preparedSceneInitialized_ = true;
}

void SceneManager::StartPreparedAssetsAsync() {
    if (!preparedLoadData_ || assetCreationStarted_) {
        return;
    }

    const std::shared_ptr<ScenePreloadData> loadData = preparedLoadData_;
    const bool recordDeferredUploads = asyncUploadBatchStarted_;
    assetCreationFuture_ = std::async(
        std::launch::async,
        [this, loadData, recordDeferredUploads]() {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            TextureManager::GetInstance()->SetAsyncUploadRecording(recordDeferredUploads);
            try {
                for (const SceneLoadManifest::TextureRequest& texture : loadData->textures) {
                    TextureManager::GetInstance()->Load(
                        texture.path,
                        texture.linear ? TextureManager::TextureColorSpace::Linear
                                       : TextureManager::TextureColorSpace::Auto);
                    preparedTextureIndex_.fetch_add(1, std::memory_order_relaxed);
                }
                for (const std::string& modelName : loadData->modelNames) {
                    ModelManager::GetInstance()->LoadModel(modelName);
                    preparedModelIndex_.fetch_add(1, std::memory_order_relaxed);
                }
            }
            catch (...) {
                TextureManager::GetInstance()->SetAsyncUploadRecording(false);
                if (SUCCEEDED(comResult)) {
                    CoUninitialize();
                }
                throw;
            }
            TextureManager::GetInstance()->SetAsyncUploadRecording(false);
            if (SUCCEEDED(comResult)) {
                CoUninitialize();
            }
        });
    assetCreationStarted_ = true;
}

float SceneManager::CalculateLoadingProgress() const {
    if (preparedSceneInitialized_) {
        return 1.0f;
    }

    const float workerRatio = preloadProgress_ ? preloadProgress_->GetRatio() : 0.0f;
    if (!preparedLoadData_) {
        return 0.08f + workerRatio * 0.52f;
    }

    const std::size_t totalAssets =
        preparedLoadData_->textures.size() + preparedLoadData_->modelNames.size();
    const std::size_t completedAssets =
        preparedTextureIndex_.load(std::memory_order_relaxed) +
        preparedModelIndex_.load(std::memory_order_relaxed);
    const float assetRatio = totalAssets == 0
        ? 1.0f
        : static_cast<float>(completedAssets) / static_cast<float>(totalAssets);
    const float assetProgress = 0.60f + (std::min)(1.0f, assetRatio) * 0.36f;
    if (sceneInitializationStarted_ && !sceneInitializationFinished_) {
        const float initializeRatio = preparedScene_
            ? std::clamp(preparedScene_->GetLoadingInitializeProgress(), 0.0f, 1.0f)
            : 0.0f;
        return 0.96f + initializeRatio * 0.025f;
    }
    if (sceneInitializationFinished_ &&
        TextureManager::GetInstance()->IsAsyncUploadBatchPending()) {
        return 0.99f;
    }
    if (asyncUploadBatchSubmitted_ &&
        TextureManager::GetInstance()->IsAsyncUploadBatchPending()) {
        return 0.97f;
    }
    return assetProgress;
}

void SceneManager::SwapToPreparedScene() {
    DirectXCommon::GetInstance()->WaitForGPUAndReset();

    if (currentScene_) {
        currentScene_->Finalize();
        currentScene_.reset();
    }

    currentScene_ = std::move(preparedScene_);
    ++sceneGeneration_;
#ifdef USE_IMGUI
    CameraEditor* cameraEditor = CameraEditor::GetInstance();
    cameraEditor->InvalidatePreviewForSceneChange();
    cameraEditor->SetObject3dCommon(
        currentScene_ ? currentScene_->GetObject3dCommon() : nullptr);
#endif
    preparedSceneInitialized_ = false;
    preparedLoadData_.reset();
    preloadProgress_.reset();
    preparedTextureIndex_ = 0;
    preparedModelIndex_ = 0;
    asyncUploadBatchStarted_ = false;
    asyncUploadBatchSubmitted_ = false;
    assetCreationStarted_ = false;
    assetCreationFinished_ = false;
    preparedAssetsReady_ = false;
    sceneInitializationStarted_ = false;
    sceneInitializationFinished_ = false;
    if (!pendingSceneNameForSwap_.empty()) {
        currentSceneName_ = pendingSceneNameForSwap_;
        pendingSceneNameForSwap_.clear();
    }
    loadingTargetSceneName_.clear();

    // 有効化処理は、SceneManager上でも現在シーンへ切り替わった後に行います。
    // カメラ、ライト、スカイボックスなどがLoadingScene側の状態で上書きされるのを防ぎます。
    if (currentScene_) {
        currentScene_->OnActivated();
    }

    Fade::GetInstance()->StartFadeIn(1.0f);
    transitionPhase_ = TransitionPhase::Idle;
}

void SceneManager::SwapToDirectNextScene() {
    DirectXCommon::GetInstance()->WaitForGPUAndReset();

    if (currentScene_) {
        currentScene_->Finalize();
        currentScene_.reset();
    }

    currentScene_ = std::move(nextScene_);
    ++sceneGeneration_;
#ifdef USE_IMGUI
    CameraEditor::GetInstance()->InvalidatePreviewForSceneChange();
#endif
    nextScene_ = nullptr;
    if (!pendingSceneNameForSwap_.empty()) {
        currentSceneName_ = pendingSceneNameForSwap_;
        pendingSceneNameForSwap_.clear();
    }

    if (currentScene_) {
        currentScene_->SetSceneManager(this);
        currentScene_->SetSceneLoadContext(activeSceneLoadContext_);
        if (debugEditor_) {
            currentScene_->SetDebugEditor(debugEditor_);
        }
        currentScene_->Initialize();
#ifdef USE_IMGUI
        CameraEditor::GetInstance()->SetObject3dCommon(currentScene_->GetObject3dCommon());
#endif
        currentScene_->OnActivated();
    }
}

void SceneManager::SetLoadingProgress(float progress) {
    if (LoadingScene* loadingScene = dynamic_cast<LoadingScene*>(currentScene_.get())) {
        loadingScene->SetProgress(std::clamp(progress, 0.0f, 1.0f));
    }
}

bool SceneManager::IsTransitionBusy() const {
    return transitionPhase_ != TransitionPhase::Idle ||
           !nextSceneName_.empty() ||
           nextScene_ != nullptr ||
           preparedScene_ != nullptr ||
           loadingFuture_.valid();
}

void SceneManager::SaveLastSceneName(const std::string& sceneName) {
    json root;
    {
        std::ifstream input(kUserConfigPath);
        if (input.is_open()) {
            try {
                input >> root;
            } catch (...) {
                root = json::object();
            }
        }
    }

    if (!root.is_object()) {
        root = json::object();
    }

    root["lastScene"] = sceneName;

    std::ofstream file(kUserConfigPath);
    if (file.is_open()) {
        file << root.dump(4);
    }
}

std::string SceneManager::LoadLastSceneName() {
    std::ifstream file(kUserConfigPath);
    if (!file.is_open()) {
        return "";
    }

    try {
        json root;
        file >> root;
        if (root.contains("lastScene")) {
            return root["lastScene"];
        }
    } catch (...) {
    }

    return "";
}
