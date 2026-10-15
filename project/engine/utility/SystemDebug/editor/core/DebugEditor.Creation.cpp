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
#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include "GhostRecorder.h" 
#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.h"
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
#include "engine/utility/SystemDebug/editor/core/preset/PresetManager.h"
#include <PresetEditor.h>
#include <MeshEffectManager.h>
#include <unordered_map>
#include <utility>
namespace fs = std::filesystem;






// ========================================================================
// 初期化
// ========================================================================

void DebugEditor::DropToFloor() {
    if (!selectedObject_ || !sceneManager_->GetCurrentScene()) return;

    engine::Math math;
    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();

    // 1. 現在のワールド座標を取得
    engine::Matrix4x4 wm = selectedObject_->GetWorldMatrix();
    engine::Vector3 currentPos = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };

    // 2. 真下に向かってレイ(光線)を飛ばす
    engine::Ray ray;
    ray.origin = currentPos;
    ray.diff = { 0.0f, -1000.0f, 0.0f }; // 下方向へ1000メートル

    engine::RayResult bestHit;
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
        if (!obj->GetIsRenderVisible()) continue; // 非表示オブジェクトはすり抜ける

        engine::Matrix4x4 targetWm = obj->GetWorldMatrix();
        engine::Vector3 wp = { targetWm.m[3][0], targetWm.m[3][1], targetWm.m[3][2] };
        engine::Vector3 ws = obj->GetTransform()->scale;

        engine::RayResult tmp;
        // AABBで簡易的に衝突判定
        if (math.IntersectRayAABB(ray, wp - ws, wp + ws, &tmp)) {
            if (tmp.distance < bestHit.distance) {
                bestHit = tmp;
            }
        }
    }

    // 5. Undo(Ctrl+Z) 用に移動前の状態を保存
    nlohmann::json beforeState = CaptureObjectState(selectedObject_);

    // 6. 実際の移動処理
    if (bestHit.isHit) {
        // 真下にオブジェクトがあった場合、その表面に乗る
        selectedObject_->SetTranslateY(bestHit.point.y + yOffset);
        DebugConsole::GetInstance()->AddLog("Dropped to Object!");
    }
    else {
        // 真下に何もない場合は、Y=0 の床に乗る
        selectedObject_->SetTranslateY(yOffset);
        DebugConsole::GetInstance()->AddLog("Dropped to Floor (Y=0)!");
    }

    selectedObject_->UpdateLocalMatrix();
    selectedObject_->UpdateWorldMatrix();
    RegisterObjectEdited(selectedObject_, beforeState, "Drop To Floor");
}

// 指定したモデルをマウス位置(GameView)に配置
// ========================================================================
void DebugEditor::SplitSelectedModelIntoMeshChildren() {
    // 元Objectは親として残し、Meshごとの描画Objectだけを子へ切り分ける。
    if (!selectedObject_ || !sceneManager_ || !sceneManager_->GetCurrentScene()) {
        return;
    }

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    Object3dCommon* objectCommon = currentScene->GetObject3dCommon();
    Model* sourceModel = selectedObject_->GetModel();
    const std::string sourceModelName = selectedObject_->GetModelName();

    if (!objectCommon || !sourceModel || sourceModelName.empty()) {
        DebugConsole::GetInstance()->AddLog("Mesh split skipped: selected object has no model.");
        return;
    }

    const uint32_t meshCount = sourceModel->GetMeshCount();
    if (meshCount <= 1) {
        DebugConsole::GetInstance()->AddLog("Mesh split skipped: model has only one mesh.");
        return;
    }

    if (selectedObject_->IsMeshDrawFiltered()) {
        DebugConsole::GetInstance()->AddLog("Mesh split skipped: selected object is already a mesh part.");
        return;
    }

    engine::Object3d* rootObject = selectedObject_;
    const std::string rootName = rootObject->GetName().empty() ? "MeshRoot" : rootObject->GetName();
    const nlohmann::json beforeState = CaptureObjectState(rootObject);

    // 分割後も見た目が変わらないよう、ルートのMaterial設定を各Meshへ複製する。
    const std::string saveCategory = rootObject->GetSaveCategory();
    const engine::Vector4 color = rootObject->GetColor();
    const BlendMode blendMode = rootObject->GetBlendMode();
    const int32_t materialType = rootObject->GetMaterialType();
    const float metallic = rootObject->GetMetallic();
    const float roughness = rootObject->GetRoughness();
    const bool enableNormalMap = rootObject->GetEnableNormalMap();
    const std::string normalMapPath = rootObject->GetNormalMapPath();
    const std::string ormMapPath = rootObject->GetOrmMapPath();
    const std::string texturePath = rootObject->GetTexturePath();
    const engine::Vector2 textureTiling = rootObject->GetTextureTiling();
    const bool autoTextureTiling = rootObject->GetAutoTextureTiling();
    const bool enableLighting = rootObject->GetEnableLighting();
    const bool enableEnvMap = rootObject->GetEnableEnvMap();
    const float envIntensity = rootObject->GetEnvIntensity();
    const float emissive = rootObject->GetEmissive();

    bool hasWaterParam = false;
    MeshRenderer::WaterParamForGPU waterParam{};
    if (rootObject->GetMeshRenderer() && rootObject->GetMeshRenderer()->GetWaterParamData()) {
        waterParam = *rootObject->GetMeshRenderer()->GetWaterParamData();
        hasWaterParam = true;
    }

    std::vector<std::unique_ptr<engine::Object3d>> meshChildren;
    std::vector<std::string> reservedNames;
    meshChildren.reserve(meshCount);

    for (uint32_t meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
        // 子は該当Meshだけ描画し、移動・回転はルートから継承させる。
        auto child = std::make_unique<engine::Object3d>();
        child->Initialize(objectCommon);

        const std::string childBaseName = rootName + "_mesh" + std::to_string(meshIndex);
        const std::string childName = MakeUniquePresetObjectName(currentScene, childBaseName, reservedNames);
        reservedNames.push_back(childName);

        child->SetName(childName);
        child->SetClassName("MeshPart");
        child->SetSaveCategory(saveCategory);
        child->SetModel(sourceModelName);
        child->SetMeshDrawIndex(static_cast<int>(meshIndex));
        child->SetColor(color);
        child->SetBlendMode(blendMode);
        child->SetMaterialType(materialType);
        child->SetMetallic(metallic);
        child->SetRoughness(roughness);
        child->SetEnableNormalMap(enableNormalMap);
        child->SetNormalMap(normalMapPath);
        child->SetOrmMap(ormMapPath);
        child->SetTexture(texturePath);
        child->SetTextureTiling(textureTiling);
        child->SetAutoTextureTiling(autoTextureTiling);
        child->SetEnableLighting(enableLighting);
        child->SetEnableEnvMap(enableEnvMap);
        child->SetEnvIntensity(envIntensity);
        child->SetEmissive(emissive);
        if (hasWaterParam && child->GetMeshRenderer() && child->GetMeshRenderer()->GetWaterParamData()) {
            *child->GetMeshRenderer()->GetWaterParamData() = waterParam;
        }

        child->SetColliderType(ColliderType::kNone);
        child->SetCollisionAttribute(0);
        child->SetCollisionMask(0);
        child->SetStatic(rootObject->IsStatic());
        child->SetTranslate({ 0.0f, 0.0f, 0.0f });
        child->SetRotation({ 0.0f, 0.0f, 0.0f });
        child->SetScale({ 1.0f, 1.0f, 1.0f });
        child->SetParent(rootObject);
        child->UpdateLocalMatrix();
        child->UpdateWorldMatrix();

        meshChildren.push_back(std::move(child));
    }

    rootObject->SetClassName("MeshRoot");
    // ルートの描画とColliderを外し、子との二重描画・二重判定を防ぐ。
    rootObject->SetModel(nullptr);
    rootObject->SetMeshDrawIndex(-1);
    rootObject->SetColliderType(ColliderType::kNone);
    rootObject->SetCollisionAttribute(0);
    rootObject->SetCollisionMask(0);
    rootObject->UpdateLocalMatrix();
    rootObject->UpdateWorldMatrix();
    RegisterObjectEdited(rootObject, beforeState, "Split Model Root");

    AddEditorObjects(std::move(meshChildren), "Split Model Mesh Parts");
    SetSelectedObject(rootObject);
    EditorManager::GetInstance()->SetSelectedObject(this);
    MarkDirtyForObject(rootObject);

    DebugConsole::GetInstance()->AddLog("Split model into mesh children: " + rootName + " (" + std::to_string(meshCount) + " meshes)");
}

void DebugEditor::InstantiateModelAtCursor(const std::string& modelName) {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    BaseScene* currentScene = sceneManager_->GetCurrentScene();

    // 1. オブジェクト生成＆モデル読み込み
    auto newObj = std::make_unique<engine::Object3d>();
    newObj->Initialize(currentScene->GetObject3dCommon());
    ModelManager::GetInstance()->LoadModel(modelName);
    newObj->SetModel(modelName);
    newObj->SetClassName("Model");
    newObj->SetName(modelName + "_" + std::to_string(currentScene->GetObjects().size()));

    // 2. マウス座標からレイキャスト
    engine::Math math;
    engine::Ray ray = ScreenPointToRay(gameViewMousePos_);
    engine::Vector3 finalPos = { 0, 0, 0 };
    bool found = false;

    // 他のオブジェクトの上に乗せられるかチェック
    auto& objects = currentScene->GetObjects();
    engine::RayResult best; best.isHit = false; best.distance = 1e5f;
    for (auto& obj : objects) {
        if (obj->GetName() == "Cursor" || obj->GetName() == "Line" || !obj->GetIsRenderVisible()) continue;
        engine::Matrix4x4 wm = obj->GetWorldMatrix();
        engine::Vector3 wp = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };
        engine::Vector3 ws = obj->GetTransform()->scale;
        engine::RayResult tmp;
        if (math.IntersectRayAABB(ray, wp - ws, wp + ws, &tmp)) {
            if (tmp.distance < best.distance) best = tmp;
        }
    }

    // 高さとカメラからの距離を考慮して配置場所を決定
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
            // カメラから交点までの「距離」を計算
            float dx = finalPos.x - ray.origin.x;
            float dy = finalPos.y - ray.origin.y;
            float dz = finalPos.z - ray.origin.z;
            float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

            // 距離が遠すぎる（20m以上先）なら、強制的にカメラの前方10mの空中に配置
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
        newObj->SetTranslate(finalPos);
    }

    newObj->UpdateLocalMatrix();
    newObj->UpdateWorldMatrix();

    // 4. シーンに追加して、即座に選択状態にする
    AddEditorObject(std::move(newObj), "Drop 3D Model");

    DebugConsole::GetInstance()->AddLog("Dropped 3D Model: " + modelName);
}

void DebugEditor::InstantiatePresetAtCursor(const std::string& presetName) {
    // プリセットに含まれる親子関係を保ったまま、ルートだけ配置点へ移動する。
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    BaseScene* currentScene = sceneManager_->GetCurrentScene();

    Object3dCommon* common = currentScene->GetObject3dCommon();
    if (!common) return;

    auto presetObjects = PresetManager::GetInstance()->CreateObjectsFromPreset(presetName, common);
    if (presetObjects.empty()) return;

    AssignPresetInstanceNames(currentScene, presetName, presetObjects, PresetRootName::PathLeaf);
    engine::Object3d* rootObject = presetObjects.front().get();

    engine::Math math;
    engine::Ray ray = ScreenPointToRay(gameViewMousePos_);
    engine::Vector3 finalPos = { 0, 0, 0 };
    bool found = false;

    auto& objects = currentScene->GetObjects();
    // カーソルRayに最も近い既存Objectの上面を配置候補にする。
    engine::RayResult best; best.isHit = false; best.distance = 1e5f;
    for (auto& obj : objects) {
        if (obj->GetName() == "Cursor" || obj->GetName() == "Line" || !obj->GetIsRenderVisible()) continue;
        engine::Matrix4x4 wm = obj->GetWorldMatrix();
        engine::Vector3 wp = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };
        engine::Vector3 ws = obj->GetTransform()->scale;
        engine::RayResult tmp;
        if (math.IntersectRayAABB(ray, wp - ws, wp + ws, &tmp)) {
            if (tmp.distance < best.distance) best = tmp;
        }
    }

    float yOffset = rootObject->GetColliderConfig().size.y;
    if (yOffset == 0.0f) yOffset = rootObject->GetTransform()->scale.y;

    if (best.isHit) {
        finalPos = best.point;
        finalPos.y += yOffset;
        found = true;
    }
    else {
        // 何も指していない場合は地面Planeかカメラ前方へ配置する。
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

    if (found && isGridSnapEnabled_) {
        finalPos.x = std::round(finalPos.x / snapValue_) * snapValue_;
        finalPos.z = std::round(finalPos.z / snapValue_) * snapValue_;
    }
    rootObject->SetTranslate(finalPos);
    rootObject->UpdateWorldMatrix();

    AddEditorObjects(std::move(presetObjects), "Drop Preset");

    DebugConsole::GetInstance()->AddLog("Dropped Preset: " + presetName);
}

void DebugEditor::InstantiatePrefabAtCursor(const std::string& prefabName) {
    // Prefabもプリセットと同じRay配置規則を使い、インスタンスの親子構造を維持する。
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    BaseScene* currentScene = sceneManager_->GetCurrentScene();

    Object3dCommon* common = currentScene->GetObject3dCommon();
    if (!common) return;

    auto prefabObjects = PresetManager::GetInstance()->CreateObjectsFromPrefab(prefabName, common);
    if (prefabObjects.empty()) return;

    AssignPresetInstanceNames(currentScene, prefabName, prefabObjects, PresetRootName::PathLeaf);
    engine::Object3d* rootObject = prefabObjects.front().get();

    engine::Math math;
    engine::Ray ray = ScreenPointToRay(gameViewMousePos_);
    engine::Vector3 finalPos = { 0, 0, 0 };
    bool found = false;

    auto& objects = currentScene->GetObjects();
    // 既存ObjectへのRayヒットを優先し、空間上で配置先を探しやすくする。
    engine::RayResult best; best.isHit = false; best.distance = 1e5f;
    for (auto& obj : objects) {
        if (obj->GetName() == "Cursor" || obj->GetName() == "Line" || !obj->GetIsRenderVisible()) continue;
        engine::Matrix4x4 wm = obj->GetWorldMatrix();
        engine::Vector3 wp = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };
        engine::Vector3 ws = obj->GetTransform()->scale;
        engine::RayResult tmp;
        if (math.IntersectRayAABB(ray, wp - ws, wp + ws, &tmp)) {
            if (tmp.distance < best.distance) best = tmp;
        }
    }

    float yOffset = rootObject->GetColliderConfig().size.y;
    if (yOffset == 0.0f) yOffset = rootObject->GetTransform()->scale.y;

    if (best.isHit) {
        finalPos = best.point;
        finalPos.y += yOffset;
        found = true;
    }
    else {
        // ヒット対象がないときは地面Planeを試し、遠すぎる点は手前へ制限する。
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

    if (found && isGridSnapEnabled_) {
        finalPos.x = std::round(finalPos.x / snapValue_) * snapValue_;
        finalPos.z = std::round(finalPos.z / snapValue_) * snapValue_;
    }
    rootObject->SetTranslate(finalPos);
    rootObject->UpdateWorldMatrix();

    AddEditorObjects(std::move(prefabObjects), "Drop Prefab");

    DebugConsole::GetInstance()->AddLog("Dropped Prefab: " + prefabName);
}


void DebugEditor::InstantiateParticleAtCursor(const std::string& particleName) {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    BaseScene* currentScene = sceneManager_->GetCurrentScene();

    // 1. オブジェクト生成とパーティクル設定
    auto newObj = std::make_unique<engine::Object3d>();
    newObj->Initialize(currentScene->GetObject3dCommon());
    
    newObj->SetModel("Stages/block"); 
    newObj->SetName("VFX_" + particleName + "_" + std::to_string(currentScene->GetObjects().size())); 
    newObj->SetClassName("GPUParticle");
    newObj->SetGPUParticleName(particleName);
    newObj->SetColor({1.0f, 0.5f, 0.0f, 0.5f});
    newObj->SetBlendMode(BlendMode::kNormal);

    // 2. マウス座標からのレイキャスト（配置場所の決定）
    engine::Math math;
    engine::Ray ray = ScreenPointToRay(gameViewMousePos_);
    engine::Vector3 finalPos = { 0, 0, 0 };
    bool found = false;

    auto& objects = currentScene->GetObjects();
    engine::RayResult best; best.isHit = false; best.distance = 1e5f;
    for (auto& obj : objects) {
        if (obj->GetName() == "Cursor" || obj->GetName() == "Line" || !obj->GetIsRenderVisible()) continue;
        engine::Matrix4x4 wm = obj->GetWorldMatrix();
        engine::Vector3 wp = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };
        engine::Vector3 ws = obj->GetTransform()->scale;
        engine::RayResult tmp;
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
    newObj->SetTranslate(finalPos);
    newObj->UpdateWorldMatrix();

    // 4. シーンに追加
    AddEditorObject(std::move(newObj), "Drop Particle");

    DebugConsole::GetInstance()->AddLog("Dropped Particle: " + particleName);

}
