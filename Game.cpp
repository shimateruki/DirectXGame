#include "Game.h"
#include "engine/3d/ModelManager.h"
#include "engine/3d/TextureManager.h"

#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"

void Game::Initialize() {
    // ★★★ 基底クラス(Framework)の初期化処理を呼び出す ★★★
    Framework::Initialize();

    // 以下、Gameクラス独自の初期化処理
    audioPlayer_ = std::make_unique<AudioPlayer>();
    XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
    xAudio2_->CreateMasteringVoice(&masteringVoice_);
    soundData1_ = audioPlayer_->SoundLoadWave("resouces/Alarm02.wav");

    debugCamera_ = std::make_unique<DebugCamera>();
    debugCamera_->Initialize();
    debugCamera_->SetInputManager(inputManager_.get()); // inputManager_はFrameworkのメンバ

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_); // dxCommon_はFrameworkのメンバ

    object3dCommon_ = std::make_unique<Object3dCommon>();
    object3dCommon_->Initialize(dxCommon_); // dxCommon_はFrameworkのメンバ

    // --- モデルの読み込み ---
    ModelManager::GetInstance()->LoadModel("resouces/plane.obj");
    ModelManager::GetInstance()->LoadModel("resouces/teapot.obj");
    ModelManager::GetInstance()->LoadModel("resouces/bunny.obj");

    // --- オブジェクトの生成 ---
    objects_.emplace_back(std::make_unique<Object3d>()); // Plane
    objects_.emplace_back(std::make_unique<Object3d>()); // Teapot
    objects_.emplace_back(std::make_unique<Object3d>()); // Bunny

    objects_[0]->Initialize(object3dCommon_.get());
    objects_[0]->SetModel("resouces/plane.obj");

    objects_[1]->Initialize(object3dCommon_.get());
    objects_[1]->SetModel("resouces/teapot.obj");
    objects_[1]->SetTranslate({ 2.0f, 0.0f, 0.0f });

    objects_[2]->Initialize(object3dCommon_.get());
    objects_[2]->SetModel("resouces/bunny.obj");
    objects_[2]->SetTranslate({ -2.0f, 0.0f, 0.0f });

    // --- スプライトの生成 ---
    sprite_ = std::make_unique<Sprite>();
    uint32_t spriteTexHandle = TextureManager::GetInstance()->Load("resouces/monsterBall.png");
    sprite_->Initialize(dxCommon_, spriteTexHandle);
    sprite_->SetPosition({ 200.0f, 360.0f });
    sprite_->SetSize({ 100.0f, 100.0f });
}

void Game::Finalize() {
    // --- Gameクラス独自の終了処理 ---
    objects_.clear();
    sprite_.reset();
    object3dCommon_.reset();
    spriteCommon_.reset();
    debugCamera_.reset();

    audioPlayer_->SoundUnload(&soundData1_);
    audioPlayer_.reset();

    // ★★★ 基底クラス(Framework)の終了処理を呼び出す ★★★
    Framework::Finalize();
}

void Game::Update() {
    // --- 入力・カメラ更新 ---
    inputManager_->Update(); // inputManager_はFrameworkのメンバ
    debugCamera_->Update();

    // --- サウンド再生 ---
    if (!audioPlayedOnce_) {
        audioPlayer_->SoundPlayWave(xAudio2_.Get(), soundData1_);
        audioPlayedOnce_ = true;
    }

    // --- ImGui ---
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Object Settings");
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
    if (ImGui::CollapsingHeader("Lighting")) {
        ImGui::ColorEdit4("Light Color", &light->color.x);
        ImGui::DragFloat3("Light Direction", &light->direction.x, 0.01f);
        ImGui::DragFloat("Light Intensity", &light->intensity, 0.01f);
    }
    if (material && ImGui::CollapsingHeader("Material")) {
        ImGui::ColorEdit4("Color", &material->color.x);
        const char* lightingTypes[] = { "None", "Lambertian", "Half Lambert" };
        ImGui::Combo("Lighting Type", &material->selectedLighting, lightingTypes, IM_ARRAYSIZE(lightingTypes));
    }
    ImGui::End();

    // --- オブジェクト更新 ---
    Matrix4x4 viewMatrix = debugCamera_->GetViewMatrix();
    Matrix4x4 projectionMatrix = debugCamera_->GetProjectionMatrix();
    for (auto& obj : objects_) {
        obj->Update(viewMatrix, projectionMatrix);
    }
    sprite_->Update();
}

void Game::Draw() {
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    // --- 描画前処理 ---
    dxCommon_->PreDraw();

    // --- 3Dオブジェクト描画 ---
    object3dCommon_->SetGraphicsCommand();
    for (const auto& obj : objects_) {
        obj->Draw(commandList);
    }

    // --- スプライト描画 ---
    spriteCommon_->SetPipeline(commandList);
    sprite_->Draw(commandList);

    // --- 描画後処理 ---
    dxCommon_->PostDraw();
}