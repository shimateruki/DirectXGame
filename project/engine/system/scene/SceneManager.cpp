#include "SceneManager.h"

#include "DirectXCommon.h"
#include "LoadingScene.h"
#include "engine/graphics/postprocess/Fade.h"
#include "json.hpp"

#include <cassert>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <utility>

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

    currentScene_ = sceneFactory_->CreateScene(firstSceneName);
    assert(currentScene_ && "First scene creation failed.");

    currentScene_->SetSceneManager(this);
    if (debugEditor_) {
        currentScene_->SetDebugEditor(debugEditor_);
    }

    currentScene_->Initialize();
    ++sceneGeneration_;
}

void SceneManager::Finalize() {
    if (loadingFuture_.valid()) {
        loadingFuture_.wait();
        preparedScene_ = loadingFuture_.get();
        preparedSceneInitialized_ = false;
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
    preparedSceneInitialized_ = false;

    nextScene_.reset();
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

        const bool asyncReady = preparedSceneInitialized_ || IsAsyncSceneReady();
        const float waitingProgress = std::min(0.95f, 0.12f + loadingElapsed_ * 0.18f);

        if (asyncReady && !preparedSceneInitialized_) {
            SetLoadingProgress(0.98f);
            PrepareLoadedSceneOnMainThread();
        }

        SetLoadingProgress(preparedSceneInitialized_ ? 1.0f : waitingProgress);

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
    Fade::GetInstance()->StartFadeOut(1.0f);

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
    DirectXCommon::GetInstance()->WaitForGPUAndReset();

    if (currentScene_) {
        currentScene_->Finalize();
        currentScene_.reset();
    }

    preparedScene_.reset();
    preparedSceneInitialized_ = false;

    loadingTargetSceneName_ = nextSceneName_;
    nextSceneName_.clear();
    loadingElapsed_ = 0.0f;

    currentScene_ = std::make_unique<LoadingScene>();
    ++sceneGeneration_;
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
    AbstractSceneFactory* factory = sceneFactory_;

    loadingFuture_ = std::async(std::launch::async, [factory, targetName]() -> std::unique_ptr<BaseScene> {
        return factory->CreateScene(targetName);
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

    if (!preparedScene_ && loadingFuture_.valid()) {
        preparedScene_ = loadingFuture_.get();
    }

    assert(preparedScene_ && "Scene creation failed during loading transition.");
    if (!preparedScene_) {
        transitionPhase_ = TransitionPhase::Idle;
        return;
    }

    preparedScene_->SetSceneManager(this);
    preparedScene_->SetSceneLoadContext(activeSceneLoadContext_);
    if (debugEditor_) {
        preparedScene_->SetDebugEditor(debugEditor_);
    }

    DirectXCommon::GetInstance()->WaitForGPUAndReset();
    preparedScene_->Initialize();
    preparedSceneInitialized_ = true;
}

void SceneManager::SwapToPreparedScene() {
    DirectXCommon::GetInstance()->WaitForGPUAndReset();

    if (currentScene_) {
        currentScene_->Finalize();
        currentScene_.reset();
    }

    currentScene_ = std::move(preparedScene_);
    ++sceneGeneration_;
    preparedSceneInitialized_ = false;
    if (!pendingSceneNameForSwap_.empty()) {
        currentSceneName_ = pendingSceneNameForSwap_;
        pendingSceneNameForSwap_.clear();
    }
    loadingTargetSceneName_.clear();

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
