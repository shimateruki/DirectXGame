#include "SceneManager.h"
#include "BaseScene.h"
#include "engine/graphics/postprocess/Fade.h"

#include "DirectXCommon.h"
#include <cassert>
#include <utility>
#include <fstream>   
#include "json.hpp"  
using json = nlohmann::json; 


// 静的メンバ変数の実体化
SceneManager* SceneManager::instance_ = nullptr;

//  GetInstanceの実装
SceneManager* SceneManager::GetInstance() {
    return instance_;
}

//  コンストラクタで自分自身を登録
SceneManager::SceneManager() {
    instance_ = this;
}


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
    // --- シーン遷移の進行状況をチェック ---
    if (!nextSceneName_.empty()) {
        // フェードアウト中なら何もしない
        if (Fade::GetInstance()->GetStatus() == Fade::Status::FadeOut) {
            if (!Fade::GetInstance()->IsFinished()) {
                return; // フェードが終わるまで待つ
            }
        }
        
        // フェードアウトが完了したら、次のシーンのインスタンスを生成して予約
        std::unique_ptr<BaseScene> newScene = sceneFactory_->CreateScene(nextSceneName_);
        if (debugEditor_) {
            newScene->SetDebugEditor(debugEditor_);
        }
        SetNextScene(std::move(newScene));
        
        // フェードインを開始
        Fade::GetInstance()->StartFadeIn(1.0f);
        nextSceneName_ = "";
    }

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
    if (!nextSceneName_.empty() || nextScene_ != nullptr) {
        // 既にシーン遷移中なので、新しいリクエストは無視
        return;
    }

    // 次のシーン名を保存し、フェードアウトを開始する
    nextSceneName_ = sceneName;
    Fade::GetInstance()->StartFadeOut(1.0f);

#ifdef USE_IMGUI
    SaveLastSceneName(sceneName);
#endif
}

void SceneManager::SaveLastSceneName(const std::string& sceneName) {
    json root;
    root["lastScene"] = sceneName;

    std::ofstream file(kUserConfigPath);
    if (file.is_open()) {
        file << root.dump(4);
        file.close();
    }
}

// ★追加: 読み込みの実装
std::string SceneManager::LoadLastSceneName() {
    std::ifstream file(kUserConfigPath);
    if (!file.is_open()) {
        return ""; // ファイルが存在しない（初回起動など）
    }

    try {
        json root;
        file >> root;
        if (root.contains("lastScene")) {
            return root["lastScene"];
        }
    }
    catch (...) {
        // エラー時は無視
    }
    return "";
}


void SceneManager::DrawUI() {
    // 現在のシーンがあれば、そのシーンのUI描画を呼ぶ
    if (currentScene_) {
        currentScene_->DrawUI();
    }
}

void SceneManager::DrawShadow() {
    if (currentScene_) {
        currentScene_->DrawShadow();
    }
}