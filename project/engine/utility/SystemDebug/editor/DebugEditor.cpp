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
#include "GhostRecorder.h" 
#include "CameraEditor.h"
#include "Transform.h"
#include "ParticleManager.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <DebugConsole.h>
#include <CollisionManager.h>
#include <filesystem> // ファイル操作用
#include <BulletManager.h>
#include <PresetManager.h>
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
    PresetManager::GetInstance()->LoadPresets();
}

// ========================================================================
// 更新 (ImGui処理)
// ========================================================================
void DebugEditor::Update() {
#ifdef USE_IMGUI
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    InputManager* input = InputManager::GetInstance();
    Math math;

    // シーン変更リセット
    if (lastUpdatedScene_ != currentScene) {
        selectedObject_ = nullptr; previewObject_ = nullptr; lastUpdatedScene_ = currentScene;
    }

    // --- カメラ制御 (設置モード用) ---
    bool isPreviewActive = (previewObject_ != nullptr);
    if (isPreviewActive && !wasPreviewActive_) {
        CameraEditor* camEditor = CameraEditor::GetInstance();
        previousCameraMode_ = (int)camEditor->GetMode();
        camEditor->SetMode(CameraEditor::Mode::Editor);
        Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
        Vector3 curPos = cam ? cam->GetEye() : Vector3{ 0, 10, -10 };
        camEditor->SetEditorCameraTransform({ curPos.x, curPos.y + 20.0f, curPos.z - 5.0f }, { ToRadians(60.0f), 0.0f, 0.0f });
    } else if (!isPreviewActive && wasPreviewActive_) {
        CameraEditor::GetInstance()->SetMode((CameraEditor::Mode)previousCameraMode_);
    }
    wasPreviewActive_ = isPreviewActive;

    // =========================================================
    //  モードA: 設置モード
    // =========================================================
    if (previewObject_) {
        if (isGameViewHovered_) {
            Ray ray = ScreenPointToRay(gameViewMousePos_);
            Vector3 finalPos = { 0, 0, 0 }; bool found = false;

            // 当たり判定 (AABB)
            auto& objects = currentScene->GetObjects();
            RayResult best; best.isHit = false; best.distance = 1e5f;
            for (auto& obj : objects) {
                if (obj.get() == previewObject_.get() || obj->GetName() == "Cursor") continue;
                // 親子関係を考慮したワールド座標で判定
                Matrix4x4 wm = obj->GetWorldMatrix();
                Vector3 wp = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };
                Vector3 ws = obj->GetTransform()->scale;
                RayResult tmp;
                if (math.IntersectRayAABB(ray, wp - ws, wp + ws, &tmp)) {
                    if (tmp.distance < best.distance) best = tmp;
                }
            }
            if (best.isHit) { finalPos = best.point; finalPos.y += 1.0f; found = true; } else if (IntersectRayPlane(ray, finalPos)) { finalPos.y = 0.5f; found = true; } else { finalPos = ray.origin + math.Normalize(ray.diff) * 10.0f; found = true; }

            if (found) {
                if (isGridSnapEnabled_) {
                    finalPos.x = std::round(finalPos.x / snapValue_) * snapValue_;
                    finalPos.z = std::round(finalPos.z / snapValue_) * snapValue_;
                }
                previewObject_->GetTransform()->translate = finalPos;
                previewObject_->SetColor({ 1.0f, 1.8f, 1.0f, 0.5f });
                previewObject_->UpdateWorldMatrix();
            }

            // ★ImGuiのクリック判定を使用 (マルチビューポート対応)
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                auto newObj = std::make_unique<Object3d>();
                newObj->Initialize(currentScene->GetObject3dCommon());
                newObj->CopyFrom(previewObject_.get());
                newObj->SetColor({ 1,1,1,1 });
                currentScene->AddObject(std::move(newObj));
            }
            if (input->IsKeyTriggered(DIK_E)) previewObject_ = nullptr;
        }
    }
    // =========================================================
    //  モードB: 通常選択モード
    // =========================================================
    else {
        // B-1. ショートカット
        if (!ImGui::GetIO().WantCaptureKeyboard) {
            if (input->IsKeyTriggered(DIK_DELETE)) DeleteSelected();
            if ((input->IsKeyPressed(DIK_LCONTROL)) && input->IsKeyTriggered(DIK_C)) DuplicateSelected();
            if ((input->IsKeyPressed(DIK_LCONTROL)) && input->IsKeyTriggered(DIK_Z)) PerformUndo();
            if ((input->IsKeyPressed(DIK_LCONTROL)) && input->IsKeyTriggered(DIK_Y)) PerformRedo();
            if ((input->IsKeyPressed(DIK_LCONTROL)) && input->IsKeyTriggered(DIK_S)) SaveScene();
        }

        // B-2. マウス選択 (ギズモを触っていない時)
        if (isGameViewHovered_ && !ImGuizmo::IsOver()) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                Ray ray = ScreenPointToRay(gameViewMousePos_);
                auto& objects = currentScene->GetObjects();
                RayResult best; best.isHit = false; best.distance = 1e5f; Object3d* hit = nullptr;

                for (auto& obj : objects) {
                    if (obj->GetName() == "Cursor" || obj->GetName() == "Line") continue;
                    Matrix4x4 wm = obj->GetWorldMatrix();
                    Vector3 wp = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };
                    Vector3 ws = obj->GetTransform()->scale;
                    RayResult tmp;
                    if (math.IntersectRayAABB(ray, wp - ws, wp + ws, &tmp)) {
                        if (tmp.distance < best.distance) { best = tmp; hit = obj.get(); }
                    }
                }
                selectedObject_ = hit;
            }
        }

        // B-3. ギズモ表示
        if (selectedObject_) {
            static ImGuizmo::OPERATION curOp = ImGuizmo::TRANSLATE;
            if (input->IsKeyTriggered(DIK_T)) curOp = ImGuizmo::TRANSLATE;
            if (input->IsKeyTriggered(DIK_R)) curOp = ImGuizmo::ROTATE;
            if (input->IsKeyTriggered(DIK_S)) curOp = ImGuizmo::SCALE;

            Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
            if (cam) {
                // ★最重要: GameViewの描画リストを使う
                ImGuizmo::SetDrawlist();
                ImGuizmo::SetRect(gameViewOffset_.x, gameViewOffset_.y, gameViewSize_.x, gameViewSize_.y);

                Matrix4x4 view = cam->GetViewMatrix();
                Matrix4x4 proj = cam->GetProjectionMatrix();
                Transform* tr = selectedObject_->GetTransform();
                Matrix4x4 world = math.MakeAffineMatrix(tr->scale, tr->rotate, tr->translate);

                float snapVal = isGridSnapEnabled_ ? snapValue_ : 0.0f;
                float snapArr[3] = { snapVal, snapVal, snapVal };

                ImGuizmo::Manipulate(&view.m[0][0], &proj.m[0][0], curOp, ImGuizmo::WORLD, &world.m[0][0], nullptr, isGridSnapEnabled_ ? snapArr : nullptr);

                if (ImGuizmo::IsUsing()) {
                    if (!isDraggingTransform_) { isDraggingTransform_ = true; tempTransformStart_ = *tr; }
                    Vector3 s, rDeg, t;
                    ImGuizmo::DecomposeMatrixToComponents(&world.m[0][0], &t.x, &rDeg.x, &s.x);
                    tr->translate = t;
                    tr->rotate = { ToRadians(rDeg.x), ToRadians(rDeg.y), ToRadians(rDeg.z) };
                    tr->scale = s;
                    selectedObject_->UpdateWorldMatrix();
                } else if (isDraggingTransform_) {
                    isDraggingTransform_ = false;
                    TransformCommand cmd = { selectedObject_, tempTransformStart_, *tr };
                    undoStack_.push_back(cmd);
                }
            }
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

    Microsoft::WRL::ComPtr<IDxcBlob> vsBlob = dxCommon_->CompileShader(L"Resources/shader/DebugPrimitive.VS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> psBlob = dxCommon_->CompileShader(L"Resources/shader/DebugPrimitive.PS.hlsl", L"ps_6_0");
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
        // 「見えない物体(透明な壁など)」は編集用に表示したままにする
        if (!drawColliders_ && !isInvisible) continue;


        // --- 行列計算 (サイズと位置) ---
        Matrix4x4 drawWorldMatrix = math.MakeIdentity4x4();

        // ★コライダーがある場合は、その形状データ(Size/Center/Rotation)に合わせて枠を変形させる
        if (type != ColliderType::kNone) {

            // コライダー設定を直接取得 (ImGuiでの変更を即座に反映させるため)
            Object3d::ColliderConfig config = obj->GetColliderConfig();

            if (type == ColliderType::kOBB) {
                // -------------------------------------------------
                // OBB (回転ありボックス) の計算
                // -------------------------------------------------

                // 1. スケール行列 (Sizeは半サイズなので2倍)
                Matrix4x4 matScale = math.MakeScaleMatrix(config.size * 2.0f);

                // 2. 回転行列 (config.rotation を使用)
                // ※回転順序はエンジンの仕様によりますが、一般的に Z*X*Y や Y*X*Z など
                Matrix4x4 matRotX = math.MakeRotateXMatrix(config.rotation.x);
                Matrix4x4 matRotY = math.MakeRotateYMatrix(config.rotation.y);
                Matrix4x4 matRotZ = math.MakeRotateZMatrix(config.rotation.z);
                // ここでは Z -> X -> Y の順で合成する例
                Matrix4x4 matRot = math.Multiply(matRotZ, math.Multiply(matRotX, matRotY));

                // 3. 平行移動行列 (Centerオフセット)
                Matrix4x4 matTrans = math.MakeTranslateMatrix(config.center);

                // 4. コライダーのローカル行列合成 (Scale * Rotate * Translate)
                Matrix4x4 matColliderLocal = math.Multiply(matScale, math.Multiply(matRot, matTrans));

                // 5. オブジェクトのワールド行列と合成
                drawWorldMatrix = math.Multiply(matColliderLocal, obj->GetWorldMatrix());

            } else if (type == ColliderType::kAABB) {
                // -------------------------------------------------
                // AABB (軸平行ボックス) の計算
                // -------------------------------------------------
                // AABBは回転しないため、サイズとオフセットのみ
                // (GetAABB()を使うとワールド座標計算済みが返るため、ここではローカル設定から計算してワールドと掛ける手法で統一)

                Matrix4x4 matScale = math.MakeScaleMatrix(config.size * 2.0f);
                Matrix4x4 matTrans = math.MakeTranslateMatrix(config.center);
                Matrix4x4 matColliderLocal = math.Multiply(matScale, matTrans);

                // AABBの場合、親の「回転」の影響を受けるとOBBになってしまうため、
                // 本来は親の座標だけを取り出して再計算するが、簡易表示として親行列を使う
                drawWorldMatrix = math.Multiply(matColliderLocal, obj->GetWorldMatrix());

            } else if (type == ColliderType::kSphere) {
                // -------------------------------------------------
                // Sphere (球) の計算
                // -------------------------------------------------
                // 球体を表すCubeワイヤーフレーム
                float radius = config.size.x; // Sphereはxを半径とする

                Matrix4x4 matScale = math.MakeScaleMatrix({ radius * 2.0f, radius * 2.0f, radius * 2.0f });
                Matrix4x4 matTrans = math.MakeTranslateMatrix(config.center);
                Matrix4x4 matColliderLocal = math.Multiply(matScale, matTrans);

                drawWorldMatrix = math.Multiply(matColliderLocal, obj->GetWorldMatrix());
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
    // 2. 弾のコライダー描画
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
            Matrix4x4 drawWorldMatrix = math.MakeIdentity4x4();

            // 弾の場合は物理挙動の結果(GetOBB)をそのまま信じて描画する
            if (type == ColliderType::kOBB) {
                OBB obb = bullet->GetOBB();
                Matrix4x4 matScale = math.MakeScaleMatrix(obb.size * 2.0f);

                // OBBの軸から回転行列を復元
                Matrix4x4 matRot = math.MakeIdentity4x4();
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




void DebugEditor::DrawImGui() {
#ifdef USE_IMGUI
    // 1. プロジェクトウィンドウ (Asset Browserなど)
    DrawProjectWindow();

    if (sceneManager_ == nullptr) return;

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (currentScene == nullptr) {
        ImGui::Begin("Inspector");
        ImGui::Text("アクティブなシーンがありません");
        ImGui::End();
        return;
    }

    // JSONパスの構築 (UI表示用)
    std::string currentJsonPath = "Resources/json/3Dobject/" + std::string(currentSceneFilename_);

    // ==========================================================================================
    // Inspector Window (シーン管理 & 選択中オブジェクトの編集)
    // ==========================================================================================
    ImGui::Begin("Inspector"); // ウィンドウ名は設定ファイル(ini)に関わるので英語推奨

    // ---------------------------------------------------------
    // 1. ファイル管理エリア (File Manager)
    // ---------------------------------------------------------
    if (ImGui::CollapsingHeader("シーンファイル管理 (Scene File)", ImGuiTreeNodeFlags_DefaultOpen)) {

        std::string directoryPath = "Resources/json/3Dobject/";
        if (!fs::exists(directoryPath)) {
            fs::create_directories(directoryPath);
        }

        // --- A. ファイル一覧コンボボックス ---
        if (ImGui::BeginCombo("既存ファイル", currentSceneFilename_)) {
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
        ImGui::InputText("保存名 (.json)", currentSceneFilename_, sizeof(currentSceneFilename_));

        // --- C. 保存ボタン (Save Scene) ---
        if (ImGui::Button("シーン保存 (All)")) {
            SaveScene();
        }
        ImGui::TextDisabled("保存先: %s", currentJsonPath.c_str());
    }
    ImGui::Separator();

    // ---------------------------------------------------------
    // 2. オブジェクト詳細 (Inspector)
    // ---------------------------------------------------------
    if (selectedObject_ == nullptr) {
        ImGui::Text("オブジェクトが選択されていません");
        ImGui::Text("Hierarchyから選択してください");
        ImGui::Separator();
        ImGui::Checkbox("コライダー枠を描画", &drawColliders_);
    } else {
        // --- 名前表示 ---
        char nameBuffer[256];
        std::string currentName = selectedObject_->GetName();
        if (currentName.empty()) currentName = "NoName";
        strcpy_s(nameBuffer, currentName.c_str());

        if (ImGui::InputText("名前", nameBuffer, sizeof(nameBuffer))) {
            selectedObject_->SetName(std::string(nameBuffer));
        }

        ImGui::Spacing();

        if (ImGui::Button("複製 (Duplicate)")) {
            DuplicateSelected();
        }

        ImGui::SameLine();

        if (ImGui::Button("単体保存 (JSON更新)")) {
            SaveSingleObject();
        }
        ImGui::Spacing();

        // --- クラス名表示 ---
        ImGui::TextDisabled("クラス: %s", selectedObject_->GetClassName().c_str());

        // --- 親の名前表示 ---
        if (selectedObject_->GetParent()) {
            ImGui::TextDisabled("親: %s", selectedObject_->GetParent()->GetName().c_str());
            if (ImGui::Button("親を解除 (Unparent)")) {
                selectedObject_->SetParent(nullptr);
            }
        } else {
            ImGui::TextDisabled("親: なし");
        }

        // --- Model Asset (InvisibleBoxでない場合のみ表示) ---
        if (selectedObject_->GetClassName() != "InvisibleBox") {
            ImGui::Separator();
            ImGui::Text("モデルアセット: %s", selectedObject_->GetModelName().c_str());
            // ドロップエリアを分かりやすく
            ImGui::Button(" [ ここにモデルをドロップして変更 ] ", ImVec2(-1, 30));

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

        // --- 可視性設定 ---
        ImGui::Separator();
        bool isVisible = selectedObject_->GetIsVisible();
        if (ImGui::Checkbox("表示 (ゲーム内)", &isVisible)) {
            selectedObject_->SetIsVisible(isVisible);
        }

        // --- Transform編集 (日本語だと直感的) ---
        ImGui::Separator();
        ImGui::Text("トランスフォーム (Transform)");
        // ★修正: Object3d::Transform -> Transform
        Transform* transform = selectedObject_->GetTransform();
        bool isTransformChanged = false;

        if (ImGui::DragFloat3("座標 (Pos)", &transform->translate.x, 0.1f)) isTransformChanged = true;

        Vector3 rotDeg = { ToDegrees(transform->rotate.x), ToDegrees(transform->rotate.y), ToDegrees(transform->rotate.z) };
        if (ImGui::DragFloat3("回転 (Rot)", &rotDeg.x, 1.0f, -360.0f, 360.0f)) {
            transform->rotate = { ToRadians(rotDeg.x), ToRadians(rotDeg.y), ToRadians(rotDeg.z) };
            isTransformChanged = true;
        }
        if (ImGui::DragFloat3("スケール (Scale)", &transform->scale.x, 0.05f)) isTransformChanged = true;

        if (isTransformChanged) {
            UpdateObjectInSceneJSON(selectedObject_, currentJsonPath);
        }

        // --- コライダー設定 ---
        ImGui::Separator();
        if (ImGui::CollapsingHeader("コリジョン設定 (Collision)", ImGuiTreeNodeFlags_DefaultOpen)) {
            Object3d::ColliderConfig colConfig = selectedObject_->GetColliderConfig();
            bool isColChanged = false;

            const char* typeNames[] = { "なし (None)", "球 (Sphere)", "箱 (AABB)", "回転箱 (OBB)" };
            int currentTypeIndex = (int)colConfig.type;
            if (ImGui::Combo("形状タイプ", &currentTypeIndex, typeNames, IM_ARRAYSIZE(typeNames))) {
                colConfig.type = (ColliderType)currentTypeIndex;
                if (colConfig.type == ColliderType::kOBB && colConfig.size.x == 0.0f) {
                    colConfig.size = { 1.0f, 1.0f, 1.0f };
                }
                isColChanged = true;
            }

            if (colConfig.type != ColliderType::kNone) {
                if (ImGui::DragFloat3("中心オフセット", &colConfig.center.x, 0.05f)) isColChanged = true;

                if (colConfig.type == ColliderType::kSphere) {
                    if (ImGui::DragFloat("半径 (Radius)", &colConfig.size.x, 0.05f, 0.0f, 100.0f)) {
                        colConfig.size.y = colConfig.size.z = colConfig.size.x;
                        isColChanged = true;
                    }
                } else {
                    if (ImGui::DragFloat3("サイズ (Size)", &colConfig.size.x, 0.05f, 0.0f, 100.0f)) isColChanged = true;
                }
                if (colConfig.type == ColliderType::kOBB) {
                    // Radian -> Degree に変換して表示
                    Vector3 rotDeg = { ToDegrees(colConfig.rotation.x), ToDegrees(colConfig.rotation.y), ToDegrees(colConfig.rotation.z) };

                    if (ImGui::DragFloat3("回転 (Rotation)", &rotDeg.x, 1.0f, -360.0f, 360.0f)) {
                        // Degree -> Radian に戻して保存
                        colConfig.rotation = { ToRadians(rotDeg.x), ToRadians(rotDeg.y), ToRadians(rotDeg.z) };
                        isColChanged = true;
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::CollapsingHeader("グラフィックス (Material)", ImGuiTreeNodeFlags_DefaultOpen)) {
                bool isGraphicsChanged = false;

                // 1. マテリアルタイプ (通常 / ガラス)
                const char* matTypes[] = { "通常 (Standard)", "ガラス (Glass)" };
                int currentMatType = selectedObject_->GetMaterialType();

                // 安全のため範囲チェック
                if (currentMatType < 0) currentMatType = 0;
                if (currentMatType > 1) currentMatType = 0;

                if (ImGui::Combo("質感 (Material Type)", &currentMatType, matTypes, IM_ARRAYSIZE(matTypes))) {
                    selectedObject_->SetMaterialType(currentMatType);
                    isGraphicsChanged = true;
                }

                // 2. ブレンドモード
                const char* blendModes[] = { "なし (None)", "通常 (Normal)", "加算 (Add)", "減算 (Subtract)", "乗算 (Multiply)", "スクリーン (Screen)" };
                int currentBlend = static_cast<int>(selectedObject_->GetBlendMode());

                if (ImGui::Combo("合成 (Blend Mode)", &currentBlend, blendModes, IM_ARRAYSIZE(blendModes))) {
                    selectedObject_->SetBlendMode(static_cast<BlendMode>(currentBlend));
                    isGraphicsChanged = true;
                }

                // 3. 色 (Color)
                Vector4 color = selectedObject_->GetColor();
                if (ImGui::ColorEdit4("色 (Color)", &color.x)) {
                    selectedObject_->SetColor(color);
                    isGraphicsChanged = true;
                }
            }
            ImGui::Separator();
            if (ImGui::CollapsingHeader("パーティクル")) {

                // マネージャーから全パーティクル情報を取得
                const auto& paramsMap = ParticleManager::GetInstance()->GetParamsMap();

                // リスト作成用の一時配列
                std::vector<const char*> itemNames;
                int currentItemIndex = 0;
                int index = 0;

                // "None" (なし) を選択肢の最初に追加
                itemNames.push_back("None");

                // 現在設定されている名前を取得
                std::string currentParticleName = selectedObject_->GetParticleName();
                if (currentParticleName.empty()) {
                    currentItemIndex = 0; // Noneが選択されている
                }

                // Mapから名前を取り出してリストに追加
                for (const auto& [name, param] : paramsMap) {
                    itemNames.push_back(name.c_str());

                    // 今の名前と一致したらインデックスを記録
                    if (name == currentParticleName) {
                        currentItemIndex = index + 1; // +1はNoneの分
                    }
                    index++;
                }

                // ★コンボボックス（リスト）表示
                if (ImGui::Combo("Effect Name", &currentItemIndex, itemNames.data(), (int)itemNames.size())) {
                    if (currentItemIndex == 0) {
                        selectedObject_->SetParticleName(""); // Noneを選択
                    } else {
                        // 選択された名前をセット
                        selectedObject_->SetParticleName(itemNames[currentItemIndex]);
                    }
                }

                // 補足情報: 保存されているか確認
                if (!currentParticleName.empty() && paramsMap.find(currentParticleName) == paramsMap.end()) {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Warning: JSON not found!");
                }
            }
        
            if (isColChanged) {
                selectedObject_->SetColliderConfig(colConfig);
                UpdateObjectInSceneJSON(selectedObject_, currentJsonPath);
            }

            // 属性設定
            ImGui::Separator();
            uint32_t currentAttr = selectedObject_->GetCollisionAttribute();
            DrawAttributeSelector("自分の属性 (Attribute)", &currentAttr);
            if (currentAttr != selectedObject_->GetCollisionAttribute()) {
                selectedObject_->SetCollisionAttribute(currentAttr);
            }

            uint32_t currentMask = selectedObject_->GetCollisionMask();
            DrawAttributeSelector("衝突対象 (Mask)", &currentMask);
            if (currentMask != selectedObject_->GetCollisionMask()) {
                selectedObject_->SetCollisionMask(currentMask);
            }
        }
        // --- Gimmick (ID設定) ---
        ImGui::Separator();
        if (ImGui::CollapsingHeader("ギミック設定 (Link IDs)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("イベント連携ID:");

            // 1. Target ID (送信先: 誰を動かすか？)
            int tID = selectedObject_->GetTargetID();
            if (ImGui::InputInt("送信先ID (Target)", &tID)) {
                selectedObject_->SetTargetID(tID);
            }
            // マウスを乗せた時の説明文
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("トリガーを作動させたい相手のIDを指定してください");

            // 2. Event ID (受信ID: 私は誰か？)
            int eID = selectedObject_->GetEventID();
            if (ImGui::InputInt("自分ID (Event)", &eID)) {
                selectedObject_->SetEventID(eID);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("ギミック等から起動されるための、自分のIDを指定してください");
        }
        ImGui::Separator();
        std::string currentPreview = selectedObject_->animName_.empty() ? "(なし)" : selectedObject_->animName_;

        if (ImGui::BeginCombo("アニメーション", currentPreview.c_str())) {

            // 1. 「設定なし」を選べるようにする
            bool isNoneSelected = selectedObject_->animName_.empty();
            if (ImGui::Selectable("(なし)", isNoneSelected)) {
                selectedObject_->animName_ = "";
                if (selectedObject_->recorder_) {
                    selectedObject_->recorder_->Stop();
                }
            }
            if (isNoneSelected) ImGui::SetItemDefaultFocus();

            // 2. ディレクトリを走査してリストアップ
            std::string dirPath = "Resources/json/animation/";

            if (fs::exists(dirPath) && fs::is_directory(dirPath)) {
                for (const auto& entry : fs::directory_iterator(dirPath)) {
                    if (entry.path().extension() == ".json") {
                        std::string fileName = entry.path().stem().string();

                        bool isSelected = (selectedObject_->animName_ == fileName);

                        if (ImGui::Selectable(fileName.c_str(), isSelected)) {
                            selectedObject_->animName_ = fileName;

                            if (selectedObject_->recorder_) {
                                // 選択中のオブジェクトが演出用カメラ(CinematicCamera)なら true にする
                                bool isCinematic = (selectedObject_->GetClassName() == "CinematicCamera");

                                selectedObject_->recorder_->Play(
                                    selectedObject_->animName_,
                                    selectedObject_->isAnimLoop_,
                                    selectedObject_->isAnimRelative_,
                                    isCinematic 
                                );
                            }
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                }
            }
            ImGui::EndCombo();
        }


        // ループと相対座標の設定
        // ##Anim をつけることでID重複を防ぎつつラベルを表示
        if (ImGui::Checkbox("ループ再生##Anim", &selectedObject_->isAnimLoop_)) {
            if (selectedObject_->recorder_ && !selectedObject_->animName_.empty()) {
                // クラス名が "CinematicCamera" なら演出モード（カメラ乗っ取り）を有効にする
                bool isCinematic = (selectedObject_->GetClassName() == "CinematicCamera");

                selectedObject_->recorder_->Play(
                    selectedObject_->animName_,
                    selectedObject_->isAnimLoop_,
                    selectedObject_->isAnimRelative_,
                    isCinematic 
                );
            }
        }
        if (ImGui::Checkbox("相対座標モード##Anim", &selectedObject_->isAnimRelative_)) {
            if (selectedObject_->recorder_ && !selectedObject_->animName_.empty()) {
                // クラス名が "CinematicCamera" なら演出モードフラグを立てる
                bool isCinematic = (selectedObject_->GetClassName() == "CinematicCamera");

                selectedObject_->recorder_->Play(
                    selectedObject_->animName_,
                    selectedObject_->isAnimLoop_,
                    selectedObject_->isAnimRelative_,
                    isCinematic 
                );
            }
        }


        // テスト再生ボタン
        if (ImGui::Button("テスト再生##Anim")) {
            if (selectedObject_->recorder_) {
                // クラス名が "CinematicCamera" なら演出モード（カメラ乗っ取り）を有効にする
                bool isCinematic = (selectedObject_->GetClassName() == "CinematicCamera");

                selectedObject_->recorder_->Play(
                    selectedObject_->animName_,
                    selectedObject_->isAnimLoop_,
                    selectedObject_->isAnimRelative_,
                    isCinematic 
                );
            }
        }

        // --- Game Data (Stats) ---
        ImGui::Separator();
        if (ImGui::CollapsingHeader("ゲームデータ (Stats)", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Event Type
            EventType currentType = selectedObject_->GetEventType();
            int currentItemIndex = static_cast<int>(currentType);
            const char* eventNames[] = { "なし", "ダメージ", "ワープ","中間ポイント","ゴール","ステージセレクト" };
            if (ImGui::Combo("イベント種類", &currentItemIndex, eventNames, IM_ARRAYSIZE(eventNames))) {
                selectedObject_->SetEventType(static_cast<EventType>(currentItemIndex));
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "--- Object Type Settings ---");

            // ==========================================================
            // 1. クラス名 (ClassName) の変更リスト
            // ==========================================================
            const char* classItems[] = {
                "Model",        // 通常
                "Spawner",      // ★敵生成
                "Player",       // プレイヤー
                "Enemy",        // 敵 (直接配置)
                "InvisibleBox", // 透明壁・トリガー
                "Block"         // 特殊ブロック
            };

            // 現在のクラス名がリストの何番目かを探す
            std::string currentClass = selectedObject_->GetClassName();
            int currentClassIndex = 0; // デフォルト0番目

            for (int i = 0; i < IM_ARRAYSIZE(classItems); i++) {
                if (currentClass == classItems[i]) {
                    currentClassIndex = i;
                    break;
                }
            }

            // コンボボックス表示
            if (ImGui::Combo("Class Type", &currentClassIndex, classItems, IM_ARRAYSIZE(classItems))) {
                // 変更を適用
                selectedObject_->SetClassName(classItems[currentClassIndex]);

                // Spawnerに変更された場合の親切設計
                if (std::string(classItems[currentClassIndex]) == "Spawner") {
                    // 名前が汎用的なら分かりやすく変える
                    if (selectedObject_->GetName().find("Object") != std::string::npos) {
                        selectedObject_->SetName("Spawner_New");
                    }
                    // パラメータ領域確保
                    if (!selectedObject_->param_.has_value()) {
                        selectedObject_->param_.emplace();
                    }
                }
            }

            // ==========================================================
            // 2. クラス別の設定関数を呼ぶ
            // ==========================================================
            if (selectedObject_->GetClassName() == "Spawner") {
                DrawSpawnerSettings();
            }



            ImGui::Spacing();
            if (selectedObject_->GetClassName() == "Enemy") {
                ImGui::Indent();
                DrawEnemyTypeSelector();
                ImGui::Unindent();
            }
            // Stats Parameters
            if (!selectedObject_->param_.has_value()) {
                if (ImGui::Button("ステータスを追加", ImVec2(-1, 0))) {
                    selectedObject_->param_.emplace();
                }
            } else {
                auto& p = selectedObject_->param_.value();
                ImGui::Text("エンティティ・ステータス:");
                ImGui::Indent();
                ImGui::DragFloat("HP (体力)", &p.hp, 1.0f, 0.0f, 9999.0f);
                ImGui::DragFloat("Max HP", &p.maxHp, 1.0f, 1.0f, 9999.0f);
                ImGui::DragFloat("速度 (Speed)", &p.speed, 0.1f, 0.0f, 100.0f);
                ImGui::DragFloat("重力 (Gravity)", &p.gravity, 0.01f, -10.0f, 10.0f);
                ImGui::DragFloat("ジャンプ力", &p.jumpPower, 0.1f, 0.0f, 100.0f);
                ImGui::Unindent();

                ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.6f, 0.6f));
                if (ImGui::Button("ステータスを削除", ImVec2(-1, 0))) {
                    selectedObject_->param_ = std::nullopt;
                }
                ImGui::PopStyleColor();
            }
        }

        ImGui::Separator();

        if (ImGui::Button("オブジェクト削除", ImVec2(-1, 0))) {
            DeleteSelected();
        }

        // --- Gizmo 操作切替 ---
        ImGui::Separator();
        ImGui::Text("ギズモ操作モード:");
        static ImGuizmo::OPERATION currentOperation = ImGuizmo::TRANSLATE;
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) {
            if (ImGui::IsKeyPressed(ImGuiKey_T)) currentOperation = ImGuizmo::TRANSLATE;
            if (ImGui::IsKeyPressed(ImGuiKey_R)) currentOperation = ImGuizmo::ROTATE;
            if (ImGui::IsKeyPressed(ImGuiKey_S)) currentOperation = ImGuizmo::SCALE;
        }
        // ここは直感的にするためカタカナか漢字で
        if (ImGui::RadioButton("移動", currentOperation == ImGuizmo::TRANSLATE)) currentOperation = ImGuizmo::TRANSLATE; ImGui::SameLine();
        if (ImGui::RadioButton("回転", currentOperation == ImGuizmo::ROTATE)) currentOperation = ImGuizmo::ROTATE; ImGui::SameLine();
        if (ImGui::RadioButton("拡大縮小", currentOperation == ImGuizmo::SCALE)) currentOperation = ImGuizmo::SCALE;
    }
    ImGui::End(); // End Inspector


    // ==========================================================================================
    // Hierarchy Window (階層構造)
    // ==========================================================================================
    ImGui::Begin("Hierarchy"); // ウィンドウ名は英語推奨

    // 1. 検索バー
    ImGui::Text("検索:");
    ImGui::SameLine();
    ImGui::InputText("##Search", searchFilter_, sizeof(searchFilter_));

    ImGui::Separator();

    std::string filterStr = searchFilter_;
    std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

    if (!filterStr.empty()) {
        // --- 検索モード ---
        ImGui::TextColored(ImVec4(0, 1, 1, 1), "検索結果:");

        auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
        for (auto& obj : objects) {
            std::string name = obj->GetName();
            if (name.empty()) continue;

            std::string nameLower = name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

            if (nameLower.find(filterStr) != std::string::npos) {
                bool isSelected = (selectedObject_ == obj.get());
                ImGui::PushID(obj.get());
                if (ImGui::Selectable(name.c_str(), isSelected)) {
                    selectedObject_ = obj.get();
                }
                ImGui::PopID();
            }
        }
    } else {

        // --- 通常モード ---
        // ========================================================
        // 1. ドロップを受け付ける UIパーツ (ボタン) を描画
        // ========================================================
        ImGui::Button("[ ここにモデルをドロップして生成 ]", ImVec2(-1, 30));

        if (ImGui::BeginDragDropTarget()) {

            // ----------------------------------------------------
            // パターンA: モデルアセット ("MODEL_ASSET")
            // ----------------------------------------------------
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
                const char* modelName = (const char*)payload->Data;

                // モデル読み込み
                ModelManager::GetInstance()->LoadModel(modelName);

                // シーンから共通リソース取得
                Object3dCommon* common = sceneManager_->GetCurrentScene()->GetObject3dCommon();
                if (common) {
                    // プレビューオブジェクト作成
                    auto newObj = std::make_unique<Object3d>();
                    newObj->Initialize(common);
                    newObj->SetModel(modelName);
                    newObj->SetClassName("Model");
                    newObj->SetName("Preview_" + std::string(modelName));

                    // 行列更新
                    newObj->UpdateLocalMatrix();
                    newObj->UpdateWorldMatrix();

                    // プレビューとして保持
                    previewObject_ = std::move(newObj);

                    DebugConsole::GetInstance()->AddLog("Placement Mode: " + std::string(modelName));
                }
            }

            // ----------------------------------------------------
            // パターンB: プリセットアセット ("PRESET_ASSET")
            // ----------------------------------------------------
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PRESET_ASSET")) {
                // 1. プリセット名を受け取る
                const char* presetName = (const char*)payload->Data;

                // 2. PresetManagerからJSONデータを取得
                const auto& presets = PresetManager::GetInstance()->GetPresets();
                if (presets.count(presetName) > 0) {
                    const json& data = presets.at(presetName);

                    // 3. モデルのロード (JSON内の "modelName" を確認)
                    std::string modelName = "cube.obj"; // デフォルト
                    if (data.contains("modelName")) {
                        modelName = data["modelName"];
                        ModelManager::GetInstance()->LoadModel(modelName);
                    }

                    // 4. プレビューオブジェクトを作成
                    Object3dCommon* common = sceneManager_->GetCurrentScene()->GetObject3dCommon();
                    if (common) {
                        auto newObj = std::make_unique<Object3d>();
                        newObj->Initialize(common);

                        // ここでJSONデータを流し込む
                        newObj->ImportFromJson(data);

                        // モデルと名前の再設定
                        newObj->SetModel(modelName);
                        newObj->SetName("Preview_" + std::string(presetName));

                        // 行列更新
                        newObj->UpdateLocalMatrix();
                        newObj->UpdateWorldMatrix();

                        // プレビューとしてセット
                        previewObject_ = std::move(newObj);

                        DebugConsole::GetInstance()->AddLog("Placement Mode (Preset): " + std::string(presetName));
                    }
                }
            }

            // ----------------------------------------------------
        // パターンC: 演出用カメラ ("CINEMATIC_CAMERA_ASSET")
        // ----------------------------------------------------
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CINEMATIC_CAMERA_ASSET")) {
                // シーンのCommonを取得
                BaseScene* currentScene = sceneManager_->GetCurrentScene();
                Object3dCommon* common = currentScene ? currentScene->GetObject3dCommon() : nullptr;

                if (common) {
                    // 1. ダミーオブジェクト(Object3d)を作成
                    auto newObj = std::make_unique<Object3d>();
                    newObj->Initialize(common);

                    // 2. モデルを設定
                    newObj->SetModel("block"); 

                    // 3. 名前をつける 
                    newObj->SetName("Camera_Cinematic");

                    // 4. クラス名を設定 (タグのようなもの)
                    newObj->SetClassName("CinematicCamera");

           

                    // 位置を少し手前に
                    newObj->SetTranslate({ 0.0f, 5.0f, -10.0f });
                    newObj->UpdateLocalMatrix();
                    newObj->UpdateWorldMatrix();

                    // シーンに追加
                    currentScene->AddObject(std::move(newObj));

                    DebugConsole::GetInstance()->AddLog("Cinematic Camera Added to Scene.");
                }
            }

            // ========================================================
            // 3. ドロップ受け付け終了 
            // ========================================================
            ImGui::EndDragDropTarget();
        }

        ImGui::Separator();

        // ========================================================
        // ツリー表示 (階層構造)
        // ========================================================
        auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
        for (auto& obj : objects) {
            // 親がいないルートオブジェクトだけを描画開始点にする
            if (obj->GetParent() == nullptr) {
                DrawHierarchyNode(obj.get());
            }
        }
    }



    // ==========================================================================================
    // Spawner Window (生成メニュー)
    // ==========================================================================================


    // ★ Invisible Box 生成ボタン
    ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.8f, 0.6f, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.8f, 0.7f, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0.8f, 0.8f, 0.8f));

    if (ImGui::Button("透明ボックス生成 (トリガー用)", ImVec2(-1, 40))) {
        Object3dCommon* common = currentScene->GetObject3dCommon();
        if (common) {
            auto newObj = std::make_unique<Object3d>();
            newObj->Initialize(common);

            newObj->SetModel(nullptr);
            newObj->SetIsVisible(false);
            newObj->SetClassName("InvisibleBox");
            newObj->SetName("Trigger_Box");

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kAABB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            newObj->SetColliderConfig(colConfig);
            newObj->SetCollisionAttribute(CollisionAttribute::kTrigger);

            newObj->SetTranslate({ 0, 2.0f, 0 });

            selectedObject_ = newObj.get();
            currentScene->AddObject(std::move(newObj));
            DebugConsole::GetInstance()->AddLog("Spawned Invisible Box");
        }
    }
    if (ImGui::Button("透明ボックス生成 (当たり判定用)", ImVec2(-1, 40))) {
        Object3dCommon* common = currentScene->GetObject3dCommon();
        if (common) {
            auto newObj = std::make_unique<Object3d>();
            newObj->Initialize(common);

            newObj->SetModel(nullptr);
            newObj->SetIsVisible(false);
            newObj->SetClassName("InvisibleBox");
            newObj->SetName("collision_Box");

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kAABB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            newObj->SetColliderConfig(colConfig);
            newObj->SetCollisionAttribute(CollisionAttribute::kGround);

            newObj->SetTranslate({ 0, 2.0f, 0 });

            selectedObject_ = newObj.get();
            currentScene->AddObject(std::move(newObj));
            DebugConsole::GetInstance()->AddLog("Spawned Invisible Box");
        }
    }
    // ... (透明ボックス生成ボタンの続き) ...

    // ★追加: 演出用カメラ生成ボタン
    if (ImGui::Button("演出用カメラ生成 (Cinematic)", ImVec2(-1, 40))) {
        Object3dCommon* common = currentScene->GetObject3dCommon();
        if (common) {
            auto newObj = std::make_unique<Object3d>();
            newObj->Initialize(common);

            newObj->SetModel("block");

            // 色を変えられるならカメラっぽい色に (例: 紫)
             newObj->SetColor({0.8f, 0.2f, 0.8f, 1.0f}); 

            newObj->SetIsVisible(true); // エディタでは見えていてほしい
            newObj->SetClassName("CinematicCamera");
            newObj->SetName("Cinematic_Camera_01"); // わかりやすい名前

            // 選択しやすいようにコライダーもつけておく
            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kAABB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            newObj->SetColliderConfig(colConfig);

            // ちょっと高い位置にスポーン
            newObj->SetTranslate({ 0, 5.0f, -10.0f });
            newObj->UpdateWorldMatrix();

            selectedObject_ = newObj.get(); // 生成したらすぐ選択状態にする
            currentScene->AddObject(std::move(newObj));
            DebugConsole::GetInstance()->AddLog("Spawned Cinematic Camera Dummy");
        }
    }

    ImGui::PopStyleColor(3);

    // 親解除用エリア
    ImGui::Dummy(ImVec2(0, 50));
    ImGui::TextDisabled("(ここにドロップして親解除)");
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_OBJ")) {
            Object3d* sourceObj = *(Object3d**)payload->Data;
            sourceObj->SetParent(nullptr);
            DebugConsole::GetInstance()->AddLog("Unparented: " + sourceObj->GetName());
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::End(); // End Hierarchy

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
    // ★修正: Object3d::Transform -> Transform
    Transform* tf = object->GetTransform();
    currentData["position"] = { tf->translate.x, tf->translate.y, tf->translate.z };
    currentData["rotation"] = { tf->rotate.x, tf->rotate.y, tf->rotate.z };
    currentData["scale"] = { tf->scale.x, tf->scale.y, tf->scale.z };

    // --- Collider Config ---
    const Object3d::ColliderConfig& config = object->GetColliderConfig();
    currentData["collider"]["type"] = static_cast<int>(config.type);
    currentData["collider"]["center"] = { config.center.x, config.center.y, config.center.z };
    currentData["collider"]["size"] = { config.size.x, config.size.y, config.size.z };
    currentData["collider"]["rotation"] = { config.rotation.x, config.rotation.y, config.rotation.z };

    // --- Attributes ---
    currentData["collisionAttribute"] = object->GetCollisionAttribute();
    currentData["collisionMask"] = object->GetCollisionMask();

    // --- Event ID ---
    currentData["eventID"] = static_cast<int>(object->GetEventType());
    currentData["targetID"] = object->GetTargetID(); // 送信ID
    currentData["myEventID"] = object->GetEventID(); // 受信ID

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
    Vector4 color = object->GetColor();
    currentData["color"] = { color.x, color.y, color.z, color.w };
    // アニメーション設定の保存
    currentData["animName"] = object->animName_;
    currentData["isAnimLoop"] = object->isAnimLoop_;

    currentData["blendMode"] = static_cast<int>(object->GetBlendMode());
    currentData["materialType"] = object->GetMaterialType();

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

    //  見つからなかった場合（新規スポーンなど）は、配列の末尾に追加
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


#ifdef USE_IMGUI
void DebugEditor::DrawProjectWindow() {
    // ---------------------------------------------------------
    // ウィンドウ開始
    // ---------------------------------------------------------
    ImGui::Begin("Project (Assets)");

    // レイアウト計算用の変数（共通）
    float windowVisibleX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    float itemSpacing = ImGui::GetStyle().ItemSpacing.x;

    // =================================================================================
    // 1. モデルファイル一覧 (Raw Models)
    //    Resources/3DModel フォルダ内の obj/gltf/glb をスキャンして表示
    // =================================================================================
    if (ImGui::CollapsingHeader("Models (Source)", ImGuiTreeNodeFlags_DefaultOpen)) {

        std::string baseDirectory = "Resources/3DModel";

        if (fs::exists(baseDirectory) && fs::is_directory(baseDirectory)) {

            // ガイドテキスト
            ImGui::TextDisabled("Drag & Drop to Scene to Place");
            ImGui::Separator();

            for (const auto& entry : fs::directory_iterator(baseDirectory)) {
                std::string displayModelName = ""; // ボタン表示名
                std::string payloadName = "";      // ロード用パス/名前

                // ---------------------------------------------------
                // ディレクトリの場合 (objファイルが入っているフォルダ等を想定)
                // ---------------------------------------------------
                if (entry.is_directory()) {
                    std::string folderName = entry.path().filename().string();

                    // フォルダ内を検索してモデルファイルを探す
                    for (const auto& subEntry : fs::directory_iterator(entry.path())) {
                        std::string subExt = subEntry.path().extension().string();
                        // 小文字変換して比較
                        std::transform(subExt.begin(), subExt.end(), subExt.begin(), ::tolower);

                        if (subExt == ".obj") {
                            displayModelName = folderName;
                            payloadName = folderName;
                            break;
                        } else if (subExt == ".gltf" || subExt == ".glb") {
                            displayModelName = subEntry.path().filename().string();
                            payloadName = subEntry.path().filename().string();
                            break;
                        }
                    }
                }

                // ---------------------------------------------------
                // ボタンの描画とドラッグ処理
                // ---------------------------------------------------
                if (!displayModelName.empty()) {
                    ImGui::PushID(displayModelName.c_str());

                    // ボタン描画 (幅100, 高さ0=自動)
                    ImGui::Button(displayModelName.c_str(), ImVec2(100, 0));

                    // --- ドラッグ&ドロップ処理 (Source) ---
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                        // タグ: MODEL_ASSET
                        ImGui::SetDragDropPayload("MODEL_ASSET", payloadName.c_str(), payloadName.size() + 1);

                        // ドラッグ中のプレビュー
                        ImGui::Text("Model: %s", displayModelName.c_str());
                        ImGui::EndDragDropSource();
                    }

                    ImGui::PopID();

                    // --- グリッドレイアウト調整 (横並び) ---
                    float lastButtonX = ImGui::GetItemRectMax().x;
                    float nextButtonX = lastButtonX + itemSpacing + 100.0f;
                    // 次のボタンがウィンドウ端を超えないなら横に並べる
                    if (nextButtonX < windowVisibleX) {
                        ImGui::SameLine();
                    }
                }
            }
        } else {
            // ディレクトリが見つからない場合のエラー表示
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Directory Not Found: %s", baseDirectory.c_str());
        }
    }

    // =================================================================================
    // 2. プリセット一覧 (Presets)
    //    JSONに保存された設定済みオブジェクト。ここから新規保存も可能にする。
    // =================================================================================
    if (ImGui::CollapsingHeader("Presets (Configured)", ImGuiTreeNodeFlags_DefaultOpen)) {

        // -------------------------------------------------------
        // ★ 新規プリセット作成エリア (選択中のオブジェクトを保存)
        // -------------------------------------------------------
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "▼ Create New Preset");

        if (selectedObject_) {
            // 名前入力欄
            static char presetNameBuf[64] = "NewPreset";
            ImGui::PushItemWidth(150); // 入力欄の幅を少し制限
            ImGui::InputText("##PresetName", presetNameBuf, 64);
            ImGui::PopItemWidth();

            ImGui::SameLine();

            // 保存ボタン
            if (ImGui::Button("Save Selection")) {
                if (strlen(presetNameBuf) > 0) {
                    // PresetManagerを使って保存
                    PresetManager::GetInstance()->AddPresetFromObject(presetNameBuf, selectedObject_);
                    DebugConsole::GetInstance()->AddLog("Saved Preset: " + std::string(presetNameBuf));
                }
            }

            // マウスオーバー時のヘルプ
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Save the currently selected object's settings as a new preset.");
            }
        } else {
            // オブジェクト未選択時のメッセージ
            ImGui::TextDisabled("(Select an object in Scene to save)");
        }

        ImGui::Separator();
        ImGui::Spacing();

        // -------------------------------------------------------
        // ★ プリセット一覧表示
        // -------------------------------------------------------
        const auto& presets = PresetManager::GetInstance()->GetPresets();

        if (presets.empty()) {
            ImGui::TextDisabled("(No Presets Saved)");
        } else {
            for (const auto& [name, data] : presets) {
                ImGui::PushID(name.c_str());

                // プリセットを目立たせるために色を変更 (緑色系)
                ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.3f, 0.6f, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.3f, 0.7f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0.3f, 0.8f, 0.8f));

                // ボタン描画
                ImGui::Button(name.c_str(), ImVec2(100, 0));

                ImGui::PopStyleColor(3); // 色設定を戻す

                // --- ドラッグ&ドロップ処理 (Source) ---
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    // タグ: PRESET_ASSET (Modelとは区別する)
                    ImGui::SetDragDropPayload("PRESET_ASSET", name.c_str(), name.size() + 1);

                    ImGui::Text("Preset: %s", name.c_str());
                    ImGui::EndDragDropSource();
                }

                ImGui::PopID();

                // --- グリッドレイアウト調整 (横並び) ---
                float lastButtonX = ImGui::GetItemRectMax().x;
                float nextButtonX = lastButtonX + itemSpacing + 100.0f;
                if (nextButtonX < windowVisibleX) {
                    ImGui::SameLine();
                }
            }
        }
    }

    ImGui::End();
}


// 属性編集用ヘルパー関数
void DebugEditor::DrawAttributeSelector(const char* label, uint32_t* attribute) {
    if (ImGui::TreeNode(label)) {
        int flags = static_cast<int>(*attribute);

        ImGui::CheckboxFlags("プレイヤー (Player)", &flags, 1 << 0);
        ImGui::CheckboxFlags("敵 (Enemy)", &flags, 1 << 1);
        ImGui::CheckboxFlags("床・地形 (Ground)", &flags, 1 << 2);
        ImGui::CheckboxFlags("弾 (Bullet)", &flags, 1 << 3);
        ImGui::CheckboxFlags("トリガー (Trigger)", &flags, 1 << 4);

        // 変更を書き戻す
        *attribute = static_cast<uint32_t>(flags);

        ImGui::TreePop();
    }
}

#endif

// ========================================================================
// ショートカット / ボタン機能の実装
// ========================================================================

// シーン全体保存
void DebugEditor::SaveScene() {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    // パスの構築
    std::string path = "Resources/json/3Dobject/" + std::string(currentSceneFilename_);

    using json = nlohmann::json;
    json sceneData;
    sceneData["objects"] = json::array();

    auto& allObjects = sceneManager_->GetCurrentScene()->GetObjects();

    for (auto& obj : allObjects) {
        // 名前がないオブジェクトは保存しない
        if (obj->GetName().empty()) continue;

        json d;
        d["name"] = obj->GetName();

        // ---------------------------------------------------------
        // クラス名と敵タイプを正しく保存する
        // ---------------------------------------------------------

        // 1. クラス名の取得と保存
        // "Player", "InvisibleBox", "Model" などが入る
        std::string className = obj->GetClassName();
        if (className.empty()) {
            className = "Model"; // デフォルト
        }
        d["type"] = className;

        // 2. 敵の種類 ("Slime", "Robot" など) を保存
        // これがないとロード時に EnemyFactory が動かない
        d["enemyType"] = obj->GetEnemyType();

        // 3. モデル名の保存
        // InvisibleBox 以外であればモデル名を保存する
        if (className != "InvisibleBox") {
            d["modelName"] = obj->GetModelName();
        }

        // ---------------------------------------------------------
        // 以下、既存のパラメータ保存
        // ---------------------------------------------------------

        // 親子関係
        if (obj->GetParent()) {
            d["parentName"] = obj->GetParent()->GetName();
        } else {
            d["parentName"] = "";
        }

        // Transform
        // ★修正: Object3d::Transform -> Transform
        Transform* objTr = obj->GetTransform();
        d["position"] = { objTr->translate.x, objTr->translate.y, objTr->translate.z };
        d["rotation"] = { objTr->rotate.x, objTr->rotate.y, objTr->rotate.z };
        d["scale"] = { objTr->scale.x, objTr->scale.y, objTr->scale.z };

        // Collider
        Object3d::ColliderConfig c = obj->GetColliderConfig();
        json cData;
        cData["type"] = (int)c.type;
        cData["center"] = { c.center.x, c.center.y, c.center.z };
        cData["size"] = { c.size.x, c.size.y, c.size.z };
        cData["rotation"] = { c.rotation.x, c.rotation.y, c.rotation.z };
        d["collider"] = cData;

        // 衝突属性
        d["collisionAttribute"] = obj->GetCollisionAttribute();
        d["collisionMask"] = obj->GetCollisionMask();

        // イベント関連
        d["eventID"] = static_cast<int>(obj->GetEventType());
        d["targetID"] = obj->GetTargetID(); // 送信ID
        d["myEventID"] = obj->GetEventID();  // 受信ID

        // Stats (Param) - ゲームパラメータ
        if (obj->param_.has_value()) {
            auto& p = obj->param_.value();
            d["param"]["hp"] = p.hp;
            d["param"]["maxHp"] = p.maxHp;
            d["param"]["speed"] = p.speed;
            d["param"]["gravity"] = p.gravity;
            d["param"]["jumpPower"] = p.jumpPower;
            d["param"]["maxFallSpeed"] = p.maxFallSpeed;
            d["param"]["enemyType"] = p.enemyType; // 文字列
            d["param"]["interval"] = p.interval;   // float
            d["param"]["maxCount"] = p.maxCount;   // int
        }
        Vector4 color = obj->GetColor();
        // [R, G, B, A] の配列としてJSONに保存
        d["color"] = { color.x, color.y, color.z, color.w };
        // アニメーション設定
        d["animName"] = obj->animName_;
        d["isAnimLoop"] = obj->isAnimLoop_;
        d["isAnimRelative"] = obj->isAnimRelative_;
        d["blendMode"] = static_cast<int>(obj->GetBlendMode());
        d["materialType"] = obj->GetMaterialType();


        // 配列に追加
        sceneData["objects"].push_back(d);
    }

    // ファイル書き込み
    std::ofstream f(path);
    if (f.is_open()) {
        f << sceneData.dump(4); // インデント4で整形して保存
        f.close();
        DebugConsole::GetInstance()->AddLog("Saved SCENE to " + path);
    } else {
        DebugConsole::GetInstance()->AddLog("Failed to save JSON to " + path);
    }
}
// 単体保存
void DebugEditor::SaveSingleObject() {
    if (!selectedObject_) return;
    std::string path = "Resources/json/3Dobject/" + std::string(currentSceneFilename_);
    UpdateObjectInSceneJSON(selectedObject_, path);
    DebugConsole::GetInstance()->AddLog("Saved SINGLE Object");
}

// 複製
void DebugEditor::DuplicateSelected() {
    if (!selectedObject_ || !sceneManager_->GetCurrentScene()) return;

    // 1. 完全なクローンを作成 (Object3d::Clone または Character::Clone が呼ばれる)
    std::unique_ptr<Object3d> newObj = selectedObject_->Clone();

    // 2. 名前変更
    static int duplicateCount = 0;
    newObj->SetName(selectedObject_->GetName() + "_Copy" + std::to_string(duplicateCount++));

    // 3. 位置ずらし
    // ★修正: GetTransform() -> translate
    newObj->GetTransform()->translate.x += 2.0f;

    // 行列更新
    newObj->UpdateWorldMatrix();

    // 4. 追加
    Object3d* ptr = newObj.get();
    sceneManager_->GetCurrentScene()->AddObject(std::move(newObj));

    // 5. 選択切り替え
    selectedObject_ = ptr;

   
}

// 削除
void DebugEditor::DeleteSelected() {
    if (!selectedObject_ || !sceneManager_->GetCurrentScene()) return;

    std::string name = selectedObject_->GetName();
    sceneManager_->GetCurrentScene()->RequestRemoveObject(selectedObject_);

    // 重要：削除したポインタを持ち続けないようにする
    selectedObject_ = nullptr;

}

// ==========================================
//  Undo処理 
// ==========================================
void DebugEditor::PerformUndo() {
    if (undoStack_.empty()) return;

    // 1. 履歴を取り出す
    TransformCommand cmd = undoStack_.back();
    undoStack_.pop_back();

    // 2. Redo用に退避
    redoStack_.push_back(cmd);

    // 3. 値を「変更前 (oldTf)」に戻す
    if (cmd.target) {
        // ★修正: 構造体ごとコピー
        *cmd.target->GetTransform() = cmd.oldTf;

        // 行列更新
        cmd.target->UpdateWorldMatrix();
    }
    DebugConsole::GetInstance()->AddLog("Undo Performed");
}

// ==========================================
//  Redo処理
// ==========================================
void DebugEditor::PerformRedo() {
    if (redoStack_.empty()) return;

    TransformCommand cmd = redoStack_.back();
    redoStack_.pop_back();

    undoStack_.push_back(cmd);

    // 4. 値を「変更後 (newTf)」に進める
    if (cmd.target) {
        *cmd.target->GetTransform() = cmd.newTf;

        cmd.target->UpdateWorldMatrix();
    }
    DebugConsole::GetInstance()->AddLog("Redo Performed");
}



// マウス位置からワールド空間へのレイを作成
Ray DebugEditor::ScreenPointToRay(const Vector2& mousePos) {
    const Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) return Ray{};

    Matrix4x4 matView = camera->GetViewMatrix();
    Matrix4x4 matProj = camera->GetProjectionMatrix();
    Matrix4x4 matViewProj = math_.Multiply(matView, matProj);
    Matrix4x4 matInverseVP = math_.Inverse(matViewProj);


    float localMouseX = mousePos.x; 
    float localMouseY = mousePos.y; 

    // 2. 範囲外チェック
    if (localMouseX < 0 || localMouseX > gameViewSize_.x ||
        localMouseY < 0 || localMouseY > gameViewSize_.y) {
        return Ray{ {0,0,0}, {0,0,0} };
    }

    // 3. NDC変換
    Vector3 nearPos, farPos;
    nearPos.x = (2.0f * localMouseX) / gameViewSize_.x - 1.0f;
    nearPos.y = 1.0f - (2.0f * localMouseY) / gameViewSize_.y;
    nearPos.z = 0.0f;

    farPos = nearPos;
    farPos.z = 1.0f;

    // 4. ワールド変換
    Vector3 worldNear = math_.Transform(nearPos, matInverseVP);
    Vector3 worldFar = math_.Transform(farPos, matInverseVP);

    Ray ray;
    ray.origin = worldNear;
    ray.diff = worldFar - worldNear;

    return ray;
}
// ---------------------------------------------------------
// レイと平面(Y=0)の交点を計算する関数
// 戻り値: 交差したら true, その座標を intersectOut に入れる
// ---------------------------------------------------------
bool DebugEditor::IntersectRayPlane(const Ray& ray, Vector3& intersectOut) {
    // 計算用のMathクラス
    Math math;

    // 平面の定義（上向きの法線, 高さ0）
    Vector3 planeNormal = { 0.0f, 1.0f, 0.0f };
    float planeHeight = 0.0f;

    // 平行かどうかチェック (内積が0に近い＝平行)
    // レイの方向ベクトルと平面の法線の内積をとる
    float denominator = math.Dot(ray.diff, planeNormal);

    // レイが水平に近いなら判定しない (0除算防止)
    if (std::abs(denominator) < 0.0001f) {
        return false;
    }

    // 交点までの距離 t を求める公式
    // t = (PlaneHeight - Dot(RayOrigin, PlaneNormal)) / Dot(RayDir, PlaneNormal)

    // まずレイの方向を正規化する
    Vector3 rayDir = math.Normalize(ray.diff);
    float dotDir = math.Dot(rayDir, planeNormal);

    // 念のため再チェック
    if (std::abs(dotDir) < 0.0001f) return false;

    // 距離tの計算
    float t = (planeHeight - math.Dot(ray.origin, planeNormal)) / dotDir;

    // tがマイナス＝カメラの後ろ側にあるので無視
    if (t < 0.0f) {
        return false;
    }

    // 交点座標 = 原点 + 方向 * 距離
    // intersectOut = ray.origin + (rayDir * t)
    Vector3 travel = { rayDir.x * t, rayDir.y * t, rayDir.z * t };
    intersectOut = { ray.origin.x + travel.x, ray.origin.y + travel.y, ray.origin.z + travel.z };

    return true;
}





void DebugEditor::DrawPreview(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    if (previewObject_) {

        previewObject_->Draw(pointLightResource, spotLightResource);
    }
}



#ifdef USE_IMGUI
void DebugEditor::DrawEnemyTypeSelector() {
    if (!selectedObject_) return;

    // ★ 登録したい敵の名前リスト 
    const char* enemyTypes[] = {
        "Slime",

    };

    // 現在の設定値を取得
    std::string currentType = selectedObject_->GetEnemyType();

    // コンボボックスで現在どれが選ばれているか判定
    int currentIndex = -1;
    for (int i = 0; i < IM_ARRAYSIZE(enemyTypes); i++) {
        if (currentType == enemyTypes[i]) {
            currentIndex = i;
            break;
        }
    }

    // --- UI描画 ---
    const char* previewValue = (currentIndex >= 0) ? enemyTypes[currentIndex] : "(未設定)";

    if (ImGui::BeginCombo("敵の種族 (Enemy Type)", previewValue)) {
        for (int i = 0; i < IM_ARRAYSIZE(enemyTypes); i++) {
            bool isSelected = (currentIndex == i);

            if (ImGui::Selectable(enemyTypes[i], isSelected)) {
                // 選ばれたらセットする
                selectedObject_->SetEnemyType(enemyTypes[i]);
                selectedObject_->SetName("Enemy_" + std::string(enemyTypes[i]));
            }

            // 初期選択位置を合わせる
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    // 補足ヘルプ
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("ロード時に生成される敵クラスを指定します。\nEmptyの場合はただの箱になります。");
    }
}



void DebugEditor::DrawSpawnerSettings() {
    if (!selectedObject_) return;

    ImGui::Separator();
    ImGui::Indent(); // 少し右にずらすと見やすい
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "[ Spawner Config ]");

    // パラメータ領域がない場合は作成
    if (!selectedObject_->param_.has_value()) {
        selectedObject_->param_.emplace();
    }
    auto& p = selectedObject_->param_.value();

    // ----------------------------------------------------
    // A. 敵の種類 (EnemyType)
    // ----------------------------------------------------
    // 簡易入力欄 (手打ち)
    // ※もしstring型の変数が直接編集しにくい場合はバッファを使います
    static char typeBuf[64] = "";
    if (typeBuf[0] == '\0') {
        // 初回コピー
        strcpy_s(typeBuf, sizeof(typeBuf), p.enemyType.c_str());
    }

    // プリセットから選ばせるコンボボックスも便利
    const char* enemyTypes[] = { "Slime", };
    int currentTypeIndex = -1;
    for (int i = 0; i < IM_ARRAYSIZE(enemyTypes); i++) {
        if (p.enemyType == enemyTypes[i]) currentTypeIndex = i;
    }

    if (ImGui::Combo("Spawn Type", &currentTypeIndex, enemyTypes, IM_ARRAYSIZE(enemyTypes))) {
        p.enemyType = enemyTypes[currentTypeIndex];
        strcpy_s(typeBuf, sizeof(typeBuf), p.enemyType.c_str());
    }

    // ----------------------------------------------------
    // B. 出現間隔 (Interval)
    // ----------------------------------------------------
    ImGui::DragFloat("Interval (sec)", &p.interval, 0.1f, 0.1f, 60.0f, "%.1f s");

    // ----------------------------------------------------
    // C. 最大数 (MaxCount)
    // ----------------------------------------------------
    ImGui::InputInt("Max Count", &p.maxCount);

    ImGui::Unindent(); // インデント戻す
}
#endif