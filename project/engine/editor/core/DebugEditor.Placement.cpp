#define NOMINMAX
#include "DebugEditor.h"
#include <numbers>
#include "SceneManager.h"    
#include "BaseScene.h"      
#include "engine/graphics/effect/DecalSystem.h"
#include "Object3D.h"
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
#include <limits>
#include <chrono>
#include <cctype>
#include <ctime>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include "GhostRecorder.h" 
#include "engine/editor/graphics/camera/CameraEditor.h"
#include "Transform.h"
#include "ParticleManager.h"
#include "EditorManager.h"
#include "PostEffectEditor.h"
#include "SpriteDebugEditor.h"
#include "ParticleEditor.h"
#include "GpuParticleEditor.h"
#include "VfxSequencerEditor.h"
#include "LightEditor.h"      
#include "IconsFontAwesome5.h"

#include <DebugConsole.h>
#include <CollisionManager.h>
#include <filesystem> // ファイル操作用
#include <game/system/combat/BulletManager.h>
#include "engine/editor/assets/preset/PresetManager.h"
#include <PresetEditor.h>
#include <MeshEffectManager.h>
#include <unordered_map>
#include <utility>
namespace fs = std::filesystem;





namespace {
    // 新規配置位置の持ち上げ量をCollider優先、次にTransformから求めます。
    float GetCreateYOffsetForObject(const engine::Object3D* object) {
        // 当たり判定の高さを優先し、未設定ならモデルの倍率から置く高さを推定する。
        if (!object)
            return 1.0f;

        const auto& collider = object->GetColliderConfig();
        if (collider.size.y > 0.0f)
            return collider.size.y;
        if (collider.size.x > 0.0f)
            return collider.size.x;

        const Transform& transform = object->GetTransform();
        if (transform.scale.y > 0.0f)
            return transform.scale.y;
        return 1.0f;
    }

    engine::Vector3 GetPlacementExtents(const engine::Object3D* object) {
        // ローカルの半寸法を返す。拡大率は後でワールド行列から一度だけ適用する。
        if (!object)
            return { 1.0f, 1.0f, 1.0f };

        const auto& collider = object->GetColliderConfig();
        return {
            collider.size.x > 0.0f ? collider.size.x : 1.0f,
            collider.size.y > 0.0f ? collider.size.y : 1.0f,
            collider.size.z > 0.0f ? collider.size.z : 1.0f
        };
    }

    bool IsNearlyZero(const engine::Vector3& value) {
        return std::abs(value.x) < 0.0001f &&
            std::abs(value.y) < 0.0001f &&
            std::abs(value.z) < 0.0001f;
    }

    engine::Vector3 NormalizeOrUp(const engine::Vector3& value) {
        if (IsNearlyZero(value)) {
            return { 0.0f, 1.0f, 0.0f };
        }

        return engine::Math::Normalize(value);
    }

    void ApplyEditorPreviewLightOverride(engine::Object3D* object) {
        // 配置プレビューはシーン照明の暗さに埋もれないよう自己発光で表示する。
        if (!object) {
            return;
        }

        if (auto* material = object->GetMaterialData()) {
            object->SetEnableLighting(false);
            object->SetSelectedLighting(0);
            object->SetEmissive((std::max)(material->emissive, 1.0f));
        }
    }

    float GetSurfaceOffset(const engine::Object3D* object, const engine::Vector3& normal) {
        if (object && object->IsDecal()) return object->GetDecalSettings().depthOffset;
        if (!object) return 1.0f;
        const auto world = object->GetWorldMatrix();
        auto* model = object->GetModel();
        engine::Vector3 lo, hi;
        if (model) {
            lo = model->GetLocalAabbMin(); hi = model->GetLocalAabbMax();
            // 分割されたモデルは、表示中のメッシュだけを配置物の寸法に使う。
            const int index = object->GetMeshDrawIndex();
            const auto& meshes = model->GetModelData().meshes;
            if (index >= 0 && index < static_cast<int>(meshes.size()) && !meshes[index].vertices.empty()) {
                lo = {(std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)()};
                hi = -lo;
                for (const auto& vertex : meshes[index].vertices) {
                    lo.x = (std::min)(lo.x, vertex.position.x); hi.x = (std::max)(hi.x, vertex.position.x);
                    lo.y = (std::min)(lo.y, vertex.position.y); hi.y = (std::max)(hi.y, vertex.position.y);
                    lo.z = (std::min)(lo.z, vertex.position.z); hi.z = (std::max)(hi.z, vertex.position.z);
                }
            }
        } else {
            const auto extents = GetPlacementExtents(object);
            const auto center = object->GetColliderConfig().center;
            lo = center - extents; hi = center + extents;
        }
        // 原点から各隅までのベクトルを面法線へ投影し、最も面へ近い底面を求める。
        // 位置の平行移動を除くことで、親の拡大・回転・モデルの原点ずれを保持する。
        float minimum = (std::numeric_limits<float>::max)();
        for (int bits = 0; bits < 8; ++bits) {
            const engine::Vector3 corner{bits & 1 ? hi.x : lo.x, bits & 2 ? hi.y : lo.y, bits & 4 ? hi.z : lo.z};
            minimum = (std::min)(minimum, engine::Math::Dot(engine::Math::TransformNormal(corner, world), normal));
        }
        return -minimum;
    }

    engine::Vector3 GetSurfaceAlignedRotation(const engine::Vector3& currentRotation, const engine::Vector3& normal) {
        // 面法線の主軸に合わせて設置物を回し、水平面では現在の向きを残す。
        constexpr float kHalfPi = std::numbers::pi_v<float> * 0.5f;
        engine::Vector3 n = NormalizeOrUp(normal);

        if (std::abs(n.y) >= std::abs(n.x) && std::abs(n.y) >= std::abs(n.z)) {
            if (n.y < 0.0f) {
                return { std::numbers::pi_v<float>, currentRotation.y, 0.0f };
            }

            return { 0.0f, currentRotation.y, 0.0f };
        }

        if (std::abs(n.x) >= std::abs(n.z)) {
            return { 0.0f, currentRotation.y, n.x > 0.0f ? -kHalfPi : kHalfPi };
        }

        return { n.z > 0.0f ? kHalfPi : -kHalfPi, currentRotation.y, 0.0f };
    }

    void ApplyGridSnap(engine::Vector3& position, const engine::Vector3& normal, bool hasSurface, bool enabled, float snapValue) {
        // 接地面に垂直な軸は動かさず、面に沿う座標だけ格子へ吸着させる。
        if (!enabled || snapValue <= 0.0f)
            return;

        const auto beforeSnap = position;
        if (!hasSurface || std::abs(normal.x) < 0.5f) {
            position.x = std::round(position.x / snapValue) * snapValue;
        }

        if (hasSurface && std::abs(normal.y) < 0.5f) {
            position.y = std::round(position.y / snapValue) * snapValue;
        }

        if (!hasSurface || std::abs(normal.z) < 0.5f) {
            position.z = std::round(position.z / snapValue) * snapValue;
        }
        // 斜面でも吸着後の点を同じ接平面へ戻し、面の奥へ埋め込まない。
        if (hasSurface)
            position = position - normal * engine::Math::Dot(position - beforeSnap, normal);
    }

    float Distance(const engine::Vector3& a, const engine::Vector3& b) {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        float dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
}

// ========================================================================
// 初期化
// ========================================================================

void DebugEditor::SetPreviewObject(std::unique_ptr<engine::Object3D> obj, const std::string& label) {
    // 前のプレビューの子と見た目を捨て、新しい候補の元状態を確定前に退避する。
    previewChildObjects_.clear();
    previewVisualStates_.clear();
    previewObject_ = std::move(obj);
    previewCreateCommandLabel_ = label;

    if (!previewObject_) {
        previewObjectOriginalColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
        previewObjectOriginalBlendMode_ = BlendMode::Normal;
        previewObjectOriginalMaterialType_ = 0;
        previewObjectOriginalSelectedLighting_ = 2;
        previewObjectOriginalEnableLighting_ = 1;
        previewObjectOriginalEmissive_ = 1.0f;
        previewObjectOriginalClassName_.clear();
        previewObjectOriginalModelName_.clear();
        previewObjectOriginalModel_ = nullptr;
        previewObjectOriginalColliderConfig_ = ColliderConfig{};
        previewObjectUsesFallbackModel_ = false;
        hasPreviewPlacementContact_ = false;
        return;
    }

    previewObjectOriginalColor_ = previewObject_->GetColor();
    previewObjectOriginalBlendMode_ = previewObject_->GetBlendMode();
    previewObjectOriginalMaterialType_ = previewObject_->GetMaterialType();

    if (auto* material = previewObject_->GetMaterialData()) {
        previewObjectOriginalSelectedLighting_ = material->selectedLighting;
        previewObjectOriginalEnableLighting_ = material->enableLighting;
    }
    else {
        previewObjectOriginalSelectedLighting_ = 2;
        previewObjectOriginalEnableLighting_ = 1;
    }

    previewObjectOriginalEmissive_ = previewObject_->GetEmissive();
    previewObjectOriginalClassName_ = previewObject_->GetClassName();
    previewObjectOriginalModelName_ = previewObject_->GetModelName();
    previewObjectOriginalModel_ = previewObject_->GetModel();
    previewObjectOriginalColliderConfig_ = previewObject_->GetColliderConfig();
    previewObjectUsesFallbackModel_ = false;
    hasPreviewPlacementContact_ = false;
    ApplyPreviewVisual(previewObject_.get());
}

void DebugEditor::OpenGameViewCreateContextMenu() {
#ifdef USE_IMGUI
    // 配置・ブラシ・経路編集中は右クリックメニューを重ねない。
    if (!sceneManager_ || !sceneManager_->GetCurrentScene())
        return;
    if (sceneManager_->IsPlaying())
        return;
    if (previewObject_ || isPresetBrushMode_ || isPathEditMode_)
        return;

    gameViewCreateMenuMousePos_ = gameViewMousePos_;
    gameViewCreateMenuScreenPos_ = {
        gameViewOffset_.x + gameViewMousePos_.x,
        gameViewOffset_.y + gameViewMousePos_.y
    };

    requestGameViewCreateMenu_ = true;
#endif
}

void DebugEditor::StartGameViewCreatePreview(std::unique_ptr<engine::Object3D> object, const std::string& label) {
#ifdef USE_IMGUI
    // メニューを開いた位置へ初回の候補を置き、次フレーム以降はマウスへ追従させる。
    if (!object || !sceneManager_ || !sceneManager_->GetCurrentScene())
        return;
    if (sceneManager_->IsPlaying())
        return;

    if (isPresetBrushMode_) {
        StopPresetBrush();
    }

    gameViewMousePos_ = gameViewCreateMenuMousePos_;
    SetPreviewObject(std::move(object), label);

    if (previewObject_) {
        PlacementResult placement = CalculateGameViewPlacement(previewObject_.get(), gameViewCreateMenuMousePos_, false);
        ApplyGameViewPlacement(previewObject_.get(), placement, true);
        ApplyPreviewVisual(previewObject_.get());
        DebugConsole::GetInstance()->AddLog("Preview Create: " + previewObject_->GetName());
    }
#else
    (void)object;
    (void)label;
#endif
}

void DebugEditor::StartGameViewCreatePreview(std::vector<std::unique_ptr<engine::Object3D>> objects, const std::string& label) {
#ifdef USE_IMGUI
    // 複合プリセットの先頭を配置基準とし、残りも確定までプレビュー所有にする。
    if (objects.empty())
        return;

    std::unique_ptr<engine::Object3D> root = std::move(objects.front());
    StartGameViewCreatePreview(std::move(root), label);
    if (!previewObject_)
        return;

    previewChildObjects_.reserve(objects.size() > 0 ? objects.size() - 1 : 0);

    for (size_t index = 1; index < objects.size(); ++index) {
        if (objects[index]) {
            previewChildObjects_.push_back(std::move(objects[index]));
            ApplyPreviewVisual(previewChildObjects_.back().get());
        }
    }

    previewObject_->UpdateLocalMatrix();
    previewObject_->UpdateWorldMatrix();
#else
    (void)objects;
    (void)label;
#endif
}

void DebugEditor::DrawGameViewCreateContextMenu() {
#ifdef USE_IMGUI
    // Scene の作成メニューと、指定位置からの試験プレイを同じ右クリック位置へ出す。
    if (!sceneManager_ || !sceneManager_->GetCurrentScene())
        return;

    constexpr const char* kPopupName = "GameViewCreateContextMenu";

    if (requestGameViewCreateMenu_) {
        ImGui::OpenPopup(kPopupName);
        requestGameViewCreateMenu_ = false;
    }

    ImGui::SetNextWindowPos(
        ImVec2(gameViewCreateMenuScreenPos_.x, gameViewCreateMenuScreenPos_.y),
        ImGuiCond_Appearing);

    if (ImGui::BeginPopup(kPopupName)) {
        hierarchyWindow_.DrawCreateContextMenu(sceneManager_->GetCurrentScene(), true);

        if (playFromPositionCallback_) {
            ImGui::Separator();

            if (ImGui::MenuItem("プレイヤーをここから開始")) {
                engine::Vector3 position = CalculateGameViewCreatePosition(nullptr);
                position.y += 1.0f;
                playFromPositionCallback_(position, "GameView指定位置");
            }

            if (selectedObject_ && ImGui::MenuItem("選択Object付近から開始")) {
                engine::Vector3 position = selectedObject_->GetWorldPosition();
                position.y += 1.0f;
                position.z += 2.0f;
                playFromPositionCallback_(position, "選択Object: " + selectedObject_->GetName());
            }

            if (ImGui::BeginMenu("チェックポイントから開始")) {
                bool found = false;

                for (const auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
                    if (!object || object->GetEventType() != EventType::Checkpoint)
                        continue;
                    found = true;
                    engine::Vector3 position = object->GetWorldPosition();
                    position.y += 1.0f;

                    if (ImGui::MenuItem(object->GetName().c_str())) {
                        playFromPositionCallback_(position, "Checkpoint: " + object->GetName());
                    }
                }

                if (!found)
                    ImGui::TextDisabled("Checkpoint EventのObjectがありません");
                ImGui::EndMenu();
            }
        }

        ImGui::EndPopup();
    }
#endif
}

engine::Vector3 DebugEditor::CalculateGameViewCreatePosition(const engine::Object3D* object) {
    // レイで配置先を得られない場合だけ、Object 固有の生成高さへ戻す。
    PlacementResult placement = CalculateGameViewPlacement(object, gameViewCreateMenuMousePos_, false);
    if (placement.found)
        return placement.position;
    return { 0.0f, GetCreateYOffsetForObject(object), 0.0f };
}

DebugEditor::PlacementResult DebugEditor::CalculateGameViewPlacement(const engine::Object3D* object, const engine::Vector2& mousePos, bool useGridSnap,
    const std::vector<std::unique_ptr<engine::Object3D>>* group,
    const std::unordered_set<std::string>* ignoredRootGuids) {
    // 選択と同じ可視モデルの面を使い、Colliderや仮の単位箱を配置先にしない。
    PlacementResult result;

    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) {
        result.position = { 0.0f, GetCreateYOffsetForObject(object), 0.0f };
        result.found = true;
        return result;
    }

    engine::Math math;
    engine::Ray ray = ScreenPointToRay(mousePos);

    if (IsNearlyZero(ray.diff)) {
        return result;
    }

    engine::RayResult best{};
    RaycastSceneSurface(ray, best, nullptr, object, false, ignoredRootGuids);

    auto offsetForSurface = [&](const engine::Vector3& normal) {
        if (!object || !group) return GetSurfaceOffset(object, normal);
        const auto origin = object->GetWorldPosition();
        float offset = (std::numeric_limits<float>::lowest)();
        bool hasModel = false;
        // 空の親を持つPrefabも、見えている子全体の底面を面へ合わせる。
        for (const auto& child : *group) {
            if (!child || !child->GetModel() || !child->GetIsRenderVisible()) continue;
            hasModel = true;
            offset = (std::max)(offset, GetSurfaceOffset(child.get(), normal) -
                engine::Math::Dot(child->GetWorldPosition() - origin, normal));
        }
        return hasModel ? offset : GetSurfaceOffset(object, normal);
    };

    float surfaceOffset = 0.0f;

    // 何にも当たらなければ基準平面、平面にも届かなければレイ上の仮位置を使う。
    if (best.isHit) {
        result.normal = NormalizeOrUp(best.normal);
        result.contactPosition = best.point;
        surfaceOffset = offsetForSurface(result.normal);
        result.position = result.contactPosition + result.normal * surfaceOffset;
        result.found = true;
        result.hasSurface = true;
    }
    else if (IntersectRayPlane(ray, result.contactPosition)) {
        result.normal = { 0.0f, 1.0f, 0.0f };
        surfaceOffset = offsetForSurface(result.normal);
        result.position = result.contactPosition + result.normal * surfaceOffset;
        result.found = true;
        result.hasSurface = true;
    }
    else {
        result.normal = { 0.0f, 1.0f, 0.0f };
        result.position = ray.origin + math.Normalize(ray.diff) * 10.0f;
        result.contactPosition = result.position;
        result.found = true;
        result.hasSurface = false;
    }

    // 吸着後の位置から接点を逆算し、デカールの面合わせにも同じ点を渡す。
    ApplyGridSnap(result.position, result.normal, result.hasSurface, useGridSnap && isGridSnapEnabled_, snapValue_);
    result.contactPosition = result.hasSurface ? result.position - result.normal * surfaceOffset : result.position;
    return result;
}

DebugEditor::PlacementResult DebugEditor::PlaceGameViewObjects(
    std::vector<std::unique_ptr<engine::Object3D>>& objects, const engine::Vector2& mousePos, bool useGridSnap,
    const std::unordered_set<std::string>* ignoredRootGuids) {
    if (objects.empty() || !objects.front()) return {};
    auto* root = objects.front().get();
    root->UpdateWorldMatrix();
    const auto placement = CalculateGameViewPlacement(root, mousePos, useGridSnap, &objects, ignoredRootGuids);
    if (placement.found)
        ApplyGameViewPlacement(root, placement, root->IsDecal());
    return placement;
}

void DebugEditor::ApplyGameViewPlacement(engine::Object3D* object, const PlacementResult& placement, bool alignToSurface) {
    // デカールは専用の面合わせを使い、通常 Object は法線に応じた回転だけ適用する。
    if (!object || !placement.found)
        return;

    object->SetTranslate(placement.position);

    if (alignToSurface && placement.hasSurface && object->IsDecal()) {
        DecalSystem::AlignToSurface(
            *object, placement.contactPosition, placement.normal, 0.0f,
            object->GetDecalSettings().depthOffset);
        if (object == previewObject_.get()) {
            previewPlacementContactPosition_ = placement.contactPosition;
            hasPreviewPlacementContact_ = placement.found;
        }

        return;
    }

    if (alignToSurface && placement.hasSurface) {
        object->SetRotation(GetSurfaceAlignedRotation(object->GetTransform()->rotate, placement.normal));
        object->UpdateWorldMatrix();
        object->SetTranslate(placement.contactPosition + placement.normal * GetSurfaceOffset(object, placement.normal));
    }

    if (object == previewObject_.get()) {
        previewPlacementContactPosition_ = placement.contactPosition;
        hasPreviewPlacementContact_ = placement.found;
    }

    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();

    if (object->GetMeshRenderer()) {
        object->GetMeshRenderer()->Update();
    }
}

void DebugEditor::UpdatePreviewPlacement() {
#ifdef USE_IMGUI
    // Game View 上にカーソルがある間だけ、未確定 Object の位置を再計算する。
    if (!previewObject_ || !isGameViewHovered_)
        return;

    PlacementResult placement = CalculateGameViewPlacement(previewObject_.get(), gameViewMousePos_, false);
    ApplyGameViewPlacement(previewObject_.get(), placement, true);
    ApplyPreviewVisual(previewObject_.get());
#endif
}

void DebugEditor::ConfirmPreviewPlacement() {
    // 確定前に半透明表示を戻し、複数 Object をまとめて Undo 対象の作成命令へ渡す。
    if (!previewObject_)
        return;

    std::string createdName = previewObject_->GetName();
    RestorePreviewVisual(previewObject_.get());
    previewObject_->UpdateLocalMatrix();
    previewObject_->UpdateWorldMatrix();

    std::vector<std::unique_ptr<engine::Object3D>> createdObjects;
    createdObjects.reserve(previewChildObjects_.size() + 1);
    createdObjects.push_back(std::move(previewObject_));

    for (auto& child : previewChildObjects_) {
        if (child) {
            RestorePreviewVisual(child.get());
            child->UpdateLocalMatrix();
            child->UpdateWorldMatrix();
            createdObjects.push_back(std::move(child));
        }
    }

    previewChildObjects_.clear();

    AddEditorObjects(std::move(createdObjects), previewCreateCommandLabel_);
    DebugConsole::GetInstance()->AddLog("Create: " + createdName);
    previewCreateCommandLabel_ = "Place Preview Object";
    previewVisualStates_.clear();
}

void DebugEditor::CancelPreviewPlacement() {
    // Scene に追加していない候補を破棄するため、Undo 履歴には何も積まない。
    if (!previewObject_)
        return;

    DebugConsole::GetInstance()->AddLog("Cancel Preview Create: " + previewObject_->GetName());
    previewChildObjects_.clear();
    previewObject_ = nullptr;
    previewVisualStates_.clear();
    previewCreateCommandLabel_ = "Place Preview Object";
}

void DebugEditor::StartPresetBrush(const std::string& presetName) {
#ifdef USE_IMGUI
    // 単発配置の候補を消してから連続配置へ切り替え、前回の刻印間隔をリセットする。
    if (presetName.empty() || !sceneManager_ || !sceneManager_->GetCurrentScene())
        return;
    if (sceneManager_->IsPlaying() || isPathEditMode_)
        return;

    if (previewObject_) {
        CancelPreviewPlacement();
    }

    brushStampedRootGuids_.clear();
    brushPresetName_ = presetName;
    isPresetBrushMode_ = true;
    hasLastBrushStamp_ = false;
    RebuildPresetBrushPreview();
    if (!isPresetBrushMode_)
        return;

    DebugConsole::GetInstance()->AddLog("Brush Start: " + brushPresetName_);
#else
    (void)presetName;
#endif
}

void DebugEditor::StopPresetBrush() {
    // プレビューだけ破棄し、確定済みの Object は Scene に残す。
    if (!isPresetBrushMode_)
        return;

    DebugConsole::GetInstance()->AddLog("Brush Stop: " + brushPresetName_);
    isPresetBrushMode_ = false;
    brushStampedRootGuids_.clear();
    brushPresetName_.clear();
    brushPlacement_ = {};
    brushPresetSource_ = {};
    brushPreviewObjects_.clear();
    hasLastBrushStamp_ = false;
}

void DebugEditor::RebuildPresetBrushPreview() {
    // 再生成中は前の位置を確定できない。空の親には接地用の仮モデルを追加しない。
    brushPlacement_ = {};
    brushPresetSource_ = {};
    brushPreviewObjects_.clear();
    if (!sceneManager_ || !sceneManager_->GetCurrentScene() || brushPresetName_.empty())
        return;

    BaseScene* scene = sceneManager_->GetCurrentScene();
    engine::Object3DCommon* common = scene->GetObject3DCommon();
    if (!common)
        return;

    brushPreviewObjects_ = PresetManager::GetInstance()->CreateObjectsFromPreset(brushPresetName_, common);

    if (brushPreviewObjects_.empty()) {
        StopPresetBrush();
        return;
    }

    brushPresetSource_ = PresetManager::GetInstance()->GetPresets().at(brushPresetName_);
    AssignPresetInstanceNames(scene, brushPresetName_, brushPreviewObjects_, PresetRootName::DisplayName);

    for (auto& object : brushPreviewObjects_) {
        ApplyBrushPreviewVisual(object.get());
    }
}

void DebugEditor::UpdatePresetBrush() {
#ifdef USE_IMGUI
    // カーソルへの追従と刻印を分け、Gizmo 操作中は誤配置しない。
    if (!isPresetBrushMode_)
        return;
    if (!sceneManager_ || !sceneManager_->GetCurrentScene() || sceneManager_->IsPlaying()) {
        StopPresetBrush();
        return;
    }

    engine::InputManager* input = engine::InputManager::GetInstance();

    if (input && input->IsKeyTriggered(DIK_E)) {
        StopPresetBrush();
        return;
    }

    if (isGameViewHovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        StopPresetBrush();
        return;
    }

    if (brushPreviewObjects_.empty()) {
        RebuildPresetBrushPreview();
    }

    if (brushPreviewObjects_.empty())
        return;

    // 範囲外・他のUI・Gizmo操作中は古い位置へ刻印しない。
    brushPlacement_ = {};
    if (!isGameViewHovered_ || ImGuizmo::IsOver() || ImGuizmo::IsUsing()) return;

    engine::Object3D* root = brushPreviewObjects_.front().get();
    brushPlacement_ = PlaceGameViewObjects(brushPreviewObjects_, gameViewMousePos_, true, &brushStampedRootGuids_);
    if (!brushPlacement_.found) return;

    for (auto& object : brushPreviewObjects_) {
        ApplyBrushPreviewVisual(object.get());
    }

    bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool dragging = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (!clicked && !dragging)
        return;

    engine::Vector3 stampPosition = root->GetWorldPosition();
    float spacing = std::max(brushSpacing_, 0.0f);
    bool farEnough = !hasLastBrushStamp_ || Distance(stampPosition, lastBrushStampPosition_) >= spacing;

    // ドラッグ中は前回位置との距離で間引き、クリック時は必ず一つ置く。
    if (clicked || farEnough) {
        StampPresetBrush();
    }
#endif
}

void DebugEditor::StampPresetBrush() {
    // プレビューそのものは再利用し、実配置用のインスタンスを別に生成する。
    if (!isPresetBrushMode_ || !brushPlacement_.found || !sceneManager_ || !sceneManager_->GetCurrentScene() ||
        sceneManager_->IsPlaying() || sceneManager_->IsTransitioning() || brushPresetName_.empty() || brushPreviewObjects_.empty())
        return;

    // Asset編集が見本の作成後に入ったら、先に表示を更新してから次の刻印を待つ。
    const auto& presets = PresetManager::GetInstance()->GetPresets();
    const auto source = presets.find(brushPresetName_);
    if (source == presets.end() || source->second != brushPresetSource_) {
        RebuildPresetBrushPreview();
        return;
    }

    BaseScene* scene = sceneManager_->GetCurrentScene();
    engine::Object3DCommon* common = scene->GetObject3DCommon();
    if (!common)
        return;

    auto objects = PresetManager::GetInstance()->CreateObjectsFromPreset(brushPresetName_, common);
    if (objects.empty())
        return;

    AssignPresetInstanceNames(scene, brushPresetName_, objects, PresetRootName::DisplayName);
    if (objects.size() != brushPreviewObjects_.size()) {
        RebuildPresetBrushPreview();
        return;
    }
    // 同じAssetから作った全ノードへ、見本で確認したローカル姿勢だけを渡す。
    // 表示用の色・材質はコピーせず、Quaternion優先フラグやScaleを失わない。
    for (size_t i = 0; i < objects.size(); ++i)
        objects[i]->SetLocalTransform(*brushPreviewObjects_[i]->GetTransform());
    auto* root = objects.front().get();
    root->UpdateWorldMatrix();
    const auto stampPosition = root->GetWorldPosition();

    AddEditorObjects(std::move(objects), "Brush Preset " + brushPresetName_);
    // 親のGUIDだけで子孫も除外し、次フレームに見本が自分の刻印へ登ることを防ぐ。
    brushStampedRootGuids_.insert(root->GetPersistentGuid());
    lastBrushStampPosition_ = stampPosition;
    hasLastBrushStamp_ = true;
}

void DebugEditor::DrawPresetBrushOverlay() {
#ifdef USE_IMGUI
    // Game View の左上に現在の対象と間隔を置き、配置中にも調整できるようにする。
    if (!isPresetBrushMode_)
        return;

    // 実モデルを持たない親やTriggerも、接点マーカーで位置を示す。
    // 見本用の立方体をObjectへ付けず、接地寸法と表示用の印を分離する。
    if (brushPlacement_.found) {
        const auto point = WorldToScreen(brushPlacement_.contactPosition);
        if (point.z >= 0) {
            auto* draw = ImGui::GetWindowDrawList();
            draw->PushClipRect({gameViewOffset_.x, gameViewOffset_.y},
                {gameViewOffset_.x + gameViewSize_.x, gameViewOffset_.y + gameViewSize_.y}, true);
            const ImVec2 center{point.x, point.y};
            const auto color = IM_COL32(255, 235, 100, 240);
            draw->AddCircle(center, 7.0f, color, 20, 2.0f);
            draw->AddLine({point.x - 11, point.y}, {point.x + 11, point.y}, color, 1.5f);
            draw->AddLine({point.x, point.y - 11}, {point.x, point.y + 11}, color, 1.5f);
            draw->PopClipRect();
        }
    }
    ImGui::SetNextWindowPos(ImVec2(gameViewOffset_.x + 12.0f, gameViewOffset_.y + 12.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.78f);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("プリセットブラシ", nullptr, flags)) {
        ImGui::Text("対象: %s", brushPresetName_.c_str());
        ImGui::SliderFloat("配置間隔", &brushSpacing_, 0.0f, 10.0f, "%.2f");
        ImGui::TextDisabled("左ドラッグ: 配置 / 右クリック or E: 終了");

        if (ImGui::Button("ブラシ終了")) {
            StopPresetBrush();
        }
    }

    ImGui::End();
#endif
}

void DebugEditor::ApplyBrushPreviewVisual(engine::Object3D* object) {
    // 元プリセットを編集せず、見本の Object だけ半透明・高輝度で表示する。
    if (!object || !object->GetModel())
        return;

    // クラスとモデルは元のまま保つ。面合わせに必要なDecal判定も維持する。
    object->SetMaterialType(0);
    object->SetBlendMode(BlendMode::Normal);
    engine::Vector4 color = object->GetColor();
    object->SetColor({ color.x, color.y, color.z, 0.45f });
    object->SetEmissive(std::max(object->GetEmissive(), 1.4f));
    ApplyEditorPreviewLightOverride(object);
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();

    if (object->GetMeshRenderer()) {
        object->GetMeshRenderer()->Update();
    }
}

void DebugEditor::ApplyPreviewVisual(engine::Object3D* object) {
    // 初回だけ元の材質状態を保存し、毎フレームの着色で退避値を上書きしない。
    if (!object)
        return;

    if (!previewObjectUsesFallbackModel_ && !object->GetModel()) {
        object->SetModel("Primitives/cube");
        object->SetColliderConfig(previewObjectOriginalColliderConfig_);
        previewObjectUsesFallbackModel_ = true;
    }

    if (previewVisualStates_.find(object) == previewVisualStates_.end()) {
        PreviewVisualState state;
        state.className = object->GetClassName();
        state.color = object->GetColor();
        state.blendMode = object->GetBlendMode();
        state.materialType = object->GetMaterialType();
        state.emissive = object->GetEmissive();

        if (auto* material = object->GetMaterialData()) {
            state.selectedLighting = material->selectedLighting;
            state.enableLighting = material->enableLighting;
        }

        previewVisualStates_[object] = state;
    }

    object->SetMaterialType(0);
    object->SetBlendMode(BlendMode::Normal);
    object->SetEmissive(1.25f);
    object->SetColor({ 0.45f, 1.0f, 0.68f, 0.46f });
    ApplyEditorPreviewLightOverride(object);
}

void DebugEditor::RestorePreviewVisual(engine::Object3D* object) {
    // 確定時には仮モデルとプレビュー用の材質設定を外し、作成前の見た目へ戻す。
    if (!object)
        return;

    if (previewObjectUsesFallbackModel_) {
        if (!previewObjectOriginalModelName_.empty()) {
            object->SetModel(previewObjectOriginalModelName_);
        }
        else {
            object->SetModel(previewObjectOriginalModel_);
        }

        object->SetColliderConfig(previewObjectOriginalColliderConfig_);
        previewObjectUsesFallbackModel_ = false;
    }

    const auto stateIt = previewVisualStates_.find(object);

    if (stateIt != previewVisualStates_.end()) {
        const PreviewVisualState& state = stateIt->second;
        object->SetClassName(state.className);
        object->SetMaterialType(state.materialType);
        object->SetBlendMode(state.blendMode);

        if (auto* material = object->GetMaterialData()) {
            object->SetSelectedLighting(state.selectedLighting);
            object->SetEnableLighting(state.enableLighting != 0);
        }

        object->SetEmissive(state.emissive);
        object->SetColor(state.color);
        return;
    }

    object->SetClassName(previewObjectOriginalClassName_);
    object->SetMaterialType(previewObjectOriginalMaterialType_);
    object->SetBlendMode(previewObjectOriginalBlendMode_);

    if (auto* material = object->GetMaterialData()) {
        object->SetSelectedLighting(previewObjectOriginalSelectedLighting_);
        object->SetEnableLighting(previewObjectOriginalEnableLighting_ != 0);
    }

    object->SetEmissive(previewObjectOriginalEmissive_);
    object->SetColor(previewObjectOriginalColor_);
}

void DebugEditor::DrawPreviewWire(ID3D12GraphicsCommandList* commandList, int& instanceCount, int maxDrawLimit) {
    // プレビュー形状の Aabb が無効ならモデルの境界を代用し、描画上限を守る。
    if (!previewObject_ || !commandList || instanceCount >= maxDrawLimit)
        return;

    Aabb aabb = previewObject_->GetAabb();

    if (!IsValidAabb(aabb)) {
        aabb = previewObject_->GetModelWorldAabb();
    }

    if (!IsValidAabb(aabb))
        return;

    engine::Vector3 size = aabb.max - aabb.min;
    engine::Vector3 center = (aabb.max + aabb.min) * 0.5f;

    engine::Math math;
    engine::Matrix4x4 world =
        math.Multiply(
            math.MakeScaleMatrix(size),
            math.MakeTranslateMatrix(center));

    primitiveDrawer_.DrawWireCube(commandList, world, { 0.2f, 1.0f, 0.45f, 1.0f }, instanceCount);
    instanceCount++;
}

void DebugEditor::DrawPreviewMarker() {
#ifdef USE_IMGUI
    // Object 中心ではなく配置面の接点を示し、浮き上がり量を線で確認できるようにする。
    if (!previewObject_)
        return;

    engine::Vector3 markerWorld = hasPreviewPlacementContact_
        ? previewPlacementContactPosition_
        : previewObject_->GetWorldPosition();
    engine::Vector3 screen = WorldToScreen(markerWorld);
    if (screen.z < 0.0f)
        return;

    engine::Vector3 objectScreen = WorldToScreen(previewObject_->GetWorldPosition());
    bool canDrawObjectLine = objectScreen.z >= 0.0f;

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImVec2 center(screen.x, screen.y);
    ImU32 outerColor = IM_COL32(40, 255, 120, 210);
    ImU32 innerColor = IM_COL32(255, 255, 255, 220);
    constexpr float kRadius = 13.0f;

    if (canDrawObjectLine) {
        ImVec2 objectCenter(objectScreen.x, objectScreen.y);
        float dx = objectCenter.x - center.x;
        float dy = objectCenter.y - center.y;

        if ((dx * dx + dy * dy) > 36.0f) {
            drawList->AddLine(center, objectCenter, IM_COL32(40, 255, 120, 120), 1.5f);
            drawList->AddCircle(objectCenter, 5.0f, IM_COL32(255, 255, 255, 150), 16, 1.5f);
        }
    }

    drawList->AddCircleFilled(center, kRadius, IM_COL32(40, 255, 120, 35), 32);
    drawList->AddCircle(center, kRadius, outerColor, 32, 2.5f);
    drawList->AddLine(ImVec2(center.x - kRadius - 5.0f, center.y), ImVec2(center.x - 4.0f, center.y), outerColor, 2.0f);
    drawList->AddLine(ImVec2(center.x + 4.0f, center.y), ImVec2(center.x + kRadius + 5.0f, center.y), outerColor, 2.0f);
    drawList->AddLine(ImVec2(center.x, center.y - kRadius - 5.0f), ImVec2(center.x, center.y - 4.0f), outerColor, 2.0f);
    drawList->AddLine(ImVec2(center.x, center.y + 4.0f), ImVec2(center.x, center.y + kRadius + 5.0f), outerColor, 2.0f);
    drawList->AddCircleFilled(center, 3.0f, innerColor);
    drawList->AddText(ImVec2(center.x + 16.0f, center.y - 8.0f), outerColor, "Place");
#endif
}

