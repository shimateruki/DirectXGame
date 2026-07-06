#include "SceneManager.h"
#include "BaseScene.h"

#include "DirectXCommon.h"
#include <cassert>
#include <utility>
#include <fstream>   
#include "json.hpp"  
#include <CinematicFade.h>
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
    if (currentScene_ == nullptr && firstSceneName != "TITLE") {
        SaveLastSceneName("TITLE");
        currentScene_ = sceneFactory_->CreateScene("TITLE");
    }
    if (currentScene_ == nullptr) {
        assert(false && "Failed to create first scene.");
        return;
    }

    currentScene_->SetSceneManager(this);
    if (debugEditor_) {
        currentScene_->SetDebugEditor(debugEditor_);
    }

    // シーンの初期化を呼び出す
    currentScene_->Initialize();
    CinematicFade::GetInstance()->Initialize(currentScene_->GetSpriteCommon());
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
    // 修正: deltaTimeのスパイク（巨大化）を防ぐ
    if (deltaTime > 0.1f) {
        deltaTime = 0.1f;
    }

    CinematicFade* fade = CinematicFade::GetInstance();

    // 1. フェードの更新を常に行う
    fade->Update(deltaTime);

    // --- 次のシーンが予約されている場合 ---
    if (nextScene_ != nullptr) {


        if (!isPlaying_ || deltaTime <= 0.0f) {
            DirectXCommon* dxCommon = DirectXCommon::GetInstance();
            dxCommon->WaitForGPUAndReset();

            // 現在のシーンを破棄
            if (currentScene_) {
                currentScene_->Finalize();
                currentScene_.reset();
            }

            // 次のシーンへ即座に入れ替え
            currentScene_ = std::move(nextScene_);
            nextScene_ = nullptr;
            currentScene_->Initialize();

            // フェードに SpriteCommon を渡して、強制的に全開状態にする
            fade->SetSpriteCommon(currentScene_->GetSpriteCommon());
            fade->StartOpen(0.0f); // 0.0秒で強制オープン
        }
        else {
            // =========================================================
            // 通常のゲームプレイ中のフェード遷移
            // =========================================================
            // ① フェードがまだ閉まり始めていなければ、閉じる処理を開始！
            if (fade->GetState() == CinematicFade::State::kIdle ||
                fade->GetState() == CinematicFade::State::kOpening) {
                fade->StartClose(0.5f); // 0.5秒で閉じる
            }

            // ② フェードが完全に閉まりきった瞬間に、裏でシーンをすり替える！
            if (fade->IsClosed()) {
                DirectXCommon* dxCommon = DirectXCommon::GetInstance();
                dxCommon->WaitForGPUAndReset();

                if (currentScene_) {
                    currentScene_->Finalize();
                    currentScene_.reset();
                }

                currentScene_ = std::move(nextScene_);
                nextScene_ = nullptr;
                currentScene_->Initialize();

                fade->SetSpriteCommon(currentScene_->GetSpriteCommon());
                fade->StartOpen(0.5f);
            }
        }
    }

    // --- 現在のシーンを更新 ---
    if (currentScene_) {
        // 画面が完全に真っ黒な間は、裏のゲーム進行を一時停止する
        if (!fade->IsClosed()) {
            currentScene_->Update(deltaTime);
        }
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
void SceneManager::ChangeScene(const std::string& sceneName, bool skipFade) {
    if (sceneFactory_ == nullptr) {
        assert(false && "SceneFactory is not set in SceneManager.");
        return;
    }
    if (nextScene_ != nullptr) {
        // 既にシーン遷移中なので、新しいリクエストは無視
        return;
    }


    skipFadeNextScene_ = skipFade;

    // ファクトリーを使ってシーンを生成
    std::unique_ptr<BaseScene> newScene = sceneFactory_->CreateScene(sceneName);
    if (newScene == nullptr) {
        assert(false && "Failed to create next scene.");
        return;
    }
    if (debugEditor_) {
        newScene->SetDebugEditor(debugEditor_);
    }

    // SetNextScene に渡して、次のフレームで遷移させる
    if (newScene) {
        // 新しいシーンに SceneManager 自身を登録する
        newScene->SetSceneManager(this);

        SetNextScene(std::move(newScene));

#ifdef USE_IMGUI
        SaveLastSceneName(sceneName);
#endif
    }
}
void SceneManager::SaveLastSceneName(const std::string& sceneName) {
    json root = json::object();
    {
        std::ifstream inputFile(kUserConfigPath);
        if (inputFile.is_open()) {
            try {
                inputFile >> root;
                if (!root.is_object()) {
                    root = json::object();
                }
            }
            catch (...) {
                root = json::object();
            }
        }
    }

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
        if (root.contains("lastScene") && root["lastScene"].is_string()) {
            return root["lastScene"].get<std::string>();
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
    CinematicFade::GetInstance()->Draw();
}

void SceneManager::DrawShadow() {
    if (currentScene_) {
        currentScene_->DrawShadow();
    }
}
