#include "SceneManager.h"
#include "BaseScene.h"
#include "TitleScene.h"
#include "GamePlayScene.h"
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
/// 初期化（最初のシーンを設定）
/// </summary>
void SceneManager::Initialize() {
    // 最初のシーンとして TitleScene を生成
    currentScene_ = std::make_unique<GamePlayScene>();

    // SceneManagerのポインタを渡す
    currentScene_->SetSceneManager(this);

    // シーンの初期化を呼び出す
    currentScene_->Initialize();
}

/// <summary>
/// 終了処理（現在のシーンを解放）
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
/// 更新（シーン切り替え処理と現在のシーンの更新）
/// </summary>
void SceneManager::Update() {
    // --- 次のシーンが予約されている場合 ---
    if (nextScene_ != nullptr) {

        DirectXCommon* dxCommon = DirectXCommon::GetInstance();

        // 1. GPU処理を完了してコマンドリストをOpen状態に戻す
        dxCommon->WaitForGPUAndReset();

        // ※ WaitForGPUAndReset() 内で commandList->Reset() 済みなので、
        // ここでは二度目の Reset() を呼ばない！

        // 2. 現在のシーンを終了・破棄
        if (currentScene_) {
            currentScene_->Finalize();
            currentScene_.reset();
        }

        // 3. 次のシーンを現在のシーンに設定
        currentScene_ = std::move(nextScene_);
        nextScene_ = nullptr;

        // 4. 新しいシーンを初期化（commandList は既にOpen状態）
        currentScene_->Initialize();
    }

    // --- 現在のシーンを更新 ---
    if (currentScene_) {
        currentScene_->Update();
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