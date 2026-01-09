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
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    InputManager* input = InputManager::GetInstance();
    Math math; // 計算用

    // シーンが変わったら選択解除
    if (lastUpdatedScene_ != currentScene) {
        selectedObject_ = nullptr;
        previewObject_ = nullptr; // プレビューもキャンセル
        lastUpdatedScene_ = currentScene;
    }

    // =========================================================
    //  モードA: 設置モード (プレビューオブジェクトを持っている時)
    // =========================================================
    if (previewObject_) {

        // ギズモやImGui操作中でなければ処理する
        if (!ImGui::GetIO().WantCaptureMouse) {

            // 1. マウス位置からレイを作成
            Vector2 mousePos = input->GetMousePosition();
            // 元々動作していた ScreenPointToRay を使用
            Ray ray = ScreenPointToRay(mousePos);

            Vector3 finalPos = { 0, 0, 0 };
            bool foundPosition = false;

            // -------------------------------------------------
            // A-1. 既存のオブジェクトとの当たり判定 (上に積む)
            // -------------------------------------------------
            auto& objects = currentScene->GetObjects();
            RayResult bestHit;
            bestHit.isHit = false;
            bestHit.distance = 100000.0f;

            for (auto& obj : objects) {
                // 自分自身やギズモ用の線などは無視
                if (obj.get() == previewObject_.get()) continue;
                if (obj->GetName() == "Cursor" || obj->GetName() == "Line") continue;

                Object3d::Transform* tf = obj->GetTransform();
                // AABB (簡易衝突判定ボックス) 作成
                Vector3 minBox = { tf->translate.x - tf->scale.x, tf->translate.y - tf->scale.y, tf->translate.z - tf->scale.z };
                Vector3 maxBox = { tf->translate.x + tf->scale.x, tf->translate.y + tf->scale.y, tf->translate.z + tf->scale.z };

                RayResult tempHit;
                if (math.IntersectRayAABB(ray, minBox, maxBox, &tempHit)) {
                    if (tempHit.distance < bestHit.distance) {
                        bestHit = tempHit;
                    }
                }
            }

            // 何かに当たったらその上に配置
            if (bestHit.isHit) {
                finalPos = bestHit.point;
                // ※とりあえず上に1.0ずらす（本来は法線方向にずらすのがベスト）
                finalPos.y += 1.0f;
                foundPosition = true;
            }

            // -------------------------------------------------
            // A-2. 地面判定 & セーフティネット
            // -------------------------------------------------
            if (!foundPosition) {
                Vector3 groundPos;
                // 地面(Y=0)との交差判定
                if (IntersectRayPlane(ray, groundPos)) {
                    finalPos = groundPos;
                    finalPos.y = 0.5f; // 地面に埋まらないように高さ調整（サイズ次第）
                    foundPosition = true;
                }
                // ★追加：地面に当たらなくても、カメラの目の前に出す！(これで絶対消えない)
                else {
                    Vector3 rayDir = math.Normalize(ray.diff);
                    // 視線の先に10.0f進んだ位置
                    finalPos = {
                        ray.origin.x + rayDir.x * 10.0f,
                        ray.origin.y + rayDir.y * 10.0f,
                        ray.origin.z + rayDir.z * 10.0f
                    };
                    foundPosition = true;
                }
            }

            // -------------------------------------------------
            // A-3. グリッドスナップ & 座標反映
            // -------------------------------------------------
            if (foundPosition) {
                // グリッドスナップ有効なら丸める
                if (isGridSnapEnabled_) {
                    float gridSize = 1.0f;
                    finalPos.x = std::round(finalPos.x / gridSize) * gridSize;
                    finalPos.z = std::round(finalPos.z / gridSize) * gridSize;
                }

                // プレビューの位置を更新
                previewObject_->GetTransform()->translate = finalPos;
				previewObject_->SetColor({ 1.0f, 1.8f, 1.0f, 0.5f }); // 半透明に見せる
                previewObject_->GetTransform()->scale = { 1.0f, 1.0f, 1.0f };
                // ★ここが重要：必ず行列を更新して描画に反映させる
				previewObject_->UpdateLocalMatrix();
                previewObject_->UpdateWorldMatrix();
            }

            // -------------------------------------------------
            // A-4. クリックで確定 (Spawn)
            // -------------------------------------------------
            // 左クリックを離した瞬間
            if (input->IsMouseButtonReleased(0)) {
                Object3dCommon* common = currentScene->GetObject3dCommon();
                if (common) {
                    auto newObj = std::make_unique<Object3d>();
                    newObj->Initialize(common);


                    newObj->CopyFrom(previewObject_.get());
                    static int spawnCount = 0;
                    newObj->SetName("Obj_" + std::to_string(spawnCount++));
                    newObj->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                    currentScene->AddObject(std::move(newObj));

                    DebugConsole::GetInstance()->AddLog("Spawned Object via CopyFrom!");
                }
            }

            // 右クリックでキャンセル
            if (input->IsKeyPressed(DIK_E)) {
                previewObject_ = nullptr;
                DebugConsole::GetInstance()->AddLog("Canceled Placement");
            }
        }
    }
    // =========================================================
    //  モードB: 通常選択モード (プレビューが無い時)
    // =========================================================
    else {

        // --- 1. ショートカットキー処理 ---
        if (!ImGui::GetIO().WantCaptureKeyboard) {
            bool isCtrl = input->IsKeyPressed(DIK_LCONTROL) || input->IsKeyPressed(DIK_RCONTROL);

            // 削除
            if (input->IsKeyTriggered(DIK_DELETE)) DeleteSelected();
            // 複製
            if (isCtrl && input->IsKeyTriggered(DIK_C)) DuplicateSelected();
            // Undo/Redo
            if (isCtrl && input->IsKeyTriggered(DIK_Z)) PerformUndo();
            if (isCtrl && input->IsKeyTriggered(DIK_Y)) PerformRedo();
            // 保存
            if (isCtrl && input->IsKeyTriggered(DIK_S)) SaveScene();
        }

        // --- 2. マウス選択処理 ---
        if (!ImGui::GetIO().WantCaptureMouse && !ImGuizmo::IsOver()) {

            if (input->IsMouseButtonReleased(0)) {
                Vector2 mousePos = input->GetMousePosition();
                Ray ray = ScreenPointToRay(mousePos);

                auto& objects = currentScene->GetObjects();
                RayResult bestHit;
                bestHit.isHit = false;
                bestHit.distance = 100000.0f;
                Object3d* hitObj = nullptr;

                for (auto& obj : objects) {
                    if (obj->GetName() == "Cursor" || obj->GetName() == "Line") continue;

                    Object3d::Transform* tf = obj->GetTransform();
                    Vector3 minBox = { tf->translate.x - tf->scale.x, tf->translate.y - tf->scale.y, tf->translate.z - tf->scale.z };
                    Vector3 maxBox = { tf->translate.x + tf->scale.x, tf->translate.y + tf->scale.y, tf->translate.z + tf->scale.z };

                    RayResult tempHit;
                    if (math.IntersectRayAABB(ray, minBox, maxBox, &tempHit)) {
                        if (tempHit.distance < bestHit.distance) {
                            bestHit = tempHit;
                            hitObj = obj.get();
                        }
                    }
                }

                if (bestHit.isHit) {
                    selectedObject_ = hitObj;
                } else {
                    selectedObject_ = nullptr;
                }
            }
        }

        // --- 3. ギズモ表示 (選択中のみ) ---
        if (selectedObject_) {
            static ImGuizmo::OPERATION currentOperation = ImGuizmo::TRANSLATE;
            static ImGuizmo::MODE currentMode = ImGuizmo::WORLD;

            // ギズモ切り替えキー
            if (!ImGui::GetIO().WantCaptureKeyboard) {
                if (input->IsKeyTriggered(DIK_T)) currentOperation = ImGuizmo::TRANSLATE;
                if (input->IsKeyTriggered(DIK_R)) currentOperation = ImGuizmo::ROTATE;
                if (input->IsKeyTriggered(DIK_S)) currentOperation = ImGuizmo::SCALE;
            }

            Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
            if (camera) {
                const Matrix4x4& view = camera->GetViewMatrix();
                const Matrix4x4& proj = camera->GetProjectionMatrix();
                Object3d::Transform* tr = selectedObject_->GetTransform();

                Matrix4x4 world = math.MakeAffineMatrix(tr->scale, tr->rotate, tr->translate);

                ImGuiIO& io = ImGui::GetIO();
                ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

                // スナップ設定
                float snapVal = 0.0f;
                if (isGridSnapEnabled_) {
                    if (currentOperation == ImGuizmo::ROTATE) snapVal = 15.0f;
                    else if (currentOperation == ImGuizmo::SCALE) snapVal = 0.1f;
                    else snapVal = 1.0f;
                }
                float snapArray[3] = { snapVal, snapVal, snapVal };
                float* snapPtr = isGridSnapEnabled_ ? snapArray : nullptr;

                // ギズモ操作
                ImGuizmo::Manipulate(&view.m[0][0], &proj.m[0][0], currentOperation, currentMode, &world.m[0][0], nullptr, snapPtr);

                // 操作開始 (Undo記録)
                if (ImGuizmo::IsUsing() && !isDraggingTransform_) {
                    isDraggingTransform_ = true;
                    tempTransformStart_ = *tr;
                    redoStack_.clear();
                }

                // 操作中 (値の反映)
                if (ImGuizmo::IsUsing()) {
                    Vector3 s, rDeg, t;
                    ImGuizmo::DecomposeMatrixToComponents(&world.m[0][0], &t.x, &rDeg.x, &s.x);
                    tr->translate = t;
                    tr->rotate = { ToRadians(rDeg.x), ToRadians(rDeg.y), ToRadians(rDeg.z) };
                    tr->scale = s;
                    selectedObject_->UpdateWorldMatrix();
                }

                // 操作終了 (Undo確定)
                if (!ImGuizmo::IsUsing() && isDraggingTransform_) {
                    isDraggingTransform_ = false;
                    TransformCommand cmd;
                    cmd.target = selectedObject_;
                    cmd.oldTf = tempTransformStart_;
                    cmd.newTf = *tr;
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
    std::string currentJsonPath = "resouces/json/3Dobject/" + std::string(currentSceneFilename_);

    // ==========================================================================================
    // Inspector Window (シーン管理 & 選択中オブジェクトの編集)
    // ==========================================================================================
    ImGui::Begin("Inspector"); // ウィンドウ名は設定ファイル(ini)に関わるので英語推奨

    // ---------------------------------------------------------
    // 1. ファイル管理エリア (File Manager)
    // ---------------------------------------------------------
    if (ImGui::CollapsingHeader("シーンファイル管理 (Scene File)", ImGuiTreeNodeFlags_DefaultOpen)) {

        std::string directoryPath = "resouces/json/3Dobject/";
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
        Object3d::Transform* transform = selectedObject_->GetTransform();
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
            std::string dirPath = "resouces/json/animation/";

            if (fs::exists(dirPath) && fs::is_directory(dirPath)) {
                for (const auto& entry : fs::directory_iterator(dirPath)) {
                    if (entry.path().extension() == ".json") {
                        std::string fileName = entry.path().stem().string();

                        bool isSelected = (selectedObject_->animName_ == fileName);

                        if (ImGui::Selectable(fileName.c_str(), isSelected)) {
                            selectedObject_->animName_ = fileName;

                            if (selectedObject_->recorder_) {
                                selectedObject_->recorder_->Play(
                                    selectedObject_->animName_,
                                    selectedObject_->isAnimLoop_,
                                    selectedObject_->isAnimRelative_
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
                selectedObject_->recorder_->Play(
                    selectedObject_->animName_,
                    selectedObject_->isAnimLoop_,
                    selectedObject_->isAnimRelative_
                );
            }
        }

        if (ImGui::Checkbox("相対座標モード##Anim", &selectedObject_->isAnimRelative_)) {
            if (selectedObject_->recorder_ && !selectedObject_->animName_.empty()) {
                selectedObject_->recorder_->Play(
                    selectedObject_->animName_,
                    selectedObject_->isAnimLoop_,
                    selectedObject_->isAnimRelative_
                );
            }
        }


        // テスト再生ボタン
        if (ImGui::Button("テスト再生##Anim")) {
            if (selectedObject_->recorder_) {
                selectedObject_->recorder_->Play(
                    selectedObject_->animName_,
                    selectedObject_->isAnimLoop_,
                    selectedObject_->isAnimRelative_
                );
            }
        }

        // --- Game Data (Stats) ---
        ImGui::Separator();
        if (ImGui::CollapsingHeader("ゲームデータ (Stats)", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Event Type
            EventType currentType = selectedObject_->GetEventType();
            int currentItemIndex = static_cast<int>(currentType);
            const char* eventNames[] = { "なし", "ダメージ", "ワープ" };
            if (ImGui::Combo("イベント種類", &currentItemIndex, eventNames, IM_ARRAYSIZE(eventNames))) {
                selectedObject_->SetEventType(static_cast<EventType>(currentItemIndex));
            }

            ImGui::Spacing();

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

        // スポーン用ドロップエリア
        ImGui::Button("[ ここにモデルをドロップして生成 ]", ImVec2(-1, 30));
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
            const char* modelName = (const char*)payload->Data;
            ModelManager::GetInstance()->LoadModel(modelName);

            Object3dCommon* common = sceneManager_->GetCurrentScene()->GetObject3dCommon();
            if (common) {
                // プレビューオブジェクト作成処理...
                auto newObj = std::make_unique<Object3d>();
                newObj->Initialize(common);
                newObj->SetModel(modelName);
                newObj->SetClassName("Model");
                newObj->SetName("Preview_" + std::string(modelName));
                newObj->UpdateLocalMatrix();
                newObj->UpdateWorldMatrix();
                previewObject_ = std::move(newObj);

                DebugConsole::GetInstance()->AddLog("Placement Mode: " + std::string(modelName));
            }
        }
        ImGui::EndDragDropTarget();


        ImGui::Separator();

        // ツリー表示
        auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
        for (auto& obj : objects) {
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

    // アニメーション設定の保存
    currentData["animName"] = object->animName_;
    currentData["isAnimLoop"] = object->isAnimLoop_;



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

void DebugEditor::DrawProjectWindow() {
    // ウィンドウ名は英語のままにしておきます (imgui.iniの保存IDとして使われるため)
    ImGui::Begin("Project (Assets)");

    // ディレクトリパス 
    // ※ typo注意: "resources" が正しいスペルですが、元のコードに合わせて "resouces" にしています
    std::string baseDirectory = "resouces/3DModel";

    if (fs::exists(baseDirectory) && fs::is_directory(baseDirectory)) {
        // 説明文を日本語化
        ImGui::Text("モデルをHierarchyへドラッグして配置");
        ImGui::Separator();

        // ウィンドウの右端座標を取得（折り返し判定用）
        float windowVisibleX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        float itemSpacing = ImGui::GetStyle().ItemSpacing.x;

        for (const auto& entry : fs::directory_iterator(baseDirectory)) {
            std::string displayModelName = ""; // ボタンに表示する名前
            std::string payloadName = "";      // ModelManagerに渡す名前

            // =======================================================
            // パターンA: フォルダの場合 (OBJなどテクスチャを含む場合)
            // =======================================================
            if (entry.is_directory()) {
                std::string folderName = entry.path().filename().string();

                // フォルダの中にあるモデルファイルを探す
                for (const auto& subEntry : fs::directory_iterator(entry.path())) {
                    std::string subExt = subEntry.path().extension().string();
                    // 念のため小文字変換
                    std::transform(subExt.begin(), subExt.end(), subExt.begin(), ::tolower);

                    // 1. OBJの場合 
                    if (subExt == ".obj") {
                        displayModelName = folderName;
                        payloadName = folderName;
                        break; // 1つ見つけたら終了
                    }
                    // 2. glTF / GLB の場合 
                    else if (subExt == ".gltf" || subExt == ".glb") {
                        displayModelName = subEntry.path().filename().string();
                        payloadName = subEntry.path().filename().string();
                        break; // 1つ見つけたら終了
                    }
                }
            }

            // =======================================================
            // モデルが見つかった場合のみボタンを描画
            // =======================================================
            if (!displayModelName.empty()) {
                // ID重複防止
                ImGui::PushID(displayModelName.c_str());

                // ボタン描画 (幅100指定)
                // 色を変えて「アセット感」を出しても良いかもしれません
                ImGui::Button(displayModelName.c_str(), ImVec2(100, 0));

                // --- ドラッグ&ドロップ処理 ---
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    // ペイロードとして名前を渡す
                    // フォルダ名なら "Player", ファイルなら "Enemy.glb" が渡される
                    ImGui::SetDragDropPayload("MODEL_ASSET", payloadName.c_str(), payloadName.size() + 1);

                    // ドラッグ中のプレビュー表示
                    ImGui::Text("生成: %s", displayModelName.c_str());
                    ImGui::EndDragDropSource();
                }

                ImGui::PopID();

                // --- レイアウト調整 (横並べ) ---
                float lastButtonX = ImGui::GetItemRectMax().x;
                float nextButtonX = lastButtonX + itemSpacing + 100.0f; // 次のボタンの右端予測

                // 次のボタンがウィンドウ内に収まるなら改行しない (SameLine)
                if (nextButtonX < windowVisibleX) {
                    ImGui::SameLine();
                }
            }
        }
    } else {
        // エラー表示
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "ディレクトリが見つかりません: %s", baseDirectory.c_str());
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



// ========================================================================
// ショートカット / ボタン機能の実装
// ========================================================================

// シーン全体保存
void DebugEditor::SaveScene() {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    // パスの構築
    std::string path = "resouces/json/" + std::string(currentSceneFilename_);

    // 今まで DrawImGui に書いていた JSON 保存ロジック
    using json = nlohmann::json;
    json sceneData;
    sceneData["objects"] = json::array();

    auto& allObjects = sceneManager_->GetCurrentScene()->GetObjects();
    for (auto& obj : allObjects) {
        if (obj->GetName().empty()) continue;

        json d;
        d["name"] = obj->GetName();

        // クラス名によるタイプ保存
        if (obj->GetClassName() == "InvisibleBox") {
            d["type"] = "InvisibleBox";
        } else {
            d["type"] = "Model";
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
        d["targetID"] = obj->GetTargetID(); // 送信ID
        d["myEventID"] = obj->GetEventID(); // 受信ID

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
        //アニメション設定
        d["animName"] = obj->animName_;
        d["isAnimLoop"] = obj->isAnimLoop_;
        d["isAnimRelative"] = obj->isAnimRelative_;
        sceneData["objects"].push_back(d);
    }

    std::ofstream f(path);
    if (f.is_open()) {
        f << sceneData.dump(4);
        f.close();
        DebugConsole::GetInstance()->AddLog("Saved SCENE to " + path);
    } else {
        DebugConsole::GetInstance()->AddLog("Failed to save JSON!");
    }
}

// 単体保存
void DebugEditor::SaveSingleObject() {
    if (!selectedObject_) return;
    std::string path = "resouces/json/" + std::string(currentSceneFilename_);
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
    newObj->GetTransform()->translate.x += 2.0f;

    // 4. 追加
    Object3d* ptr = newObj.get();
    sceneManager_->GetCurrentScene()->AddObject(std::move(newObj));

    // 5. 選択切り替え
    selectedObject_ = ptr;

    DebugConsole::GetInstance()->AddLog("Duplicated Object: " + ptr->GetName());
}

// 削除
void DebugEditor::DeleteSelected() {
    if (!selectedObject_ || !sceneManager_->GetCurrentScene()) return;

    std::string name = selectedObject_->GetName();
    sceneManager_->GetCurrentScene()->RequestRemoveObject(selectedObject_);

    // 重要：削除したポインタを持ち続けないようにする
    selectedObject_ = nullptr;

    DebugConsole::GetInstance()->AddLog("Deleted Object: " + name);
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
        // Transform構造体を丸ごとコピーできるので楽です！
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
    // 1. カメラ情報の取得
    const Camera* camera = CameraManager::GetInstance()->GetActiveCamera(); 
    if (!camera) return Ray{}; // カメラがない場合は空を返す

    // ビュー行列とプロジェクション行列
    Matrix4x4 matView = camera->GetViewMatrix();
    Matrix4x4 matProj = camera->GetProjectionMatrix();
    Matrix4x4 matViewProj = math_.Multiply(matView, matProj);

    // 逆行列（スクリーン→ワールドに戻すため）
    Matrix4x4 matInverseVP = math_.Inverse(matViewProj);

    // 2. スクリーン座標をNDC（-1.0 ~ 1.0）に変換
    float screenW = (float)WinApp::kClientWidth;
    float screenH = (float)WinApp::kClientHeight;

    // NDC座標計算 (Y軸は反転させるのが一般的)
    Vector3 nearPos, farPos;
    nearPos.x = (2.0f * mousePos.x) / screenW - 1.0f;
    nearPos.y = 1.0f - (2.0f * mousePos.y) / screenH;
    nearPos.z = 0.0f; // ニアクリップ平面 (手前)

    farPos = nearPos;
    farPos.z = 1.0f;  // ファークリップ平面 (奥)

    // 3. ワールド座標に変換
    Vector3 worldNear = math_.Transform(nearPos, matInverseVP);
    Vector3 worldFar = math_.Transform(farPos, matInverseVP);

    // 4. レイの作成
    Ray ray;
    ray.origin = worldNear; // 発射地点
    ray.diff = worldFar - worldNear; // 方向ベクトル (長さ含む)

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