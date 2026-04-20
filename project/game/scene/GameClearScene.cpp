#define NOMINMAX
#include "GameClearScene.h"
#include "DirectXCommon.h"
#include "InputManager.h"
#include "AudioPlayer.h"
#include "Object3dCommon.h"
#include "SpriteCommon.h"
#include "Object3d.h"
#include "Sprite.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "ParticleSystem.h"
#include "imgui.h"
#include "LightManager.h"
#include "SceneManager.h"
#include "DebugConsole.h"
#include "BulletManager.h"
#include "LevelLoader.h"
#include "GameRule.h"
#include "CameraEditor.h"
#include "LightEditor.h"
#include "ParticleManager.h"
#include "GPUParticleManager.h"
#include <SaveDataManager.h>

void GameClearScene::Initialize() {
    // --- 1. システム基盤の取得 ---
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();
    audioPlayer_ = AudioPlayer::GetInstance();

    // --- 2. リソースロード ---
    LOG("GameClearScene Initialized!");

    bgmHandle_ = audioPlayer_->LoadSoundFile("Resources/bgm/Alarm02.mp3");

    // --- 3. 共通クラス・マネージャの初期化 ---
    CameraManager::GetInstance()->Initialize();
    CameraManager::GetInstance()->SetInputManager(inputManager_);

    object3dCommon_ = std::make_unique<Object3dCommon>();
    object3dCommon_->Initialize(dxCommon_);

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    particleCommon_ = std::make_unique<ParticleCommon>();
    particleCommon_->Initialize(dxCommon_);

    particleSystem_ = std::make_unique<ParticleSystem>();
    particleSystem_->Initialize(particleCommon_.get(), "Resources/sprite/white.png");

    // ★追加: シングルトンのParticleManagerに今のシーンのシステムを紐づける！
    ParticleManager::GetInstance()->Initialize(particleSystem_.get());

    // ライトエディタの設定
    LightEditor::GetInstance()->SetObject3dCommon(object3dCommon_.get());

    // --- 4. サブシステムの生成 ---
    objectManager_ = std::make_unique<ObjectManager>();
    levelLoader_ = std::make_unique<LevelLoader>();
    gameRule_ = std::make_unique<GameRule>();
    gameRule_->Initialize(this);

    // 弾マネージャの初期化
    BulletManager::GetInstance()->Initialize(object3dCommon_.get(), CollisionManager::GetInstance());

    //  GPUパーティクルの初期化
    GPUParticleManager::GetInstance()->Initialize(dxCommon_);
    GPUParticleManager::GetInstance()->LoadAllPresets("Resources/json/gpu_particles/");
    gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/white.png");

    // --- 5. 固定スプライトの生成 (必要であれば) ---
 // --- セーブデータの読み込み ---
    SaveDataManager::GetInstance()->Load();
    float clearTime = SaveDataManager::GetInstance()->GetLatestClearTime();
    float bestTime = SaveDataManager::GetInstance()->GetBestTime();

    // --- 今回のタイムUI ---
    clearTimeUI_ = std::make_unique<TimeAttackUI>();
    clearTimeUI_->Initialize(spriteCommon_.get());
    clearTimeUI_->SetPosition({ 400.0f, 300.0f }); // 画面中央付近など好きな位置に
    clearTimeUI_->SetTime(clearTime);
    clearTimeUI_->Update(0.0f); // 1回だけUpdateを呼んでテクスチャを数字に反映させる

    // --- ベストタイムUI ---
    bestTimeUI_ = std::make_unique<TimeAttackUI>();
    bestTimeUI_->Initialize(spriteCommon_.get());
    bestTimeUI_->SetPosition({ 400.0f, 450.0f }); // 今回のタイムの下などに配置
    bestTimeUI_->SetTime(bestTime);
    bestTimeUI_->Update(0.0f);

    // --- 6. レベルデータの読み込み ---
    // 自前関数ではなくLevelLoaderに委譲
    levelLoader_->LoadObjectLayout(this, "Resources/json/3Dobject/gameClearScene.json");
    levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/gameClearScene.json");
    if (player_) {
        // プレイヤーの移動入力やカメラ操作をシャットアウト！
        player_->SetIsControlActive(false);
        player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
        player_->SetGravity(0.0f);
    }
    LightManager::GetInstance()->LoadState("Resources/json/light/gameClearScene.json");
    CameraEditor::GetInstance()->Initialize();
    CameraEditor::GetInstance()->LoadFile("gameClear_camera.json");

    dxCommon_->FlushCommandQueue(false);
}

void GameClearScene::Finalize() {
    CollisionManager::GetInstance()->ClearObjects();
    BulletManager::GetInstance()->Finalize();

    // スマートポインタの解放
    objectManager_.reset();
    sprites_.clear();
    particleSystem_.reset();
    particleCommon_.reset();
    spriteCommon_.reset();
    object3dCommon_.reset();
}

void GameClearScene::Update(float deltaTime) {
    // =========================================================
    // 1. 基盤システムの更新 (常に動かすもの)
    // =========================================================
    LightEditor::GetInstance()->Update();

    // カメラのターゲット設定
    Object3d* cameraTarget = player_;
    if (!cameraTarget && objectManager_ && !objectManager_->GetObjects().empty()) {
        cameraTarget = objectManager_->GetObjects().front().get();
    }

    // カメラとライトの更新
    CameraEditor::GetInstance()->Update(cameraTarget, false);
    CameraManager::GetInstance()->Update();

    // ゲームオブジェクト・エフェクトの更新
    if (objectManager_) objectManager_->Update(deltaTime);
    particleSystem_->Update(deltaTime);
    GPUParticleManager::GetInstance()->Update(deltaTime); // GPUパーティクルも忘れずに！

    BulletManager::GetInstance()->Update(deltaTime);
    CollisionManager::GetInstance()->Update();

    // =========================================================
    // 2. 演出ステートマシンの更新
    // =========================================================
    stateTimer_ += deltaTime;

    switch (clearState_) {
    case ClearState::kPlayerAction:
        // 【状態1】プレイヤーの勝利アクション中
        // 3秒間モーションを見せてからタイム表示へ移行
        if (stateTimer_ > 3.0f) {
            clearState_ = ClearState::kShowTime;
            stateTimer_ = 0.0f;

            // タイム表示開始のログ
            DebugConsole::GetInstance()->AddLog("【RESULT】 タイム表示開始！");
        }
        break;

    case ClearState::kShowTime:
        // 【状態2】リザルトタイムのフェードイン
        uiAlpha_ += deltaTime * 2.0f; // 約0.5秒でフェード完了
        if (uiAlpha_ >= 1.0f) {
            uiAlpha_ = 1.0f;

            // タイムが完全に出てから少し余韻(1秒)を置いてメニュー選択へ
            if (stateTimer_ > 1.0f) {
                clearState_ = ClearState::kSelectMenu;
                DebugConsole::GetInstance()->AddLog("【RESULT】 メニュー選択可能");
            }
        }
        break;

    case ClearState::kSelectMenu:
    {
        // 【状態3】リトライ or タイトルの選択
        InputManager* input = InputManager::GetInstance();

        // --- メニュー選択（上下） ---
        if (input->IsKeyTriggered(DIK_UP) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_UP)) {
            currentMenuIndex_--;
            if (currentMenuIndex_ < 0) currentMenuIndex_ = (int)MenuIndex::Max - 1;
        }
        if (input->IsKeyTriggered(DIK_DOWN) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_DOWN)) {
            currentMenuIndex_++;
            if (currentMenuIndex_ >= (int)MenuIndex::Max) currentMenuIndex_ = 0;
        }

        // --- 決定処理 ---
        if (input->IsKeyTriggered(DIK_SPACE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {
            if (currentMenuIndex_ == (int)MenuIndex::Retry) {
                SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
            }
            else if (currentMenuIndex_ == (int)MenuIndex::Title) {
                SceneManager::GetInstance()->ChangeScene("TITLE");
            }
        }

        // 選択中のメニューを分かりやすくするための色調整 (例)
        // ※後ほど、Retry/Title用のスプライトをここで色変えするとリッチになります！
        break;
    }
    }

    // =========================================================
    // 3. UI（スプライト）の最終更新
    // =========================================================

    // タイムアタックUIの更新
    if (clearTimeUI_) clearTimeUI_->Update(deltaTime);
    if (bestTimeUI_) bestTimeUI_->Update(deltaTime);

    // シーン内の全スプライトを更新
    for (auto& sprite : sprites_) {
        sprite->Update();
    }
}
void GameClearScene::Draw() {
    // --- 一人称視点判定 ---
    bool isFirstPerson = false;
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
#ifndef _DEBUG
    if (camera->GetFollowTarget() && camera->GetFollowMode() == Camera::FollowMode::kFirstPerson) {
        isFirstPerson = true;
    }
#endif

    ID3D12Resource* pointLightRes = LightManager::GetInstance()->GetPointLightResource();
    ID3D12Resource* spotLightRes = LightManager::GetInstance()->GetSpotLightResource();
    object3dCommon_->SetGraphicsCommand();

    auto& objects = objectManager_->GetObjects();

    // --- 1. 不透明描画 ---
    for (auto& obj : objects) {
        if (isFirstPerson && obj.get() == player_) continue;
        if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7) continue; // ★修正: フォグ(7)も不透明パスから除外
        obj->Draw(pointLightRes, spotLightRes);
    }

    // --- 2. 中間描画 (弾・デバッグ) ---
    BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
    LightEditor::GetInstance()->Draw3D();

    // --- 3. 透明描画 ---
    for (auto& obj : objects) {
        if (isFirstPerson && obj.get() == player_) continue;
        if (obj->GetMaterialType() == 1) { // 透明のみ描画
            obj->Draw(pointLightRes, spotLightRes);
        }
    }
    particleSystem_->Draw();

    // =======================================================
    // 4. ローカルフォグ (霧の箱) の描画！
    // =======================================================
    bool hasFog = false;
    for (auto& obj : objects) {
        if (obj->GetMaterialType() == 7) hasFog = true;
    }

    if (hasFog) {
        dxCommon_->PreDrawLocalFog();
        for (auto& obj : objects) {
            if (obj->GetMaterialType() == 7) {
                obj->DrawLocalFog(dxCommon_->GetDepthSrvHandle());
            }
        }
        dxCommon_->PostDrawLocalFog();
    }

    // =======================================================
    // 5. GPUパーティクルの描画！
    // =======================================================
    dxCommon_->UpdateGrabTexture();
    dxCommon_->PreDrawLocalFog();
    if (camera) {
        GPUParticleManager::GetInstance()->Draw(
            dxCommon_->GetCommandList(),
            camera->GetViewMatrix(),
            camera->GetProjectionMatrix(),
            gpuParticleTexHandle_,
            dxCommon_->GetDepthSrvHandle()
        );
    }
    dxCommon_->PostDrawLocalFog();
}



// ====================================================================
// UI描画専用の関数
// ====================================================================
void GameClearScene::DrawUI() {
    // --- 4. 2D描画 (UIスプライト) ---
    spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
    for (auto& sprite : sprites_) {
        sprite->Draw();
    }
    if (clearTimeUI_) clearTimeUI_->Draw();
    if (bestTimeUI_) bestTimeUI_->Draw();
}

// ★追加: シャドウマップ描画の実装
void GameClearScene::DrawShadow() {
    if (objectManager_) {
        objectManager_->DrawShadow();
    }
}