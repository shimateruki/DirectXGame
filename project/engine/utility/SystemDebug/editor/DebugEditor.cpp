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
// コライダー描画処理 (完全版)
// ========================================================================
void DebugEditor::DrawDebug(ID3D12GraphicsCommandList* commandList) {
    if (!drawColliders_ || sceneManager_ == nullptr) return;

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (!currentScene) return;

    // パイプライン設定
    commandList->SetPipelineState(primitivePipelineState_.Get());
    commandList->SetGraphicsRootSignature(primitiveRootSignature_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    commandList->IASetVertexBuffers(0, 1, &cubeVertexBufferView_);
    commandList->IASetIndexBuffer(&cubeIndexBufferView_);

    int instanceCount = 0;
    Math math; // 行列計算用

    // =========================================================
    // 1. シーン内のオブジェクトを描画 (Player, Bossなど)
    // =========================================================
    auto& objects = currentScene->GetObjects();

    for (const auto& obj : objects) {
        if (!obj) continue;
        if (instanceCount >= kMaxInstances) break;

        ColliderType type = obj->GetColliderType();
        Matrix4x4 drawWorldMatrix = math.makeIdentity4x4();
        Vector4 color = { 0.0f, 1.0f, 0.0f, 1.0f }; // デフォルト緑

        // --- OBB ---
        if (type == ColliderType::kOBB) {
            OBB obb = obj->GetOBB();
            Matrix4x4 matScale = math.MakeScaleMatrix(obb.size * 2.0f);
            Matrix4x4 matRot = math.makeIdentity4x4();
            matRot.m[0][0] = obb.orientations[0].x; matRot.m[0][1] = obb.orientations[0].y; matRot.m[0][2] = obb.orientations[0].z;
            matRot.m[1][0] = obb.orientations[1].x; matRot.m[1][1] = obb.orientations[1].y; matRot.m[1][2] = obb.orientations[1].z;
            matRot.m[2][0] = obb.orientations[2].x; matRot.m[2][1] = obb.orientations[2].y; matRot.m[2][2] = obb.orientations[2].z;
            Matrix4x4 matTrans = math.MakeTranslateMatrix(obb.center);
            drawWorldMatrix = math.Multiply(matScale, math.Multiply(matRot, matTrans));
            color = { 1.0f, 0.2f, 0.2f, 1.0f }; // 赤
        }
        // --- AABB ---
        else if (type == ColliderType::kAABB) {
            AABB aabb = obj->GetAABB();
            Vector3 center = (aabb.min + aabb.max) * 0.5f;
            Vector3 size = aabb.max - aabb.min;
            Matrix4x4 matScale = math.MakeScaleMatrix(size);
            Matrix4x4 matTrans = math.MakeTranslateMatrix(center);
            drawWorldMatrix = math.Multiply(matScale, matTrans);
            color = { 0.0f, 1.0f, 0.0f, 1.0f }; // 緑
        }
        // --- Sphere ---
        else if (type == ColliderType::kSphere) {
            float radius = obj->GetCollisionRadius();
            Vector3 center = obj->GetWorldPosition();
            Matrix4x4 matScale = math.MakeScaleMatrix({ radius * 2.0f, radius * 2.0f, radius * 2.0f });
            Matrix4x4 matTrans = math.MakeTranslateMatrix(center);
            drawWorldMatrix = math.Multiply(matScale, matTrans);
            color = { 0.0f, 0.5f, 1.0f, 1.0f }; // 青
        }

        DrawWireCube(commandList, drawWorldMatrix, color, instanceCount);
        instanceCount++;
    }


    const auto& bullets = BulletManager::GetInstance()->GetBullets();

    for (const auto& bullet : bullets) {
        if (!bullet || bullet->IsDead()) continue;
        if (instanceCount >= kMaxInstances) break;

        ColliderType type = bullet->GetColliderType();
        Matrix4x4 drawWorldMatrix = math.makeIdentity4x4();
        Vector4 color = { 0.0f, 1.0f, 0.0f, 1.0f };

        // --- OBB (スピニングブレードなど) ---
        if (type == ColliderType::kOBB) {
            OBB obb = bullet->GetOBB();
            Matrix4x4 matScale = math.MakeScaleMatrix(obb.size * 2.0f);
            Matrix4x4 matRot = math.makeIdentity4x4();
            matRot.m[0][0] = obb.orientations[0].x; matRot.m[0][1] = obb.orientations[0].y; matRot.m[0][2] = obb.orientations[0].z;
            matRot.m[1][0] = obb.orientations[1].x; matRot.m[1][1] = obb.orientations[1].y; matRot.m[1][2] = obb.orientations[1].z;
            matRot.m[2][0] = obb.orientations[2].x; matRot.m[2][1] = obb.orientations[2].y; matRot.m[2][2] = obb.orientations[2].z;
            Matrix4x4 matTrans = math.MakeTranslateMatrix(obb.center);
            drawWorldMatrix = math.Multiply(matScale, math.Multiply(matRot, matTrans));
            color = { 1.0f, 0.2f, 0.2f, 1.0f }; // 赤
        }
        // --- AABB ---
        else if (type == ColliderType::kAABB) {
            AABB aabb = bullet->GetAABB();
            Vector3 center = (aabb.min + aabb.max) * 0.5f;
            Vector3 size = aabb.max - aabb.min;
            Matrix4x4 matScale = math.MakeScaleMatrix(size);
            Matrix4x4 matTrans = math.MakeTranslateMatrix(center);
            drawWorldMatrix = math.Multiply(matScale, matTrans);
            color = { 0.0f, 1.0f, 0.0f, 1.0f }; // 緑
        }
        // --- Sphere (通常の弾) ---
        else if (type == ColliderType::kSphere) {
            float radius = bullet->GetCollisionRadius();
            Vector3 center = bullet->GetWorldPosition();
            Matrix4x4 matScale = math.MakeScaleMatrix({ radius * 2.0f, radius * 2.0f, radius * 2.0f });
            Matrix4x4 matTrans = math.MakeTranslateMatrix(center);
            drawWorldMatrix = math.Multiply(matScale, matTrans);
            color = { 0.0f, 0.5f, 1.0f, 1.0f }; // 青
        }

        DrawWireCube(commandList, drawWorldMatrix, color, instanceCount);
        instanceCount++;
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

    // 1. プロジェクトウィンドウを表示 (ここからドラッグ開始)
    DrawProjectWindow();

    if (sceneManager_ == nullptr) return;

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (currentScene == nullptr) {
        ImGui::Begin("Inspector");
        ImGui::Text("No active scene.");
        ImGui::End();
        return;
    }

    // ==========================================================================================
    // Inspector Window (シーン管理 & 選択中オブジェクトの編集)
    // ==========================================================================================
    ImGui::Begin("Inspector");

    // =========================================================
    //  1. ファイル管理エリア (File Manager)
    // =========================================================
    if (ImGui::CollapsingHeader("Scene File Manager", ImGuiTreeNodeFlags_DefaultOpen)) {

        // 保存先のディレクトリパス
        std::string directoryPath = "resouces/json/";

        // ディレクトリが存在しない場合は作成しておく（安全対策）
        if (!fs::exists(directoryPath)) {
            fs::create_directories(directoryPath);
        }

        // --- A. ファイル一覧 (Combo Box) ---
        // 既存のファイルをクリックで選べる機能
        if (ImGui::BeginCombo("Existing Files", currentSceneFilename_)) {
            if (fs::exists(directoryPath)) {
                for (const auto& entry : fs::directory_iterator(directoryPath)) {
                    // .jsonファイルのみを表示
                    if (entry.path().extension() == ".json") {
                        std::string filename = entry.path().filename().string();

                        bool isSelected = (std::string(currentSceneFilename_) == filename);
                        if (ImGui::Selectable(filename.c_str(), isSelected)) {
                            // 選んだファイル名を入力欄にコピー
                            strcpy_s(currentSceneFilename_, filename.c_str());
                        }

                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                }
            }
            ImGui::EndCombo();
        }

        // --- B. ファイル名入力欄 ---
        // 新規作成や、上記で選んだ名前の確認用
        ImGui::InputText("Filename (.json)", currentSceneFilename_, sizeof(currentSceneFilename_));

        // パスの生成 (resouces/json/ + 入力名)
        std::string fullPath = directoryPath + std::string(currentSceneFilename_);

        // --- C. 保存ボタン (Save) ---
        if (ImGui::Button("Save Scene")) {
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

                // コライダー保存
                Object3d::ColliderConfig c = obj->GetColliderConfig();
                json cData;
                cData["type"] = (int)c.type;
                cData["center"] = { c.center.x, c.center.y, c.center.z };
                cData["size"] = { c.size.x, c.size.y, c.size.z };
                d["collider"] = cData;

                // 属性とマスク
                d["collisionAttribute"] = obj->GetCollisionAttribute();
                d["collisionMask"] = obj->GetCollisionMask();

                sceneData["objects"].push_back(d);
            }

            std::ofstream f(fullPath);
            if (f.is_open()) {
                f << sceneData.dump(4);
                f.close();
                DebugConsole::GetInstance()->AddLog("Saved scene to " + fullPath);
            } else {
                DebugConsole::GetInstance()->AddLog("Failed to save JSON! Check folder.");
            }
        }

        // 現在のターゲットパスを表示
        ImGui::TextDisabled("Target Path: %s", fullPath.c_str());
    }
    ImGui::Separator();


    // =========================================================
    // 2. オブジェクト詳細 (Inspector) 
    // =========================================================
    if (selectedObject_ == nullptr) {
        ImGui::Text("No object selected.");
        ImGui::Text("Select from Object List.");
        ImGui::Separator();
        ImGui::Checkbox("Draw Colliders", &drawColliders_);
    } else {
        // --- 名前編集 ---
        char nameBuffer[256];
        std::string currentName = selectedObject_->GetName();
        if (currentName.empty()) currentName = "NoName";
        strcpy_s(nameBuffer, currentName.c_str());

    

        // ---  D&D受け入れエリア (モデル差し替え) ---
        ImGui::Separator();
        ImGui::Text("Model Asset:");
        ImGui::Button(" [ Drop Model Here to Switch ] ", ImVec2(-1, 30));

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
                const char* modelName = (const char*)payload->Data;
                ModelManager::GetInstance()->LoadModel(modelName); // 必要ならロード
                selectedObject_->SetModel(modelName);
                DebugConsole::GetInstance()->AddLog("Switched model to: " + std::string(modelName));
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::Separator();
        // ----------------------------------

        // --- Transform編集 ---
        Object3d::Transform* transform = selectedObject_->GetTransform();
        ImGui::DragFloat3("Position", &transform->translate.x, 0.1f);

        Vector3 rotDeg = { ToDegrees(transform->rotate.x), ToDegrees(transform->rotate.y), ToDegrees(transform->rotate.z) };
        if (ImGui::DragFloat3("Rotation (Degrees)", &rotDeg.x, 1.0f, -360.0f, 360.0f)) {
            transform->rotate = { ToRadians(rotDeg.x), ToRadians(rotDeg.y), ToRadians(rotDeg.z) };
        }
        ImGui::DragFloat3("Scale", &transform->scale.x, 0.05f);

        // ==========================================================
        //  コライダー設定 (Collision) 
        // ==========================================================
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Collision Settings", ImGuiTreeNodeFlags_DefaultOpen)) {

            // Object3dから現在の設定を取得
            Object3d::ColliderConfig colConfig = selectedObject_->GetColliderConfig();
            bool isColChanged = false;

            // 1. 種類の選択
            const char* typeNames[] = { "None", "Sphere", "AABB", "OBB" };
            int currentTypeIndex = (int)colConfig.type;

            if (ImGui::Combo("Type", &currentTypeIndex, typeNames, IM_ARRAYSIZE(typeNames))) {
                colConfig.type = (ColliderType)currentTypeIndex;

                // OBBにした瞬間サイズが0だと見えないので初期値を入れる親切設計
                if (colConfig.type == ColliderType::kOBB) {
                    if (colConfig.size.x == 0.0f && colConfig.size.y == 0.0f && colConfig.size.z == 0.0f) {
                        colConfig.size = { 1.0f, 1.0f, 1.0f };
                    }
                }
                isColChanged = true;
            }

            // 2. 詳細設定 (None以外なら表示)
            if (colConfig.type != ColliderType::kNone) {
                // 中心オフセット
                if (ImGui::DragFloat3("Center", &colConfig.center.x, 0.05f)) {
                    isColChanged = true;
                }

                // サイズ / 半径
                if (colConfig.type == ColliderType::kSphere) {
                    // 球の場合は X成分を半径として使う
                    if (ImGui::DragFloat("Radius", &colConfig.size.x, 0.05f, 0.0f, 100.0f)) {
                        colConfig.size.y = colConfig.size.x;
                        colConfig.size.z = colConfig.size.x;
                        isColChanged = true;
                    }
                } else {
                    // AABB, OBB は XYZサイズ
                    if (ImGui::DragFloat3("Size", &colConfig.size.x, 0.05f, 0.0f, 100.0f)) {
                        isColChanged = true;
                    }
                }
            }

            // 変更があれば適用
            if (isColChanged) {
                selectedObject_->SetColliderConfig(colConfig);
                // JSON単体更新関数（必要なら使う）
                UpdateObjectInSceneJSON(selectedObject_, "resouces/json/" + std::string(currentSceneFilename_));
            }

            ImGui::Separator();
            ImGui::Text("Collision Logic");

            // --- 3. 属性 (Self Attribute) ---
            uint32_t currentAttr = selectedObject_->GetCollisionAttribute();
            DrawAttributeSelector("Self Attribute (Category)", &currentAttr);
            if (currentAttr != selectedObject_->GetCollisionAttribute()) {
                selectedObject_->SetCollisionAttribute(currentAttr);
                UpdateObjectInSceneJSON(selectedObject_, "resouces/json/" + std::string(currentSceneFilename_));
            }

            // --- 4. マスク (Collision Mask) ---
            uint32_t currentMask = selectedObject_->GetCollisionMask();
            DrawAttributeSelector("Collision Mask (Filter)", &currentMask);
            if (currentMask != selectedObject_->GetCollisionMask()) {
                selectedObject_->SetCollisionMask(currentMask);
                UpdateObjectInSceneJSON(selectedObject_, "resouces/json/" + std::string(currentSceneFilename_));
            }
        }
        // ==========================================================

        ImGui::Separator();

        // --- 単体更新ボタン ---
        // (オートセーブを使わず手動で単体を更新したい場合用)
        if (ImGui::Button("Update This Object JSON")) {
            UpdateObjectInSceneJSON(selectedObject_, "resouces/json/" + std::string(currentSceneFilename_));
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
        if (ImGui::Button("Delete", ImVec2(0, 0))) {
            currentScene->RequestRemoveObject(selectedObject_);
            selectedObject_ = nullptr;
        }

        // --- Gizmo Control ---
        ImGui::Separator();
        ImGui::Text("Gizmo Operation:");

        static ImGuizmo::OPERATION currentOperation = ImGuizmo::TRANSLATE;
        static float snapTranslate[3] = { 0.5f, 0.5f, 0.5f };
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

        ImGui::Text("Snap (Hold Ctrl):");
        ImGui::SameLine();
        if (currentOperation == ImGuizmo::TRANSLATE) ImGui::InputFloat3("##SnapT", snapTranslate);
        else if (currentOperation == ImGuizmo::ROTATE) ImGui::InputFloat("##SnapR", &snapRotation);
        else ImGui::InputFloat("##SnapS", &snapScale);
    }
    ImGui::End(); // End Inspector


    // ==========================================================================================
    // Object List Window
    // ==========================================================================================
    ImGui::Begin("Object List");

    // ドロップ誘導ボタン
    ImGui::Button("[ DROP MODEL HERE TO SPAWN ]", ImVec2(-1, 50));

    // ドロップ受け付け
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {

            const char* modelName = (const char*)payload->Data;
            ModelManager::GetInstance()->LoadModel(modelName);

            Object3dCommon* common = currentScene->GetObject3dCommon();
            if (common) {
                auto newObj = std::make_unique<Object3d>();
                newObj->Initialize(common);
                newObj->SetModel(modelName);

                static int spawnCount = 0;
                newObj->SetName(std::string(modelName) + "_" + std::to_string(spawnCount++));
                newObj->GetTransform()->translate = { 0.0f, 0.0f, 0.0f };

                currentScene->AddObject(std::move(newObj));
                DebugConsole::GetInstance()->AddLog("Spawned: " + std::string(modelName));
            }
        }
        ImGui::EndDragDropTarget();
    }

    // リスト表示
    std::vector<std::unique_ptr<Object3d>>& objects = currentScene->GetObjects();
    for (auto& obj : objects) {
        const std::string& objName = obj->GetName();
        if (objName.empty()) continue;

        bool isSelected = (obj.get() == selectedObject_);
        if (ImGui::Selectable(objName.c_str(), isSelected)) {
            selectedObject_ = obj.get();
        }
    }
    ImGui::End();


    // ==========================================================================================
    // Spawner Window
    // ==========================================================================================
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
    using json = nlohmann::json;
    std::ifstream file(filename);
    if (!file.is_open()) return;

    json sceneData;
    try {
        sceneData = json::parse(file);
    }
    catch (...) {
        return;
    }
    file.close();

    bool found = false;
    if (sceneData.contains("objects") && sceneData["objects"].is_array()) {
        for (auto& objData : sceneData["objects"]) {
            // 名前で一致するオブジェクトを探す
            if (objData.contains("name") && objData["name"] == object->GetName()) {

                // --- 1. Transform (既存) ---
                Object3d::Transform* tf = object->GetTransform();
                objData["position"] = { tf->translate.x, tf->translate.y, tf->translate.z };
                objData["rotation"] = { tf->rotate.x, tf->rotate.y, tf->rotate.z };
                objData["scale"] = { tf->scale.x, tf->scale.y, tf->scale.z };

                // --- 2. ColliderConfig (Type, Center, Size) ---
                const Object3d::ColliderConfig& config = object->GetColliderConfig();
                objData["collider"]["type"] = static_cast<int>(config.type);
                objData["collider"]["center"] = { config.center.x, config.center.y, config.center.z };
                objData["collider"]["size"] = { config.size.x, config.size.y, config.size.z };

                // --- 3. Attribute & Mask (属性) ---
                objData["collisionAttribute"] = object->GetCollisionAttribute();
                objData["collisionMask"] = object->GetCollisionMask();

                found = true;
                break;
            }
        }
    }

    // 新規追加ロジックが必要な場合はここに記述 

    // ファイル書き込み
    std::ofstream outFile(filename);
    if (outFile.is_open()) {
        outFile << sceneData.dump(4); // インデント4で保存
        outFile.close();
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

        // 変更を書き戻す
        *attribute = static_cast<uint32_t>(flags);

        ImGui::TreePop();
    }
}