
#define NOMINMAX
#include "GamePlayScene.h"
#include "DirectXCommon.h"
#include "InputManager.h"
#include "AudioPlayer.h"
#include "Object3dCommon.h"
#include "SpriteCommon.h"
#include "Object3d.h"
#include "Sprite.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "CameraManager.h"    // ★ Draw() で使うためインクルード
#include "CollisionManager.h"
#include "ParticleSystem.h"
#include "imgui.h"
#include"LightManager.h"
#include <EventManager.h>

// ★★★ シーンマネージャ対応で追加 ★★★
#include "SceneManager.h" // SceneManager をインクルード
#include <cassert>        // assert() のために追加

// ★ デバッグビルド時のみ DebugEditor をインクルード
#ifdef _DEBUG
#include "DebugEditor.h" 
#endif

// --- JSON (保存機能) ---
#include <fstream>
#include <string>
#include "json.hpp" 



void GamePlayScene::LoadObjectLayout(const std::string& filename) {
    using json = nlohmann::json;
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::string warnMsg = "Warning: Could not open " + filename + " for Object layout.\n";
        OutputDebugStringA(warnMsg.c_str());
        return; // ファイルが開けなければ処理中断
    }

    json sceneData;
    try {
        sceneData = json::parse(file);
        if (sceneData.contains("objects") && sceneData["objects"].is_array()) {
            for (const auto& objData : sceneData["objects"]) {
                if (!objData.contains("name") || !objData["name"].is_string()) continue;
                std::string name = objData["name"].get<std::string>(); // .get<string>() を使う

                // objects_ 配列から名前で検索
                Object3d* targetObject = nullptr;
                for (auto& obj : objects_) {
                    // ★ GetName() が実装されている前提
                    if (obj && !obj->GetName().empty() && obj->GetName() == name) {
                        targetObject = obj.get();
                        break;
                    }
                }

                // 見つかったら Transform を更新
                if (targetObject) {
                    Object3d::Transform* transform = targetObject->GetTransform();

                    if (objData.contains("position") && objData["position"].is_array() && objData["position"].size() == 3) {
                        transform->translate.x = objData["position"][0].get<float>(); // .get<float>()
                        transform->translate.y = objData["position"][1].get<float>();
                        transform->translate.z = objData["position"][2].get<float>();
                    }
                    if (objData.contains("rotation") && objData["rotation"].is_array() && objData["rotation"].size() == 3) {
                        transform->rotate.x = objData["rotation"][0].get<float>(); // ラジアン前提
                        transform->rotate.y = objData["rotation"][1].get<float>();
                        transform->rotate.z = objData["rotation"][2].get<float>();
                    }
                    if (objData.contains("scale") && objData["scale"].is_array() && objData["scale"].size() == 3) {
                        transform->scale.x = objData["scale"][0].get<float>();
                        transform->scale.y = objData["scale"][1].get<float>();
                        transform->scale.z = objData["scale"][2].get<float>();
                    }

                    // ★ 更新を即時反映させるため Update を呼ぶ
                    //   (ただし、UpdateMatrix を統合した場合は Object3d::Update() を呼ぶ)
                    targetObject->Update();
                }
            } // for (objData) 終わり
        } // if contains("objects") 終わり
    }
    catch (json::parse_error& e) {
        OutputDebugStringA(("Failed to parse " + filename + "\n").c_str());
        OutputDebugStringA(e.what());
        OutputDebugStringA("\n");
    }

    file.close();
}

void GamePlayScene::LoadSpriteLayout(const std::string& filename) {
    using json = nlohmann::json;
    std::ifstream file(filename);

    if (!file.is_open()) {
        // ファイルが開けなかった場合 (初回起動時など) は何もしないか、警告を出す
        std::string warnMsg = "Warning: Could not open " + filename + "\n";
        OutputDebugStringA(warnMsg.c_str());
        return;
    }

    json layoutData;
    try {
        layoutData = json::parse(file); // JSON をパース

        // "sprites" 配列が存在し、配列形式かチェック
        if (layoutData.contains("sprites") && layoutData["sprites"].is_array()) {

            // JSON 配列内の各スプライトデータを処理
            for (const auto& spriteData : layoutData["sprites"]) {

                // "name" がなければスキップ (名前で識別するため必須)
                if (!spriteData.contains("name") || !spriteData["name"].is_string()) {
                    continue;
                }
                std::string name = spriteData["name"];

                // --- 名前を使って sprites_ ベクターから該当スプライトを検索 ---
                Sprite* targetSprite = nullptr;
                for (auto& sprite : sprites_) {
                    // Sprite クラスに GetName() が実装されている前提
                    if (sprite && !sprite->GetName().empty() && sprite->GetName() == name) {
                        targetSprite = sprite.get();
                        break;
                    }
                }

                // --- 見つかったスプライトのプロパティを更新 ---
                if (targetSprite) {

                    // Position (Vector2)
                    if (spriteData.contains("position") && spriteData["position"].is_array() && spriteData["position"].size() == 2) {
                        targetSprite->SetPosition({
                            spriteData["position"][0].get<float>(), // .get<float>() で型を指定
                            spriteData["position"][1].get<float>()
                            });
                    }

                    // Size (Vector2)
                    if (spriteData.contains("size") && spriteData["size"].is_array() && spriteData["size"].size() == 2) {
                        targetSprite->SetSize({
                            spriteData["size"][0].get<float>(),
                            spriteData["size"][1].get<float>()
                            });
                    }

                    // Anchor Point (Vector2)
                    if (spriteData.contains("anchor") && spriteData["anchor"].is_array() && spriteData["anchor"].size() == 2) {
                        targetSprite->SetAnchorPoint({
                            spriteData["anchor"][0].get<float>(),
                            spriteData["anchor"][1].get<float>()
                            });
                    }

                    // Color (Vector4)
                    if (spriteData.contains("color") && spriteData["color"].is_array() && spriteData["color"].size() == 4) {
                        targetSprite->SetColor({
                            spriteData["color"][0].get<float>(),
                            spriteData["color"][1].get<float>(),
                            spriteData["color"][2].get<float>(),
                            spriteData["color"][3].get<float>()
                            });
                    }

                    // Rotation (float) - もし Sprite に回転があれば
                    // if (spriteData.contains("rotation") && spriteData["rotation"].is_number()) {
                    //     targetSprite->SetRotation(spriteData["rotation"].get<float>());
                    // }

                    // ★ 更新したプロパティを反映させるために Update() を呼ぶ (重要)
                    targetSprite->Update();
                }
            } // for (spriteData) 終わり
        } // if (contains("sprites")) 終わり

    }
    catch (json::parse_error& e) {
        // JSON パース失敗時のエラー処理
        OutputDebugStringA("Failed to parse sprite_layout.json\n");
        OutputDebugStringA(e.what());
        OutputDebugStringA("\n");
    }

    file.close(); // ファイルを閉じる
}

void GamePlayScene::AddObject(std::unique_ptr<Object3d> object) {
    if (object == nullptr) {
        return;
    }

    // ★ デフォルトの衝突設定 (スポーンしたオブジェクト用)
    // (必要に応じて変更してください)
    object->SetCollisionAttribute(kGround); // 仮に「地面」
    object->SetCollisionMask(~kGround);
    object->SetColliderType(ColliderType::kAABB);
    object->SetCollisionSize({ 1.0f, 1.0f, 1.0f });

    // 衝突マネージャに登録
    CollisionManager::GetInstance()->AddObject(object.get());

    // シーンのリストに追加
    objects_.emplace_back(std::move(object));
}

void GamePlayScene::Initialize() {
    // ★ using 宣言は必ず関数の「内側」に書く
    using json = nlohmann::json;



    // --- 基盤クラスのポインタを保持 ---
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();
    audioPlayer_ = AudioPlayer::GetInstance();

    // --- 各種初期化 ---
    bgmHandle_ = audioPlayer_->LoadSoundFile("resouces/bgm/Alarm02.mp3"); 
    CameraManager::GetInstance()->Initialize();
    CameraManager::GetInstance()->SetInputManager(inputManager_); // カメラに InputManager を渡す
    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);
    object3dCommon_ = std::make_unique<Object3dCommon>();
    object3dCommon_->Initialize(dxCommon_);
    particleCommon_ = std::make_unique<ParticleCommon>();
    particleCommon_->Initialize(dxCommon_);
    particleSystem_ = std::make_unique<ParticleSystem>();
    // ( "particle.png" など、使用するテクスチャパスを指定)
    particleSystem_->Initialize(particleCommon_.get(), "resouces/sprite/white.png");

    // --- オブジェクトの生成 ---
    // Plane
    auto plane = std::make_unique<Object3d>();
    plane->Initialize(object3dCommon_.get());
    plane->SetModel("plane");
    plane->SetName("Plane"); // ★ デバッグエディタ用に名前を設定
    objects_.emplace_back(std::move(plane));

    // Player (Teapot) - インデックス [1] になる
    auto playerObj = std::make_unique<Player>(); // 1. 一時的な unique_ptr として作成
    playerObj->Initialize(object3dCommon_.get(), inputManager_);
    playerObj->SetModel("block");
    playerObj->SetTranslate({ 2.0f, 0.0f, 0.0f });
    playerObj->SetName("Player");

    player_ = playerObj.get(); // 2. ★ メンバ変数(player_)に「生ポインタ」をキャッシュする
    objects_.emplace_back(std::move(playerObj)); // 3. 所有権は objects_[1] に移す

    //// Enemy (Bunny) - インデックス [2] になる
    auto enemy = std::make_unique<Object3d>();
    enemy->Initialize(object3dCommon_.get());
    enemy->SetModel("bunny");
    enemy->SetTranslate({ 2.0f, 0.0f, 0.0f }); 
    enemy->SetName("Enemy");
    //enemy->SetParent(player_); // 4. ★ .get() は不要。キャッシュした生ポインタを渡す
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
        objects_[i]->SetCollisionSize({ 1.0f, 1.0f, 1.0f }); // (ブロックモデルに合わせた半分のサイズ)
        CollisionManager::GetInstance()->AddObject(objects_[i].get());
    }

    // Plane (Index 0) - 地面判定を追加する場合
    objects_[0]->SetCollisionAttribute(kGround);
    objects_[0]->SetCollisionMask(~kGround);
    objects_[0]->SetColliderType(ColliderType::kAABB);
    objects_[0]->SetCollisionSize({ 10.0f, 0.1f, 10.0f }); // 薄い箱
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
    flameSprite->SetSize({ 64.0f,64.0f });
    sprites_.push_back(std::move(flameSprite));

  



    LoadObjectLayout("scene_layout.json"); // 3Dオブジェクト配置読み込み
	LoadSpriteLayout("sprite_layout.json"); // スプライト配置読み込み

    // ★ シーンマネージャ対応: DebugEditor を作成して初期化 (JSON読み込みの後)
#ifdef _DEBUG
    debugEditor_ = std::make_unique<DebugEditor>();
    debugEditor_->Initialize(this, dxCommon_);
	spriteDebugEditor_ = std::make_unique<SpriteDebugEditor>();
	spriteDebugEditor_->Initialize(this,inputManager_);
    particleEditor_ = std::make_unique<ParticleEditor>();
    particleEditor_->Initialize(particleSystem_.get());
#endif

    EventManager::GetInstance()->Subscribe(
        [this](const PlayerHitEvent& event) {
            // イベントが発生したら、OnPlayerHit 関数を呼ぶ
            this->OnPlayerHit(event);
        }
    );



    dxCommon_->FlushCommandQueue(false); // 初期化完了時にコマンドをフラッシュ
}

void GamePlayScene::Finalize() {
    // ★ シーンマネージャ対応: DebugEditor の終了処理 (先に)
#ifdef _DEBUG
    if (debugEditor_) {
        debugEditor_->Finalize();
    }
    if (spriteDebugEditor_) {
        spriteDebugEditor_->Finalize();
    }
#endif

  

    // ★ シーンマネージャ対応: CollisionManager をクリア
    CollisionManager::GetInstance()->ClearObjects();

    // (↓ ユーザー提供のコード)
    // 解放漏れを防ぐため、逆順で解放するのが安全
    particleSystem_.reset();
    particleCommon_.reset();
    sprites_.clear();
    spriteCommon_.reset();
    objects_.clear();
    object3dCommon_.reset();
    // bgmHandle_ の解放処理があればここに追加
}

void GamePlayScene::Update(float deltaTime) {

    bool isSpriteEditorBusy = false; // 2Dギズモが使用中か


    // ★ シーンマネージャ対応: DebugEditor の更新
#ifdef _DEBUG
    if (debugEditor_) {
        debugEditor_->Update();
    }
    if (spriteDebugEditor_) {
        spriteDebugEditor_->Update(); 

        //  ギズモがマウスを使っているか確認
        isSpriteEditorBusy = spriteDebugEditor_->IsMouseBusy();
    }
    if (particleEditor_) {
        particleEditor_->Update();
    }

#endif

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
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    camera->SetInputEnabled(!isSpriteEditorBusy);
    // --- 常に実行される更新 ---
    CameraManager::GetInstance()->Update(); // カメラ行列の最終計算

 
    // --- パーティクル更新 ---

        particleSystem_->Update(deltaTime);
    


    // --- 1. ゲームロジック (オブジェクト・スプライト) 更新 ---
    for (auto& obj : objects_) {
        obj->Update(); // Player::Update() などが呼ばれる
    }

    // [フェーズ1] 全オブジェクトのローカル行列を計算
    if (player_) {
        player_->UpdateLocalMatrix();
    }
    for (const auto& object : objects_) {
        object->UpdateLocalMatrix();
    }

    if (player_) {
        player_->UpdateWorldMatrix(); // Playerが親なら先に計算
    }
    for (const auto& object : objects_) {
 
        object->UpdateWorldMatrix(); // 子のワールド行列計算
    }




    for (auto& sprite : sprites_) {
        sprite->Update(); // アニメーション更新など
    }

    if (inputManager_->IsKeyTriggered(DIK_P)) {
        particleSystem_->SpawnParticles(
            { 0.0f, 1.0f, 0.0f }, 100, // 場所, 数
            5.0f, nullptr, 1.0f,     // 速度, 方向, ばらつき
            { 1.0f, 0.5f, 0.0f, 1.0f }, { 1.0f, 1.0f, 0.0f, 0.0f }, // 開始色, 終了色
            0.5f, 3.0f,                // 寿命(min, max)
            1.0f, 0.1f                 // サイズ(start, end)
        );
    }



    // --- 2. 物理 (衝突判定) 更新 ---
    CollisionManager::GetInstance()->Update();



}

void GamePlayScene::Draw() {

    // ★ シーンマネージャ対応: DebugEditor のデバッグ描画 (3Dオブジェクトより先に描画)
    // (注: dxCommon_->PreDraw() は Game.cpp の Draw() で呼ばれます)
#ifdef _DEBUG
    if (debugEditor_) {
        debugEditor_->DrawDebug(dxCommon_->GetCommandList());
    }
#endif
     

   
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
#ifdef _DEBUG
        if (spriteDebugEditor_) {
            spriteDebugEditor_->Draw();
        }
#endif
        particleSystem_->Draw();

    }



/// <summary>
/// PlayerHitEvent を処理する関数 
/// </summary>
void GamePlayScene::OnPlayerHit(const PlayerHitEvent& event) {

    uint32_t attribute = event.hitObject->GetCollisionAttribute();

    if (attribute & kEnemy) {
        // 敵に当たった！
        OutputDebugStringA("Hit Enemy! (Handled by GamePlayScene)\n");
    }
  
}