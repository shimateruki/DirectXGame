// =================================================================
// 修正後の main.cpp (完全版)
// =================================================================
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
#include "Object3d.h" // 新しくインクルード
#include "SpriteCommon.h" 

#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"


int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    WinApp winApp;
    winApp.Initialize(L"CG2", 1280, 720);

    DirectXCommon* dxCommon = DirectXCommon::GetInstance();
    dxCommon->Initialize(&winApp);

    ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

    TextureManager::GetInstance()->Initialize(dxCommon);

    // オーディオ初期化
    AudioPlayer* audioPlayer = new AudioPlayer();
    Microsoft::WRL::ComPtr<IXAudio2> xAudio2;
    IXAudio2MasteringVoice* masteringVoice = nullptr;
    XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    xAudio2->CreateMasteringVoice(&masteringVoice);
    SoundData soundData1 = audioPlayer->SoundLoadWave("resouces/Alarm02.wav");
    bool audioPlayedOnce = false;

    // 入力、デバッグカメラ初期化
    InputManager* inputManager = new InputManager();
    inputManager->Initialize(winApp.GetHwnd());
    DebugCamera* debugCamera = new DebugCamera();
    debugCamera->Initialize();
    debugCamera->SetInputManager(inputManager);

    auto spriteCommon = std::make_unique<SpriteCommon>();
    spriteCommon->Initialize(dxCommon);

    // 3Dオブジェクト共通部の生成と初期化
    auto object3dCommon = std::make_unique<Object3dCommon>();
    object3dCommon->Initialize(dxCommon);

    // 表示したい3Dオブジェクトを生成
    std::vector<std::unique_ptr<Object3d>> objects;
    objects.emplace_back(std::make_unique<Object3d>()); // Plane
    objects.emplace_back(std::make_unique<Object3d>()); // Teapot
    objects.emplace_back(std::make_unique<Object3d>()); // Bunny

    // 各オブジェクトを初期化
    objects[0]->Initialize(object3dCommon.get(), "resouces/plane.obj");
    objects[1]->Initialize(object3dCommon.get(), "resouces/teapot.obj");
    objects[2]->Initialize(object3dCommon.get(), "resouces/bunny.obj");

    // スプライトの生成
    uint32_t spriteTexHandle = TextureManager::GetInstance()->Load("resouces/monsterBall.png");
    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(dxCommon, spriteTexHandle);
    sprite->SetPosition({ 200.0f, 360.0f }); // 例えば画面中央に表示
	sprite->SetSize({ 100.0f, 100.0f }); // サイズを指定

    // --- ゲームループ ---
    while (winApp.Update() == false)
    {
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

        ImGui::Begin("Object Settings");

        static int selected = 0;
        const char* items[] = { "Plane", "Teapot", "Bunny" };
        ImGui::Combo("View Select", &selected, items, IM_ARRAYSIZE(items));

        // 選択中のオブジェクトのポインタを取得
        Object3d* currentObject = objects[selected].get();
        Object3d::Transform* transform = currentObject->GetTransform(); // ★★★ このように修正 ★★★
        Object3d::Material* material = currentObject->GetMaterial();
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
        if (ImGui::CollapsingHeader("Material")) {
            ImGui::ColorEdit4("Color", &material->color.x);
            const char* lightingTypes[] = { "None","Lambertian", "Half Lambert"};
            ImGui::Combo("Lighting Type", &material->selectedLighting, lightingTypes, IM_ARRAYSIZE(lightingTypes));
        }

        ImGui::End();

        // --- 更新処理 ---
        Matrix4x4 viewMatrix = debugCamera->GetViewMatrix();
        Matrix4x4 projectionMatrix = debugCamera->GetProjectionMatrix();
        // すべての3Dオブジェクトを更新
        for (auto& obj : objects) {
            obj->Update(viewMatrix, projectionMatrix);
        }
        sprite->Update();

        // --- 描画処理 ---
        dxCommon->PreDraw();

        // 3Dオブジェクトの描画準備
        object3dCommon->SetGraphicsCommand();

        // 選択中の3Dオブジェクトを描画
        objects[selected]->Draw(commandList);

        spriteCommon->SetPipeline(commandList);
        // スプライトの描画
        sprite->Draw(commandList);

        dxCommon->PostDraw();
    }

    // --- 解放処理 ---
    // unique_ptrを使っているのでdeleteは不要
    objects.clear();
    audioPlayer->SoundUnload(&soundData1);
    delete audioPlayer;
    delete inputManager;
    delete debugCamera;

    dxCommon->Finalize();
    CoUninitialize();

    return 0;
}