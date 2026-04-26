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
#include "EditorManager.h"
#include "PostEffectEditor.h"
#include "SpriteDebugEditor.h"
#include "ParticleEditor.h"
#include "GPUParticleEditor.h"
#include "VFXSequencerEditor.h"
#include "LightEditor.h"      
#include "IconsFontAwesome5.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <DebugConsole.h>
#include <CollisionManager.h>
#include <filesystem> // ファイル操作用
#include <BulletManager.h>
#include <PresetManager.h>
#include <MeshEffectManager.h>
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
    PresetManager::GetInstance()->LoadPresets();
    hierarchyWindow_.Initialize(this);
    projectWindow_.Initialize(this, dxCommon);
    inspectorWindow_.Initialize(this);
    serializer_.Initialize(this);
    primitiveDrawer_.Initialize(dxCommon);
}

// ========================================================================
// 更新 (ImGui処理)
// ========================================================================
// ========================================================================
// 更新 (ImGui処理 / エディタ操作のコア)
// ========================================================================
void DebugEditor::Update() {
#ifdef USE_IMGUI
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    InputManager* input = InputManager::GetInstance();
    Math math;

    // =========================================================
    // 1. エディタ状態の同期と更新
    // =========================================================
    IEditable* current = EditorManager::GetInstance()->GetSelectedObject();
    static std::string s_lastSyncedSceneFilename = "";
    std::string currentLoadedName = currentScene->GetLoadedFilename();

    // ファイル名が前フレームから変わった時「だけ」同期する
    // （コンボボックスでの手動切り替え操作を邪魔しないための工夫）
    if (!currentLoadedName.empty() && s_lastSyncedSceneFilename != currentLoadedName) {
        SetSceneFilename(currentLoadedName);
        s_lastSyncedSceneFilename = currentLoadedName;
    }

    CameraEditor::GetInstance()->SetGameViewHovered(isGameViewHovered_);

    // 選択対象が「DebugEditor自身」である間は、以前選んだオブジェクトを保持し続ける
    if (current != nullptr && current != this) {
        Object3d* obj = dynamic_cast<Object3d*>(current);
        if (obj) {
            selectedObject_ = obj;
        }
    }

    // シーン変更リセット
    if (lastUpdatedScene_ != currentScene) {
        selectedObject_ = nullptr;
        previewObject_ = nullptr;
        lastUpdatedScene_ = currentScene;
    }

    // --- カメラ制御 (設置モード用) ---
    bool isPreviewActive = (previewObject_ != nullptr);
    if (isPreviewActive && !wasPreviewActive_) {
        CameraEditor* camEditor = CameraEditor::GetInstance();
        previousCameraMode_ = (int)camEditor->GetMode();
        camEditor->SetMode(CameraEditor::Mode::Editor);
        // ★ 修正: カメラを強制的に上空へワープさせるお節介機能を完全削除！
        // （これで現在の視点をキープしたまま配置作業に入れます）
    }
    else if (!isPreviewActive && wasPreviewActive_) {
        CameraEditor::GetInstance()->SetMode((CameraEditor::Mode)previousCameraMode_);
    }
    wasPreviewActive_ = isPreviewActive;

    // =========================================================
    //  モードA: 古い設置モード (Hierarchy等からのプレビュー配置)
    // =========================================================
    if (previewObject_) {
        if (isGameViewHovered_) {
            Ray ray = ScreenPointToRay(gameViewMousePos_);
            Vector3 finalPos = { 0, 0, 0 };
            bool found = false;

            // 当たり判定 (AABB)
            auto& objects = currentScene->GetObjects();
            RayResult best; best.isHit = false; best.distance = 1e5f;
            for (auto& obj : objects) {
                if (obj.get() == previewObject_.get() || obj->GetName() == "Cursor") continue;

                Matrix4x4 wm = obj->GetWorldMatrix();
                Vector3 wp = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };
                Vector3 ws = obj->GetTransform()->scale;
                RayResult tmp;
                if (math.IntersectRayAABB(ray, wp - ws, wp + ws, &tmp)) {
                    if (tmp.distance < best.distance) best = tmp;
                }
            }

            // 高さを自動調整して配置
            float yOffset = previewObject_->GetColliderConfig().size.y;
            if (yOffset == 0.0f) yOffset = previewObject_->GetTransform()->scale.y;

            if (best.isHit) {
                finalPos = best.point;
                finalPos.y += yOffset; // 自動フィットした高さを使用
                found = true;
            }
            else if (IntersectRayPlane(ray, finalPos)) {
                finalPos.y = yOffset;
                found = true;
            }
            else {
                finalPos = ray.origin + math.Normalize(ray.diff) * 10.0f;
                found = true;
            }

            if (found) {
                if (isGridSnapEnabled_) {
                    finalPos.x = std::round(finalPos.x / snapValue_) * snapValue_;
                    finalPos.z = std::round(finalPos.z / snapValue_) * snapValue_;
                }
                previewObject_->GetTransform()->translate = finalPos;
                previewObject_->SetColor({ 1.0f, 1.8f, 1.0f, 0.5f }); // プレビュー用の半透明緑
                previewObject_->UpdateWorldMatrix();
            }

            // ★ImGuiのクリック判定を使用 (マルチビューポート対応)
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                auto newObj = std::make_unique<Object3d>();
                newObj->Initialize(currentScene->GetObject3dCommon());
                newObj->CopyFrom(previewObject_.get());
                newObj->SetColor({ 1,1,1,1 }); // 色を元に戻す
                currentScene->AddObject(std::move(newObj));
            }

            // 右クリック または Eキー で配置モードキャンセル
            if (input->IsKeyTriggered(DIK_E) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                previewObject_ = nullptr;
            }
        }
    }
    // =========================================================
    //  モードB: 通常選択・編集モード
    // =========================================================
    else {
        if (!ImGui::GetIO().WantCaptureKeyboard) {

            // --- ショートカットキー処理 ---
            if (input->IsKeyTriggered(DIK_DELETE)) {
                // パス編集モード中なら「点」を消す！
                if (isPathEditMode_ && selectedObject_ && selectedObject_->recorder_ && selectedObject_->recorder_->IsPinSelected()) {
                    selectedObject_->recorder_->DeleteSelectedPin();
                }
                // 通常モードなら「オブジェクト」を消す！
                else if (!isPathEditMode_) {
                    DeleteSelected();
                }
            }

            if ((input->IsKeyPressed(DIK_LCONTROL)) && input->IsKeyTriggered(DIK_C)) DuplicateSelected();
            if ((input->IsKeyPressed(DIK_LCONTROL)) && input->IsKeyTriggered(DIK_Z)) PerformUndo();
            if ((input->IsKeyPressed(DIK_LCONTROL)) && input->IsKeyTriggered(DIK_Y)) PerformRedo();
            if (input->IsKeyTriggered(DIK_END)) DropToFloor();

            // カメラフォーカス機能
            if (input->IsKeyTriggered(DIK_F) && selectedObject_) {
                Vector3 targetPos = { selectedObject_->GetWorldMatrix().m[3][0],
                                      selectedObject_->GetWorldMatrix().m[3][1],
                                      selectedObject_->GetWorldMatrix().m[3][2] };

                // オブジェクトの少し手前・斜め上にカメラをワープさせる
                Vector3 newCamPos = { targetPos.x, targetPos.y + 5.0f, targetPos.z - 10.0f };
                Vector3 newCamRot = { ToRadians(20.0f), 0.0f, 0.0f };
                CameraEditor::GetInstance()->SetEditorCameraTransform(newCamPos, newCamRot);
            }
        }

        // --- マウス選択処理 (ギズモを触っていない ＆ パス編集中じゃない時) ---
        if (!isPathEditMode_ && isGameViewHovered_ && !ImGuizmo::IsOver()) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                Ray ray = ScreenPointToRay(gameViewMousePos_);
                auto& objects = currentScene->GetObjects();
                RayResult best; best.isHit = false; best.distance = 1e5f; Object3d* hit = nullptr;

                for (auto& obj : objects) {
                    if (obj->GetName() == "Cursor" || obj->GetName() == "Line") continue;
                    if (!obj->GetIsVisible() || obj->GetIsLocked()) continue;

                    Matrix4x4 wm = obj->GetWorldMatrix();
                    Vector3 wp = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };
                    Vector3 ws = obj->GetTransform()->scale;
                    RayResult tmp;

                    if (math.IntersectRayAABB(ray, wp - ws, wp + ws, &tmp)) {
                        if (tmp.distance < best.distance) { best = tmp; hit = obj.get(); }
                    }
                }

                if (hit) {
                    selectedObject_ = hit;
                    EditorManager::GetInstance()->SetSelectedObject(this);
                }
            }
        }

        // --- ギズモ (ImGuizmo) 操作 ---
        if (selectedObject_) {
            if (!isPathEditMode_ && !selectedObject_->GetIsLocked()) {
                static ImGuizmo::OPERATION curOp = ImGuizmo::TRANSLATE;
                if (input->IsKeyTriggered(DIK_T)) curOp = ImGuizmo::TRANSLATE;
                if (input->IsKeyTriggered(DIK_R)) curOp = ImGuizmo::ROTATE;
                if (input->IsKeyTriggered(DIK_S)) curOp = ImGuizmo::SCALE;

                Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
                if (cam) {
                    ImGuizmo::SetDrawlist();
                    ImGuizmo::SetRect(gameViewOffset_.x, gameViewOffset_.y, gameViewSize_.x, gameViewSize_.y);

                    Matrix4x4 view = cam->GetViewMatrix();
                    Matrix4x4 proj = cam->GetProjectionMatrix();
                    Transform* tr = selectedObject_->GetTransform();

                    selectedObject_->UpdateWorldMatrix();
                    Matrix4x4 world = selectedObject_->GetWorldMatrix();

                    float snapVal = isGridSnapEnabled_ ? snapValue_ : 0.0f;
                    float snapArr[3] = { snapVal, snapVal, snapVal };

                    ImGuizmo::Manipulate(&view.m[0][0], &proj.m[0][0], curOp, ImGuizmo::WORLD, &world.m[0][0], nullptr, isGridSnapEnabled_ ? snapArr : nullptr);

                    if (ImGuizmo::IsUsing()) {
                        if (!isDraggingTransform_) { isDraggingTransform_ = true; tempTransformStart_ = *tr; }

                        Matrix4x4 newLocalMat = world;
                        if (selectedObject_->GetParent()) {
                            Matrix4x4 parentWorldInv = math.Inverse(selectedObject_->GetParent()->GetWorldMatrix());
                            newLocalMat = math.Multiply(world, parentWorldInv);
                        }

                        Vector3 s, rDeg, t;
                        ImGuizmo::DecomposeMatrixToComponents(&newLocalMat.m[0][0], &t.x, &rDeg.x, &s.x);

                        tr->translate = t;
                        tr->scale = s;
                        tr->quaternion = math_.MatrixToQuaternion(newLocalMat);
                        tr->isQuaternionMaster = true; // クォータニオン優先モードにする
                        tr->rotate = { ToRadians(rDeg.x), ToRadians(rDeg.y), ToRadians(rDeg.z) };

                        selectedObject_->UpdateLocalMatrix();
                        selectedObject_->UpdateWorldMatrix();

                    }
                    else if (isDraggingTransform_) {
                        isDraggingTransform_ = false;
                        TransformCommand cmd = { selectedObject_, tempTransformStart_, *tr };
                        undoStack_.push_back(cmd);
                    }
                }
            }
        }
    }

    // =========================================================
    // 3. UI描画関連 (最前面)
    // =========================================================
    DrawSaveNotification();
    Draw3DIcons();
#endif
}
// ========================================================================
// 終了処理
// ========================================================================
void DebugEditor::Finalize() {
    primitiveDrawer_.Finalize();
}


// ========================================================================
// コライダー描画処理 
// ========================================================================
void DebugEditor::DrawDebug(ID3D12GraphicsCommandList* commandList) {
    if (sceneManager_ == nullptr) return;

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (!currentScene) return;

    // =========================================================
    // ★ PrimitiveDrawer にパイプライン設定を丸投げ！
    // =========================================================
    primitiveDrawer_.PreDraw(commandList);

    int instanceCount = 0;
    const int kMaxDrawLimit = 2048; // 描画上限（PrimitiveDrawerのバッファサイズと合わせる）
    Math math;

    // =========================================================
    // 1. シーン内のオブジェクトを描画 (統合版)
    // =========================================================
    auto& objects = currentScene->GetObjects();

    for (const auto& obj : objects) {
        if (!obj) continue;
        if (!obj->GetIsVisible()) continue;
        // インスタンス描画の上限チェック
        if (instanceCount >= kMaxDrawLimit) break;

        ColliderType type = obj->GetColliderType();
        bool isInvisibleObj = (obj->GetClassName() == "InvisibleBox");

        // --- 描画判定 ---
        // コライダーがなく、かつ「見える物体（モデルあり）」ならデバッグ線は不要
        if (type == ColliderType::kNone && !isInvisibleObj) continue;

        // コライダー表示OFF設定の時、「見える物体」のコライダーは消すが、
        // 「見えない物体(透明な壁など)」は編集用に表示したままにする
        if (!drawColliders_ && !isInvisibleObj) continue;

        // --- 行列計算 (サイズと位置) ---
        Matrix4x4 drawWorldMatrix = math.MakeIdentity4x4();

        // コライダーがある場合は、その形状データ(Size/Center/Rotation)に合わせて枠を変形させる
        if (type != ColliderType::kNone) {
            // コライダー設定を直接取得 (ImGuiでの変更を即座に反映させるため)
            Object3d::ColliderConfig config = obj->GetColliderConfig();

            if (type == ColliderType::kOBB) {
                // OBB (回転ありボックス) の計算
                Matrix4x4 matScale = math.MakeScaleMatrix(config.size * 2.0f);
                Matrix4x4 matRotX = math.MakeRotateXMatrix(config.rotation.x);
                Matrix4x4 matRotY = math.MakeRotateYMatrix(config.rotation.y);
                Matrix4x4 matRotZ = math.MakeRotateZMatrix(config.rotation.z);
                Matrix4x4 matRot = math.Multiply(matRotZ, math.Multiply(matRotX, matRotY));
                Matrix4x4 matTrans = math.MakeTranslateMatrix(config.center);
                Matrix4x4 matColliderLocal = math.Multiply(matScale, math.Multiply(matRot, matTrans));
                drawWorldMatrix = math.Multiply(matColliderLocal, obj->GetWorldMatrix());

            }
            else if (type == ColliderType::kAABB) {
                // AABB (軸平行ボックス) の計算
                Matrix4x4 matScale = math.MakeScaleMatrix(config.size * 2.0f);
                Matrix4x4 matTrans = math.MakeTranslateMatrix(config.center);
                Matrix4x4 matColliderLocal = math.Multiply(matScale, matTrans);
                drawWorldMatrix = math.Multiply(matColliderLocal, obj->GetWorldMatrix());

            }
            else if (type == ColliderType::kSphere) {
                // Sphere (球) の計算
                float radius = config.size.x; // Sphereはxを半径とする
                Matrix4x4 matScale = math.MakeScaleMatrix({ radius * 2.0f, radius * 2.0f, radius * 2.0f });
                Matrix4x4 matTrans = math.MakeTranslateMatrix(config.center);
                Matrix4x4 matColliderLocal = math.Multiply(matScale, matTrans);
                drawWorldMatrix = math.Multiply(matColliderLocal, obj->GetWorldMatrix());
            }
            else if (type == ColliderType::kCylinder) {
                // size.x を半径、size.y を高さ(の半分)として扱っている想定
                float radius = config.size.x;
                float height = config.size.y;
                Matrix4x4 matScale = math.MakeScaleMatrix({ radius * 2.0f, height * 2.0f, radius * 2.0f });
                Matrix4x4 matTrans = math.MakeTranslateMatrix(config.center);
                Matrix4x4 matColliderLocal = math.Multiply(matScale, matTrans);
                drawWorldMatrix = math.Multiply(matColliderLocal, obj->GetWorldMatrix());
            }

        }
        else {
            // コライダー未設定の「見えない箱」の場合の救済措置
            // Transformそのままで表示（これがないと選択すらできなくなる）
            drawWorldMatrix = obj->GetWorldMatrix();
        }

        // --- 色の決定 ---
        Vector4 color;
        if (isInvisibleObj) { // ★修正: isInvisible ではなく isInvisibleObj を使う
            // 見えないオブジェクトは「紫」固定
            color = { 0.6f, 0.0f, 0.8f, 1.0f };
        }
        else {
            // 通常オブジェクトはコライダー種別ごとの色
            switch (type) {
            case ColliderType::kOBB:    color = { 1.0f, 0.2f, 0.2f, 1.0f }; break; // 赤
            case ColliderType::kAABB:   color = { 0.0f, 1.0f, 0.0f, 1.0f }; break; // 緑
            case ColliderType::kSphere: color = { 0.0f, 0.5f, 1.0f, 1.0f }; break; // 青
            case ColliderType::kCylinder: color = { 1.0f, 0.5f, 0.0f, 1.0f }; break; // オレンジ色
            default:                    color = { 1.0f, 1.0f, 1.0f, 1.0f }; break; // 白
            }
        }

        // =========================================================
        // ★ PrimitiveDrawer で描画実行！
        // =========================================================
        if (type == ColliderType::kSphere) {
            primitiveDrawer_.DrawWireSphere(commandList, drawWorldMatrix, color, instanceCount);
        }
        else if (type == ColliderType::kCylinder) {
            primitiveDrawer_.DrawWireCylinder(commandList, drawWorldMatrix, color, instanceCount);
        }
        else {
            primitiveDrawer_.DrawWireCube(commandList, drawWorldMatrix, color, instanceCount);
        }
        instanceCount++;
    }

    // =========================================================
    // 2. 弾のコライダー描画
    // =========================================================
    if (drawColliders_) {
        const auto& bullets = BulletManager::GetInstance()->GetBullets();

        for (const auto& bullet : bullets) {
            if (!bullet || bullet->IsDead()) continue;
            if (instanceCount >= kMaxDrawLimit) break;

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

            }
            else if (type == ColliderType::kAABB) {
                AABB aabb = bullet->GetAABB();
                Vector3 center = (aabb.min + aabb.max) * 0.5f;
                Vector3 size = aabb.max - aabb.min;
                Matrix4x4 matScale = math.MakeScaleMatrix(size);
                Matrix4x4 matTrans = math.MakeTranslateMatrix(center);
                drawWorldMatrix = math.Multiply(matScale, matTrans);

            }
            else if (type == ColliderType::kSphere) {
                float radius = bullet->GetCollisionRadius();
                Vector3 center = bullet->GetWorldPosition();
                Matrix4x4 matScale = math.MakeScaleMatrix({ radius * 2.0f, radius * 2.0f, radius * 2.0f });
                Matrix4x4 matTrans = math.MakeTranslateMatrix(center);
                drawWorldMatrix = math.Multiply(matScale, matTrans);
            }

            // =========================================================
            // ★ PrimitiveDrawer で弾も描画実行！
            // =========================================================
            if (type == ColliderType::kSphere) {
                primitiveDrawer_.DrawWireSphere(commandList, drawWorldMatrix, color, instanceCount);
            }
            else if (type == ColliderType::kCylinder) {
                primitiveDrawer_.DrawWireCylinder(commandList, drawWorldMatrix, color, instanceCount);
            }
            else {
                primitiveDrawer_.DrawWireCube(commandList, drawWorldMatrix, color, instanceCount);
            }
            instanceCount++;
        }
        // =========================================================
    // 3. エフェクトのコライダー描画
    // =========================================================
        if (drawColliders_) {
            // ★ 修正：ゲーム中のエフェクト ＋ エディタのプレビューエフェクト を両方集める
            std::vector<EffectObject3d*> effectsToDraw;

            for (const auto& eff : MeshEffectManager::GetInstance()->GetActiveEffects()) {
                if (eff) effectsToDraw.push_back(eff.get());
            }
            if (EffectObject3d* preview = MeshEffectManager::GetInstance()->GetPreviewEffectForDebug()) {
                effectsToDraw.push_back(preview);
            }

            // 集めたエフェクトを描画！
            for (EffectObject3d* effect : effectsToDraw) {
                if (instanceCount >= kMaxDrawLimit) break;

                ColliderType type = effect->GetColliderType();
                if (type == ColliderType::kNone) continue;

                // エフェクトの判定枠はシアン（水色）にして区別
                Vector4 color = { 0.0f, 1.0f, 1.0f, 1.0f };
                Matrix4x4 drawWorldMatrix = math.MakeIdentity4x4();

                Object3d::ColliderConfig config = effect->GetColliderConfig();

                if (type == ColliderType::kOBB) {
                    Matrix4x4 matScale = math.MakeScaleMatrix(config.size * 2.0f);
                    Matrix4x4 matRotX = math.MakeRotateXMatrix(config.rotation.x);
                    Matrix4x4 matRotY = math.MakeRotateYMatrix(config.rotation.y);
                    Matrix4x4 matRotZ = math.MakeRotateZMatrix(config.rotation.z);
                    Matrix4x4 matRot = math.Multiply(matRotZ, math.Multiply(matRotX, matRotY));
                    Matrix4x4 matTrans = math.MakeTranslateMatrix(config.center);
                    Matrix4x4 matColliderLocal = math.Multiply(matScale, math.Multiply(matRot, matTrans));
                    drawWorldMatrix = math.Multiply(matColliderLocal, effect->GetWorldMatrix());

                }
                else if (type == ColliderType::kAABB) {
                    Matrix4x4 matScale = math.MakeScaleMatrix(config.size * 2.0f);
                    Matrix4x4 matTrans = math.MakeTranslateMatrix(config.center);
                    Matrix4x4 matColliderLocal = math.Multiply(matScale, matTrans);
                    drawWorldMatrix = math.Multiply(matColliderLocal, effect->GetWorldMatrix());

                }
                else if (type == ColliderType::kSphere) {
                    float radius = config.size.x;
                    Matrix4x4 matScale = math.MakeScaleMatrix({ radius * 2.0f, radius * 2.0f, radius * 2.0f });
                    Matrix4x4 matTrans = math.MakeTranslateMatrix(config.center);
                    Matrix4x4 matColliderLocal = math.Multiply(matScale, matTrans);
                    drawWorldMatrix = math.Multiply(matColliderLocal, effect->GetWorldMatrix());

                }
                else if (type == ColliderType::kCylinder) {
                    float radius = config.size.x;
                    float height = config.size.y;
                    Matrix4x4 matScale = math.MakeScaleMatrix({ radius * 2.0f, height * 2.0f, radius * 2.0f });
                    Matrix4x4 matTrans = math.MakeTranslateMatrix(config.center);
                    Matrix4x4 matColliderLocal = math.Multiply(matScale, matTrans);
                    drawWorldMatrix = math.Multiply(matColliderLocal, effect->GetWorldMatrix());
                }

                if (type == ColliderType::kSphere) {
                    primitiveDrawer_.DrawWireSphere(commandList, drawWorldMatrix, color, instanceCount);
                }
                else if (type == ColliderType::kCylinder) {
                    primitiveDrawer_.DrawWireCylinder(commandList, drawWorldMatrix, color, instanceCount);
                }
                else {
                    primitiveDrawer_.DrawWireCube(commandList, drawWorldMatrix, color, instanceCount);
                }
                instanceCount++;
            }
        }
    }
}

// ==========================================================================================
// 1. 左パネル：Hierarchy (階層構造) と 生成メニュー
// ==========================================================================================
void DebugEditor::DrawHierarchy() {
    hierarchyWindow_.Draw();
}
// ==========================================================================================
// 2. 右パネル：Inspector (選択したオブジェクトの詳細設定)
// ==========================================================================================
void DebugEditor::DrawImGui() {
    inspectorWindow_.Draw();
}



#ifdef USE_IMGUI
void DebugEditor::DrawProjectWindow() {
    projectWindow_.Draw();
}
#endif



void DebugEditor::SaveScene(SaveMode mode) {
    // 1. シリアライザーに重たい処理をすべて丸投げする
    std::string savedFiles = serializer_.SaveScene(currentSceneFilename_, mode);

    // 2. 通知などの「エディタ側」の演出だけを行う
    std::string baseName = currentSceneFilename_;
    size_t extPos = baseName.find(".json");
    if (extPos != std::string::npos) baseName = baseName.substr(0, extPos);

    TriggerSaveNotification(baseName + " (" + savedFiles + ")");
}
void DebugEditor::SaveSingleObject() {
    if (!selectedObject_) return;

    // シリアライザーに丸投げ！
    serializer_.UpdateObjectInSceneJSON(selectedObject_, std::string(currentSceneFilename_));

    DebugConsole::GetInstance()->AddLog("Saved SINGLE Object: " + selectedObject_->GetName());
    TriggerSaveNotification(std::string(currentSceneFilename_));
}

// 複製 (スマート・コピペ版)
void DebugEditor::DuplicateSelected() {
    if (!selectedObject_ || !sceneManager_->GetCurrentScene()) return;

    // 1. 完全なクローンを作成
    std::unique_ptr<Object3d> newObj = selectedObject_->Clone();

    // 2. 名前変更
    static int duplicateCount = 0;
    newObj->SetName(selectedObject_->GetName() + "_Copy" + std::to_string(duplicateCount++));

    // =========================================================
    //  マウスカーソルの位置(レイキャスト)を計算してペースト！
    // =========================================================
    Math math;
    Ray ray = ScreenPointToRay(gameViewMousePos_);
    Vector3 finalPos = { 0, 0, 0 };
    bool found = false;

    // A. まず、他のオブジェクトの表面にマウスポインタが乗っているか判定
    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
    RayResult best; best.isHit = false; best.distance = 1e5f;
    for (auto& obj : objects) {
        // 自分自身、カーソル、非表示のものは無視
        if (obj.get() == selectedObject_ || obj->GetName() == "Cursor" || obj->GetName() == "Line" || !obj->GetIsVisible()) continue;

        Matrix4x4 wm = obj->GetWorldMatrix();
        Vector3 wp = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };
        Vector3 ws = obj->GetTransform()->scale;
        RayResult tmp;
        if (math.IntersectRayAABB(ray, wp - ws, wp + ws, &tmp)) {
            if (tmp.distance < best.distance) best = tmp;
        }
    }

    if (best.isHit) {
        // オブジェクトに当たった場合、その表面に置く（めり込まないように高さを足す）
        finalPos = best.point;
        finalPos.y += newObj->GetTransform()->scale.y;
        found = true;
    }
    else {
        // 1. コピー元（選択中）のワールド座標を取得
        Matrix4x4 sourceWm = selectedObject_->GetWorldMatrix();
        Vector3 sourcePos = { sourceWm.m[3][0], sourceWm.m[3][1], sourceWm.m[3][2] };

        // 2. カメラ位置(ray.origin)からコピー元までの距離を計算
        float diffX = sourcePos.x - ray.origin.x;
        float diffY = sourcePos.y - ray.origin.y;
        float diffZ = sourcePos.z - ray.origin.z;
        float distToRef = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);

        // 3. マウスクリックしたレイの方向に、その距離だけ進んだ場所を新しい点とする
        Vector3 rayDir = math.Normalize(ray.diff);
        finalPos = { ray.origin.x + rayDir.x * distToRef,
                     ray.origin.y + rayDir.y * distToRef,
                     ray.origin.z + rayDir.z * distToRef };
        found = true;
    }


    // C. 座標の最終決定
    if (found) {
        // グリッドスナップがONなら、その位置でスナップさせる
        if (isGridSnapEnabled_) {
            finalPos.x = std::round(finalPos.x / snapValue_) * snapValue_;
            finalPos.z = std::round(finalPos.z / snapValue_) * snapValue_;
        }
        newObj->GetTransform()->translate = finalPos;
        DebugConsole::GetInstance()->AddLog("Smart Pasted at Mouse Cursor!");
    }
    else {
        // カーソルが空を向いていた等でレイが当たらなかった場合の救済措置（今まで通り横にずらす）
        newObj->GetTransform()->translate.x += 2.0f;
        DebugConsole::GetInstance()->AddLog("Pasted at offset (Ray missed).");
    }
    // =========================================================

    // 行列更新
    newObj->UpdateWorldMatrix();

    // 4. 追加
    Object3d* ptr = newObj.get();
    sceneManager_->GetCurrentScene()->AddObject(std::move(newObj));

    // 5. 選択を新しい方に切り替え
    selectedObject_ = ptr;
}
// 削除
void DebugEditor::DeleteSelected() {
    if (!selectedObject_ || !sceneManager_->GetCurrentScene()) return;

    // ：Undo/Redoスタックから、削除されるオブジェクトの履歴を安全に消去する
    undoStack_.erase(
        std::remove_if(undoStack_.begin(), undoStack_.end(),
            [this](const TransformCommand& cmd) { return cmd.target == selectedObject_; }),
        undoStack_.end()
    );
    redoStack_.erase(
        std::remove_if(redoStack_.begin(), redoStack_.end(),
            [this](const TransformCommand& cmd) { return cmd.target == selectedObject_; }),
        redoStack_.end()
    );

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

// ========================================================================
// セーブ通知のトリガー
// ========================================================================
void DebugEditor::TriggerSaveNotification(const std::string& filename) {
#ifdef USE_IMGUI
    saveNotificationTimer_ = 1.0f; // 1.5秒間画面に表示する
    saveNotificationMsg_ = "[ " + filename + " ] にセーブが完了しました！";
#endif
}


// ========================================================================
// セーブ通知の描画処理 (フラッシュ & メッセージ)
// ========================================================================
void DebugEditor::DrawSaveNotification() {
#ifdef USE_IMGUI
    if (saveNotificationTimer_ <= 0.0f) return;

    // タイマー減算 (ImGuiのDeltaTimeを利用してフレームレート非依存に)
    saveNotificationTimer_ -= ImGui::GetIO().DeltaTime;

    // フェードアウトの割合 (1.0 -> 0.0 に向かって減っていく)
    float fadeRatio = saveNotificationTimer_ / 1.5f;
    if (fadeRatio > 1.0f) fadeRatio = 1.0f;

    // 最前面レイヤーを取得
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    // 1. 全画面の白いフラッシュ描画
    int whiteAlpha = (int)(fadeRatio * 80.0f);
    drawList->AddRectFilled(ImVec2(0, 0), displaySize, IM_COL32(255, 255, 255, whiteAlpha));

    // 2. 文字の描画 (同じく最前面レイヤーに直接描く)
    float fontSize = ImGui::GetFontSize() * 2.0f; // 文字を2倍サイズに
    const char* text = saveNotificationMsg_.c_str();

    // 文字のピクセルサイズを計算して、画面中央の座標を求める
    ImVec2 baseTextSize = ImGui::CalcTextSize(text);
    ImVec2 textSize = ImVec2(baseTextSize.x * 2.0f, baseTextSize.y * 2.0f);
    ImVec2 textPos = ImVec2((displaySize.x - textSize.x) * 0.5f, (displaySize.y - textSize.y) * 0.5f);

    // 文字のアルファ値 (フェードに合わせて透明になっていく)
    int textAlpha = (int)(fadeRatio * 255.0f);
    ImU32 textColor = IM_COL32(40, 40, 40, textAlpha);         // 濃いグレー
    ImU32 outlineColor = IM_COL32(255, 255, 255, textAlpha);   // 白いフチ取り

    // 少し文字を見やすくするために白い縁取り（シャドウ）を先に描画
    drawList->AddText(ImGui::GetFont(), fontSize, ImVec2(textPos.x + 2, textPos.y + 2), outlineColor, text);
    // メインの文字を描画
    drawList->AddText(ImGui::GetFont(), fontSize, textPos, textColor, text);
#endif
}


// ========================================================================
// 3D座標をGameViewのスクリーン座標に変換
// ========================================================================
Vector3 DebugEditor::WorldToScreen(const Vector3& worldPos) {
    Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
    if (!cam) return { 0, 0, -1 }; // カメラが無い場合はZを-1(無効)にして返す

    Matrix4x4 viewProj = math_.Multiply(cam->GetViewMatrix(), cam->GetProjectionMatrix());

    // NDC座標系（-1.0 ～ 1.0）への変換
    Vector3 ndc = math_.Transform(worldPos, viewProj);

    // カメラの「後ろ」にある場合はZが0未満、またはW除算の関係で範囲外になる
    if (ndc.z < 0.0f || ndc.z > 1.0f) {
        return { 0, 0, -1 }; // 描画しないフラグとしてZに-1を入れる
    }

    // NDCからGameViewのピクセル座標へ変換
    float screenX = gameViewOffset_.x + (ndc.x + 1.0f) * 0.5f * gameViewSize_.x;
    float screenY = gameViewOffset_.y + (1.0f - ndc.y) * 0.5f * gameViewSize_.y;

    return { screenX, screenY, ndc.z };
}

// ========================================================================
// 3D空間へのアイコンオーバーレイ描画
// ========================================================================
void DebugEditor::Draw3DIcons() {
#ifdef USE_IMGUI
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    // 最前面に描画するためのImGuiレイヤーを取得
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();

    for (const auto& obj : objects) {
        if (!obj->GetIsVisible()) continue;
        std::string className = obj->GetClassName();
        const char* iconStr = nullptr;
        ImU32 iconColor = IM_COL32(255, 255, 255, 200); // デフォルト白（半透明）

        // クラスごとに表示するアイコンを変える！
        if (className == "InvisibleBox") {
            //  名前でトリガーか当たり判定かを区別する！
            if (obj->GetName().find("Trigger") != std::string::npos) {
                iconStr = ICON_FA_BOLT; // 雷マーク (トリガー用)
                iconColor = IM_COL32(255, 255, 0, 200); // 黄色
            }
            else {
                iconStr = ICON_FA_SHIELD_ALT; // 盾マーク (当たり判定用)
                iconColor = IM_COL32(100, 200, 255, 200); // 水色
            }
        }
        else if (className == "Spawner") {
            iconStr = ICON_FA_BOX_OPEN;
            iconColor = IM_COL32(255, 150, 50, 200);  // オレンジ
        }
        else if (className == "CinematicCamera") {
            iconStr = ICON_FA_VIDEO;
            iconColor = IM_COL32(255, 100, 255, 200); // ピンク
        }

        // アイコンが設定されたオブジェクトのみ処理
        if (iconStr) {
            // オブジェクトのワールド座標を取得
            Vector3 worldPos = { obj->GetWorldMatrix().m[3][0], obj->GetWorldMatrix().m[3][1], obj->GetWorldMatrix().m[3][2] };

            // カメラの場合は、頭上にアイコンを出すためにY軸を少し上げる
            if (className == "CinematicCamera") {
                worldPos.y += 1.5f;
            }

            // 3D -> 2D 変換
            Vector3 screenPos = WorldToScreen(worldPos);

            // Zが0以上の時だけ（カメラの前にいる時だけ）描画
            if (screenPos.z >= 0.0f) {
                // 選択中のオブジェクトはアイコンを少し大きく、不透明にする
                float fontSize = (selectedObject_ == obj.get()) ? ImGui::GetFontSize() * 1.5f : ImGui::GetFontSize() * 1.2f;
                if (selectedObject_ == obj.get()) iconColor = IM_COL32(255, 255, 0, 255); // 選択中は黄色

                // 影（アウトライン）を黒で描画して見やすくする
                ImVec2 pos = ImVec2(screenPos.x, screenPos.y);
                drawList->AddText(ImGui::GetFont(), fontSize, ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 255), iconStr);
                // 本体の描画
                drawList->AddText(ImGui::GetFont(), fontSize, pos, iconColor, iconStr);
            }
        }
    }
#endif
}

// ==========================================
//  一発・床ピタッ！ (接地機能)
// ==========================================
void DebugEditor::DropToFloor() {
    if (!selectedObject_ || !sceneManager_->GetCurrentScene()) return;

    Math math;
    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();

    // 1. 現在のワールド座標を取得
    Matrix4x4 wm = selectedObject_->GetWorldMatrix();
    Vector3 currentPos = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };

    // 2. 真下に向かってレイ(光線)を飛ばす
    Ray ray;
    ray.origin = currentPos;
    ray.diff = { 0.0f, -1000.0f, 0.0f }; // 下方向へ1000メートル

    RayResult bestHit;
    bestHit.isHit = false;
    bestHit.distance = 1e5f;

    // 3. めり込み防止のための「足元までのオフセット(高さの半分)」を計算
    float yOffset = 0.0f;
    ColliderType type = selectedObject_->GetColliderType();
    if (type == ColliderType::kAABB || type == ColliderType::kOBB) {
        yOffset = selectedObject_->GetColliderConfig().size.y;
    }
    else if (type == ColliderType::kSphere) {
        yOffset = selectedObject_->GetColliderConfig().size.x; // 球体はXを半径としている想定
    }
    else {
        // コライダーが無い場合はスケールのYを基準にする
        yOffset = selectedObject_->GetTransform()->scale.y;
    }

    // 4. 真下にある足場（他のオブジェクト）を探す
    for (auto& obj : objects) {
        if (obj.get() == selectedObject_) continue; // 自分自身は無視
        if (obj->GetName() == "Cursor" || obj->GetName() == "Line") continue;
        if (!obj->GetIsVisible()) continue; // 非表示オブジェクトはすり抜ける

        Matrix4x4 targetWm = obj->GetWorldMatrix();
        Vector3 wp = { targetWm.m[3][0], targetWm.m[3][1], targetWm.m[3][2] };
        Vector3 ws = obj->GetTransform()->scale;

        RayResult tmp;
        // AABBで簡易的に衝突判定
        if (math.IntersectRayAABB(ray, wp - ws, wp + ws, &tmp)) {
            if (tmp.distance < bestHit.distance) {
                bestHit = tmp;
            }
        }
    }

    // 5. Undo(Ctrl+Z) 用に移動前の状態を保存
    TransformCommand cmd;
    cmd.target = selectedObject_;
    cmd.oldTf = *selectedObject_->GetTransform();

    // 6. 実際の移動処理
    if (bestHit.isHit) {
        // 真下にオブジェクトがあった場合、その表面に乗る
        selectedObject_->GetTransform()->translate.y = bestHit.point.y + yOffset;
        DebugConsole::GetInstance()->AddLog("Dropped to Object!");
    }
    else {
        // 真下に何もない場合は、Y=0 の床に乗る
        selectedObject_->GetTransform()->translate.y = yOffset;
        DebugConsole::GetInstance()->AddLog("Dropped to Floor (Y=0)!");
    }

    // 7. Undo履歴の登録と行列更新
    cmd.newTf = *selectedObject_->GetTransform();
    undoStack_.push_back(cmd);
    redoStack_.clear();

    selectedObject_->UpdateLocalMatrix();
    selectedObject_->UpdateWorldMatrix();
}
// ========================================================================
// ★ 指定したモデルをマウス位置(GameView)に即座に配置する
// ========================================================================
void DebugEditor::InstantiateModelAtCursor(const std::string& modelName) {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    BaseScene* currentScene = sceneManager_->GetCurrentScene();

    // 1. オブジェクト生成＆モデル読み込み
    auto newObj = std::make_unique<Object3d>();
    newObj->Initialize(currentScene->GetObject3dCommon());
    ModelManager::GetInstance()->LoadModel(modelName);
    newObj->SetModel(modelName);
    newObj->SetClassName("Model");
    newObj->SetName(modelName + "_" + std::to_string(currentScene->GetObjects().size()));

    // 2. マウス座標からレイキャスト
    Math math;
    Ray ray = ScreenPointToRay(gameViewMousePos_);
    Vector3 finalPos = { 0, 0, 0 };
    bool found = false;

    // 他のオブジェクトの上に乗せられるかチェック
    auto& objects = currentScene->GetObjects();
    RayResult best; best.isHit = false; best.distance = 1e5f;
    for (auto& obj : objects) {
        if (obj->GetName() == "Cursor" || obj->GetName() == "Line" || !obj->GetIsVisible()) continue;
        Matrix4x4 wm = obj->GetWorldMatrix();
        Vector3 wp = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };
        Vector3 ws = obj->GetTransform()->scale;
        RayResult tmp;
        if (math.IntersectRayAABB(ray, wp - ws, wp + ws, &tmp)) {
            if (tmp.distance < best.distance) best = tmp;
        }
    }

    // =======================================================
    // ★ 修正: 高さと「距離」を考慮して配置場所を決定する！
    // =======================================================
    float yOffset = newObj->GetColliderConfig().size.y;
    if (yOffset == 0.0f) yOffset = newObj->GetTransform()->scale.y;

    if (best.isHit) {
        // オブジェクトに当たった場合はその上に乗せる
        finalPos = best.point;
        finalPos.y += yOffset;
        found = true;
    }
    else {
        // 当たらなかった場合、地面（Y=0）との交点を計算
        if (IntersectRayPlane(ray, finalPos)) {
            // ★ 追加: カメラから交点までの「距離」を計算！
            float dx = finalPos.x - ray.origin.x;
            float dy = finalPos.y - ray.origin.y;
            float dz = finalPos.z - ray.origin.z;
            float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

            // ★ 距離が遠すぎる（20m以上先）なら、強制的にカメラの前方10mの空中に置く
            if (distance > 20.0f) {
                finalPos = ray.origin + math.Normalize(ray.diff) * 10.0f;
            }
            else {
                finalPos.y = yOffset; // 近ければ地面に乗せる
            }
            found = true;
        }
        else {
            // 空を向いていた場合は、カメラの前方10mに置く
            finalPos = ray.origin + math.Normalize(ray.diff) * 10.0f;
            found = true;
        }
    }

    // 座標の確定とスナップ適用
    if (found) {
        if (isGridSnapEnabled_) {
            finalPos.x = std::round(finalPos.x / snapValue_) * snapValue_;
            finalPos.z = std::round(finalPos.z / snapValue_) * snapValue_;
        }
        newObj->GetTransform()->translate = finalPos;
    }

    newObj->UpdateLocalMatrix();
    newObj->UpdateWorldMatrix();

    // 4. シーンに追加して、即座に選択状態にする
    Object3d* ptr = newObj.get();
    currentScene->AddObject(std::move(newObj));
    SetSelectedObject(ptr);
    EditorManager::GetInstance()->SetSelectedObject(this);

    DebugConsole::GetInstance()->AddLog("Dropped 3D Model: " + modelName);
}

void DebugEditor::InstantiatePresetAtCursor(const std::string& presetName) {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    BaseScene* currentScene = sceneManager_->GetCurrentScene();

    // 1. オブジェクト生成とプリセット適用
    auto newObj = std::make_unique<Object3d>();
    newObj->Initialize(currentScene->GetObject3dCommon());
    
    // プリセットの設定（モデル、色、パラメータ等）を流し込む
    PresetManager::GetInstance()->ApplyPresetToObject(presetName, newObj.get());
    
    // 名前をプリセット名ベースにする
    newObj->SetName(presetName + "_" + std::to_string(currentScene->GetObjects().size()));

    // 2. マウス座標からのレイキャスト（配置場所の決定）
    Math math;
    Ray ray = ScreenPointToRay(gameViewMousePos_);
    Vector3 finalPos = { 0, 0, 0 };
    bool found = false;

    auto& objects = currentScene->GetObjects();
    RayResult best; best.isHit = false; best.distance = 1e5f;
    for (auto& obj : objects) {
        if (obj->GetName() == "Cursor" || obj->GetName() == "Line" || !obj->GetIsVisible()) continue;
        Matrix4x4 wm = obj->GetWorldMatrix();
        Vector3 wp = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };
        Vector3 ws = obj->GetTransform()->scale;
        RayResult tmp;
        if (math.IntersectRayAABB(ray, wp - ws, wp + ws, &tmp)) {
            if (tmp.distance < best.distance) best = tmp;
        }
    }

    float yOffset = newObj->GetColliderConfig().size.y;
    if (yOffset == 0.0f) yOffset = newObj->GetTransform()->scale.y;

    if (best.isHit) {
        finalPos = best.point;
        finalPos.y += yOffset;
        found = true;
    }
    else {
        if (IntersectRayPlane(ray, finalPos)) {
            float dx = finalPos.x - ray.origin.x;
            float dy = finalPos.y - ray.origin.y;
            float dz = finalPos.z - ray.origin.z;
            float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

            if (distance > 20.0f) {
                finalPos = ray.origin + math.Normalize(ray.diff) * 10.0f;
            } else {
                finalPos.y = yOffset;
            }
            found = true;
        } else {
            finalPos = ray.origin + math.Normalize(ray.diff) * 10.0f;
            found = true;
        }
    }

    // 3. 座標確定とスナップ
    if (found && isGridSnapEnabled_) {
        finalPos.x = std::round(finalPos.x / snapValue_) * snapValue_;
        finalPos.z = std::round(finalPos.z / snapValue_) * snapValue_;
    }
    newObj->GetTransform()->translate = finalPos;
    newObj->UpdateWorldMatrix();

    // 4. シーンに追加
    Object3d* ptr = newObj.get();
    currentScene->AddObject(std::move(newObj));
    SetSelectedObject(ptr);
    EditorManager::GetInstance()->SetSelectedObject(this);

    DebugConsole::GetInstance()->AddLog("Dropped Preset: " + presetName);
}