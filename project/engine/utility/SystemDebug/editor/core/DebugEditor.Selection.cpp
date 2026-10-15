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
#include "GhostDirector.h"
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

void DebugEditor::SetSelectedObject(engine::Object3d* obj) {
    // 現在のSceneに属するObjectだけ選択し、古いSceneのポインタは保持しません。
    selectedObjects_.clear();
    if (obj && IsObjectInCurrentScene(obj)) {
        selectedObject_ = obj;
        selectedObjects_.push_back(obj);
    } else {
        selectedObject_ = nullptr;
    }
}

void DebugEditor::SyncObjectSelectionToInspector() {
    // Ghost系Editorが選択中ならInspectorを奪わず、通常のObject選択だけ同期します。
    EditorManager* editorManager = EditorManager::GetInstance();
    if ((ghostRecorder_ && editorManager->GetSelectedObject() == ghostRecorder_) ||
        (ghostDirector_ && editorManager->GetSelectedObject() == ghostDirector_)) {
        return;
    }

    if (selectedObject_) {
        editorManager->SetSelectedObject(this);
    } else {
        editorManager->ClearSelection();
    }
}

bool DebugEditor::FocusSceneObject(engine::Object3d* object) {
    // フォーカス対象が現行Sceneに残っていることを確認してからカメラへ渡します。
    if (!object || !IsObjectInCurrentScene(object)) {
        return false;
    }
    return CameraEditor::GetInstance()->FocusSceneObject(object);
}

bool DebugEditor::IsObjectSelected(const engine::Object3d* object) const {
    // 選択配列に残った古いポインタを肯定しないようScene所属も調べます。
    if (!object || !IsObjectInCurrentScene(object)) return false;

    for (engine::Object3d* selected : selectedObjects_) {
        if (selected == object) return true;
    }
    return false;
}

int DebugEditor::GetSelectionOverlayMode() const {
    // UI保存用の整数値として選択表示モードを返します。
    return static_cast<int>(selectionOverlayMode_);
}

void DebugEditor::SetSelectionOverlayMode(int mode) {
    // 永続化データの範囲外値を無視し、表示モードを不正な列挙値にしません。
    if (mode < static_cast<int>(SelectionOverlayMode::Compact) ||
        mode > static_cast<int>(SelectionOverlayMode::Hidden)) {
        return;
    }
    selectionOverlayMode_ = static_cast<SelectionOverlayMode>(mode);
}

void DebugEditor::ClearObjectSelection() {
    // グループ変形の開始状態も破棄し、次の選択へ前回の基準を持ち越しません。
    selectedObject_ = nullptr;
    selectedObjects_.clear();
    groupTransformStartStates_.clear();
}

void DebugEditor::AddSelectedObject(engine::Object3d* object) {
    // 同じObjectを重複登録せず、最後に追加したものを主選択にします。
    if (!object || !IsObjectInCurrentScene(object)) return;

    for (engine::Object3d* selected : selectedObjects_) {
        if (selected == object) {
            selectedObject_ = object;
            return;
        }
    }

    selectedObjects_.push_back(object);
    selectedObject_ = object;
}

void DebugEditor::ToggleSelectedObject(engine::Object3d* object) {
    // 除外後の主選択を最後の残存Objectへ移し、選択配列と同期します。
    if (!object || !IsObjectInCurrentScene(object)) return;

    for (auto it = selectedObjects_.begin(); it != selectedObjects_.end(); ++it) {
        if (*it == object) {
            selectedObjects_.erase(it);
            selectedObject_ = selectedObjects_.empty() ? nullptr : selectedObjects_.back();
            return;
        }
    }

    AddSelectedObject(object);
}

void DebugEditor::PruneInvalidSelectedObjects() {
    // Sceneの再読込や削除で無効になった選択を除き、重複も整理します。
    std::vector<engine::Object3d*> validObjects;
    validObjects.reserve(selectedObjects_.size());

    for (engine::Object3d* object : selectedObjects_) {
        if (!object || !IsObjectInCurrentScene(object)) continue;

        bool alreadyAdded = false;
        for (engine::Object3d* added : validObjects) {
            if (added == object) {
                alreadyAdded = true;
                break;
            }
        }
        if (!alreadyAdded) {
            validObjects.push_back(object);
        }
    }

    selectedObjects_ = validObjects;
    if (!selectedObject_ || !IsObjectInCurrentScene(selectedObject_)) {
        selectedObject_ = selectedObjects_.empty() ? nullptr : selectedObjects_.back();
    }
}

engine::Object3d* DebugEditor::PickObjectAtGameViewPos(const engine::Vector2& mousePos) {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return nullptr;

    engine::Ray ray = ScreenPointToRay(mousePos);
    if (engine::Math::Dot(ray.diff, ray.diff) < 1e-12f) return nullptr;
    float bestDistance = 1.0f;
    engine::Object3d* hit = nullptr;

    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
    for (auto& obj : objects) {
        if (!obj || obj->IsEditorInternal()) continue;
        if (obj->GetName() == "Cursor" || obj->GetName() == "Line") continue;
        if (!obj->GetIsRenderVisible() || obj->GetIsLocked()) continue;

        const engine::Matrix4x4 worldMatrix = obj->GetWorldMatrix();
        // ゼロスケールの変換行列は逆変換できないため、選択判定から除外する。
        const engine::Vector3 axisX = { worldMatrix.m[0][0], worldMatrix.m[0][1], worldMatrix.m[0][2] };
        const engine::Vector3 axisY = { worldMatrix.m[1][0], worldMatrix.m[1][1], worldMatrix.m[1][2] };
        const engine::Vector3 axisZ = { worldMatrix.m[2][0], worldMatrix.m[2][1], worldMatrix.m[2][2] };
        if (engine::Math::Dot(axisX, axisX) < 1e-12f ||
            engine::Math::Dot(axisY, axisY) < 1e-12f ||
            engine::Math::Dot(axisZ, axisZ) < 1e-12f) continue;

        const engine::Matrix4x4 inverseWorld = engine::Math::Inverse(worldMatrix);
        const engine::Ray localRay = {
            engine::Math::Transform(ray.origin, inverseWorld),
            engine::Math::TransformNormal(ray.diff, inverseWorld)
        };
        if (!std::isfinite(localRay.origin.x) || !std::isfinite(localRay.origin.y) || !std::isfinite(localRay.origin.z) ||
            !std::isfinite(localRay.diff.x) || !std::isfinite(localRay.diff.y) || !std::isfinite(localRay.diff.z)) continue;
        engine::Vector3 localMin = { -0.5f, -0.5f, -0.5f };
        engine::Vector3 localMax = { 0.5f, 0.5f, 0.5f };
        Model* model = obj->GetModel();
        if (model) {
            localMin = model->GetLocalAabbMin();
            localMax = model->GetLocalAabbMax();
        }
        if (!std::isfinite(localMin.x) || !std::isfinite(localMin.y) || !std::isfinite(localMin.z) ||
            !std::isfinite(localMax.x) || !std::isfinite(localMax.y) || !std::isfinite(localMax.z) ||
            localMin.x > localMax.x || localMin.y > localMax.y || localMin.z > localMax.z) continue;
        const float origins[3] = { localRay.origin.x, localRay.origin.y, localRay.origin.z };
        const float directions[3] = { localRay.diff.x, localRay.diff.y, localRay.diff.z };
        const float mins[3] = { localMin.x, localMin.y, localMin.z };
        const float maxs[3] = { localMax.x, localMax.y, localMax.z };
        float entry = 0.0f;
        float exit = 1.0f;
        bool startsInside = true;
        bool intersects = true;
        for (int axis = 0; axis < 3; ++axis) {
            if (origins[axis] < mins[axis] || origins[axis] > maxs[axis]) startsInside = false;
            if (std::abs(directions[axis]) < 1e-6f) {
                if (origins[axis] < mins[axis] || origins[axis] > maxs[axis]) intersects = false;
                continue;
            }
            float nearT = (mins[axis] - origins[axis]) / directions[axis];
            float farT = (maxs[axis] - origins[axis]) / directions[axis];
            if (nearT > farT) std::swap(nearT, farT);
            entry = (std::max)(entry, nearT);
            exit = (std::min)(exit, farT);
            if (entry > exit) {
                intersects = false;
                break;
            }
        }

        if (!intersects || entry > bestDistance) continue;

        // 静的モデルは面との交差を調べ、AABB の空間部分で手前の物体を選ばない。
        // アニメーション中の頂点はCPU側と一致しないため、従来の箱判定を使う。
        const bool useMeshSurface = model && !model->GetModelData().hasSkinning &&
            !model->GetModelData().usesNodeAnimationProxy;
        if (useMeshSurface) {
            const auto& meshes = model->GetModelData().meshes;
            const int drawIndex = obj->GetMeshDrawIndex();
            for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
                if (drawIndex >= 0 && meshIndex != static_cast<size_t>(drawIndex)) continue;
                const auto& mesh = meshes[meshIndex];
                for (size_t index = 0; index + 2 < mesh.indices.size(); index += 3) {
                    const uint32_t i0 = mesh.indices[index];
                    const uint32_t i1 = mesh.indices[index + 1];
                    const uint32_t i2 = mesh.indices[index + 2];
                    if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) continue;
                    const auto& a = mesh.vertices[i0].position;
                    const auto& b = mesh.vertices[i1].position;
                    const auto& c = mesh.vertices[i2].position;
                    const engine::Vector3 edge1 = { b.x - a.x, b.y - a.y, b.z - a.z };
                    const engine::Vector3 edge2 = { c.x - a.x, c.y - a.y, c.z - a.z };
                    const engine::Vector3 p = engine::Math::Cross(localRay.diff, edge2);
                    const float determinant = engine::Math::Dot(edge1, p);
                    if (std::abs(determinant) < 1e-8f) continue;
                    const float inverseDeterminant = 1.0f / determinant;
                    const engine::Vector3 fromA = { localRay.origin.x - a.x, localRay.origin.y - a.y, localRay.origin.z - a.z };
                    const float u = engine::Math::Dot(fromA, p) * inverseDeterminant;
                    if (u < 0.0f || u > 1.0f) continue;
                    const engine::Vector3 q = engine::Math::Cross(fromA, edge1);
                    const float v = engine::Math::Dot(localRay.diff, q) * inverseDeterminant;
                    if (v < 0.0f || u + v > 1.0f) continue;
                    const float distance = engine::Math::Dot(edge2, q) * inverseDeterminant;
                    if (distance >= 0.0f && distance <= 1.0f && distance < bestDistance) {
                        bestDistance = distance;
                        hit = obj.get();
                    }
                }
            }
        } else {
            // 箱の内側から始まるレイは出口を比較し、周囲の大きなObjectを優先しない。
            const float distance = startsInside ? exit : entry;
            if (distance >= 0.0f && distance < bestDistance) {
                bestDistance = distance;
                hit = obj.get();
            }
        }
    }

    // 地形編集グループは見た目の子をクリックしても、当たり判定を含む区画単位で選択する。
    // 子メッシュだけを調整したい場合はHierarchyから直接選択できる。
    if (hit && hit->GetParent() && hit->GetParent()->GetTag() == "TerrainEditGroup") {
        return hit->GetParent();
    }
    return hit;
}

engine::Vector3 DebugEditor::GetObjectWorldPositionForSelection(engine::Object3d* object) {
    // モデルのワールドAABBが有効なら、その中心を画面選択の代表点にします。
    if (!object) return { 0.0f, 0.0f, 0.0f };

    AABB modelAabb = object->GetModelWorldAABB();
    if (IsValidAabb(modelAabb)) {
        return {
            (modelAabb.min.x + modelAabb.max.x) * 0.5f,
            (modelAabb.min.y + modelAabb.max.y) * 0.5f,
            (modelAabb.min.z + modelAabb.max.z) * 0.5f
        };
    }

    engine::Matrix4x4 worldMatrix = object->GetWorldMatrix();
    return { worldMatrix.m[3][0], worldMatrix.m[3][1], worldMatrix.m[3][2] };
}

void DebugEditor::SelectObjectsInGameViewRect(const engine::Vector2& start, const engine::Vector2& end, bool additive) {
    // Game View内のドラッグ矩形を画面座標へ変換してObjectの投影位置と比べます。
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    const float localLeft = start.x < end.x ? start.x : end.x;
    const float localRight = start.x < end.x ? end.x : start.x;
    const float localTop = start.y < end.y ? start.y : end.y;
    const float localBottom = start.y < end.y ? end.y : start.y;

    const float screenLeft = gameViewOffset_.x + localLeft;
    const float screenRight = gameViewOffset_.x + localRight;
    const float screenTop = gameViewOffset_.y + localTop;
    const float screenBottom = gameViewOffset_.y + localBottom;

    if (!additive) {
        ClearObjectSelection();
    }

    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
    for (auto& obj : objects) {
        if (!obj || obj->IsEditorInternal()) continue;
        if (obj->GetName() == "Cursor" || obj->GetName() == "Line") continue;
        if (!obj->GetIsRenderVisible() || obj->GetIsLocked()) continue;

        engine::Vector3 screenPosition = WorldToScreen(GetObjectWorldPositionForSelection(obj.get()));
        if (screenPosition.z < 0.0f) continue;

        if (screenPosition.x >= screenLeft && screenPosition.x <= screenRight &&
            screenPosition.y >= screenTop && screenPosition.y <= screenBottom) {
            AddSelectedObject(obj.get());
        }
    }

    if (selectedObject_ || !additive) {
        SyncObjectSelectionToInspector();
    }
}

void DebugEditor::DrawSelectionRectangleOverlay() {
#ifdef USE_IMGUI
    // ドラッグ中だけ矩形をGame Viewに重ね、選択確定前の範囲を示します。
    if (!isSelectionRectDragging_ || !isSelectionRectReady_) return;

    const float minX = selectionRectStart_.x < selectionRectEnd_.x ? selectionRectStart_.x : selectionRectEnd_.x;
    const float maxX = selectionRectStart_.x < selectionRectEnd_.x ? selectionRectEnd_.x : selectionRectStart_.x;
    const float minY = selectionRectStart_.y < selectionRectEnd_.y ? selectionRectStart_.y : selectionRectEnd_.y;
    const float maxY = selectionRectStart_.y < selectionRectEnd_.y ? selectionRectEnd_.y : selectionRectStart_.y;

    ImVec2 rectMin = ImVec2(gameViewOffset_.x + minX, gameViewOffset_.y + minY);
    ImVec2 rectMax = ImVec2(gameViewOffset_.x + maxX, gameViewOffset_.y + maxY);

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    drawList->AddRectFilled(rectMin, rectMax, IM_COL32(80, 160, 255, 45));
    drawList->AddRect(rectMin, rectMax, IM_COL32(80, 190, 255, 230), 0.0f, 0, 2.0f);
#endif
}

void DebugEditor::DrawSelectedObjectBoundsOverlay() {
#ifdef USE_IMGUI
    if (!drawSelectionBounds_) return;
    PruneInvalidSelectedObjects();
    if (selectedObjects_.empty() || selectionOverlayMode_ == SelectionOverlayMode::Hidden) return;
    if (gameViewSize_.x <= 1.0f || gameViewSize_.y <= 1.0f) return;

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const std::size_t selectedCount = selectedObjects_.size();
    const bool showTemporaryDetails =
        selectionOverlayMode_ == SelectionOverlayMode::Compact && ImGui::GetIO().KeyAlt;
    const ImVec2 viewMin = ImVec2(gameViewOffset_.x, gameViewOffset_.y);
    const ImVec2 viewMax = ImVec2(
        gameViewOffset_.x + gameViewSize_.x,
        gameViewOffset_.y + gameViewSize_.y);

    auto clampFloat = [](float value, float minimum, float maximum) {
        return (std::max)(minimum, (std::min)(value, maximum));
    };
    auto drawPivotMarker = [&](const ImVec2& center, ImU32 color, bool primary) {
        const float radius = primary ? 5.0f : 4.5f;
        drawList->AddCircleFilled(center, radius + 2.0f, IM_COL32(10, 24, 36, 190), 16);
        drawList->AddCircle(center, radius, color, 16, primary ? 2.0f : 1.7f);
        drawList->AddLine(
            ImVec2(center.x - 2.5f, center.y),
            ImVec2(center.x + 2.5f, center.y),
            color,
            1.3f);
        drawList->AddLine(
            ImVec2(center.x, center.y - 2.5f),
            ImVec2(center.x, center.y + 2.5f),
            color,
            1.3f);
    };
    auto drawCornerBrackets = [&](const ImVec2& minimum, const ImVec2& maximum, ImU32 color, float thickness) {
        const float width = maximum.x - minimum.x;
        const float height = maximum.y - minimum.y;
        const float bracketLength = clampFloat((std::min)(width, height) * 0.22f, 8.0f, 18.0f);
        const ImU32 shadowColor = IM_COL32(8, 18, 28, 210);
        const float shadowThickness = thickness + 2.0f;
        const ImVec2 starts[8] = {
            { minimum.x, minimum.y }, { minimum.x, minimum.y },
            { maximum.x, minimum.y }, { maximum.x, minimum.y },
            { minimum.x, maximum.y }, { minimum.x, maximum.y },
            { maximum.x, maximum.y }, { maximum.x, maximum.y }
        };
        const ImVec2 ends[8] = {
            { minimum.x + bracketLength, minimum.y }, { minimum.x, minimum.y + bracketLength },
            { maximum.x - bracketLength, minimum.y }, { maximum.x, minimum.y + bracketLength },
            { minimum.x + bracketLength, maximum.y }, { minimum.x, maximum.y - bracketLength },
            { maximum.x - bracketLength, maximum.y }, { maximum.x, maximum.y - bracketLength }
        };

        for (int i = 0; i < 8; ++i) {
            drawList->AddLine(starts[i], ends[i], shadowColor, shadowThickness);
        }
        for (int i = 0; i < 8; ++i) {
            drawList->AddLine(starts[i], ends[i], color, thickness);
        }
    };

    drawList->PushClipRect(viewMin, viewMax, true);

    for (engine::Object3d* object : selectedObjects_) {
        if (!object || !IsObjectInCurrentScene(object)) continue;

        engine::Vector3 corners[8] = {};
        engine::Vector3 center = GetObjectWorldPositionForSelection(object);
        bool hasOrientedModelBounds = false;

        // ワールドAABBを再投影せず、ローカル境界を直接変換して回転物の過剰な膨らみを防ぐ。
        if (Model* model = object->GetModel()) {
            const engine::Vector3 localMin = model->GetLocalAabbMin();
            const engine::Vector3 localMax = model->GetLocalAabbMax();
            const bool isFiniteBounds =
                std::isfinite(localMin.x) && std::isfinite(localMin.y) && std::isfinite(localMin.z) &&
                std::isfinite(localMax.x) && std::isfinite(localMax.y) && std::isfinite(localMax.z);
            if (isFiniteBounds &&
                localMax.x >= localMin.x && localMax.y >= localMin.y && localMax.z >= localMin.z) {
                const engine::Vector3 localCorners[8] = {
                    { localMin.x, localMin.y, localMin.z },
                    { localMax.x, localMin.y, localMin.z },
                    { localMin.x, localMax.y, localMin.z },
                    { localMax.x, localMax.y, localMin.z },
                    { localMin.x, localMin.y, localMax.z },
                    { localMax.x, localMin.y, localMax.z },
                    { localMin.x, localMax.y, localMax.z },
                    { localMax.x, localMax.y, localMax.z }
                };
                const engine::Matrix4x4 worldMatrix = object->GetWorldMatrix();
                for (int i = 0; i < 8; ++i) {
                    corners[i] = engine::Math::Transform(localCorners[i], worldMatrix);
                }
                const engine::Vector3 localCenter = {
                    (localMin.x + localMax.x) * 0.5f,
                    (localMin.y + localMax.y) * 0.5f,
                    (localMin.z + localMax.z) * 0.5f
                };
                center = engine::Math::Transform(localCenter, worldMatrix);
                hasOrientedModelBounds = true;
            }
        }

        if (!hasOrientedModelBounds) {
            AABB aabb = object->GetModelWorldAABB();
            if (!IsValidAabb(aabb)) {
                const engine::Vector3 scale = object->GetTransform()->scale;
                const engine::Vector3 halfSize = {
                    (std::max)(std::abs(scale.x), 0.5f),
                    (std::max)(std::abs(scale.y), 0.5f),
                    (std::max)(std::abs(scale.z), 0.5f)
                };
                aabb.min = center - halfSize;
                aabb.max = center + halfSize;
            }
            corners[0] = { aabb.min.x, aabb.min.y, aabb.min.z };
            corners[1] = { aabb.max.x, aabb.min.y, aabb.min.z };
            corners[2] = { aabb.min.x, aabb.max.y, aabb.min.z };
            corners[3] = { aabb.max.x, aabb.max.y, aabb.min.z };
            corners[4] = { aabb.min.x, aabb.min.y, aabb.max.z };
            corners[5] = { aabb.max.x, aabb.min.y, aabb.max.z };
            corners[6] = { aabb.min.x, aabb.max.y, aabb.max.z };
            corners[7] = { aabb.max.x, aabb.max.y, aabb.max.z };
            center = GetAabbCenter(aabb);
        }

        const bool isPrimary = object == selectedObject_;
        const ImU32 frameColor =
            isPrimary ? IM_COL32(255, 226, 72, 255) : IM_COL32(80, 210, 255, 235);
        const bool showBounds = isPrimary ||
            selectionOverlayMode_ == SelectionOverlayMode::Detailed ||
            showTemporaryDetails;

        if (!showBounds) {
            const engine::Vector3 screen = WorldToScreen(center);
            if (screen.z < 0.0f || !std::isfinite(screen.x) || !std::isfinite(screen.y)) continue;

            const ImVec2 markerCenter = ImVec2(
                clampFloat(screen.x, viewMin.x + 7.0f, viewMax.x - 7.0f),
                clampFloat(screen.y, viewMin.y + 7.0f, viewMax.y - 7.0f));
            drawPivotMarker(markerCenter, frameColor, false);
            continue;
        }

        bool hasScreenPoint = false;
        int visibleCornerCount = 0;
        engine::Vector3 projectedCorners[8] = {};
        bool cornerVisible[8] = {};
        ImVec2 rectMin = ImVec2(0.0f, 0.0f);
        ImVec2 rectMax = ImVec2(0.0f, 0.0f);

        for (int i = 0; i < 8; ++i) {
            projectedCorners[i] = WorldToScreen(corners[i]);
            const engine::Vector3& screen = projectedCorners[i];
            if (screen.z < 0.0f || !std::isfinite(screen.x) || !std::isfinite(screen.y)) continue;

            cornerVisible[i] = true;
            ++visibleCornerCount;
            if (!hasScreenPoint) {
                rectMin = ImVec2(screen.x, screen.y);
                rectMax = rectMin;
                hasScreenPoint = true;
            } else {
                rectMin.x = (std::min)(rectMin.x, screen.x);
                rectMin.y = (std::min)(rectMin.y, screen.y);
                rectMax.x = (std::max)(rectMax.x, screen.x);
                rectMax.y = (std::max)(rectMax.y, screen.y);
            }
        }

        const engine::Vector3 centerScreen = WorldToScreen(center);
        const bool hasCenterScreen = centerScreen.z >= 0.0f &&
            std::isfinite(centerScreen.x) && std::isfinite(centerScreen.y);
        if (!hasScreenPoint && !hasCenterScreen) continue;

        const bool intersectsView = hasScreenPoint &&
            rectMax.x >= viewMin.x && rectMin.x <= viewMax.x &&
            rectMax.y >= viewMin.y && rectMin.y <= viewMax.y;
        const float rawWidth = hasScreenPoint ? (std::max)(rectMax.x - rectMin.x, 0.0f) : 0.0f;
        const float rawHeight = hasScreenPoint ? (std::max)(rectMax.y - rectMin.y, 0.0f) : 0.0f;
        const float widthCoverage = rawWidth / gameViewSize_.x;
        const float heightCoverage = rawHeight / gameViewSize_.y;
        const float areaCoverage = widthCoverage * heightCoverage;
        const bool projectionIsUnstable = visibleCornerCount < 8;
        const bool isOversized =
            widthCoverage > 0.70f || heightCoverage > 0.70f || areaCoverage > 0.32f;
        const bool useCompactMarker =
            selectionOverlayMode_ == SelectionOverlayMode::Compact &&
            !showTemporaryDetails &&
            (projectionIsUnstable || isOversized || !intersectsView);

        if (useCompactMarker) {
            if (!hasCenterScreen) continue;

            const ImVec2 markerCenter = ImVec2(
                clampFloat(centerScreen.x, viewMin.x + 9.0f, viewMax.x - 9.0f),
                clampFloat(centerScreen.y, viewMin.y + 9.0f, viewMax.y - 9.0f));
            drawPivotMarker(markerCenter, frameColor, true);

            std::string label = object->GetName().empty() ? "Selected" : object->GetName();
            if (selectedCount > 1) {
                label += " (+" + std::to_string(selectedCount - 1) + ")";
            }
            const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
            ImVec2 labelPos = ImVec2(markerCenter.x + 10.0f, markerCenter.y - textSize.y * 0.5f);
            labelPos.x = clampFloat(labelPos.x, viewMin.x + 4.0f, viewMax.x - textSize.x - 4.0f);
            labelPos.y = clampFloat(labelPos.y, viewMin.y + 4.0f, viewMax.y - textSize.y - 4.0f);
            drawList->AddText(
                ImVec2(labelPos.x + 1.0f, labelPos.y + 1.0f),
                IM_COL32(0, 0, 0, 220),
                label.c_str());
            drawList->AddText(labelPos, IM_COL32(255, 245, 150, 255), label.c_str());
            continue;
        }

        if (!hasScreenPoint || !intersectsView) continue;

        rectMin.x = clampFloat(rectMin.x, viewMin.x + 2.0f, viewMax.x - 2.0f);
        rectMin.y = clampFloat(rectMin.y, viewMin.y + 2.0f, viewMax.y - 2.0f);
        rectMax.x = clampFloat(rectMax.x, viewMin.x + 2.0f, viewMax.x - 2.0f);
        rectMax.y = clampFloat(rectMax.y, viewMin.y + 2.0f, viewMax.y - 2.0f);
        if (rectMax.x <= rectMin.x || rectMax.y <= rectMin.y) continue;

        // 遠距離の小さい物体でも選択表示が潰れない最低サイズを確保する。
        constexpr float minimumDisplaySize = 16.0f;
        const ImVec2 rectCenter = ImVec2(
            (rectMin.x + rectMax.x) * 0.5f,
            (rectMin.y + rectMax.y) * 0.5f);
        if (rectMax.x - rectMin.x < minimumDisplaySize) {
            rectMin.x = clampFloat(
                rectCenter.x - minimumDisplaySize * 0.5f,
                viewMin.x + 2.0f,
                viewMax.x - 2.0f);
            rectMax.x = clampFloat(
                rectCenter.x + minimumDisplaySize * 0.5f,
                viewMin.x + 2.0f,
                viewMax.x - 2.0f);
        }
        if (rectMax.y - rectMin.y < minimumDisplaySize) {
            rectMin.y = clampFloat(
                rectCenter.y - minimumDisplaySize * 0.5f,
                viewMin.y + 2.0f,
                viewMax.y - 2.0f);
            rectMax.y = clampFloat(
                rectCenter.y + minimumDisplaySize * 0.5f,
                viewMin.y + 2.0f,
                viewMax.y - 2.0f);
        }

        const bool showDetailedBounds =
            selectionOverlayMode_ == SelectionOverlayMode::Detailed || showTemporaryDetails;
        if (showDetailedBounds) {
            constexpr int edges[12][2] = {
                { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },
                { 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },
                { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
            };
            const float thickness = isPrimary ? 2.0f : 1.5f;
            for (const auto& edge : edges) {
                if (!cornerVisible[edge[0]] || !cornerVisible[edge[1]]) continue;
                const ImVec2 start =
                    ImVec2(projectedCorners[edge[0]].x, projectedCorners[edge[0]].y);
                const ImVec2 end =
                    ImVec2(projectedCorners[edge[1]].x, projectedCorners[edge[1]].y);
                drawList->AddLine(start, end, IM_COL32(8, 18, 28, 210), thickness + 2.0f);
                drawList->AddLine(start, end, frameColor, thickness);
            }
        } else {
            drawCornerBrackets(rectMin, rectMax, frameColor, isPrimary ? 2.2f : 1.7f);
        }

        if (isPrimary) {
            std::string label = object->GetName().empty() ? "Selected" : object->GetName();
            if (selectedCount > 1) {
                label += " (+" + std::to_string(selectedCount - 1) + ")";
            }
            const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
            ImVec2 labelPos = ImVec2(rectMin.x, rectMin.y - textSize.y - 4.0f);
            if (labelPos.y < viewMin.y + 3.0f) {
                labelPos.y = rectMin.y + 4.0f;
            }
            labelPos.x = clampFloat(labelPos.x, viewMin.x + 4.0f, viewMax.x - textSize.x - 4.0f);
            labelPos.y = clampFloat(labelPos.y, viewMin.y + 3.0f, viewMax.y - textSize.y - 3.0f);
            drawList->AddText(
                ImVec2(labelPos.x + 1.0f, labelPos.y + 1.0f),
                IM_COL32(0, 0, 0, 220),
                label.c_str());
            drawList->AddText(labelPos, IM_COL32(255, 245, 150, 255), label.c_str());
        }
    }

    drawList->PopClipRect();
#endif
}

bool DebugEditor::IsObjectInCurrentScene(const engine::Object3d* object) const {
    // 生ポインタの逆参照前に、現行Sceneの所有Objectとアドレスを照合します。
    if (!object || object->IsEditorInternal() || !sceneManager_ || !sceneManager_->GetCurrentScene()) {
        return false;
    }

    const auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
    for (const auto& candidate : objects) {
        if (candidate.get() == object) {
            return true;
        }
    }
    return false;
}

void DebugEditor::ClearInvalidSelectedObject() {
    // 選択が失われたら変形操作も止め、Inspectorに無効Objectを残しません。
    const bool editorWasSelected = EditorManager::GetInstance()->GetSelectedObject() == this;
    PruneInvalidSelectedObjects();
    if (selectedObject_) {
        return;
    }

    ClearObjectSelection();
    isDraggingTransform_ = false;
    hasInspectorEditStart_ = false;
    inspectorEditStartStates_.clear();
    tempObjectStateStart_.clear();
    if (editorWasSelected) {
        EditorManager::GetInstance()->ClearSelection();
    }
}

