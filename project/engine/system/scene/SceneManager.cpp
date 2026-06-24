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
}

void SceneManager::Finalize() {
    if (loadingFuture_.valid()) {
        loadingFuture_.wait();
        preparedScene_ = loadingFuture_.get();
    }

    DirectXCommon::GetInstance()->WaitForGPUAndReset();

    if (currentScene_) {
        currentScene_->Finalize();
        currentScene_.reset();
    }

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

    case TransitionPhase::Loading:
        loadingElapsed_ += effectiveDeltaTime;
        if (currentScene_) {
            currentScene_->Update(effectiveDeltaTime);
        }

        {
            const bool asyncReady = IsAsyncSceneReady();
            const float waitingProgress = std::min(0.78f, 0.12f + loadingElapsed_ * 0.24f);
            SetLoadingProgress(asyncReady ? 0.86f : waitingProgress);
        }

        if (loadingElapsed_ >= minLoadingDisplayTime_ &&
            IsAsyncSceneReady() &&
            Fade::GetInstance()->GetStatus() != Fade::Status::FadeIn) {
            SetLoadingProgress(0.92f);
            PrepareLoadedSceneOnMainThread();
            SetLoadingProgress(1.0f);
            Fade::GetInstance()->StartFadeOut(0.35f);
            transitionPhase_ = TransitionPhase::FadingOutLoading;
        }
        return;

    case TransitionPhase::FadingOutLoading:
        if (currentScene_) {
            currentScene_->Update(effectiveDeltaTime);
        }
        SetLoadingProgress(1.0f);

        if (Fade::GetInstance()->GetStatus() == Fade::Status::FadeOut && !Fade::GetInstance()->IsFinished()) {
            return;
        }
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

    nextSceneName_ = sceneName;
    pendingSceneNameForSwap_ = sceneName;
    transitionPhase_ = TransitionPhase::FadingOutCurrent;
    Fade::GetInstance()->StartFadeOut(1.0f);

#ifdef USE_IMGUI
    SaveLastSceneName(sceneName);
#endif
}

void SceneManager::BeginLoadingTransition() {
    DirectXCommon::GetInstance()->WaitForGPUAndReset();

    if (currentScene_) {
        currentScene_->Finalize();
        currentScene_.reset();
    }

    loadingTargetSceneName_ = nextSceneName_;
    nextSceneName_.clear();
    loadingElapsed_ = 0.0f;

    currentScene_ = std::make_unique<LoadingScene>();
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
    if (!preparedScene_ && loadingFuture_.valid()) {
        preparedScene_ = loadingFuture_.get();
    }

    assert(preparedScene_ && "Scene creation failed during loading transition.");
    if (!preparedScene_) {
        transitionPhase_ = TransitionPhase::Idle;
        return;
    }

    preparedScene_->SetSceneManager(this);
    if (debugEditor_) {
        preparedScene_->SetDebugEditor(debugEditor_);
    }

    // DirectXリソース生成は共通コマンドリストを使うため、メインスレッドで初期化する。
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
    nextScene_ = nullptr;
    if (!pendingSceneNameForSwap_.empty()) {
        currentSceneName_ = pendingSceneNameForSwap_;
        pendingSceneNameForSwap_.clear();
    }

    if (currentScene_) {
        currentScene_->SetSceneManager(this);
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
