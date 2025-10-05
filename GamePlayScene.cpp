#include "GamePlayScene.h"
#include "Framework.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#include"Game.h"
void GamePlayScene::Initialize() {
    // Frameworkのインスタンスから各種ポインタを取得
    if (auto* game = dynamic_cast<Game*>(Game::GetInstance())) {
        framework_ = game;
        dxCommon_ = game->GetDxCommon();
        inputManager_ = game->GetInputManager();
    }

    // 以下は、以前Game.cppにあった初期化処理
    audioPlayer_ = std::make_unique<AudioPlayer>();
    XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
    xAudio2_->CreateMasteringVoice(&masteringVoice_);
    soundData1_ = audioPlayer_->SoundLoadWave("resouces/Alarm02.wav");

    debugCamera_ = std::make_unique<DebugCamera>();
    debugCamera_->Initialize();
    debugCamera_->SetInputManager(inputManager_);

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    object3dCommon_ = std::make_unique<Object3dCommon>();
    object3dCommon_->Initialize(dxCommon_);

    // モデルの読み込み
    ModelManager::GetInstance()->LoadModel("resouces/plane.obj");
    ModelManager::GetInstance()->LoadModel("resouces/teapot.obj");
    ModelManager::GetInstance()->LoadModel("resouces/bunny.obj");

    // オブジェクトの生成
    objects_.emplace_back(std::make_unique<Object3d>()); // Plane
    objects_.emplace_back(std::make_unique<Object3d>()); // Teapot
    objects_.emplace_back(std::make_unique<Object3d>()); // Bunny

    objects_[0]->Initialize(object3dCommon_.get());
    objects_[0]->SetModel("resouces/plane.obj");
    // ... (以下、オブジェクト設定は同様)
    objects_[1]->Initialize(object3dCommon_.get());
    objects_[1]->SetModel("resouces/teapot.obj");
    objects_[1]->SetTranslate({ 2.0f, 0.0f, 0.0f });

    objects_[2]->Initialize(object3dCommon_.get());
    objects_[2]->SetModel("resouces/bunny.obj");
    objects_[2]->SetTranslate({ -2.0f, 0.0f, 0.0f });

    // スプライトの生成
    sprite_ = std::make_unique<Sprite>();
    uint32_t spriteTexHandle = TextureManager::GetInstance()->Load("resouces/monsterBall.png");
    sprite_->Initialize(dxCommon_, spriteTexHandle);
    sprite_->SetPosition({ 200.0f, 360.0f });
    sprite_->SetSize({ 100.0f, 100.0f });
}

void GamePlayScene::Finalize() {
    // このシーンで確保したメモリを解放
    objects_.clear();
    sprite_.reset();
    object3dCommon_.reset();
    spriteCommon_.reset();
    debugCamera_.reset();

    audioPlayer_->SoundUnload(&soundData1_);
    audioPlayer_.reset();
}

void GamePlayScene::Update() {
    debugCamera_->Update();

    if (!audioPlayedOnce_) {
        audioPlayer_->SoundPlayWave(xAudio2_.Get(), soundData1_);
        audioPlayedOnce_ = true;
    }

    // ESCキーでタイトルに戻る
    if (inputManager_->IsKeyTriggered(DIK_ESCAPE)) {
        RequestNextScene("TITLE");
    }

    // ImGui
    ImGui::Begin("Object Settings");
    // ... (以前のGame::Update()のImGui処理と同じ)
    static int selected = 0;
    const char* items[] = { "Plane", "Teapot", "Bunny" };
    ImGui::Combo("View Select", &selected, items, IM_ARRAYSIZE(items));
    Object3d* currentObject = objects_[selected].get();
    Object3d::Transform* transform = currentObject->GetTransform();
    Model::Material* material = currentObject->GetMaterial();
    Object3d::DirectionalLight* light = currentObject->GetDirectionalLight();
    if (ImGui::CollapsingHeader("Object Transform")) {
        ImGui::DragFloat3("Translate", &transform->translate.x, 0.01f);
        ImGui::DragFloat3("Rotate", &transform->rotate.x, 0.01f);
        ImGui::DragFloat3("Scale", &transform->scale.x, 0.01f);
    }
    ImGui::End();

    // オブジェクト更新
    Matrix4x4 viewMatrix = debugCamera_->GetViewMatrix();
    Matrix4x4 projectionMatrix = debugCamera_->GetProjectionMatrix();
    for (auto& obj : objects_) {
        obj->Update(viewMatrix, projectionMatrix);
    }
    sprite_->Update();
}

void GamePlayScene::Draw() {
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    // 3Dオブジェクト描画
    object3dCommon_->SetGraphicsCommand();
    for (const auto& obj : objects_) {
        obj->Draw(commandList);
    }

    // スプライト描画
    spriteCommon_->SetPipeline(commandList);
    sprite_->Draw(commandList);
}