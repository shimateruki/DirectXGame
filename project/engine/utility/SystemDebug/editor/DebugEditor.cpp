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
#include <BulletManager.h>
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
    if (sceneManager_ == nullptr) return;

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (!currentScene) return;

    // パイプライン設定
    commandList->SetPipelineState(primitivePipelineState_.Get());
    commandList->SetGraphicsRootSignature(primitiveRootSignature_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    commandList->IASetVertexBuffers(0, 1, &cubeVertexBufferView_);
    commandList->IASetIndexBuffer(&cubeIndexBufferView_);

    int instanceCount = 0;
    Math math;

    // =========================================================
    // 1. シーン内のオブジェクトを描画 (統合版)
    // =========================================================
    auto& objects = currentScene->GetObjects();

    for (const auto& obj : objects) {
        if (!obj) continue;
        // インスタンス描画の上限チェック
        if (instanceCount >= kMaxInstances) break;

        ColliderType type = obj->GetColliderType();
        bool isInvisible = !obj->GetIsVisible();

        // --- 描画判定 ---
        // コライダーがなく、かつ「見える物体（モデルあり）」ならデバッグ線は不要
        if (type == ColliderType::kNone && !isInvisible) continue;

        // コライダー表示OFF設定の時、「見える物体」のコライダーは消すが、
        if (!drawColliders_ && !isInvisible) continue;

        // --- 行列計算 (サイズと位置) ---
        Matrix4x4 drawWorldMatrix = math.makeIdentity4x4();

        // ★コライダーがある場合は、その形状データ(Size/Center)に合わせて枠を変形させる
        if (type != ColliderType::kNone) {
            if (type == ColliderType::kOBB) {
                OBB obb = obj->GetOBB();
                Matrix4x4 matScale = math.MakeScaleMatrix(obb.size * 2.0f); // Sizeは半サイズなので2倍
                Matrix4x4 matRot = math.makeIdentity4x4();
                matRot.m[0][0] = obb.orientations[0].x; matRot.m[0][1] = obb.orientations[0].y; matRot.m[0][2] = obb.orientations[0].z;
                matRot.m[1][0] = obb.orientations[1].x; matRot.m[1][1] = obb.orientations[1].y; matRot.m[1][2] = obb.orientations[1].z;
                matRot.m[2][0] = obb.orientations[2].x; matRot.m[2][1] = obb.orientations[2].y; matRot.m[2][2] = obb.orientations[2].z;
                Matrix4x4 matTrans = math.MakeTranslateMatrix(obb.center);

                // Scale * Rotation * Translation
                drawWorldMatrix = math.Multiply(matScale, math.Multiply(matRot, matTrans));
            } else if (type == ColliderType::kAABB) {
                AABB aabb = obj->GetAABB();
                Vector3 center = (aabb.min + aabb.max) * 0.5f;
                Vector3 size = aabb.max - aabb.min;

                Matrix4x4 matScale = math.MakeScaleMatrix(size);
                Matrix4x4 matTrans = math.MakeTranslateMatrix(center);

                drawWorldMatrix = math.Multiply(matScale, matTrans);
            } else if (type == ColliderType::kSphere) {
                // 球体の場合もCubeワイヤーフレームで代用（あるいは別途Sphere用のメッシュがあればそちらを使う）
                float radius = obj->GetCollisionRadius();
                Vector3 center = obj->GetWorldPosition(); // Sphereは通常Offsetがない場合が多いが、あれば加算

                Matrix4x4 matScale = math.MakeScaleMatrix({ radius * 2.0f, radius * 2.0f, radius * 2.0f });
                Matrix4x4 matTrans = math.MakeTranslateMatrix(center);

                drawWorldMatrix = math.Multiply(matScale, matTrans);
            }
        } else {
            // コライダー未設定の「見えない箱」の場合の救済措置
            // Transformそのままで表示（これがないと選択すらできなくなる）
            drawWorldMatrix = obj->GetWorldMatrix();
        }

        // --- 色の決定 ---
        Vector4 color;
        if (isInvisible) {
            // 見えないオブジェクトは「紫」固定
            color = { 0.6f, 0.0f, 0.8f, 1.0f };
        } else {
            // 通常オブジェクトはコライダー種別ごとの色
            switch (type) {
            case ColliderType::kOBB:    color = { 1.0f, 0.2f, 0.2f, 1.0f }; break; // 赤
            case ColliderType::kAABB:   color = { 0.0f, 1.0f, 0.0f, 1.0f }; break; // 緑
            case ColliderType::kSphere: color = { 0.0f, 0.5f, 1.0f, 1.0f }; break; // 青
            default:                    color = { 1.0f, 1.0f, 1.0f, 1.0f }; break; // 白
            }
        }

        // 描画実行
        DrawWireCube(commandList, drawWorldMatrix, color, instanceCount);
        instanceCount++;
    }

    // =========================================================
    // 2. 弾のコライダー描画 (完全版)
    // =========================================================
    if (drawColliders_) {
        const auto& bullets = BulletManager::GetInstance()->GetBullets();

        for (const auto& bullet : bullets) {
            if (!bullet || bullet->IsDead()) continue;
            if (instanceCount >= kMaxInstances) break;

            ColliderType type = bullet->GetColliderType();
            if (type == ColliderType::kNone) continue;

            // 弾は黄色固定
            Vector4 color = { 1.0f, 1.0f, 0.0f, 1.0f };
            Matrix4x4 drawWorldMatrix = math.makeIdentity4x4();

            if (type == ColliderType::kOBB) {
                OBB obb = bullet->GetOBB();
                Matrix4x4 matScale = math.MakeScaleMatrix(obb.size * 2.0f);
                Matrix4x4 matRot = math.makeIdentity4x4();
                matRot.m[0][0] = obb.orientations[0].x; matRot.m[0][1] = obb.orientations[0].y; matRot.m[0][2] = obb.orientations[0].z;
                matRot.m[1][0] = obb.orientations[1].x; matRot.m[1][1] = obb.orientations[1].y; matRot.m[1][2] = obb.orientations[1].z;
                matRot.m[2][0] = obb.orientations[2].x; matRot.m[2][1] = obb.orientations[2].y; matRot.m[2][2] = obb.orientations[2].z;
                Matrix4x4 matTrans = math.MakeTranslateMatrix(obb.center);
                drawWorldMatrix = math.Multiply(matScale, math.Multiply(matRot, matTrans));
            } else if (type == ColliderType::kAABB) {
                AABB aabb = bullet->GetAABB();
                Vector3 center = (aabb.min + aabb.max) * 0.5f;
                Vector3 size = aabb.max - aabb.min;
                Matrix4x4 matScale = math.MakeScaleMatrix(size);
                Matrix4x4 matTrans = math.MakeTranslateMatrix(center);
                drawWorldMatrix = math.Multiply(matScale, matTrans);
            } else if (type == ColliderType::kSphere) {
                float radius = bullet->GetCollisionRadius();
                Vector3 center = bullet->GetWorldPosition();
                Matrix4x4 matScale = math.MakeScaleMatrix({ radius * 2.0f, radius * 2.0f, radius * 2.0f });
                Matrix4x4 matTrans = math.MakeTranslateMatrix(center);
                drawWorldMatrix = math.Multiply(matScale, matTrans);
            }

            DrawWireCube(commandList, drawWorldMatrix, color, instanceCount);
            instanceCount++;
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



#include "DebugEditor.h"
// その他必要なヘッダーは DebugEditor.cpp の先頭にある前提

void DebugEditor::DrawImGui() {
#ifdef USE_IMGUI
    using json = nlohmann::json;

    // 1. プロジェクトウィンドウ (Asset Browserなど)
    DrawProjectWindow();

    if (sceneManager_ == nullptr) return;

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (currentScene == nullptr) {
        ImGui::Begin("Inspector");
        ImGui::Text("No active scene.");
        ImGui::End();
        return;
    }

    // JSONパスの構築
    std::string currentJsonPath = "resources/json/" + std::string(currentSceneFilename_);

    // ==========================================================================================
    // Inspector Window (シーン管理 & 選択中オブジェクトの編集)
    // ==========================================================================================
    ImGui::Begin("Inspector");

    // ---------------------------------------------------------
    // 1. ファイル管理エリア (File Manager)
    // ---------------------------------------------------------
    if (ImGui::CollapsingHeader("Scene File Manager", ImGuiTreeNodeFlags_DefaultOpen)) {

        std::string directoryPath = "resources/json/";
        if (!fs::exists(directoryPath)) {
            fs::create_directories(directoryPath);
        }

        // --- A. ファイル一覧コンボボックス ---
        if (ImGui::BeginCombo("Existing Files", currentSceneFilename_)) {
            if (fs::exists(directoryPath)) {
                for (const auto& entry : fs::directory_iterator(directoryPath)) {
                    if (entry.path().extension() == ".json") {
                        std::string filename = entry.path().filename().string();
                        bool isSelected = (std::string(currentSceneFilename_) == filename);
                        if (ImGui::Selectable(filename.c_str(), isSelected)) {
                            strcpy_s(currentSceneFilename_, filename.c_str());
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                }
            }
            ImGui::EndCombo();
        }

        // --- B. ファイル名入力 ---
        ImGui::InputText("Filename (.json)", currentSceneFilename_, sizeof(currentSceneFilename_));

        // --- C. 保存ボタン (Save Scene) ---
        // ★ InvisibleBox 対応版
        if (ImGui::Button("Save Scene (All)")) {
            json sceneData;
            sceneData["objects"] = json::array();
            std::vector<std::unique_ptr<Object3d>>& allObjects = currentScene->GetObjects();

            for (auto& obj : allObjects) {
                if (obj->GetName().empty()) continue;

                json d;
                d["name"] = obj->GetName();

                // ★ クラス名によるタイプ保存 (ここが重要)
                if (obj->GetClassName() == "InvisibleBox") {
                    d["type"] = "InvisibleBox";
                    // InvisibleBoxの場合、modelNameは保存しなくてよい
                } else {
                    d["type"] = "Model"; // 通常モデル
                    d["modelName"] = obj->GetModelName();
                }

                // 親子関係
                if (obj->GetParent()) {
                    d["parentName"] = obj->GetParent()->GetName();
                } else {
                    d["parentName"] = "";
                }

                // Transform
                Object3d::Transform* objTr = obj->GetTransform();
                d["position"] = { objTr->translate.x, objTr->translate.y, objTr->translate.z };
                d["rotation"] = { objTr->rotate.x, objTr->rotate.y, objTr->rotate.z };
                d["scale"] = { objTr->scale.x, objTr->scale.y, objTr->scale.z };

                // Collider
                Object3d::ColliderConfig c = obj->GetColliderConfig();
                json cData;
                cData["type"] = (int)c.type;
                cData["center"] = { c.center.x, c.center.y, c.center.z };
                cData["size"] = { c.size.x, c.size.y, c.size.z };
                d["collider"] = cData;

                d["collisionAttribute"] = obj->GetCollisionAttribute();
                d["collisionMask"] = obj->GetCollisionMask();
                d["eventID"] = static_cast<int>(obj->GetEventType());

                // Stats (Param)
                if (obj->param_.has_value()) {
                    auto& p = obj->param_.value();
                    d["param"]["hp"] = p.hp;
                    d["param"]["maxHp"] = p.maxHp;
                    d["param"]["speed"] = p.speed;
                    d["param"]["gravity"] = p.gravity;
                    d["param"]["jumpPower"] = p.jumpPower;
                    d["param"]["maxFallSpeed"] = p.maxFallSpeed;
                }

                // エディタ用の可視設定も保存したければここに追加
                // d["isVisible"] = obj->GetIsVisible();

                sceneData["objects"].push_back(d);
            }

            std::ofstream f(currentJsonPath);
            if (f.is_open()) {
                f << sceneData.dump(4);
                f.close();
                DebugConsole::GetInstance()->AddLog("Saved scene to " + currentJsonPath);
            } else {
                DebugConsole::GetInstance()->AddLog("Failed to save JSON! Check folder.");
            }
        }
        ImGui::TextDisabled("Target Path: %s", currentJsonPath.c_str());
    }
    ImGui::Separator();


    // ---------------------------------------------------------
    // 2. オブジェクト詳細 (Inspector)
    // ---------------------------------------------------------
    if (selectedObject_ == nullptr) {
        ImGui::Text("No object selected.");
        ImGui::Text("Select from Hierarchy.");
        ImGui::Separator();
        ImGui::Checkbox("Draw Colliders", &drawColliders_);
    } else {
        // --- 名前表示 ---
        char nameBuffer[256];
        std::string currentName = selectedObject_->GetName();
        if (currentName.empty()) currentName = "NoName";
        strcpy_s(nameBuffer, currentName.c_str());

        if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
            selectedObject_->SetName(std::string(nameBuffer));
        }

        // --- クラス名表示 (デバッグ用) ---
        ImGui::TextDisabled("Class: %s", selectedObject_->GetClassName().c_str());

        // --- 親の名前表示 ---
        if (selectedObject_->GetParent()) {
            ImGui::TextDisabled("Parent: %s", selectedObject_->GetParent()->GetName().c_str());
            if (ImGui::Button("Unparent")) {
                selectedObject_->SetParent(nullptr);
            }
        } else {
            ImGui::TextDisabled("Parent: None");
        }

        // --- Model Asset (InvisibleBoxでない場合のみ表示) ---
        if (selectedObject_->GetClassName() != "InvisibleBox") {
            ImGui::Separator();
            ImGui::Text("Model Asset: %s", selectedObject_->GetModelName().c_str());
            ImGui::Button(" [ Drop Model Here to Switch ] ", ImVec2(-1, 30));

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
                    const char* modelName = (const char*)payload->Data;
                    ModelManager::GetInstance()->LoadModel(modelName);
                    selectedObject_->SetModel(modelName);
                    DebugConsole::GetInstance()->AddLog("Switched model to: " + std::string(modelName));
                }
                ImGui::EndDragDropTarget();
            }
        }

        // --- 可視性設定 (InvisibleBoxなら重要) ---
        ImGui::Separator();
        bool isVisible = selectedObject_->GetIsVisible();
        if (ImGui::Checkbox("Is Visible (In Game)", &isVisible)) {
            selectedObject_->SetIsVisible(isVisible);
        }

        // --- Transform編集 ---
        ImGui::Separator();
        ImGui::Text("Transform");
        Object3d::Transform* transform = selectedObject_->GetTransform();
        bool isTransformChanged = false;

        if (ImGui::DragFloat3("Position", &transform->translate.x, 0.1f)) isTransformChanged = true;

        Vector3 rotDeg = { ToDegrees(transform->rotate.x), ToDegrees(transform->rotate.y), ToDegrees(transform->rotate.z) };
        if (ImGui::DragFloat3("Rotation", &rotDeg.x, 1.0f, -360.0f, 360.0f)) {
            transform->rotate = { ToRadians(rotDeg.x), ToRadians(rotDeg.y), ToRadians(rotDeg.z) };
            isTransformChanged = true;
        }
        if (ImGui::DragFloat3("Scale", &transform->scale.x, 0.05f)) isTransformChanged = true;

        if (isTransformChanged) {
            UpdateObjectInSceneJSON(selectedObject_, currentJsonPath);
        }

        // --- コライダー設定 ---
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Collision Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            Object3d::ColliderConfig colConfig = selectedObject_->GetColliderConfig();
            bool isColChanged = false;

            const char* typeNames[] = { "None", "Sphere", "AABB", "OBB" };
            int currentTypeIndex = (int)colConfig.type;
            if (ImGui::Combo("Type", &currentTypeIndex, typeNames, IM_ARRAYSIZE(typeNames))) {
                colConfig.type = (ColliderType)currentTypeIndex;
                if (colConfig.type == ColliderType::kOBB && colConfig.size.x == 0.0f) {
                    colConfig.size = { 1.0f, 1.0f, 1.0f };
                }
                isColChanged = true;
            }

            if (colConfig.type != ColliderType::kNone) {
                if (ImGui::DragFloat3("Center", &colConfig.center.x, 0.05f)) isColChanged = true;

                if (colConfig.type == ColliderType::kSphere) {
                    if (ImGui::DragFloat("Radius", &colConfig.size.x, 0.05f, 0.0f, 100.0f)) {
                        colConfig.size.y = colConfig.size.z = colConfig.size.x;
                        isColChanged = true;
                    }
                } else {
                    if (ImGui::DragFloat3("Size", &colConfig.size.x, 0.05f, 0.0f, 100.0f)) isColChanged = true;
                }
            }

            if (isColChanged) {
                selectedObject_->SetColliderConfig(colConfig);
                UpdateObjectInSceneJSON(selectedObject_, currentJsonPath);
            }

            // 属性設定
            ImGui::Separator();
            uint32_t currentAttr = selectedObject_->GetCollisionAttribute();
            DrawAttributeSelector("Self Attribute", &currentAttr);
            if (currentAttr != selectedObject_->GetCollisionAttribute()) {
                selectedObject_->SetCollisionAttribute(currentAttr);
            }

            uint32_t currentMask = selectedObject_->GetCollisionMask();
            DrawAttributeSelector("Collision Mask", &currentMask);
            if (currentMask != selectedObject_->GetCollisionMask()) {
                selectedObject_->SetCollisionMask(currentMask);
            }
        }

        // --- Game Data (Stats) ---
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Game Data", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Event Type
            EventType currentType = selectedObject_->GetEventType();
            int currentItemIndex = static_cast<int>(currentType);
            const char* eventNames[] = { "None", "Damage" };
            if (ImGui::Combo("Event Type", &currentItemIndex, eventNames, IM_ARRAYSIZE(eventNames))) {
                selectedObject_->SetEventType(static_cast<EventType>(currentItemIndex));
            }

            ImGui::Spacing();

            // Stats Parameters
            if (!selectedObject_->param_.has_value()) {
                if (ImGui::Button("Add Entity Stats", ImVec2(-1, 0))) {
                    selectedObject_->param_.emplace();
                }
            } else {
                auto& p = selectedObject_->param_.value();
                ImGui::Text("Entity Status:");
                ImGui::Indent();
                ImGui::DragFloat("HP", &p.hp, 1.0f, 0.0f, 9999.0f);
                ImGui::DragFloat("Max HP", &p.maxHp, 1.0f, 1.0f, 9999.0f);
                ImGui::DragFloat("Speed", &p.speed, 0.1f, 0.0f, 100.0f);
                ImGui::DragFloat("Gravity", &p.gravity, 0.01f, -10.0f, 10.0f);
                ImGui::DragFloat("Jump Power", &p.jumpPower, 0.1f, 0.0f, 100.0f);
                ImGui::Unindent();

                ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.6f, 0.6f));
                if (ImGui::Button("Remove Stats", ImVec2(-1, 0))) {
                    selectedObject_->param_ = std::nullopt;
                }
                ImGui::PopStyleColor();
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Delete Object", ImVec2(-1, 0))) {
            currentScene->RequestRemoveObject(selectedObject_);
            selectedObject_ = nullptr;
        }

        // --- Gizmo 操作切替 ---
        ImGui::Separator();
        ImGui::Text("Gizmo Operation:");
        static ImGuizmo::OPERATION currentOperation = ImGuizmo::TRANSLATE;
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) {
            if (ImGui::IsKeyPressed(ImGuiKey_T)) currentOperation = ImGuizmo::TRANSLATE;
            if (ImGui::IsKeyPressed(ImGuiKey_R)) currentOperation = ImGuizmo::ROTATE;
            if (ImGui::IsKeyPressed(ImGuiKey_S)) currentOperation = ImGuizmo::SCALE;
        }
        if (ImGui::RadioButton("Translate", currentOperation == ImGuizmo::TRANSLATE)) currentOperation = ImGuizmo::TRANSLATE; ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", currentOperation == ImGuizmo::ROTATE)) currentOperation = ImGuizmo::ROTATE; ImGui::SameLine();
        if (ImGui::RadioButton("Scale", currentOperation == ImGuizmo::SCALE)) currentOperation = ImGuizmo::SCALE;
    }
    ImGui::End(); // End Inspector


    // ==========================================================================================
    // Hierarchy Window (階層構造)
    // ==========================================================================================
    ImGui::Begin("Hierarchy");

    // スポーン用ドロップエリア
    ImGui::Button("[ DROP MODEL HERE TO SPAWN ]", ImVec2(-1, 30));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
            const char* modelName = (const char*)payload->Data;
            ModelManager::GetInstance()->LoadModel(modelName);

            Object3dCommon* common = currentScene->GetObject3dCommon();
            if (common) {
                auto newObj = std::make_unique<Object3d>();
                newObj->Initialize(common);
                newObj->SetModel(modelName);
                newObj->SetClassName("Model"); // 通常モデル

                static int spawnCount = 0;
                newObj->SetName(std::string(modelName) + "_" + std::to_string(spawnCount++));
                currentScene->AddObject(std::move(newObj));
                DebugConsole::GetInstance()->AddLog("Spawned: " + std::string(modelName));
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::Separator();

    // ツリー表示 (再帰処理)
    std::vector<std::unique_ptr<Object3d>>& objects = currentScene->GetObjects();
    for (auto& obj : objects) {
        // ルートオブジェクト(親がいないやつ)のみ描画開始
        if (obj->GetParent() == nullptr) {
            DrawHierarchyNode(obj.get());
        }
    }

    // 親解除用エリア
    ImGui::Dummy(ImVec2(0, 50));
    ImGui::TextDisabled("(Drop here to unparent)");
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_OBJ")) {
            Object3d* sourceObj = *(Object3d**)payload->Data;
            sourceObj->SetParent(nullptr); // 親解除
            DebugConsole::GetInstance()->AddLog("Unparented: " + sourceObj->GetName());
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::End(); // End Hierarchy


    // ==========================================================================================
    // Spawner Window (生成メニュー)
    // ==========================================================================================
    ImGui::Begin("Spawner");

    // ★ Invisible Box 生成ボタン
    ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.8f, 0.6f, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.8f, 0.7f, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0.8f, 0.8f, 0.8f));

    if (ImGui::Button("Spawn Invisible Box (Trigger)", ImVec2(-1, 40))) {
        Object3dCommon* common = currentScene->GetObject3dCommon();
        if (common) {
            auto newObj = std::make_unique<Object3d>();
            newObj->Initialize(common);

            // Invisible Box 設定
            newObj->SetModel(nullptr);
            newObj->SetIsVisible(false); // ゲーム中は見えない
            newObj->SetClassName("InvisibleBox");
            newObj->SetName("Trigger_Box");

            // コライダー自動付与
            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kAABB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            newObj->SetColliderConfig(colConfig);
            newObj->SetCollisionAttribute(CollisionAttribute::kTrigger); // すり抜け

            // カメラの前などに配置
            newObj->SetTranslate({ 0, 2.0f, 0 });

            // 選択状態にする
            selectedObject_ = newObj.get();
            currentScene->AddObject(std::move(newObj));
            DebugConsole::GetInstance()->AddLog("Spawned Invisible Box");
        }
    }
    ImGui::PopStyleColor(3);

    ImGui::Separator();
    ImGui::Text("Standard Models:");

    // 通常モデル生成
    if (ImGui::Button("Refresh Model List")) {
        modelNames_ = ModelManager::GetInstance()->GetLoadedModelNames();
        selectedModelIndex_ = 0;
    }

    if (!modelNames_.empty()) {
        std::vector<const char*> namesCStr;
        for (const std::string& name : modelNames_) {
            namesCStr.push_back(name.c_str());
        }
        ImGui::ListBox("##ModelList", &selectedModelIndex_, namesCStr.data(), (int)namesCStr.size(), 5);

        if (ImGui::Button("Spawn Selected Model", ImVec2(-1, 0))) {
            if (selectedModelIndex_ >= 0 && selectedModelIndex_ < modelNames_.size()) {
                std::string modelName = modelNames_[selectedModelIndex_];
                Object3dCommon* common = currentScene->GetObject3dCommon();
                if (common) {
                    auto newObj = std::make_unique<Object3d>();
                    newObj->Initialize(common);
                    newObj->SetModel(modelName);
                    newObj->SetClassName("Model");

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

// ---------------------------------------------------------------------
//  階層構造を再帰的に描画するヘルパー関数
// 
// ---------------------------------------------------------------------
void DebugEditor::DrawHierarchyNode(Object3d* obj) {
#ifdef USE_IMGUI
    if (!obj) return;

    // ノードのフラグ設定
    ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (selectedObject_ == obj) {
        node_flags |= ImGuiTreeNodeFlags_Selected;
    }

    // 子供がいないならリーフノード（葉っぱ）として扱う

    bool hasChildren = false;


    // オブジェクト名の取得
    std::string name = obj->GetName();
    if (name.empty()) name = "NoName";
    if (obj->GetClassName() == "InvisibleBox") {
        name = "[Trigger] " + name; // わかりやすく装飾
    }

    // ツリーノード描画
    bool node_open = ImGui::TreeNodeEx((void*)obj, node_flags, name.c_str());

    // クリック処理
    if (ImGui::IsItemClicked()) {
        selectedObject_ = obj;
    }

    // --- Drag: 子供にするために持ち上げる ---
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("HIERARCHY_OBJ", &obj, sizeof(Object3d*));
        ImGui::Text("Move %s", name.c_str());
        ImGui::EndDragDropSource();
    }

    // --- Drop: 誰かを受け入れて子供にする ---
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_OBJ")) {
            Object3d* sourceObj = *(Object3d**)payload->Data;
            // 自分自身や、自分の親を子供にしないようチェック
            if (sourceObj != obj && sourceObj->GetParent() != obj) {
                sourceObj->SetParent(obj);
                DebugConsole::GetInstance()->AddLog(sourceObj->GetName() + " is now child of " + obj->GetName());
            }
        }
        ImGui::EndDragDropTarget();
    }

    // 子供がいれば再帰描画
    if (node_open) {
        // 現在のシーンから、このobjを親に持つオブジェクトを探して描画
        if (sceneManager_ && sceneManager_->GetCurrentScene()) {
            auto& allObjs = sceneManager_->GetCurrentScene()->GetObjects();
            for (auto& child : allObjs) {
                if (child->GetParent() == obj) {
                    DrawHierarchyNode(child.get());
                }
            }
        }
        ImGui::TreePop();
    }
#endif
}

/// <summary>
/// scene_layout.json を読み込み、特定のオブジェクトの情報だけを更新して上書き保存する
/// </summary>
void DebugEditor::UpdateObjectInSceneJSON(Object3d* object, const std::string& filename) {
    using json = nlohmann::json;
    std::ifstream file(filename);

    json sceneData;

    // 1. 既存のJSONファイルを読み込む
    if (file.is_open()) {
        try {
            if (file.peek() != std::ifstream::traits_type::eof()) {
                sceneData = json::parse(file);
            } else {
                sceneData["objects"] = json::array();
            }
        }
        catch (...) {
            // パースエラー時は空で初期化
            sceneData["objects"] = json::array();
        }
        file.close();
    } else {
        // ファイルがない場合は新規作成として扱う
        sceneData["objects"] = json::array();
    }

    // =========================================================
    // 2. 保存するデータを構築 (オブジェクトの現在の状態)
    // =========================================================
    json currentData;

    // --- 基本情報 ---
    currentData["name"] = object->GetName();

    // タイプ判定 (InvisibleBox か Model か)
    if (object->GetClassName() == "InvisibleBox") {
        currentData["type"] = "InvisibleBox";
        // モデル名は不要なので保存しない (あるいは空文字)
    } else {
        currentData["type"] = "Model";
        currentData["modelName"] = object->GetModelName();
    }

    //  親子関係
    if (object->GetParent()) {
        currentData["parentName"] = object->GetParent()->GetName();
    } else {
        currentData["parentName"] = "";
    }

    // --- Transform ---
    Object3d::Transform* tf = object->GetTransform();
    currentData["position"] = { tf->translate.x, tf->translate.y, tf->translate.z };
    currentData["rotation"] = { tf->rotate.x, tf->rotate.y, tf->rotate.z };
    currentData["scale"] = { tf->scale.x, tf->scale.y, tf->scale.z };

    // --- Collider Config ---
    const Object3d::ColliderConfig& config = object->GetColliderConfig();
    currentData["collider"]["type"] = static_cast<int>(config.type);
    currentData["collider"]["center"] = { config.center.x, config.center.y, config.center.z };
    currentData["collider"]["size"] = { config.size.x, config.size.y, config.size.z };

    // --- Attributes ---
    currentData["collisionAttribute"] = object->GetCollisionAttribute();
    currentData["collisionMask"] = object->GetCollisionMask();

    // --- Event ID ---
    currentData["eventID"] = static_cast<int>(object->GetEventType());

    // --- Entity Parameters (Stats) ---
    if (object->param_.has_value()) {
        auto& p = object->param_.value();
        currentData["param"]["hp"] = p.hp;
        currentData["param"]["maxHp"] = p.maxHp;
        currentData["param"]["speed"] = p.speed;
        currentData["param"]["gravity"] = p.gravity;
        currentData["param"]["jumpPower"] = p.jumpPower;
        currentData["param"]["maxFallSpeed"] = p.maxFallSpeed;
    }

    // =========================================================
    // 3. JSON配列内を探して更新 or 追加
    // =========================================================
    bool found = false;

    // "objects" 配列がない場合は作成
    if (!sceneData.contains("objects") || !sceneData["objects"].is_array()) {
        sceneData["objects"] = json::array();
    }

    for (auto& objData : sceneData["objects"]) {
        // 名前で一致するオブジェクトを探す
        if (objData.contains("name") && objData["name"] == object->GetName()) {
            // 見つかったらデータを丸ごと上書き更新
            objData = currentData;
            found = true;
            break;
        }
    }

    // ★ 見つからなかった場合（新規スポーンなど）は、配列の末尾に追加
    if (!found) {
        sceneData["objects"].push_back(currentData);
        DebugConsole::GetInstance()->AddLog("Added new object to JSON: " + object->GetName());
    } else {
        DebugConsole::GetInstance()->AddLog("Updated object in JSON: " + object->GetName());
    }

    // =========================================================
    // 4. ファイル書き込み
    // =========================================================
    std::ofstream outFile(filename);
    if (outFile.is_open()) {
        outFile << sceneData.dump(4); // インデント4で整形保存
        outFile.close();
    } else {
        DebugConsole::GetInstance()->AddLog("Failed to write JSON file: " + filename);
    }
}

void DebugEditor::DrawProjectWindow() {
    ImGui::Begin("Project (Assets)");

    std::string baseDirectory = "resouces/3DModel";

    if (fs::exists(baseDirectory) && fs::is_directory(baseDirectory)) {
        ImGui::Text("Drag model to Object List!"); // 誘導メッセージ変更
        ImGui::Separator();

        for (const auto& entry : fs::directory_iterator(baseDirectory)) {
            if (entry.is_directory()) {
                std::string folderName = entry.path().filename().string();
                std::string foundObjName = "";
                for (const auto& subEntry : fs::directory_iterator(entry.path())) {
                    if (subEntry.path().extension() == ".obj") {
                        foundObjName = subEntry.path().stem().string();
                        break;
                    }
                }
                if (foundObjName.empty()) continue;

                // IDプッシュ
                ImGui::PushID(folderName.c_str());

                // ボタン表示
                ImGui::Button(folderName.c_str(), ImVec2(100, 0));

                // --- ドラッグ処理  ---
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    ImGui::SetDragDropPayload("MODEL_ASSET", foundObjName.c_str(), foundObjName.size() + 1);
                    ImGui::Text("Spawn: %s", foundObjName.c_str());
                    ImGui::EndDragDropSource();
                }

                // IDポップ (ドラッグ処理の後に移動)
                ImGui::PopID();

                // レイアウト調整
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


// 属性編集用ヘルパー関数
void DebugEditor::DrawAttributeSelector(const char* label, uint32_t* attribute) {
    if (ImGui::TreeNode(label)) {
        int flags = static_cast<int>(*attribute);

        // 各属性のチェックボックス
        ImGui::CheckboxFlags("Player", &flags, 1 << 0);
        ImGui::CheckboxFlags("Enemy", &flags, 1 << 1);
        ImGui::CheckboxFlags("Ground", &flags, 1 << 2);
        ImGui::CheckboxFlags("Bullet", &flags, 1 << 3);
        ImGui::CheckboxFlags("Trigger", &flags, 1 << 4);


        // 変更を書き戻す
        *attribute = static_cast<uint32_t>(flags);

        ImGui::TreePop();
    }
}



