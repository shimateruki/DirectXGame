
#define DIRECTINPUT_VERSION 0x0800

#include <windows.h>
#include <string>
#include <vector>
#include <chrono>
#include <format>
#include <filesystem>
#include <fstream>
#include <memory>

#include "AudioPlayer.h"
#include "Math.h"
#include "debugCamera.h"
#include "InputManager.h"
#include "WinApp.h"
#include "DirectXCommon.h"
#include "TextureManager.h"
#include "Sprite.h"
#include "Object3dCommon.h"
#include "Object3d.h"
#include "SpriteCommon.h"
#include "ModelManager.h"  

#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"


int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    WinApp winApp;
    winApp.Initialize(L"CG2", 1280, 720);

    DirectXCommon* dxCommon = DirectXCommon::GetInstance();
    dxCommon->Initialize(&winApp);

    // ★★★ 3Dモデルマネージャの初期化 ★★★
    ModelManager::GetInstance()->Initialize(dxCommon);

    ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

    TextureManager::GetInstance()->Initialize(dxCommon);


    AudioPlayer* audioPlayer = new AudioPlayer();
    Microsoft::WRL::ComPtr<IXAudio2> xAudio2;
    IXAudio2MasteringVoice* masteringVoice = nullptr;
    XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    xAudio2->CreateMasteringVoice(&masteringVoice);
    SoundData soundData1 = audioPlayer->SoundLoadWave("resouces/Alarm02.wav");
    bool audioPlayedOnce = false;

    InputManager* inputManager = new InputManager();
    inputManager->Initialize(winApp.GetHwnd());
    DebugCamera* debugCamera = new DebugCamera();
    debugCamera->Initialize();
    debugCamera->SetInputManager(inputManager);

    auto spriteCommon = std::make_unique<SpriteCommon>();
    spriteCommon->Initialize(dxCommon);

    auto object3dCommon = std::make_unique<Object3dCommon>();
    object3dCommon->Initialize(dxCommon);



    ModelManager::GetInstance()->LoadModel("resouces/plane.obj");
    ModelManager::GetInstance()->LoadModel("resouces/teapot.obj");
    ModelManager::GetInstance()->LoadModel("resouces/bunny.obj");

    // 表示したい3Dオブジェクトを生成
    std::vector<std::unique_ptr<Object3d>> objects;
    objects.emplace_back(std::make_unique<Object3d>()); // Plane
    objects.emplace_back(std::make_unique<Object3d>()); // Teapot
    objects.emplace_back(std::make_unique<Object3d>()); // Bunny

    // 各オブジェクトを初期化し、ファイルパスでモデルをセット
    objects[0]->Initialize(object3dCommon.get());
    objects[0]->SetModel("resouces/plane.obj");

    objects[1]->Initialize(object3dCommon.get());
    objects[1]->SetModel("resouces/teapot.obj");
    objects[1]->SetTranslate({ 2.0f, 0.0f, 0.0f }); // 新しいセッターを使用

    objects[2]->Initialize(object3dCommon.get());
    objects[2]->SetModel("resouces/bunny.obj");
    objects[2]->SetTranslate({ -2.0f, 0.0f, 0.0f }); // 新しいセッターを使用

    // スプライトの生成
    uint32_t spriteTexHandle = TextureManager::GetInstance()->Load("resouces/monsterBall.png");
    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(dxCommon, spriteTexHandle);
    sprite->SetPosition({ 200.0f, 360.0f });
    sprite->SetSize({ 100.0f, 100.0f });

    // --- ゲームループ ---
    while (winApp.Update() == false)
    {
        // ... (入力、デバッグカメラ更新、オーディオ再生は変更なし) ...
        inputManager->Update();
        debugCamera->Update();
        if (!audioPlayedOnce) {
            audioPlayer->SoundPlayWave(xAudio2.Get(), soundData1);
            audioPlayedOnce = true;
        }

        // --- ImGui ---
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // ... (ImGuiの中身は変更なし) ...
        ImGui::Begin("Object Settings");
        static int selected = 0;
        const char* items[] = { "Plane", "Teapot", "Bunny" };
        ImGui::Combo("View Select", &selected, items, IM_ARRAYSIZE(items));
        Object3d* currentObject = objects[selected].get();
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

        // --- 更新処理 ---
        Matrix4x4 viewMatrix = debugCamera->GetViewMatrix();
        Matrix4x4 projectionMatrix = debugCamera->GetProjectionMatrix();
        for (auto& obj : objects) {
            obj->Update(viewMatrix, projectionMatrix);
        }
        sprite->Update();

        // --- 描画処理 ---
        dxCommon->PreDraw();
        object3dCommon->SetGraphicsCommand();

        // ★★★ すべてのオブジェクトを描画 ★★★
        for (const auto& obj : objects) {
            obj->Draw(commandList);
        }

        spriteCommon->SetPipeline(commandList);
        sprite->Draw(commandList);

        dxCommon->PostDraw();
    }

    // --- 解放処理 ---
    objects.clear();
    audioPlayer->SoundUnload(&soundData1);
    delete audioPlayer;
    delete inputManager;
    delete debugCamera;

    // ★★★ 3Dモデルマネージャの終了処理 ★★★
    ModelManager::GetInstance()->Finalize();

    dxCommon->Finalize();
    CoUninitialize();

    return 0;
}