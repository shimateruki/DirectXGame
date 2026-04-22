#define NOMINMAX
#include "TitleScene.h"
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
#include "GameProgress.h"
#include <cmath>    // std::sin
#include <algorithm> // std::transform
#include <cctype>    // ::tolower

#include "Easing.h" // 追加: イージング関数利用
#include <CinematicFade.h>
#include <PostEffect.h>

void TitleScene::Initialize() {
    // --- 1. システム基盤の取得 ---
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();
    audioPlayer_ = AudioPlayer::GetInstance();

    // --- 2. モデルのプリロード ---
    ModelManager::GetInstance()->LoadModel("player");
    ModelManager::GetInstance()->LoadModel("teapot");
    LOG("TitleScene Initialized!");

    bgmHandle_ = audioPlayer_->LoadSoundFile("Resources/bgm/Alarm02.mp3");

    // --- 3. マネージャ・共通クラスの初期化 ---
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

    LightEditor::GetInstance()->SetObject3dCommon(object3dCommon_.get());

    // --- 4. サブシステムの生成 ---
    objectManager_ = std::make_unique<ObjectManager>();
    levelLoader_ = std::make_unique<LevelLoader>();
    gameRule_ = std::make_unique<GameRule>();
    gameRule_->Initialize(this);

    BulletManager::GetInstance()->Initialize(object3dCommon_.get(), CollisionManager::GetInstance());

    //  GPUパーティクルの初期化
    gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/white.png");

    // --- 6. レイアウトの読み込み (LevelLoaderへ委譲) ---
    levelLoader_->LoadObjectLayout(this, "Resources/json/3Dobject/titleScene.json");
    levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/titleScene.json");

    LightManager::GetInstance()->LoadState("Resources/json/light/titleScene.json");
    CameraEditor::GetInstance()->Initialize();
    CameraEditor::GetInstance()->LoadFile("title_camera.json");

    // --- スプライト参照取得 ---
    titleSprite_ = GetSpriteByName("title.png");
    startTextSprite_ = GetSpriteByName("gameStartText.png");
    settingTextSprite_ = GetSpriteByName("setting.png");
    optionUI_ = std::make_unique<OptionUI>();
    optionUI_->Initialize(this, spriteCommon_.get());

    // --- イントロ演出: 初期位置・透明度をセット ---
    introPlaying_ = true;
    introTimer_ = 0.0f;
    // 読み込み直後のレイアウト位置を目標にする
    if (titleSprite_) {
        titleTargetPos_ = titleSprite_->GetPosition();
        titleStartPos_ = titleTargetPos_;
        titleStartPos_.y += 40.0f; // 下から上に浮かび上がる
        titleSprite_->SetPosition(titleStartPos_);
        Vector4 col = titleSprite_->GetColor();
        col.w = 0.0f;
        titleSprite_->SetColor(col);
    }
    if (startTextSprite_) {
        startTextTargetPos_ = startTextSprite_->GetPosition();
        startTextStartPos_ = startTextTargetPos_;
        startTextStartPos_.y += 30.0f;
        startTextSprite_->SetPosition(startTextStartPos_);
        Vector4 col = startTextSprite_->GetColor();
        col.w = 0.0f;
        startTextSprite_->SetColor(col);
    }

    // --- オブジェクト一覧をログ出力して調査（デバッグ用） ---
    LOG("TitleScene: listing loaded objects:");
    if (objectManager_) {
        for (auto& obj : objectManager_->GetObjects()) {
            if (!obj) continue;
            LOG(" - name:\"%s\" class:\"%s\" enemyType:\"%s\"", obj->GetName().c_str(), obj->GetClassName().c_str(), obj->GetEnemyType().c_str());
        }
    }

    // --- enemy_core をシーン内から探して初期化（複数対応・名前バリエーション） ---
    enemyCores_.clear();
    enemyCoreBaseYs_.clear();
    if (objectManager_) {
        for (auto& objPtr : objectManager_->GetObjects()) {
            if (!objPtr) continue;
            const std::string& nm = objPtr->GetName();
            std::string lower = nm;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

            bool matched = false;
            // 直接一致候補
            if (lower.find("enemy__core") != std::string::npos || lower.find("enemy_core") != std::string::npos) {
                matched = true;
            }
            // 汎用: 名前に enemy と core が含まれる場合
            if (!matched && lower.find("enemy") != std::string::npos && lower.find("core") != std::string::npos) {
                matched = true;
            }
            // 敵タイプ（Factoryで生成された場合など）
            if (!matched) {
                std::string et = objPtr->GetEnemyType();
                std::transform(et.begin(), et.end(), et.begin(), ::tolower);
                if (et.find("bosscore") != std::string::npos || et.find("boss") != std::string::npos) {
                    matched = true;
                }
            }

            if (matched) {
                enemyCores_.push_back(objPtr.get());
                enemyCoreBaseYs_.push_back(objPtr->GetWorldPosition().y);
            }
        }
    }
    LOG("Found %d enemy core(s) to animate.", (int)enemyCores_.size());
    // =======================================================
     // ★ リスタート演出（電脳リブート）と完全初期化
     // =======================================================
    SceneManager* scm = SceneManager::GetInstance();


    PostEffect::GetInstance()->ResetToBaseParams();

    if (scm->ShouldSkipFade()) {
        CinematicFade::GetInstance()->StartOpen(0.3f);
        scm->ResetSkipFade();
    }
    else {
        CinematicFade::GetInstance()->StartOpen(0.5f);
    }
    dxCommon_->FlushCommandQueue(false);
}

void TitleScene::Finalize() {
    CollisionManager::GetInstance()->ClearObjects();
    BulletManager::GetInstance()->Finalize();

    objectManager_.reset();
    sprites_.clear();
    particleSystem_.reset();
    particleCommon_.reset();
    spriteCommon_.reset();
    object3dCommon_.reset();
}

void TitleScene::Update(float deltaTime) {

    InputManager* input = InputManager::GetInstance();
    Vector4 normalColor = { 0.5f, 0.5f, 0.5f, 1.0f }; // 非選択時は少し暗くする
    Vector4 selectColor = { 1.0f, 1.0f, 1.0f, 1.0f }; // 選択時は明るく白！

    // --- イントロ演出の更新（黒帯が消える演出後にタイトル等をフェード＋浮上） ---
    if (introPlaying_) {
        introTimer_ += deltaTime;
        // 黒帯等の演出待ち時間が終わったらフェード＆浮上を開始
        if (introTimer_ >= introDelay_) {
            float t = (introTimer_ - introDelay_) / introDuration_;
            if (t > 1.0f) t = 1.0f;
            float ease = Easing::OutCubic(t);

            // タイトル
            if (titleSprite_) {
                Vector2 p = titleSprite_->GetPosition();
                p.y = Math::Lerp(titleStartPos_.y, titleTargetPos_.y, ease);
                titleSprite_->SetPosition(p);
                Vector4 c = titleSprite_->GetColor();
                c.w = ease;
                titleSprite_->SetColor(c);
            }
            // スタートテキスト
            if (startTextSprite_) {
                Vector2 p = startTextSprite_->GetPosition();
                p.y = Math::Lerp(startTextStartPos_.y, startTextTargetPos_.y, ease);
                startTextSprite_->SetPosition(p);
                Vector4 c = startTextSprite_->GetColor();
                c.w = ease;
                startTextSprite_->SetColor(c);
            }

            if (t >= 1.0f) {
                introPlaying_ = false; // 演出終了、以降通常の入力受付
            }
        }
        // イントロ中はメニュー入力や選択の処理をスキップする（だが他の更新は続行）
    }
    else {
        // =================================================
        // ステートに応じた入力・UI操作処理
        // =================================================
        switch (currentState_) {
        case TitleState::MainMenu:
            // 上下選択
            if (input->IsActionTriggered("Forward")) {
                do {
                    currentMenuIndex_--;
                    if (currentMenuIndex_ < 0) currentMenuIndex_ = (int)MenuIndex::Max - 1;
                } while (currentMenuIndex_ == (int)MenuIndex::Setting && !settingEnabled_);
            }
            if (input->IsActionTriggered("Backward")) {
                do {
                    currentMenuIndex_++;
                    if (currentMenuIndex_ >= (int)MenuIndex::Max) currentMenuIndex_ = 0;
                } while (currentMenuIndex_ == (int)MenuIndex::Setting && !settingEnabled_);
            }

            // 色の更新
            if (startTextSprite_) startTextSprite_->SetColor(currentMenuIndex_ == (int)MenuIndex::GameStart ? selectColor : normalColor);
            if (settingTextSprite_) {
                // 設定が無効なら常に非選択カラーにする（視覚的に選べないことを示す）
                if (!settingEnabled_) settingTextSprite_->SetColor(normalColor);
                else settingTextSprite_->SetColor(currentMenuIndex_ == (int)MenuIndex::Setting ? selectColor : normalColor);
            }

            // 決定 (Spaceキーのみ)
            if (input->IsActionTriggered("Jump")) {
                if (currentMenuIndex_ == (int)MenuIndex::GameStart) {
                    // ゲーム開始！
                    GameProgress::GetInstance()->Reset();
                    SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
                }
                else if (currentMenuIndex_ == (int)MenuIndex::Setting) {
                    // 設定が有効なときのみ遷移（通常はここに来ない）
                    if (settingEnabled_) {
                        currentState_ = TitleState::OptionMenu;
                    }
                }
            }
            break;

        case TitleState::OptionMenu:
            // ★ OptionUI に処理を丸投げ！ 戻る要求(true)が返ってきたらメインに戻す
            if (optionUI_ && optionUI_->Update(deltaTime)) {
                currentState_ = TitleState::MainMenu;
            }
            break;
        }
    }

    // =================================================
    // 常に実行される更新処理
    // =================================================
    LightEditor::GetInstance()->Update();
    Object3d* cameraTarget = player_;
    if (!cameraTarget && objectManager_ && !objectManager_->GetObjects().empty()) {
        cameraTarget = objectManager_->GetObjects().front().get();
    }


    CameraEditor::GetInstance()->Update(cameraTarget, false);
    CameraManager::GetInstance()->Update();

    // --- enemy_core を上下移動（複数対応） ---
    if (!enemyCores_.empty()) {
        enemyCoreTimer_ += deltaTime * enemyCoreSpeed_;
        for (size_t i = 0; i < enemyCores_.size(); ++i) {
            Object3d* core = enemyCores_[i];
            if (!core) continue;
            float baseY = (i < enemyCoreBaseYs_.size()) ? enemyCoreBaseYs_[i] : core->GetWorldPosition().y;
            // 少し位相ずらすことで複数並んだときに同調しないようにする
            float phase = enemyCoreTimer_ + static_cast<float>(i) * 0.7f;
            float newY = baseY + std::sin(phase) * enemyCoreAmplitude_;
            Vector3 pos = core->GetTranslate(); // コピー
            pos.y = newY;
            core->SetTranslate(pos);
            core->UpdateWorldMatrix();
        }
    }

    // オブジェクト一括更新 (ObjectManagerに委譲)
    if (objectManager_) objectManager_->Update(deltaTime);
    if (particleSystem_) particleSystem_->Update(deltaTime);

    for (auto& sprite : sprites_) {
        sprite->Update();
    }

    // 各種グローバル更新
    BulletManager::GetInstance()->Update(deltaTime);
    CollisionManager::GetInstance()->Update();
}



void TitleScene::DrawUI() {
    spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
    for (auto& sprite : sprites_) {
        Sprite* sp = sprite.get();

        if (currentState_ == TitleState::MainMenu) {
            // メインメニュー時：OptionUI管轄のスプライトなら描画をスキップ
            if (optionUI_ && optionUI_->IsOptionSprite(sp)) {
                continue;
            }
        }
        else if (currentState_ == TitleState::OptionMenu) {
            // オプションメニュー時：メインメニュー用の文字なら描画をスキップ
            if (sp == startTextSprite_ || sp == settingTextSprite_) {
                continue;
            }
        }

        sprite->Draw();
    }
    if (currentState_ == TitleState::OptionMenu && optionUI_) {
        optionUI_->DrawKeyIcons();
    }
}

void TitleScene::Draw() {
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

// シャドウマップ描画の実装
void TitleScene::DrawShadow() {
    if (objectManager_) {
        objectManager_->DrawShadow();
    }
}