#include "SceneManager.h"
#include "BaseScene.h"

#include "DirectXCommon.h"
#include <cassert>
#include <utility>

/// <summary>
/// デストラクタ
/// </summary>
SceneManager::~SceneManager() {
    Finalize();
}

/// <summary>
/// 初期化
/// </summary>
void SceneManager::Initialize(AbstractSceneFactory* factory, const std::string& firstSceneName) {

    sceneFactory_ = factory; // ファクトリーを保持

    currentScene_ = sceneFactory_->CreateScene(firstSceneName); //ファクトリー経由で生成

    // SceneManagerのポインタを渡す
    currentScene_->SetSceneManager(this);
    if (debugEditor_) {
        currentScene_->SetDebugEditor(debugEditor_);
    }

    // シーンの初期化を呼び出す
    currentScene_->Initialize();
}
/// <summary>
/// 終了処理
/// </summary>
void SceneManager::Finalize() {
    // GPU処理をすべて完了させてから破棄
    DirectXCommon::GetInstance()->WaitForGPUAndReset();

    if (currentScene_) {
        currentScene_->Finalize();
        currentScene_.reset();
        currentScene_ = nullptr;
    }
}

/// <summary>
/// 更新
/// </summary>
void SceneManager::Update(float deltaTime) {
    // --- 次のシーンが予約されている場合 ---
    if (nextScene_ != nullptr) {

        DirectXCommon* dxCommon = DirectXCommon::GetInstance();

        // 1. GPU処理を完了してコマンドリストをOpen状態に戻す
        dxCommon->WaitForGPUAndReset();

        // 2. 現在のシーンを終了・破棄
        if (currentScene_) {
            currentScene_->Finalize();
            currentScene_.reset();
        }

        // 3. 次のシーンを現在のシーンに設定
        currentScene_ = std::move(nextScene_);
        nextScene_ = nullptr;

        // 4. 新しいシーンを初期化
        currentScene_->Initialize();
    }

    // --- 現在のシーンを更新 ---
    if (currentScene_) {
        currentScene_->Update(deltaTime);
    }
}


/// <summary>
/// 描画（現在のシーンの描画）
/// </summary>
void SceneManager::Draw() {
    if (currentScene_) {
        currentScene_->Draw();
    }
}

/// <summary>
/// 次のシーンを予約する
/// </summary>
void SceneManager::SetNextScene(std::unique_ptr<BaseScene> nextScene) {
    nextScene_ = std::move(nextScene);
}
BaseScene* SceneManager::GetCurrentScene() const {
    // 保持しているカレントシーンの生ポインタを返す
    return currentScene_.get();
}
void SceneManager::ChangeScene(const std::string& sceneName) {
    if (sceneFactory_ == nullptr) {
        assert(false && "SceneFactory is not set in SceneManager.");
        return;
    }
    if (nextScene_ != nullptr) {
        // 既にシーン遷移中なので、新しいリクエストは無視
        return;
    }

    // ファクトリーを使ってシーンを生成
    std::unique_ptr<BaseScene> newScene = sceneFactory_->CreateScene(sceneName);
    if (debugEditor_) {
        newScene->SetDebugEditor(debugEditor_);
    }
    // SetNextScene に渡して、次のフレームで遷移させる
    if (newScene) {
        SetNextScene(std::move(newScene));
    }
}