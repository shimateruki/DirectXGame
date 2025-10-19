// ★★★ windows.h の min/max マクロ競合を回避するため、必ずファイルの先頭に置く ★★★
#define NOMINMAX

#include "GamePlayScene.h"
#include "engine/base/DirectXCommon.h"
#include "engine/io/InputManager.h"
#include "engine/audio/AudioPlayer.h"
#include "engine/3d/Object3dCommon.h"
#include "engine/2d/SpriteCommon.h"
#include "engine/3d/Object3d.h"
#include "engine/2d/Sprite.h"
#include "engine/3d/ModelManager.h"
#include "engine/3d/TextureManager.h"
#include "engine/3d/CameraManager.h"   // ★ Draw() で使うためインクルード
#include "engine/3d/CollisionManager.h"
#include "engine/3d/ParticleSystem.h"
#include "externals/imgui/imgui.h"

// ▼▼▼ ゲーム側のオブジェクトをインクルード ▼▼▼
#include "Player.h" // (Playerクラスがあると仮定)

// --- JSON (保存機能) ---
#include <fstream>
#include <string>
#include "externals/nlohmann/json.hpp" // (配置したパスに合わせてください)
// ---------------------------------


void GamePlayScene::Initialize() {
    // ★ using 宣言は必ず関数の「内側」に書く
    using json = nlohmann::json;

    // --- 基盤クラスのポインタを保持 ---
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();
    audioPlayer_ = AudioPlayer::GetInstance();

    // --- 各種初期化 ---
    bgmHandle_ = audioPlayer_->LoadSoundFile("resouces/bgm/Alarm02.mp3"); // (パス注意)
    CameraManager::GetInstance()->Initialize();
    CameraManager::GetInstance()->SetInputManager(inputManager_); // カメラに InputManager を渡す
    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);
    object3dCommon_ = std::make_unique<Object3dCommon>();
    object3dCommon_->Initialize(dxCommon_);
    particleCommon_ = std::make_unique<ParticleCommon>();
    particleCommon_->Initialize(dxCommon_);
    particleSystem_ = std::make_unique<ParticleSystem>();
    particleSystem_->Initialize(particleCommon_.get());

    // --- オブジェクトの生成 ---
    // Plane
    auto plane = std::make_unique<Object3d>();
    plane->Initialize(object3dCommon_.get());
    plane->SetModel("plane");
    plane->SetName("Plane"); // ★ デバッグエディタ用に名前を設定
    objects_.emplace_back(std::move(plane));

    // Player (Teapot) - インデックス [1] になる
    auto player = std::make_unique<Player>(); // Playerクラスを使う
    player->Initialize(object3dCommon_.get()); // Player独自のInitialize
    player->SetModel("teapot");
    player->SetTranslate({ 2.0f, 0.0f, 0.0f });
    player->SetName("Player"); // ★ デバッグエディタ用に名前を設定
    objects_.emplace_back(std::move(player));

    //// Enemy (Bunny) - インデックス [2] になる
    auto enemy = std::make_unique<Object3d>();
    enemy->Initialize(object3dCommon_.get());
    enemy->SetModel("bunny");
    enemy->SetTranslate({ -2.0f, 0.0f, 0.0f });
    enemy->SetName("Enemy"); // ★ デバッグエディタ用に名前を設定
    objects_.emplace_back(std::move(enemy));

    ////// Block (fence) - インデックス [3] から
    for (int i = 0; i < 5; ++i) {
        auto block = std::make_unique<Object3d>();
        block->Initialize(object3dCommon_.get());
        block->SetModel("block");
        block->SetTranslate({ -4.0f, 0.0f, (float)i * 1.8f - 4.0f });
        block->SetName("Block_" + std::to_string(i)); // ★ デバッグエディタ用に名前を設定
        objects_.emplace_back(std::move(block));
    }

    // --- カメラの設定 ---
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();

    // 1. [共通] 追従対象（Player）を設定
    // (デバッグビルド中は Camera::SetTarget 内部で自動的に無効化される)
    camera->SetTarget(&objects_[1]->GetTransform()->translate);

    // 2. [Release用] 追従モードとパラメータを設定 (どちらか一方を選ぶ)
#ifndef _DEBUG // Releaseビルドの時だけ設定が有効になるように
    // --- 例A：ズーム可能な第三者視点 ---
    camera->SetFollowMode(Camera::FollowMode::kAimable);
    camera->ConfigAimable(10.0f, 2.0f, 20.0f); // 初期距離, Min距離, Max距離

    // --- 例B：一人称視点 ---
    //camera->SetFollowMode(Camera::FollowMode::kFirstPerson);
    //camera->ConfigFirstPerson({ 0.0f, 0.5f, 0.1f }); // 視点のオフセット (少し前に出す)

    // --- 例C：固定視点 (デフォルト) ---
    // camera->SetFollowMode(Camera::FollowMode::kFixed);
    // camera->ConfigFixed({0.0f, 5.0f, -15.0f}); // 固定オフセット
#endif


    // --- 衝突判定の設定 ---
    CollisionManager::GetInstance()->ClearObjects();

    // Player (Index 1)
    objects_[1]->SetCollisionAttribute(kPlayer);
    objects_[1]->SetCollisionMask(~kPlayer); // Player以外と当たる
    // (形状とサイズは Player::Initialize で設定済みと仮定)
    CollisionManager::GetInstance()->AddObject(objects_[1].get());

    // Enemy (Index 2)
    objects_[2]->SetCollisionAttribute(kEnemy);
    objects_[2]->SetCollisionMask(~kEnemy); // Enemy以外と当たる
    objects_[2]->SetColliderType(ColliderType::kSphere);
    objects_[2]->SetCollisionRadius(1.0f);
    CollisionManager::GetInstance()->AddObject(objects_[2].get());

    // Blocks (Index 3 から)
    for (size_t i = 3; i < objects_.size(); ++i) {
        objects_[i]->SetCollisionAttribute(kGround);
        objects_[i]->SetCollisionMask(~kGround); // Ground以外と当たる
        objects_[i]->SetColliderType(ColliderType::kAABB);
        objects_[i]->SetCollisionSize({ 1.5f, 1.5f, 1.5f }); // (ブロックモデルに合わせた半分のサイズ)
        CollisionManager::GetInstance()->AddObject(objects_[i].get());
    }
    // Plane (Index 0) - 地面判定を追加する場合
    
    objects_[0]->SetCollisionAttribute(kGround);
    objects_[0]->SetCollisionMask(~kGround);
    objects_[0]->SetColliderType(ColliderType::kAABB);
    objects_[0]->SetCollisionSize({10.0f, 0.1f, 10.0f}); // 薄い箱
    CollisionManager::GetInstance()->AddObject(objects_[0].get());
    


    // --- スプライトの生成 ---
    uint32_t monsterBallHandle = Sprite::LoadTexture("monsterBall"); // resouces/sprite/monsterBall.png
    auto monsterBallSprite = std::make_unique<Sprite>();
    monsterBallSprite->Initialize(spriteCommon_.get(), monsterBallHandle);
    monsterBallSprite->SetPosition({ 200.0f, 360.0f });
    monsterBallSprite->SetSize({ 100.0f, 100.0f });
    sprites_.push_back(std::move(monsterBallSprite));

    uint32_t flameHandle = Sprite::LoadTexture("sample"); // resouces/sprite/sample.png
    auto flameSprite = std::make_unique<Sprite>();
    flameSprite->Initialize(spriteCommon_.get(), flameHandle);
    flameSprite->SetAnimation(4, 0.15f, true); // 4コマ, 0.15秒/コマ, ループ
    flameSprite->Play();
    flameSprite->SetPosition({ 640.0f, 360.0f });
    sprites_.push_back(std::move(flameSprite));


    // --- JSONレイアウトの読み込み (Initializeの最後) ---
    std::ifstream file("scene_layout.json");
    if (file.is_open()) {
        json sceneData;
        try {
            sceneData = json::parse(file);
            if (sceneData.contains("objects") && sceneData["objects"].is_array()) {
                for (const auto& objData : sceneData["objects"]) {
                    if (!objData.contains("name")) continue; // 名前がないデータは無視
                    std::string name = objData["name"];

                    Object3d* targetObject = nullptr;
                    for (auto& obj : objects_) {
                        if (!obj->GetName().empty() && obj->GetName() == name) { // 名前が一致するか
                            targetObject = obj.get();
                            break;
                        }
                    }

                    if (targetObject) {
                        Object3d::Transform* transform = targetObject->GetTransform();
                        if (objData.contains("position") && objData["position"].is_array() && objData["position"].size() == 3) {
                            transform->translate.x = objData["position"][0];
                            transform->translate.y = objData["position"][1];
                            transform->translate.z = objData["position"][2];
                        }
                        if (objData.contains("rotation") && objData["rotation"].is_array() && objData["rotation"].size() == 3) {
                            transform->rotate.x = objData["rotation"][0]; // JSONにはラジアンで保存されている前提
                            transform->rotate.y = objData["rotation"][1];
                            transform->rotate.z = objData["rotation"][2];
                        }
                        if (objData.contains("scale") && objData["scale"].is_array() && objData["scale"].size() == 3) {
                            transform->scale.x = objData["scale"][0];
                            transform->scale.y = objData["scale"][1];
                            transform->scale.z = objData["scale"][2];
                        }
                    }
                }
            }
        }
        catch (json::parse_error& e) {
            OutputDebugStringA("Failed to parse scene_layout.json\n");
            OutputDebugStringA(e.what());
            OutputDebugStringA("\n");
        }
        file.close();
    }

    dxCommon_->FlushCommandQueue(false); // 初期化完了時にコマンドをフラッシュ
}

void GamePlayScene::Finalize() {
    // 解放漏れを防ぐため、逆順で解放するのが安全
    particleSystem_.reset();
    particleCommon_.reset();
    sprites_.clear();
    spriteCommon_.reset();
    objects_.clear();
    object3dCommon_.reset();
    // bgmHandle_ の解放処理があればここに追加
}

void GamePlayScene::Update() {

    // --- Releaseビルド時のカメラ入力処理 ---
#ifndef _DEBUG
    // デバッグビルド「でない」場合
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    Camera::FollowMode currentMode = camera->GetFollowMode(); // 現在のモードを取得

    // モードに応じて入力をカメラに伝える
    if (currentMode == Camera::FollowMode::kAimable) {
        // マウスホイールでズーム
        float wheelDelta = inputManager_->GetMouseWheelDelta();
        if (wheelDelta != 0.0f) {
            camera->AddZoom(wheelDelta);
        }
    }
    if (currentMode == Camera::FollowMode::kAimable || currentMode == Camera::FollowMode::kFirstPerson) {
        // 右クリック(1)中だけ視点回転
        if (inputManager_->IsMouseButtonPressed(1)) {
            Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
            if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) { // 無駄な呼び出しを避ける
                camera->AddRotation(mouseDelta);
            }
        }
    }
#endif

    // --- 常に実行される更新 ---
    CameraManager::GetInstance()->Update(); // カメラ行列の最終計算

    // BGM再生・停止 (テスト用)
    if (inputManager_->IsKeyTriggered(DIK_P)) {
        audioPlayer_->Play(bgmHandle_, true);
    }
    if (inputManager_->IsKeyTriggered(DIK_S)) {
        audioPlayer_->Stop(bgmHandle_);
    }

    // --- ImGui (デバッグビルド時のみ表示される想定) ---
#ifdef _DEBUG
    // (DebugEditor側に移したので、シーン固有のImGuiは削除 or 必要なら追加)
    /*
    ImGui::Begin("Scene Control (GamePlayScene)");
    ImGui::Checkbox("Draw Particles", &isDrawParticles_);
    if (isDrawParticles_) {
        if (inputManager_->IsMouseButtonTriggered(0)) { // 左クリックでパーティクル発生 (テスト用)
            OutputDebugStringA("Spawn Particles Triggered!\n");
            particleSystem_->SpawnParticles({ 0.0f, 0.1f, 0.0f }, 10);
        }
    }
    ImGui::End();
    */
#endif

    // --- パーティクル更新 ---
    // (isDrawParticles_ フラグでオンオフできるようにする)
    if (isDrawParticles_) {
        particleSystem_->Update();
    }

    // --- 1. ゲームロジック (オブジェクト・スプライト) 更新 ---
    for (auto& obj : objects_) {
        obj->Update(); // Player::Update() などが呼ばれる
    }
    for (auto& sprite : sprites_) {
        sprite->Update(); // アニメーション更新など
    }

    // --- 2. 物理 (衝突判定) 更新 ---
    CollisionManager::GetInstance()->Update();

    // --- 3. 行列 (描画準備) 更新 ---
    for (auto& obj : objects_) {
        obj->UpdateMatrix(); // ワールド行列の計算
    }
}

void GamePlayScene::Draw() {

    // --- パーティクル描画 (オンの場合) ---
    if (isDrawParticles_) {
        particleSystem_->Draw();
    }
    // --- 通常描画 (オフの場合) ---
    else {
        // --- Releaseビルド時の一人称視点判定 ---
        bool isFirstPerson = false;
#ifndef _DEBUG
        Camera* camera = CameraManager::GetInstance()->GetMainCamera();
        // 追従対象が設定されており、かつモードが一人称視点か？
        if (camera->GetTarget() && camera->GetFollowMode() == Camera::FollowMode::kFirstPerson) {
            isFirstPerson = true;
        }
#endif

        // --- 3Dオブジェクト描画 ---
        object3dCommon_->SetGraphicsCommand(); // 共通コマンド設定

        for (size_t i = 0; i < objects_.size(); ++i) {
            // ★ 一人称視点なら Player (Index 1) をスキップ
            if (isFirstPerson && i == 1) {
                continue;
            }

            // ブレンドモードを設定して描画 (必要なら)
            // object3dCommon_->SetPipelineState(objects_[i]->GetBlendMode()); 
            objects_[i]->Draw();
        }

        // --- スプライト描画 ---
        spriteCommon_->SetPipeline(dxCommon_->GetCommandList()); // スプライト用パイプライン設定
        for (auto& sprite : sprites_) {
            sprite->Draw();
        }
    }
}

