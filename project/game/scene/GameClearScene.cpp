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
#include "PlayerState.h"
void GameClearScene::Initialize() {
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();
    audioPlayer_ = AudioPlayer::GetInstance();

    bgmHandle_ = audioPlayer_->LoadSoundFile("Resources/bgm/Alarm02.mp3");

    CameraManager::GetInstance()->Initialize();
    CameraManager::GetInstance()->SetInputManager(inputManager_);

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    object3dCommon_ = std::make_unique<Object3dCommon>();
    object3dCommon_->Initialize(dxCommon_);

    particleCommon_ = std::make_unique<ParticleCommon>();
    particleCommon_->Initialize(dxCommon_);

    particleSystem_ = std::make_unique<ParticleSystem>();
    particleSystem_->Initialize(particleCommon_.get(), "Resources/sprite/white.png");

    objectManager_ = std::make_unique<ObjectManager>();

    // --- セーブデータ読み込み ---
    SaveDataManager::GetInstance()->Load();
    float clearTime = SaveDataManager::GetInstance()->GetLatestClearTime();
    float bestTime = SaveDataManager::GetInstance()->GetBestTime();

    // --- タイムUI初期化（右寄せ配置） ---
    clearTimeUI_ = std::make_unique<TimeAttackUI>();
    clearTimeUI_->Initialize(spriteCommon_.get());
    clearTimeUI_->SetPosition({ 850.0f, 350.0f });
    clearTimeUI_->SetTime(clearTime);
    clearTimeUI_->SetAlpha(0.0f);
    bestTimeUI_ = std::make_unique<TimeAttackUI>();
    bestTimeUI_->Initialize(spriteCommon_.get());
    bestTimeUI_->SetPosition({ 850.0f, 500.0f });
    bestTimeUI_->SetTime(bestTime);
    bestTimeUI_->SetAlpha(0.0f);
    // --- レベルデータ読み込み ---
    levelLoader_ = std::make_unique<LevelLoader>();
    levelLoader_->LoadObjectLayout(this, "Resources/json/3Dobject/gameClearScene.json");
    levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/gameClearScene.json");

    // =========================================================
    // ★ 修正：エディター配置スプライトの特定と初期非表示化
    // =========================================================
    for (auto& sprite : sprites_) {
 
        if (sprite->GetName() == "GameClear.png") gameClearSprite_ = sprite.get();
        if (sprite->GetName() == "restartText.png")     retryTextSprite_ = sprite.get();
        if (sprite->GetName() == "title.png")     titleTextSprite_ = sprite.get();
    }

    auto HideSprite = [](Sprite* s) {
        if (s) { Vector4 c = s->GetColor(); c.w = 0.0f; s->SetColor(c); }
        };
    HideSprite(gameClearSprite_);
    HideSprite(retryTextSprite_);
    HideSprite(titleTextSprite_);

    if (player_) {
        player_->SetIsControlActive(false);

        // 1. JSONで配置した位置（＝ガッツポーズを見せたい最高の場所）を記憶！
        targetPlayerPos_ = player_->GetTransform()->translate;
        targetPlayerRot_ = player_->GetRotation();
        // 2. プレイヤーを画面の奥（または手前）にワープさせる！
        Vector3 startPos = targetPlayerPos_;
        startPos.z += 15.0f;

        player_->GetTransform()->translate = startPos;
        player_->UpdateWorldMatrix();

        // 3. 最初は「走りステート」にする！
        player_->ChangeState(std::make_unique<PlayerStateRun>());
    }
    LightManager::GetInstance()->LoadState("Resources/json/light/gameClearScene.json");
    CameraEditor::GetInstance()->Initialize();
    CameraEditor::GetInstance()->LoadFile("gameClear_camera.json");
    gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/white.png");
    GPUParticleManager::GetInstance()->Initialize(dxCommon_);
    GPUParticleManager::GetInstance()->LoadAllPresets("Resources/json/gpu_particles/");
    clearState_ = ClearState::kRunIn;
    stateTimer_ = 0.0f;
    resultAlpha_ = 0.0f;
    menuAlpha_ = 0.0f;
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
    LightEditor::GetInstance()->Update();
    Object3d* cameraTarget = player_;
    if (!cameraTarget && objectManager_ && !objectManager_->GetObjects().empty()) {
        cameraTarget = objectManager_->GetObjects().front().get();
    }

    CameraEditor::GetInstance()->Update(cameraTarget, false);
    CameraManager::GetInstance()->Update();

    if (objectManager_) objectManager_->Update(deltaTime);
    particleSystem_->Update(deltaTime);
    GPUParticleManager::GetInstance()->Update(deltaTime);

    BulletManager::GetInstance()->Update(deltaTime);
    CollisionManager::GetInstance()->Update();

    // =========================================================
    // ★ 修正：演出シーケンス制御とメニュー選択フィードバック
    // =========================================================
    stateTimer_ += deltaTime;

    switch (clearState_) {
    case ClearState::kRunIn:
    {
        if (player_) {
            Vector3 currentPos = player_->GetTransform()->translate;
            Vector3 dir = { targetPlayerPos_.x - currentPos.x, 0.0f, targetPlayerPos_.z - currentPos.z };
            float dist = std::sqrt(dir.x * dir.x + dir.z * dir.z);

            if (dist > 0.5f) {
                // 走らせ続ける
                dir.x /= dist; dir.z /= dist;
                float runSpeed = 12.0f;
                player_->SetVelocity({ dir.x * runSpeed, 0.0f, dir.z * runSpeed });

                float angle = std::atan2(dir.x, dir.z);
                player_->SetRotation({ 0.0f, angle, 0.0f });
            }
            else {
                // ゴール到達！！
                player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
                player_->GetTransform()->translate = targetPlayerPos_;

                // =========================================================
                // ★ 追加：走るのをやめた瞬間、エディターで設定した「最高の向き」に戻す！
                // =========================================================
                player_->SetRotation(targetPlayerRot_);

                // 勝利ステートへ移行！
                player_->ChangeState(std::make_unique<PlayerStateWin>());

                clearState_ = ClearState::kVictoryMotion;
                stateTimer_ = 0.0f;
            }
        }
        break;
    }
    case ClearState::kVictoryMotion:
        // 勝利ジャンプの頂点（PlayerStateWinで設定した約0.6秒）を待つ
        if (stateTimer_ > 0.6f) {
            clearState_ = ClearState::kShowResult;
            stateTimer_ = 0.0f;
        }
        break;

    case ClearState::kShowResult:
        // ロゴとタイマーをフェードイン
        resultAlpha_ += deltaTime * 2.0f;
        if (resultAlpha_ > 1.0f) resultAlpha_ = 1.0f;

        if (gameClearSprite_) {
            Vector4 c = gameClearSprite_->GetColor(); c.w = resultAlpha_;
            gameClearSprite_->SetColor(c);
        }
        if (clearTimeUI_) clearTimeUI_->SetAlpha(resultAlpha_);
        if (bestTimeUI_) bestTimeUI_->SetAlpha(resultAlpha_);

        // ★ 完全に表示された後、ボタン入力を待つ
        if (resultAlpha_ >= 1.0f) {
            if (inputManager_->IsKeyTriggered(DIK_SPACE) || inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {

                clearState_ = ClearState::kShowMenu;
                stateTimer_ = 0.0f;

                if (player_) {
                
                    player_->ChangeState(std::make_unique<PlayerStateWinReturn>());

                    DebugConsole::GetInstance()->AddLog("【RESULT】 メニューへ。自然落下＆ポーズ戻し開始！");
                }
            }
        }
        break;
        // タイマーが出てから1秒後にメニュー選択へ
        if (stateTimer_ > 1.0f) {
            clearState_ = ClearState::kShowMenu;
            stateTimer_ = 0.0f;
        }
        break;

    case ClearState::kShowMenu:
    {
        // メニュー自体のフェードイン
        menuAlpha_ += deltaTime * 2.0f;
        if (menuAlpha_ > 1.0f) menuAlpha_ = 1.0f;

        // 入力
        if (inputManager_->IsKeyTriggered(DIK_UP) || inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_UP)) {
            currentMenuIndex_ = (int)MenuIndex::Retry;
        }
        if (inputManager_->IsKeyTriggered(DIK_DOWN) || inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_DOWN)) {
            currentMenuIndex_ = (int)MenuIndex::Title;
        }

        // --- 選択フィードバック演出 ---
        auto ApplyMenuEffect = [&](Sprite* s, bool isSelected) {
            if (!s) return;
            // 選択中：不透明(1.0)＋白(1,1,1)＋少し拡大
            // 未選択：半透明(0.3)＋暗い(0.5,0.5,0.5)＋元のサイズ
            float alpha = (isSelected ? 1.0f : 0.3f) * menuAlpha_;
            Vector4 color = isSelected ? Vector4{ 1,1,1,alpha } : Vector4{ 0.5f,0.5f,0.5f,alpha };
            s->SetColor(color);

            // サイズでの強調（元のサイズを320x80と仮定して1.1倍に）
            Vector2 baseSize = { 320.0f, 80.0f };
            s->SetSize(isSelected ? Vector2{ baseSize.x * 1.1f, baseSize.y * 1.1f } : baseSize);
            };

        ApplyMenuEffect(retryTextSprite_, currentMenuIndex_ == (int)MenuIndex::Retry);
        ApplyMenuEffect(titleTextSprite_, currentMenuIndex_ == (int)MenuIndex::Title);

        // 決定
        if (inputManager_->IsKeyTriggered(DIK_SPACE) || inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {
            if (currentMenuIndex_ == (int)MenuIndex::Retry) SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
            else SceneManager::GetInstance()->ChangeScene("TITLE");
        }
        break;
    }
    }

    // UIの行列更新
    if (clearTimeUI_) clearTimeUI_->Update(deltaTime);
    if (bestTimeUI_) bestTimeUI_->Update(deltaTime);

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