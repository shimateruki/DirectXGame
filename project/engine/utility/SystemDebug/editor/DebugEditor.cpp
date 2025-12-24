#define NOMINMAX
#include "DebugEditor.h"
#include "SceneManager.h"    
#include "BaseScene.h"      
#include "Object3d.h"
#include "imgui.h"
#include <fstream>
#include <string>
#include <vector>
#include "json.hpp"
#include "ImGuizmo.h"
#include "CameraManager.h"
#include "WinApp.h"
#include "Math.h"
#include "DirectXCommon.h"
#include "CollisionConfig.h"
#include "ModelManager.h"      
#include "InputManager.h"   
#include <cmath>
#include <cassert> 

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <DebugConsole.h>
#include <CollisionManager.h>
#include <filesystem> // ファイル操作用
namespace fs = std::filesystem;
const float PI = (float)M_PI;

float ToRadians(float degrees) { return degrees * (PI / 180.0f); }
float ToDegrees(float radians) { return radians * (180.0f / PI); }

// ========================================================================
// 初期化
// ========================================================================
void DebugEditor::Initialize(SceneManager* sceneManager, DirectXCommon* dxCommon) {
    sceneManager_ = sceneManager;
    dxCommon_ = dxCommon;
    selectedObject_ = nullptr;
    lastUpdatedScene_ = nullptr;
    InitializePrimitiveDrawing();
}

// ========================================================================
// 更新 (ImGui処理)
// ========================================================================
void DebugEditor::Update() {
#ifdef USE_IMGUI
    using json = nlohmann::json;
    if (sceneManager_ == nullptr) return;

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (currentScene == nullptr) {
        selectedObject_ = nullptr;
        lastUpdatedScene_ = nullptr;
        return;
    }

    //シーンの更新
    if (lastUpdatedScene_ != currentScene) {
        selectedObject_ = nullptr;
        lastUpdatedScene_ = currentScene;
    }

    // ギズモ操作のロジック
    if (selectedObject_) {
        const Camera* camera = CameraManager::GetInstance()->GetMainCamera();
        if (!camera) return;

        const Matrix4x4& view = camera->GetViewMatrix();
        const Matrix4x4& proj = camera->GetProjectionMatrix();
        Object3d::Transform* tr = selectedObject_->GetTransform();
        Math m;
        Matrix4x4 world = m.MakeAffineMatrix(tr->scale, tr->rotate, tr->translate);
        ImGuiIO& io = ImGui::GetIO();
        ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
        static ImGuizmo::OPERATION currentOperation = ImGuizmo::TRANSLATE;
        static ImGuizmo::MODE currentMode = ImGuizmo::WORLD;
        static float snapTranslate[3] = { 0.1f, 0.1f, 0.1f };
        static float snapRotation = 15.0f;
        static float snapScale = 0.1f;
        float snapVals[3];
        if (currentOperation == ImGuizmo::ROTATE) snapVals[0] = snapVals[1] = snapVals[2] = snapRotation;
        else if (currentOperation == ImGuizmo::SCALE) snapVals[0] = snapVals[1] = snapVals[2] = snapScale;
        else { snapVals[0] = snapTranslate[0]; snapVals[1] = snapTranslate[1]; snapVals[2] = snapTranslate[2]; }

        float* snap = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) ? snapVals : nullptr;

        ImGuizmo::Manipulate(&view.m[0][0], &proj.m[0][0], currentOperation, currentMode, &world.m[0][0], nullptr, snap);

        if (ImGuizmo::IsUsing()) {
            Vector3 s, rDeg, t;
            ImGuizmo::DecomposeMatrixToComponents(&world.m[0][0], &t.x, &rDeg.x, &s.x);
            tr->translate = t;
            tr->rotate = { ToRadians(rDeg.x), ToRadians(rDeg.y), ToRadians(rDeg.z) };
            tr->scale = s;
        }
    }
#endif
}

// ========================================================================
// 終了処理
// ========================================================================
void DebugEditor::Finalize() {
    if (primitiveWVPData_) { primitiveWVPBuffer_->Unmap(0, nullptr); primitiveWVPData_ = nullptr; }
    if (primitiveColorData_) { primitiveColorBuffer_->Unmap(0, nullptr); primitiveColorData_ = nullptr; }
}

// ========================================================================
// プリミティブ描画の初期化
// ========================================================================
void DebugEditor::InitializePrimitiveDrawing() {

    assert(dxCommon_); ID3D12Device* device = dxCommon_->GetDevice(); HRESULT hr;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{}; rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; params[0].Descriptor.ShaderRegister = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; params[1].Descriptor.ShaderRegister = 1;
    rsDesc.pParameters = params; rsDesc.NumParameters = _countof(params);
    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob, errBlob;
    hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob); if (FAILED(hr)) { OutputDebugStringA((char*)errBlob->GetBufferPointer()); assert(false); }
    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&primitiveRootSignature_)); assert(SUCCEEDED(hr));

    D3D12_INPUT_ELEMENT_DESC inputElems[] = { { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 } };
    D3D12_INPUT_LAYOUT_DESC inputLayout = { inputElems, _countof(inputElems) };
    D3D12_RASTERIZER_DESC rasterDesc{}; rasterDesc.CullMode = D3D12_CULL_MODE_NONE; rasterDesc.FillMode = D3D12_FILL_MODE_WIREFRAME;

    Microsoft::WRL::ComPtr<IDxcBlob> vsBlob = dxCommon_->CompileShader(L"resouces/shader/DebugPrimitive.VS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> psBlob = dxCommon_->CompileShader(L"resouces/shader/DebugPrimitive.PS.hlsl", L"ps_6_0");
    assert(vsBlob && psBlob);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = primitiveRootSignature_.Get(); psoDesc.InputLayout = inputLayout;
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() }; psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.RasterizerState = rasterDesc; psoDesc.NumRenderTargets = 1; psoDesc.RTVFormats[0] = dxCommon_->GetRTVFormat();
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    psoDesc.SampleDesc.Count = 1; psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    D3D12_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState = blendDesc;
    D3D12_DEPTH_STENCIL_DESC depthDesc = dxCommon_->GetDefaultDepthStencilDesc();
    depthDesc.DepthEnable = TRUE;
    psoDesc.DepthStencilState = depthDesc;
    psoDesc.DSVFormat = dxCommon_->GetDSVFormat();

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&primitivePipelineState_)); assert(SUCCEEDED(hr));

    Vector4 cubeVerts[] = { {-0.5f,-0.5f,-0.5f,1}, {0.5f,-0.5f,-0.5f,1}, {-0.5f,0.5f,-0.5f,1}, {0.5f,0.5f,-0.5f,1}, {-0.5f,-0.5f,0.5f,1}, {0.5f,-0.5f,0.5f,1}, {-0.5f,0.5f,0.5f,1}, {0.5f,0.5f,0.5f,1} };
    uint32_t cubeIdx[] = { 0,1, 1,3, 3,2, 2,0, 4,5, 5,7, 7,6, 6,4, 0,4, 1,5, 2,6, 3,7 };
    cubeVertexBuffer_ = dxCommon_->CreateBufferResource(sizeof(cubeVerts)); cubeVertexBufferView_ = { cubeVertexBuffer_->GetGPUVirtualAddress(), sizeof(cubeVerts), sizeof(Vector4) };
    void* vbData; hr = cubeVertexBuffer_->Map(0, nullptr, &vbData); assert(SUCCEEDED(hr)); memcpy(vbData, cubeVerts, sizeof(cubeVerts)); cubeVertexBuffer_->Unmap(0, nullptr);
    cubeIndexBuffer_ = dxCommon_->CreateBufferResource(sizeof(cubeIdx)); cubeIndexBufferView_ = { cubeIndexBuffer_->GetGPUVirtualAddress(), sizeof(cubeIdx), DXGI_FORMAT_R32_UINT };
    void* ibData; hr = cubeIndexBuffer_->Map(0, nullptr, &ibData); assert(SUCCEEDED(hr)); memcpy(ibData, cubeIdx, sizeof(cubeIdx)); cubeIndexBuffer_->Unmap(0, nullptr);

    primitiveWVPBuffer_ = dxCommon_->CreateBufferResource(sizeof(AlignedMatrix4x4) * kMaxInstances);
    hr = primitiveWVPBuffer_->Map(0, nullptr, (void**)&primitiveWVPData_);
    assert(SUCCEEDED(hr));

    primitiveColorBuffer_ = dxCommon_->CreateBufferResource(sizeof(AlignedVector4) * kMaxInstances);
    hr = primitiveColorBuffer_->Map(0, nullptr, (void**)&primitiveColorData_);
    assert(SUCCEEDED(hr));
}

// ========================================================================
// コライダー描画処理
// ========================================================================
void DebugEditor::DrawDebug(ID3D12GraphicsCommandList* commandList) {
    if (!drawColliders_ || sceneManager_ == nullptr) return;

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (currentScene == nullptr) return;
    const auto& colliders = CollisionManager::GetInstance()->GetObjects();
    if (colliders.empty()) return;

    commandList->SetPipelineState(primitivePipelineState_.Get());
    commandList->SetGraphicsRootSignature(primitiveRootSignature_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    commandList->IASetVertexBuffers(0, 1, &cubeVertexBufferView_);
    commandList->IASetIndexBuffer(&cubeIndexBufferView_);

    Math math; const Camera* camera = CameraManager::GetInstance()->GetMainCamera(); if (!camera) return;

    int instanceIndex = 0;

    for (auto& obj : colliders) {
        if (instanceIndex >= kMaxInstances) {
            break;
        }

        ColliderType type = obj->GetColliderType();
        Vector4 color = { 0.0f, 1.0f, 0.0f, 1.0f };

        if (type == ColliderType::kAABB) {
            AABB aabb = obj->GetAABB(); Vector3 c = (aabb.min + aabb.max) * 0.5f; Vector3 s = aabb.max - aabb.min;
            if (s.x <= 0 || s.y <= 0 || s.z <= 0) continue;
            Matrix4x4 world = math.Multiply(math.MakeScaleMatrix(s), math.MakeTranslateMatrix(c));

            DrawWireCube(commandList, world, color, instanceIndex);
            instanceIndex++;

        } else if (type == ColliderType::kSphere) {
            Vector3 c = obj->GetWorldPosition(); float r = obj->GetCollisionRadius();
            if (r <= 0) continue;
            Matrix4x4 world = math.Multiply(math.MakeScaleMatrix({ r * 2, r * 2, r * 2 }), math.MakeTranslateMatrix(c));

            DrawWireCube(commandList, world, { 0.0f, 0.0f, 1.0f, 1.0f }, instanceIndex);
            instanceIndex++;
        }
    }
}

// ========================================================================
// ワイヤーフレームの立方体を描画する内部関数
// ========================================================================
void DebugEditor::DrawWireCube(ID3D12GraphicsCommandList* commandList, const Matrix4x4& worldMatrix, const Vector4& color, int instanceIndex) {
    Math math; const Camera* camera = CameraManager::GetInstance()->GetMainCamera(); if (!camera) return;

    primitiveWVPData_[instanceIndex].matrix = math.Multiply(worldMatrix, math.Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix()));

    Vector4 opaqueColor = color;
    opaqueColor.w = 1.0f;
    primitiveColorData_[instanceIndex].vector = opaqueColor;

    D3D12_GPU_VIRTUAL_ADDRESS wvpGpuAddress = primitiveWVPBuffer_->GetGPUVirtualAddress() + (static_cast<UINT64>(instanceIndex) * sizeof(AlignedMatrix4x4));
    D3D12_GPU_VIRTUAL_ADDRESS colorGpuAddress = primitiveColorBuffer_->GetGPUVirtualAddress() + (static_cast<UINT64>(instanceIndex) * sizeof(AlignedVector4));

    commandList->SetGraphicsRootConstantBufferView(0, wvpGpuAddress);
    commandList->SetGraphicsRootConstantBufferView(1, colorGpuAddress);

    commandList->DrawIndexedInstanced(24, 1, 0, 0, 0);
}


void DebugEditor::DrawImGui() {
#ifdef USE_IMGUI
    using json = nlohmann::json;

    // 1. プロジェクトウィンドウを表示
    DrawProjectWindow();

    if (sceneManager_ == nullptr) return;

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (currentScene == nullptr) {
        ImGui::Begin("Inspector");
        ImGui::Text("No active scene.");
        ImGui::End();
        return;
    }

    std::string targetSceneFile = "resouces/json/scene_layout.json";

    // --- Inspector Window ---
    ImGui::Begin("Inspector");

    if (selectedObject_ == nullptr) {
        ImGui::Text("No object selected.");
        ImGui::Text("Select from Object List.");
        ImGui::Separator();
        ImGui::Checkbox("Draw Colliders", &drawColliders_);
    } else {
        // --- オブジェクト情報の表示と編集 ---
        char nameBuffer[256];
        strcpy_s(nameBuffer, selectedObject_->GetName().c_str());
        if (ImGui::InputText("Name", nameBuffer, 256)) {
            selectedObject_->SetName(std::string(nameBuffer));
        }

        // --- ★ D&D受け入れエリア (Target) ---
        ImGui::Separator();
        ImGui::Text("Model Asset:");
        ImGui::Button(" [ Drop Model Here ] ", ImVec2(-1, 30)); // 横幅いっぱいのボタン（見た目用）

        // ドロップを受け付ける処理
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
                const char* modelName = (const char*)payload->Data;

                // ログ出力
                DebugConsole::GetInstance()->AddLog("Switching model to: " + std::string(modelName));

                // ★ モデル切り替え実行
                selectedObject_->SetModel(modelName);
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::Separator();
        // ----------------------------------

        Object3d::Transform* transform = selectedObject_->GetTransform();
        ImGui::DragFloat3("Position", &transform->translate.x, 0.1f);

        Vector3 rotDeg = { ToDegrees(transform->rotate.x), ToDegrees(transform->rotate.y), ToDegrees(transform->rotate.z) };
        if (ImGui::DragFloat3("Rotation (Degrees)", &rotDeg.x, 1.0f, -360.0f, 360.0f)) {
            transform->rotate = { ToRadians(rotDeg.x), ToRadians(rotDeg.y), ToRadians(rotDeg.z) };
        }
        ImGui::DragFloat3("Scale", &transform->scale.x, 0.05f);

        ImGui::Separator();

        // --- 保存ボタン ---
        if (ImGui::Button("Save Scene Layout")) {
            json sceneData;
            sceneData["objects"] = json::array();
            std::vector<std::unique_ptr<Object3d>>& allObjects = currentScene->GetObjects();

            for (auto& obj : allObjects) {
                if (obj->GetName().empty()) continue;

                Object3d::Transform* objTr = obj->GetTransform();
                json d;
                d["name"] = obj->GetName();
                 d["modelName"] = obj->GetModelName(); 
                d["position"] = { objTr->translate.x, objTr->translate.y, objTr->translate.z };
                d["rotation"] = { objTr->rotate.x, objTr->rotate.y, objTr->rotate.z };
                d["scale"] = { objTr->scale.x, objTr->scale.y, objTr->scale.z };

                sceneData["objects"].push_back(d);
            }

            std::ofstream f(targetSceneFile);
            if (f.is_open()) {
                f << sceneData.dump(4);
                f.close();
                DebugConsole::GetInstance()->AddLog("Saved scene to " + targetSceneFile);
            } else {
                DebugConsole::GetInstance()->AddLog("Failed to save JSON! Check folder.");
            }
        }

        // --- 単体更新ボタン ---
        if (ImGui::Button("Update This Object")) {
            UpdateObjectInSceneJSON(selectedObject_, targetSceneFile);
        }

        // --- 複製 / 削除 ---
        ImGui::Separator();
        if (ImGui::Button("Duplicate")) {
            std::unique_ptr<Object3d> newObj = selectedObject_->Clone();
            static int duplicateCount = 0;
            newObj->SetName(selectedObject_->GetName() + "_copy" + std::to_string(duplicateCount++));
            currentScene->AddObject(std::move(newObj));
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(0, 0))) { // 赤くしたいならPushStyleColor推奨
            currentScene->RequestRemoveObject(selectedObject_);
            selectedObject_ = nullptr;
        }

        // --- Gizmo Control (選択中のみ表示) ---
        ImGui::Separator();
        ImGui::Text("Gizmo Operation:");

        static ImGuizmo::OPERATION currentOperation = ImGuizmo::TRANSLATE;
        static ImGuizmo::MODE currentMode = ImGuizmo::WORLD;
        static float snapTranslate[3] = { 0.5f, 0.5f, 0.5f }; // スナップ値を少し大きくしました
        static float snapRotation = 45.0f;
        static float snapScale = 0.5f;

        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) {
            if (ImGui::IsKeyPressed(ImGuiKey_T)) currentOperation = ImGuizmo::TRANSLATE;
            if (ImGui::IsKeyPressed(ImGuiKey_R)) currentOperation = ImGuizmo::ROTATE;
            if (ImGui::IsKeyPressed(ImGuiKey_S)) currentOperation = ImGuizmo::SCALE;
        }

        if (ImGui::RadioButton("Translate", currentOperation == ImGuizmo::TRANSLATE)) currentOperation = ImGuizmo::TRANSLATE; ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", currentOperation == ImGuizmo::ROTATE)) currentOperation = ImGuizmo::ROTATE; ImGui::SameLine();
        if (ImGui::RadioButton("Scale", currentOperation == ImGuizmo::SCALE)) currentOperation = ImGuizmo::SCALE;

        // Snap設定
        ImGui::Text("Snap (Hold Ctrl):");
        ImGui::SameLine();
        if (currentOperation == ImGuizmo::TRANSLATE) ImGui::InputFloat3("##SnapT", snapTranslate);
        else if (currentOperation == ImGuizmo::ROTATE) ImGui::InputFloat("##SnapR", &snapRotation);
        else ImGui::InputFloat("##SnapS", &snapScale);
    }
    ImGui::End(); // End Inspector


    // --- Object List Window ---
    ImGui::Begin("Object List");
    std::vector<std::unique_ptr<Object3d>>& objects = currentScene->GetObjects();
    for (auto& obj : objects) {
        const std::string& objName = obj->GetName();
        if (objName.empty()) continue;

        // 選択状態のハイライト
        bool isSelected = (obj.get() == selectedObject_);
        if (ImGui::Selectable(objName.c_str(), isSelected)) {
            selectedObject_ = obj.get();
        }
    }
    ImGui::End();


    // --- Object Spawner Window ---
    ImGui::Begin("Spawner");
    if (ImGui::Button("Refresh Model List")) {
        modelNames_ = ModelManager::GetInstance()->GetLoadedModelNames();
        selectedModelIndex_ = 0;
    }

    if (!modelNames_.empty()) {
        std::vector<const char*> namesCStr;
        for (const std::string& name : modelNames_) {
            namesCStr.push_back(name.c_str());
        }
        ImGui::ListBox("Loaded Models", &selectedModelIndex_, namesCStr.data(), (int)namesCStr.size(), 5);

        if (ImGui::Button("Spawn Object")) {
            if (selectedModelIndex_ >= 0 && selectedModelIndex_ < modelNames_.size()) {
                std::string modelName = modelNames_[selectedModelIndex_];
                Object3dCommon* common = currentScene->GetObject3dCommon();
                if (common) {
                    auto newObj = std::make_unique<Object3d>();
                    newObj->Initialize(common);
                    newObj->SetModel(modelName);
                    static int spawnCount = 0;
                    newObj->SetName(modelName + "_" + std::to_string(spawnCount++));
                    currentScene->AddObject(std::move(newObj));
                    DebugConsole::GetInstance()->AddLog("Spawned: " + newObj->GetName());
                }
            }
        }
    } else {
        ImGui::Text("No models loaded.");
    }
    ImGui::End();

#endif
}


/// <summary>
/// scene_layout.json を読み込み、特定のオブジェクトの情報だけを更新して上書き保存する
/// </summary>
void DebugEditor::UpdateObjectInSceneJSON(Object3d* object, const std::string& filename) {
    if (object == nullptr) {
        DebugConsole::GetInstance()->AddLog("ERROR: No object selected to update JSON.");
        return;
    }
    std::string objectName = object->GetName();
    if (objectName.empty()) {
        DebugConsole::GetInstance()->AddLog("ERROR: Selected object has no name. Cannot update JSON.");
        return;
    }

    using json = nlohmann::json;
    json root;

    // --- 1. 既存のJSONファイルを読み込む ---
    std::ifstream file_in(filename);
    if (!file_in.is_open()) {
        DebugConsole::GetInstance()->AddLog("ERROR: Failed to open file for reading: " + filename);
        return;
    }

    try {
        root = json::parse(file_in); // JSONをパース
    }
    catch (json::parse_error& e) {
        DebugConsole::GetInstance()->AddLog("ERROR: Failed to parse JSON: " + filename);
        DebugConsole::GetInstance()->AddLog(e.what());
        file_in.close();
        return;
    }
    file_in.close(); // 読み込み終了

    // --- 2. "objects" 配列を探し、該当する name のデータを上書き ---
    bool objectFound = false;
    if (root.contains("objects") && root["objects"].is_array()) {
        for (auto& objData : root["objects"]) { // 配列をループ

            // name が一致するかチェック
            if (objData.contains("name") && objData["name"].get<std::string>() == objectName) {

                // ★ 見つかった！ 選択中のオブジェクトの現在地で上書き
				Object3d::Transform* transform = object->GetTransform();
				objData["modelName"] = object->GetModelName();
                objData["position"] = {
                    transform->translate.x,
                    transform->translate.y,
                    transform->translate.z
                };
                objData["rotation"] = {
                    transform->rotate.x,
                    transform->rotate.y,
                    transform->rotate.z
                };
                objData["scale"] = {
                    transform->scale.x,
                    transform->scale.y,
                    transform->scale.z
                };

                objectFound = true;
                break; // 更新完了
            }
        }
    }

    if (!objectFound) {
        // スポーンなどで新しく作られ、JSONにまだ存在しないオブジェクトの場合
        DebugConsole::GetInstance()->AddLog("Warning: Object '" + objectName + "' not found in " + filename);
        DebugConsole::GetInstance()->AddLog("Use 'Save Scene Layout' (全体保存) to add new objects.");
        return;
    }

    // --- 3. JSONファイルに上書き保存 ---
    std::ofstream file_out(filename); // 同じファイル名で書き込み用に開く
    if (file_out) {
        file_out << root.dump(4); // 4スペースでインデントして書き出し
        file_out.close();
        DebugConsole::GetInstance()->AddLog("Updated " + objectName + " in " + filename);
    } else {
        DebugConsole::GetInstance()->AddLog("ERROR: Failed to open file for writing: " + filename);
    }
}
void DebugEditor::DrawProjectWindow() {
    ImGui::Begin("Project (Assets)");

    // ★ ModelManager のパス設定と一字一句合わせる
    std::string baseDirectory = "resouces/3DModel";

    if (fs::exists(baseDirectory) && fs::is_directory(baseDirectory)) {
        ImGui::Text("Drag model to Inspector!");
        ImGui::Separator();

        for (const auto& entry : fs::directory_iterator(baseDirectory)) {
            if (entry.is_directory()) {
                // 1. まずフォルダ名を取得
                std::string folderName = entry.path().filename().string();

                // 2. ★重要: フォルダの中にある .obj ファイルを実際に探す！
                std::string foundObjName = "";
                for (const auto& subEntry : fs::directory_iterator(entry.path())) {
                    if (subEntry.path().extension() == ".obj") {
                        // 拡張子(.obj)を抜いた名前を取得 
                        foundObjName = subEntry.path().stem().string();
                        break; // 1個見つけたら終わり（1フォルダ1モデルの前提）
                    }
                }

                // .obj が見つからなかったら表示しない
                if (foundObjName.empty()) continue;

                // ボタン表示（フォルダ名で表示したほうが分かりやすいかも）
                ImGui::PushID(folderName.c_str());
                ImGui::Button(folderName.c_str(), ImVec2(100, 0));
                ImGui::PopID();

                // --- ドラッグ処理 ---
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
             
                    // とりあえず見つけた .obj の名前を渡してみます。
                    ImGui::SetDragDropPayload("MODEL_ASSET", foundObjName.c_str(), foundObjName.size() + 1);

                    ImGui::Text("Assign: %s", foundObjName.c_str());
                    ImGui::EndDragDropSource();
                }

                // レイアウト調整（前と同じ）
                float windowVisibleX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
                float lastButtonX = ImGui::GetItemRectMax().x;
                float nextButtonX = lastButtonX + ImGui::GetStyle().ItemSpacing.x + 100;
                if (nextButtonX < windowVisibleX) {
                    ImGui::SameLine();
                }
            }
        }
    } else {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Directory not found: %s", baseDirectory.c_str());
    }
    ImGui::End();
}