#include "InspectorWindow.h"
#include "InspectorTextureCatalog.h"
#include "DebugEditor.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "Object3d.h"
#include "engine/graphics/3d/material/MaterialInstance.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "IconsFontAwesome5.h"
#include "EditorManager.h"
#include "ParticleManager.h"
#include "GPUParticleManager.h"
#include "ModelManager.h"
#include "GhostRecorder.h"
#include "CameraEditor.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "GameplayStatusManager.h"
#include "EditorPropertyDrawer.h"
#include "EditorPropertyRegistry.h"
#include "EditorCommandRegistry.h"
#include "EditorAssetDragPayload.h"
#include "ImGuizmo.h"
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <PresetManager.h>
static const float PI = (float)M_PI;
static float ToRadians(float degrees) { return degrees * (PI / 180.0f); }
static float ToDegrees(float radians) { return radians * (180.0f / PI); }
namespace fs = std::filesystem;

namespace {
std::string ToLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::vector<Object3d*> CollectInspectorTargets(DebugEditor* editor, Object3d* primary) {
    std::vector<Object3d*> targets;
    if (!primary) {
        return targets;
    }

    const std::vector<Object3d*>& selectedObjects = editor->GetSelectedObjects();
    if (selectedObjects.empty()) {
        targets.push_back(primary);
        return targets;
    }

    targets.reserve(selectedObjects.size());
    for (Object3d* object : selectedObjects) {
        if (!object) {
            continue;
        }
        if (std::find(targets.begin(), targets.end(), object) != targets.end()) {
            continue;
        }
        targets.push_back(object);
    }

    if (std::find(targets.begin(), targets.end(), primary) == targets.end()) {
        targets.push_back(primary);
    }
    return targets;
}

void MarkInspectorTargetsDirty(DebugEditor* editor, const std::vector<Object3d*>& targets) {
    if (!editor) {
        return;
    }
    for (Object3d* object : targets) {
        if (!object) {
            continue;
        }
        editor->MarkDirtyForObject(object);
    }
}

void RefreshInspectorTargetMatrices(const std::vector<Object3d*>& targets) {
    for (Object3d* object : targets) {
        if (!object) {
            continue;
        }
        object->UpdateLocalMatrix();
        object->UpdateWorldMatrix();
    }
}

float SafeScaleRatio(float after, float before) {
    if (std::fabs(before) < 0.0001f) {
        return 1.0f;
    }
    return after / before;
}

void ApplyPrimaryTransformDeltaToTargets(
    const std::vector<Object3d*>& targets,
    Object3d* primary,
    const Vector3& beforePosition,
    const Vector3& afterPosition,
    const Vector3& beforeRotation,
    const Vector3& afterRotation,
    const Vector3& beforeScale,
    const Vector3& afterScale,
    bool applyPosition,
    bool applyRotation,
    bool applyScale) {

    if (!primary) {
        return;
    }

    const Vector3 positionDelta = {
        afterPosition.x - beforePosition.x,
        afterPosition.y - beforePosition.y,
        afterPosition.z - beforePosition.z,
    };
    const Vector3 rotationDelta = {
        afterRotation.x - beforeRotation.x,
        afterRotation.y - beforeRotation.y,
        afterRotation.z - beforeRotation.z,
    };
    const Vector3 scaleRatio = {
        SafeScaleRatio(afterScale.x, beforeScale.x),
        SafeScaleRatio(afterScale.y, beforeScale.y),
        SafeScaleRatio(afterScale.z, beforeScale.z),
    };

    for (Object3d* object : targets) {
        if (!object || object == primary) {
            continue;
        }

        Transform* transform = object->GetTransform();
        if (!transform) {
            continue;
        }

        if (applyPosition) {
            transform->translate.x += positionDelta.x;
            transform->translate.y += positionDelta.y;
            transform->translate.z += positionDelta.z;
        }
        if (applyRotation) {
            transform->rotate.x += rotationDelta.x;
            transform->rotate.y += rotationDelta.y;
            transform->rotate.z += rotationDelta.z;
            transform->isQuaternionMaster = false;
        }
        if (applyScale) {
            transform->scale.x *= scaleRatio.x;
            transform->scale.y *= scaleRatio.y;
            transform->scale.z *= scaleRatio.z;
        }
    }
}

bool IsSpriteCardObject(const Object3d* object) {
    if (!object) {
        return false;
    }
    const std::string modelName = object->GetModelName();
    const std::string texturePath = object->GetTexturePath();
    const std::string name = object->GetName();
    return modelName == "Primitives/plane" ||
        texturePath.find("Resources/sprite/") == 0 ||
        name.find("SpriteCard") != std::string::npos ||
        name.find("2.5D") != std::string::npos;
}

void ConfigureSpriteCardObject(Object3d* object) {
    if (!object) {
        return;
    }

    object->SetClassName("Model");
    object->SetModel("Primitives/plane");
    if (object->GetName().empty() || object->GetName() == "Object") {
        object->SetName("SpriteCard_2_5D");
    }
    if (object->GetTexturePath().empty()) {
        object->SetTexture("Resources/sprite/common/white.png");
    }
    object->SetMaterialType(0);
    object->SetBlendMode(BlendMode::kNormal);
    object->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    object->SetEnableNormalMap(false);
    object->SetNormalMap("");
    object->SetOrmMap("");
    object->SetEnableEnvMap(false);
    object->SetEnableLighting(false);
    object->SetEmissive(1.35f);
    object->SetTextureTiling({ 1.0f, 1.0f });
    object->SetAutoTextureTiling(false);
    object->SetCollisionAttribute(0);
    object->SetCollisionMask(0);

    Object3d::ColliderConfig colliderConfig = object->GetColliderConfig();
    colliderConfig.type = ColliderType::kNone;
    object->SetColliderConfig(colliderConfig);
}

void ApplyRegisteredComponentToTargets(
    DebugEditor* editor,
    const std::vector<Object3d*>& targets,
    const std::string& componentTypeId,
    bool add) {
    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    bool changed = false;
    for (Object3d* target : targets) {
        if (!target) {
            continue;
        }
        changed |= add
            ? registry->AddComponent(target, componentTypeId)
            : registry->RemoveComponent(target, componentTypeId);
    }
    if (changed) {
        MarkInspectorTargetsDirty(editor, targets);
        RefreshInspectorTargetMatrices(targets);
    }
}

void ApplyRegisteredComponentPresetToTargets(
    DebugEditor* editor,
    const std::vector<Object3d*>& targets,
    const std::string& componentTypeId,
    const std::string& presetId) {
    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    bool changed = false;
    for (Object3d* target : targets) {
        changed |= registry->ApplyComponentPreset(target, componentTypeId, presetId);
    }
    if (changed) {
        MarkInspectorTargetsDirty(editor, targets);
        RefreshInspectorTargetMatrices(targets);
    }
}

void DrawRegisteredComponentRow(
    DebugEditor* editor,
    const std::vector<Object3d*>& targets,
    const EditorComponentDescriptor& component) {
    ImGui::PushID(component.typeId.c_str());
    ImGui::BeginGroup();
    ImGui::TextUnformatted(component.displayName.c_str());
    ImGui::TextDisabled("%s", component.description.c_str());
    ImGui::EndGroup();

    if (component.removable && component.remove) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            ApplyRegisteredComponentToTargets(editor, targets, component.typeId, false);
        }
    }
    else {
        ImGui::SameLine();
        ImGui::TextDisabled("固定");
    }
    ImGui::Separator();
    ImGui::PopID();
}

void DrawRegisteredAddComponentMenu(
    DebugEditor* editor,
    const std::vector<Object3d*>& targets,
    Object3d* primary) {
    if (!ImGui::BeginPopup("AddRegisteredComponentMenu")) {
        return;
    }

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    ImGui::TextDisabled("追加するComponent");
    ImGui::Separator();

    bool hasAddableComponent = false;
    for (const EditorComponentDescriptor* component : registry->GetApplicableComponentsForObject(primary)) {
        if (!component) {
            continue;
        }
        ImGui::PushID(component->typeId.c_str());
        if (component->add) {
            hasAddableComponent = true;
            const bool present = registry->IsComponentPresent(primary, component->typeId);
            if (ImGui::MenuItem(component->displayName.c_str(), nullptr, false, !present)) {
                ApplyRegisteredComponentToTargets(editor, targets, component->typeId, true);
            }
            if (ImGui::IsItemHovered() && !component->description.empty()) {
                ImGui::SetTooltip("%s", component->description.c_str());
            }
        }
        for (const EditorComponentPresetDescriptor& preset : component->presets) {
            hasAddableComponent = true;
            ImGui::PushID(preset.id.c_str());
            if (ImGui::MenuItem(preset.displayName.c_str())) {
                ApplyRegisteredComponentPresetToTargets(
                    editor,
                    targets,
                    component->typeId,
                    preset.id);
            }
            if (ImGui::IsItemHovered() && !preset.description.empty()) {
                ImGui::SetTooltip("%s", preset.description.c_str());
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    if (!hasAddableComponent) {
        ImGui::TextDisabled("追加可能なComponentはありません。");
    }
    ImGui::EndPopup();
}

bool HasRegisteredComponent(const Object3d* object, const char* componentTypeId) {
    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    return registry->IsComponentPresent(object, componentTypeId);
}

std::vector<Object3d*> CollectRegisteredComponentTargets(
    const std::vector<Object3d*>& targets,
    const char* componentTypeId) {
    std::vector<Object3d*> result;
    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    for (Object3d* target : targets) {
        if (target && registry->IsComponentPresent(target, componentTypeId)) {
            result.push_back(target);
        }
    }
    return result;
}

bool IsPrefabPropertyOverridden(const Object3d* object, const std::string& propertyPath) {
    if (!object || !object->IsPrefabInstance()) {
        return false;
    }
    for (const auto& entry : PresetManager::GetInstance()->GetPrefabOverrides(object)) {
        if (entry.propertyPath == propertyPath) {
            return true;
        }
    }
    return false;
}

void DrawAutomaticComponentProperties(
    DebugEditor* editor,
    Object3d* primary,
    const std::vector<Object3d*>& targets,
    const EditorComponentDescriptor& component) {
    if (!editor || !primary ||
        component.inspectorMode != EditorComponentInspectorMode::Automatic) {
        return;
    }

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    const std::vector<const EditorPropertyDescriptor*> properties =
        registry->GetPropertiesForComponent(component.typeId);
    if (properties.empty()) {
        return;
    }

    std::vector<Object3d*> componentTargets;
    componentTargets.reserve(targets.size());
    for (Object3d* target : targets) {
        if (!target || (component.applicable && !component.applicable(*target)) ||
            (component.present && !component.present(*target))) {
            continue;
        }
        componentTargets.push_back(target);
    }
    if (componentTargets.empty()) {
        return;
    }

    ImGui::Indent();
    if (componentTargets.size() != targets.size()) {
        ImGui::TextDisabled(
            "このComponentを持つ%zu / %zu Objectを編集",
            componentTargets.size(),
            targets.size());
    }
    if (ImGui::BeginTable(
        ("##AutoComponentProperties_" + component.typeId).c_str(),
        2,
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        for (const EditorPropertyDescriptor* property : properties) {
            if (!property || !registry->IsApplicable(primary, property->path)) {
                continue;
            }

            ImGui::PushID(property->path.c_str());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const bool multiEditable = HasEditorPropertyFlag(property->flags, EditorPropertyFlags::MultiEdit);
            const bool mixed = multiEditable && registry->HasMixedValue(componentTargets, property->path);
            const bool prefabOverride =
                HasEditorPropertyFlag(property->flags, EditorPropertyFlags::PrefabOverride) &&
                IsPrefabPropertyOverridden(primary, property->path);
            if (prefabOverride) {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.63f, 0.20f, 1.0f),
                    mixed ? "%s * —" : "%s *",
                    property->displayName.c_str());
            }
            else if (mixed) {
                ImGui::TextDisabled("%s —", property->displayName.c_str());
            }
            else {
                ImGui::TextUnformatted(property->displayName.c_str());
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextDisabled("%s", property->path.c_str());
                if (prefabOverride) {
                    ImGui::TextUnformatted("Prefab Override");
                }
                ImGui::EndTooltip();
            }

            ImGui::TableSetColumnIndex(1);
            nlohmann::json value = registry->GetValue(primary, property->path);
            const bool readOnly = HasEditorPropertyFlag(property->flags, EditorPropertyFlags::ReadOnly);
            ImGui::BeginDisabled(readOnly);
            EditorPropertyDrawOptions options;
            options.compact = true;
            options.mixed = mixed;
            const bool changed = EditorPropertyDrawer::DrawValue(*property, value, "##Value", options);
            ImGui::EndDisabled();

            if (!multiEditable && componentTargets.size() > 1) {
                ImGui::SameLine();
                ImGui::TextDisabled("代表のみ");
            }

            if (changed && !readOnly) {
                for (Object3d* target : componentTargets) {
                    if (!target || (!multiEditable && target != primary) ||
                        !registry->IsApplicable(target, property->path)) {
                        continue;
                    }
                    registry->SetValue(target, property->path, value);
                }
                MarkInspectorTargetsDirty(editor, componentTargets);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Unindent();
}

void DrawComponentPanel(DebugEditor* editor, Object3d* primary, const std::vector<Object3d*>& targets) {
    if (!primary) {
        return;
    }

    ImGui::SeparatorText("Components");
    ImGui::TextDisabled("Object3Dの既存機能をUnity風のComponentとして追加/削除します。");
    ImGui::TextDisabled("Prefab InstanceではRegistry Propertyの差分をOverrideとして追跡します。");

    if (ImGui::Button("Add Component", ImVec2(-1.0f, 30.0f))) {
        ImGui::OpenPopup("AddRegisteredComponentMenu");
    }
    DrawRegisteredAddComponentMenu(editor, targets, primary);

    ImGui::Spacing();
    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    const std::vector<const EditorComponentDescriptor*> registeredComponents =
        registry->GetComponentsForObject(primary);
    bool hasOptionalComponent = false;
    for (const EditorComponentDescriptor* component : registeredComponents) {
        if (!component || !component->showInInspector) {
            continue;
        }
        hasOptionalComponent |= component->removable;
        DrawRegisteredComponentRow(editor, targets, *component);
        DrawAutomaticComponentProperties(editor, primary, targets, *component);
    }

    if (!hasOptionalComponent) {
        ImGui::TextDisabled("追加済みの任意Componentはありません。Add Componentから追加できます。");
    }
}}

namespace {

bool DrawSceneObjectNameCombo(const char* label, BaseScene* scene, Object3d* cameraObject, std::string& objectName) {
    bool changed = false;
    const char* preview = objectName.empty() ? "(未選択)" : objectName.c_str();
    if (!ImGui::BeginCombo(label, preview)) {
        return false;
    }

    if (ImGui::Selectable("(未選択)", objectName.empty())) {
        objectName.clear();
        changed = true;
    }
    if (scene) {
        for (const auto& object : scene->GetObjects()) {
            if (!object || object->IsEditorInternal() || object.get() == cameraObject || object->GetName().empty()) {
                continue;
            }
            const bool selected = objectName == object->GetName();
            if (ImGui::Selectable(object->GetName().c_str(), selected)) {
                objectName = object->GetName();
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
    }
    ImGui::EndCombo();
    return changed;
}

bool HasMixedInspectorValue(const std::vector<Object3d*>& targets, const char* propertyPath) {
    return EditorPropertyRegistry::GetInstance()->HasMixedValue(targets, propertyPath);
}

void DrawMixedValueHint(bool mixed) {
    if (!mixed) {
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("— 複数の値");
}

void PushMixedCheckbox(bool mixed) {
    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed);
}

void PopMixedCheckbox() {
    ImGui::PopItemFlag();
}

void DrawCameraObjectInspector(DebugEditor* editor, BaseScene* scene, Object3d* cameraObject) {
    if (!editor || !cameraObject) {
        return;
    }

    // 旧CinematicCameraを選択した時点で、新しい保存形式へ安全に移行します。
    cameraObject->SetClassName("Camera");
    cameraObject->SetSaveCategory("Camera");
    SceneCameraSettings& settings = cameraObject->GetSceneCameraSettings();
    bool changed = false;

    ImGui::SeparatorText("Camera Object");
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), ICON_FA_VIDEO " Camera専用Inspector");
    ImGui::TextDisabled("このObject自身が編集・保存・プレビュー・Ghost Recorder再生の元データです。");
    if (ImGui::Checkbox("有効", &settings.enabled)) changed = true;

    const char* roleNames[] = { "Main", "Cinematic" };
    int role = static_cast<int>(settings.role);
    if (ImGui::Combo("役割", &role, roleNames, IM_ARRAYSIZE(roleNames))) {
        settings.role = static_cast<SceneCameraRole>(role);
        changed = true;
    }

    ImGui::SeparatorText("Transform");
    Transform* transform = cameraObject->GetTransform();
    if (ImGui::DragFloat3("位置", &transform->translate.x, 0.1f)) changed = true;
    Vector3 rotationDegrees = {
        ToDegrees(transform->rotate.x),
        ToDegrees(transform->rotate.y),
        ToDegrees(transform->rotate.z),
    };
    if (ImGui::DragFloat3("回転 (度)", &rotationDegrees.x, 0.5f)) {
        transform->rotate = {
            ToRadians(rotationDegrees.x),
            ToRadians(rotationDegrees.y),
            ToRadians(rotationDegrees.z),
        };
        transform->isQuaternionMaster = false;
        changed = true;
    }
    if (changed) {
        cameraObject->UpdateLocalMatrix();
        cameraObject->UpdateWorldMatrix();
    }

    ImGui::SeparatorText("Lens / Blend");
    float fovDegrees = ToDegrees(settings.fovY);
    if (ImGui::SliderFloat("FOV (度)", &fovDegrees, 10.0f, 120.0f, "%.1f")) {
        settings.fovY = ToRadians(fovDegrees);
        changed = true;
    }
    if (ImGui::DragFloat("Near Clip", &settings.nearClip, 0.01f, 0.001f, 100.0f, "%.3f")) changed = true;
    if (ImGui::DragFloat("Far Clip", &settings.farClip, 1.0f, 1.0f, 100000.0f, "%.1f")) changed = true;
    settings.nearClip = (std::max)(settings.nearClip, 0.001f);
    settings.farClip = (std::max)(settings.farClip, settings.nearClip + 0.01f);
    if (ImGui::DragFloat("Blend In (秒)", &settings.blendInDuration, 0.02f, 0.0f, 10.0f, "%.2f")) changed = true;
    if (ImGui::DragFloat("Blend Out (秒)", &settings.blendOutDuration, 0.02f, 0.0f, 10.0f, "%.2f")) changed = true;
    const char* easingNames[] = { "Linear", "Ease In", "Ease Out", "Ease In Out", "Smoother Step" };
    int easing = static_cast<int>(settings.easing);
    if (ImGui::Combo("切替Easing", &easing, easingNames, IM_ARRAYSIZE(easingNames))) {
        settings.easing = static_cast<SceneCameraEasing>(easing);
        changed = true;
    }

    ImGui::SeparatorText("Eye / Follow");
    const char* eyeSourceNames[] = { "Camera ObjectのTransform", "Scene Objectを追従" };
    int eyeSource = static_cast<int>(settings.eyeSource);
    if (ImGui::Combo("Eye取得元", &eyeSource, eyeSourceNames, IM_ARRAYSIZE(eyeSourceNames))) {
        settings.eyeSource = static_cast<SceneCameraEyeSource>(eyeSource);
        changed = true;
    }
    if (settings.eyeSource == SceneCameraEyeSource::kSceneObject) {
        if (DrawSceneObjectNameCombo("追従Object##CameraEye", scene, cameraObject, settings.eyeObjectName)) changed = true;
    }
    if (ImGui::DragFloat3("Eyeオフセット", &settings.eyeOffset.x, 0.05f)) changed = true;
    const char* followModeNames[] = { "完全追従", "遅延追従" };
    int eyeFollow = static_cast<int>(settings.eyeFollowMode);
    if (ImGui::Combo("Eye追従方式", &eyeFollow, followModeNames, IM_ARRAYSIZE(followModeNames))) {
        settings.eyeFollowMode = static_cast<SceneCameraFollowMode>(eyeFollow);
        changed = true;
    }
    if (settings.eyeFollowMode == SceneCameraFollowMode::kSmooth &&
        ImGui::DragFloat("Eye追従レスポンス", &settings.eyeFollowResponse, 0.1f, 0.1f, 40.0f, "%.2f")) {
        changed = true;
    }

    ImGui::SeparatorText("Target / Follow");
    const char* targetModeNames[] = { "固定座標", "Scene Objectを注視", "Cameraの前方" };
    int targetMode = static_cast<int>(settings.targetMode);
    if (ImGui::Combo("注視方法", &targetMode, targetModeNames, IM_ARRAYSIZE(targetModeNames))) {
        settings.targetMode = static_cast<SceneCameraTargetMode>(targetMode);
        changed = true;
    }
    if (settings.targetMode == SceneCameraTargetMode::kFixedPoint) {
        if (ImGui::DragFloat3("固定注視点", &settings.fixedTarget.x, 0.1f)) changed = true;
    }
    else if (settings.targetMode == SceneCameraTargetMode::kSceneObject) {
        if (DrawSceneObjectNameCombo("注視Object##CameraTarget", scene, cameraObject, settings.targetObjectName)) changed = true;
    }
    else {
        if (ImGui::DragFloat("前方距離", &settings.forwardDistance, 0.1f, 0.1f, 1000.0f, "%.1f")) changed = true;
    }
    if (settings.targetMode != SceneCameraTargetMode::kFixedPoint) {
        if (ImGui::DragFloat3("注視オフセット", &settings.targetOffset.x, 0.05f)) changed = true;
        int targetFollow = static_cast<int>(settings.targetFollowMode);
        if (ImGui::Combo("Target追従方式", &targetFollow, followModeNames, IM_ARRAYSIZE(followModeNames))) {
            settings.targetFollowMode = static_cast<SceneCameraFollowMode>(targetFollow);
            changed = true;
        }
        if (settings.targetFollowMode == SceneCameraFollowMode::kSmooth &&
            ImGui::DragFloat("Target追従レスポンス", &settings.targetFollowResponse, 0.1f, 0.1f, 40.0f, "%.2f")) {
            changed = true;
        }
    }

    ImGui::SeparatorText("Ghost Recorder");
    const std::string recordPreview = cameraObject->GetRecordPathName().empty() ? "(なし)" : cameraObject->GetRecordPathName();
    if (ImGui::BeginCombo("パスデータ", recordPreview.c_str())) {
        if (ImGui::Selectable("(なし)", cameraObject->GetRecordPathName().empty())) {
            cameraObject->SetRecordPathName("");
            if (cameraObject->recorder_) cameraObject->recorder_->Stop();
            changed = true;
        }
        const fs::path directory = "Resources/json/animation";
        if (fs::exists(directory) && fs::is_directory(directory)) {
            for (const auto& entry : fs::directory_iterator(directory)) {
                if (entry.path().extension() != ".json") continue;
                const std::string pathName = entry.path().stem().string();
                const bool selected = cameraObject->GetRecordPathName() == pathName;
                if (ImGui::Selectable(pathName.c_str(), selected)) {
                    cameraObject->SetRecordPathName(pathName);
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    bool recordLoop = cameraObject->IsRecordLoop();
    if (ImGui::Checkbox("ループ再生##CameraRecord", &recordLoop)) {
        cameraObject->SetRecordLoop(recordLoop);
        changed = true;
    }
    bool recordRelative = cameraObject->IsRecordRelative();
    if (ImGui::Checkbox("相対座標##CameraRecord", &recordRelative)) {
        cameraObject->SetRecordRelative(recordRelative);
        changed = true;
    }

    if (ImGui::Button(ICON_FA_PLAY " Cameraをテスト再生")) {
        CameraEditor::GetInstance()->PlaySceneObjectCamera(CameraManager::GetInstance()->GetMainCamera(), cameraObject);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_STOP " 停止##CameraObject")) {
        if (cameraObject->recorder_) cameraObject->recorder_->Stop();
        CameraEditor::GetInstance()->StopSceneObjectCamera(CameraManager::GetInstance()->GetMainCamera());
    }
    if (!cameraObject->GetRecordPathName().empty() && ImGui::Button(ICON_FA_GHOST " Ghost Recorder再生")) {
        if (!cameraObject->recorder_) cameraObject->InitializeRecorder(nullptr);
        if (cameraObject->recorder_) {
            cameraObject->recorder_->Play(
                cameraObject->GetRecordPathName(),
                cameraObject->IsRecordLoop(),
                cameraObject->IsRecordRelative(),
                true);
        }
    }

    if (changed) {
        editor->MarkDirtyForObject(cameraObject);
    }
}

std::vector<Object3d*> CollectSceneObjects(BaseScene* scene) {
    std::vector<Object3d*> objects;
    if (!scene) {
        return objects;
    }
    objects.reserve(scene->GetObjects().size());
    for (const auto& object : scene->GetObjects()) {
        if (object && !object->IsEditorInternal()) {
            objects.push_back(object.get());
        }
    }
    return objects;
}

std::vector<Object3d*> CollectPrefabInstanceObjects(BaseScene* scene, const std::string& instanceId) {
    std::vector<Object3d*> objects;
    if (!scene || instanceId.empty()) {
        return objects;
    }
    for (const auto& object : scene->GetObjects()) {
        if (object && object->IsPrefabInstance() &&
            object->GetPrefabInstanceInfo().instanceId == instanceId) {
            objects.push_back(object.get());
        }
    }
    return objects;
}

void DrawPrefabInstancePanel(DebugEditor* editor, BaseScene* scene, Object3d* object) {
    if (!editor || !scene || !object || !object->IsPrefabInstance()) {
        return;
    }

    PresetManager* manager = PresetManager::GetInstance();
    const auto info = object->GetPrefabInstanceInfo();
    const auto overrides = manager->GetPrefabOverrides(object);
    const auto componentOverrides = manager->GetPrefabComponentOverrides(object);
    const auto variantOverrides = manager->GetPrefabVariantOverrides(object);
    const auto variantComponentOverrides = manager->GetPrefabVariantComponentOverrides(object);
    const auto structureSummary = manager->GetPrefabStructureOverrideSummary(info.prefabName);

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.33f, 0.50f, 1.0f));
    const bool open = ImGui::CollapsingHeader(ICON_FA_CUBES " Prefab Instance", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor();
    if (!open) {
        return;
    }

    ImGui::Text("Prefab: %s", info.prefabName.empty() ? "(名称不明)" : info.prefabName.c_str());
    const std::string basePrefabName = manager->GetPrefabBaseName(info.prefabName);
    if (!basePrefabName.empty()) {
        ImGui::TextColored(ImVec4(0.70f, 0.55f, 1.0f, 1.0f),
            ICON_FA_CODE_BRANCH " Variant of: %s", basePrefabName.c_str());
    }
    ImGui::TextDisabled("Asset ID: %s", info.assetId.c_str());
    ImGui::TextDisabled("Instance ID: %s", info.instanceId.c_str());
    if (editor->IsPrefabEditMode()) {
        ImGui::TextColored(ImVec4(0.35f, 0.78f, 1.0f, 1.0f),
            ICON_FA_CUBE " Prefab ModeでAssetを直接編集中です。");
        ImGui::TextDisabled("保存・破棄はHierarchy上部から実行します。");
        ImGui::Separator();
        return;
    }
    if (!manager->HasValidPrefabSource(object)) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.25f, 1.0f),
            ICON_FA_EXCLAMATION_TRIANGLE " 元Prefabが見つかりません。UnpackするかPrefab Assetを復元してください。");
    }
    if (info.isRoot) {
        ImGui::TextDisabled("ルートの位置・回転はScene配置値として保持されます。");
        if (ImGui::Button(ICON_FA_CUBE " Prefab Modeで開く")) {
            editor->BeginPrefabEditSession(info.prefabName);
            return;
        }
    }

    const std::vector<Object3d*> sceneObjects = CollectSceneObjects(scene);
    const std::vector<Object3d*> instanceObjects = CollectPrefabInstanceObjects(scene, info.instanceId);

    if (ImGui::Button(ICON_FA_UPLOAD " Apply All", ImVec2(112.0f, 0.0f))) {
        EditorTransactionManager::GetInstance()->BeginGroup("Apply All Prefab Overrides");
        const auto beforeStates = editor->CaptureObjectStates(sceneObjects);
        const int applied = manager->ApplyAllPrefabOverrides(object, sceneObjects);
        editor->RegisterObjectsEdited(beforeStates, "Prefab Apply Propagation");
        EditorTransactionManager::GetInstance()->EndGroup();
        DebugConsole::GetInstance()->AddLog("Prefab Apply All: " + std::to_string(applied) + " overrides");
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_UNDO " Revert All", ImVec2(112.0f, 0.0f))) {
        const auto beforeStates = editor->CaptureObjectStates(instanceObjects);
        const int reverted = manager->RevertAllPrefabOverrides(object, sceneObjects);
        editor->RegisterObjectsEdited(beforeStates, "Prefab Revert All");
        DebugConsole::GetInstance()->AddLog("Prefab Revert All: " + std::to_string(reverted) + " overrides");
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_UNLINK " Unpack", ImVec2(96.0f, 0.0f))) {
        const auto beforeStates = editor->CaptureObjectStates(instanceObjects);
        const int unpacked = manager->UnpackPrefabInstance(object, sceneObjects);
        editor->RegisterObjectsEdited(beforeStates, "Unpack Prefab Instance");
        DebugConsole::GetInstance()->AddLog("Prefab Unpack: " + std::to_string(unpacked) + " objects");
    }

    ImGui::SeparatorText(("Component Overrides (" + std::to_string(componentOverrides.size()) + ")").c_str());
    if (componentOverrides.empty()) {
        ImGui::TextDisabled("このObjectにComponent構造差分はありません。");
    }
    for (const auto& entry : componentOverrides) {
        ImGui::PushID(("Component_" + entry.componentTypeId).c_str());
        ImGui::Text("%s", entry.displayName.c_str());
        ImGui::SameLine();
        ImGui::TextColored(
            entry.instancePresent ? ImVec4(0.45f, 0.90f, 0.55f, 1.0f) : ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
            entry.instancePresent ? "追加" : "削除");

        if (ImGui::SmallButton(ICON_FA_UPLOAD " Apply")) {
            EditorTransactionManager::GetInstance()->BeginGroup("Apply Prefab Component Override");
            const auto beforeStates = editor->CaptureObjectStates(sceneObjects);
            if (manager->ApplyPrefabComponent(object, entry.componentTypeId, sceneObjects)) {
                editor->RegisterObjectsEdited(beforeStates, "Prefab Component Apply Propagation");
                DebugConsole::GetInstance()->AddLog("Prefab Component Apply: " + entry.componentTypeId);
            }
            EditorTransactionManager::GetInstance()->EndGroup();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_UNDO " Revert")) {
            const auto beforeStates = editor->CaptureObjectStates(instanceObjects);
            if (manager->RevertPrefabComponent(object, entry.componentTypeId)) {
                editor->RegisterObjectsEdited(beforeStates, "Prefab Component Revert");
                DebugConsole::GetInstance()->AddLog("Prefab Component Revert: " + entry.componentTypeId);
            }
        }
        ImGui::PopID();
    }

    ImGui::SeparatorText(("Property Overrides (" + std::to_string(overrides.size()) + ")").c_str());
    if (overrides.empty()) {
        ImGui::TextDisabled("このObjectに適用差分はありません。");
    }
    for (const auto& entry : overrides) {
        ImGui::PushID(entry.propertyPath.c_str());
        ImGui::Text("%s", entry.displayName.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", entry.propertyPath.c_str());

        if (ImGui::SmallButton(ICON_FA_UPLOAD " Apply")) {
            EditorTransactionManager::GetInstance()->BeginGroup("Apply Prefab Override");
            const auto beforeStates = editor->CaptureObjectStates(sceneObjects);
            if (manager->ApplyPrefabProperty(object, entry.propertyPath, sceneObjects)) {
                editor->RegisterObjectsEdited(beforeStates, "Prefab Apply Propagation");
                DebugConsole::GetInstance()->AddLog("Prefab Apply: " + entry.propertyPath);
            }
            EditorTransactionManager::GetInstance()->EndGroup();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_UNDO " Revert")) {
            const auto beforeStates = editor->CaptureObjectStates(instanceObjects);
            if (manager->RevertPrefabProperty(object, entry.propertyPath)) {
                editor->RegisterObjectsEdited(beforeStates, "Prefab Revert Property");
                DebugConsole::GetInstance()->AddLog("Prefab Revert: " + entry.propertyPath);
            }
        }
        ImGui::PopID();
    }

    if (!basePrefabName.empty()) {
        const std::size_t variantOverrideCount = variantOverrides.size() + variantComponentOverrides.size();
        ImGui::SeparatorText(("Variant Source Overrides (" + std::to_string(variantOverrideCount) + ")").c_str());
        ImGui::TextDisabled("InstanceのApplyで作られた、基底Prefabに対するVariant固有差分です。");
        if (structureSummary.HasOverrides()) {
            ImGui::Text("構造差分: Object追加 %d / 削除 %d / 移動 %d / Component %d / その他 %d",
                structureSummary.addedObjects,
                structureSummary.removedObjects,
                structureSummary.reparentedObjects,
                structureSummary.componentOverrides,
                structureSummary.rawNodeOverrides);
        }
        if (variantOverrides.empty() && variantComponentOverrides.empty()) {
            ImGui::TextDisabled("このObjectにはVariant固有差分がありません。");
        }
        for (const auto& entry : variantComponentOverrides) {
            ImGui::PushID(("VariantComponent_" + entry.componentTypeId).c_str());
            ImGui::Text("%s", entry.displayName.c_str());
            ImGui::SameLine();
            ImGui::TextColored(
                entry.variantPresent ? ImVec4(0.45f, 0.90f, 0.55f, 1.0f) : ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
                entry.variantPresent ? "追加" : "削除");
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_UNDO " Baseへ戻す")) {
                EditorTransactionManager::GetInstance()->BeginGroup("Revert Prefab Variant Component Override");
                const auto beforeStates = editor->CaptureObjectStates(sceneObjects);
                if (manager->RevertPrefabVariantComponent(object, entry.componentTypeId, sceneObjects)) {
                    editor->RegisterObjectsEdited(beforeStates, "Variant Component Revert Propagation");
                    DebugConsole::GetInstance()->AddLog("Prefab Variant Component Revert: " + entry.componentTypeId);
                }
                EditorTransactionManager::GetInstance()->EndGroup();
            }
            ImGui::PopID();
        }
        for (const auto& entry : variantOverrides) {
            ImGui::PushID(("Variant_" + entry.propertyPath).c_str());
            ImGui::Text("%s", entry.displayName.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", entry.propertyPath.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_UNDO " Baseへ戻す")) {
                EditorTransactionManager::GetInstance()->BeginGroup("Revert Prefab Variant Override");
                const auto beforeStates = editor->CaptureObjectStates(sceneObjects);
                if (manager->RevertPrefabVariantProperty(object, entry.propertyPath, sceneObjects)) {
                    editor->RegisterObjectsEdited(beforeStates, "Variant Revert Propagation");
                    DebugConsole::GetInstance()->AddLog("Prefab Variant Revert: " + entry.propertyPath);
                }
                EditorTransactionManager::GetInstance()->EndGroup();
            }
            ImGui::PopID();
        }
    }
    ImGui::Separator();
}

std::string NormalizeMaterialInstanceSavePath(const char* text) {
    fs::path path = text ? fs::path(text) : fs::path();
    if (path.empty()) return {};
    if (path.extension() != ".json") path.replace_extension(".json");
    if (!path.has_parent_path()) path = fs::path("Resources/json/material_instances") / path;
    return path.lexically_normal().generic_string();
}

void DrawMaterialInstancePanel(
    DebugEditor* editor,
    Object3d* selectedObject,
    const std::vector<Object3d*>& targets) {
    if (!editor || !selectedObject) return;

    ImGui::SeparatorText("Material Instance");
    const std::string& linkedPath = selectedObject->GetMaterialInstancePath();
    ImGui::Text("リンク: %s", linkedPath.empty() ? "未設定" : linkedPath.c_str());
    ImGui::TextDisabled("リンク中はScene読込時にAssetの設定一式を再適用します。");

    static std::vector<std::string> assets;
    static bool assetsLoaded = false;
    if (!assetsLoaded) {
        assets = MaterialInstanceAsset::Discover();
        assetsLoaded = true;
    }
    if (ImGui::BeginCombo("共有Materialを選択", linkedPath.empty() ? "未設定" : linkedPath.c_str())) {
        for (const std::string& asset : assets) {
            const bool selected = asset == linkedPath;
            if (ImGui::Selectable(asset.c_str(), selected)) {
                std::string error;
                bool applied = false;
                for (Object3d* target : targets) {
                    applied |= target && target->ApplyMaterialInstance(asset, &error);
                }
                if (applied) {
                    MarkInspectorTargetsDirty(editor, targets);
                    DebugConsole::GetInstance()->AddLog("Material Instance applied: " + asset);
                } else if (!error.empty()) {
                    DebugConsole::GetInstance()->AddLog("Material Instance error: " + error);
                }
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        if (assets.empty()) ImGui::TextDisabled("保存済みAssetはまだありません。");
        ImGui::EndCombo();
    }

    if (!linkedPath.empty()) {
        if (ImGui::Button(ICON_FA_SYNC " 再読込##MaterialInstance")) {
            std::string error;
            bool applied = false;
            for (Object3d* target : targets) {
                if (!target) continue;
                const std::string path = target->GetMaterialInstancePath().empty()
                    ? linkedPath : target->GetMaterialInstancePath();
                applied |= target->ApplyMaterialInstance(path, &error);
            }
            if (applied) MarkInspectorTargetsDirty(editor, targets);
            if (!error.empty()) DebugConsole::GetInstance()->AddLog("Material Instance error: " + error);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UNLINK " リンク解除##MaterialInstance")) {
            for (Object3d* target : targets) if (target) target->ClearMaterialInstanceLink();
            MarkInspectorTargetsDirty(editor, targets);
        }
    }

    static char savePath[260] = "NewMaterial.json";
    ImGui::SetNextItemWidth(-130.0f);
    ImGui::InputText("##MaterialInstanceSavePath", savePath, sizeof(savePath));
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SAVE " 現在値を保存")) {
        const std::string path = NormalizeMaterialInstanceSavePath(savePath);
        std::string error;
        if (selectedObject->SaveMaterialInstance(path, &error)) {
            for (Object3d* target : targets) {
                if (target && target != selectedObject) target->ApplyMaterialInstance(path, nullptr);
            }
            assets = MaterialInstanceAsset::Discover();
            MarkInspectorTargetsDirty(editor, targets);
            DebugConsole::GetInstance()->AddLog("Material Instance saved: " + path);
        } else {
            DebugConsole::GetInstance()->AddLog("Material Instance error: " + error);
        }
    }
}

void DrawDecalPanel(
    DebugEditor* editor,
    Object3d* selectedObject,
    const std::vector<Object3d*>& targets) {
    if (!editor || !selectedObject || !selectedObject->IsDecal()) return;
    if (!ImGui::CollapsingHeader("Surface Decal", ImGuiTreeNodeFlags_DefaultOpen)) return;

    Object3d::DecalSettings settings = selectedObject->GetDecalSettings();
    bool changed = false;
    changed |= ImGui::DragFloat2("投影サイズ", &settings.size.x, 0.05f, 0.01f, 100.0f);
    changed |= ImGui::DragFloat("面オフセット", &settings.depthOffset, 0.001f, 0.0001f, 0.2f, "%.4f");
    changed |= ImGui::DragFloat("寿命 (0=永続)", &settings.lifetime, 0.05f, 0.0f, 120.0f);
    changed |= ImGui::DragFloat("Fade In", &settings.fadeIn, 0.02f, 0.0f, 10.0f);
    changed |= ImGui::DragFloat("Fade Out", &settings.fadeOut, 0.02f, 0.0f, 10.0f);
    changed |= ImGui::Checkbox("寿命後に削除対象", &settings.transient);

    if (changed) {
        for (Object3d* target : targets) {
            if (!target || !target->IsDecal()) continue;
            const Object3d::DecalSettings oldSettings = target->GetDecalSettings();
            target->SetDecalSettings(settings);
            Transform* transform = target->GetTransform();
            const Matrix4x4 rotation = transform->isQuaternionMaster
                ? Math::MakeRotateQuaternionMatrix(transform->quaternion)
                : Math::MakeRotateMatrix(transform->rotate);
            const Vector3 normal = { -rotation.m[2][0], -rotation.m[2][1], -rotation.m[2][2] };
            transform->translate += normal * (settings.depthOffset - oldSettings.depthOffset);
            transform->scale = { settings.size.x * 0.5f, settings.size.y * 0.5f, 1.0f };
            target->UpdateLocalMatrix();
            target->UpdateWorldMatrix();
        }
        MarkInspectorTargetsDirty(editor, targets);
    }

    if (ImGui::Button(ICON_FA_PLAY " Fadeを最初から確認")) {
        for (Object3d* target : targets) if (target && target->IsDecal()) target->RestartDecalPlayback();
    }
    ImGui::TextDisabled("Game View配置では地面・壁の法線へ自動整列します。回転はTransformで調整できます。");
}

}

void InspectorWindow::Initialize(DebugEditor* editor) {
    editor_ = editor;
}

void InspectorWindow::Draw() {
#ifdef USE_IMGUI
    if (editor_->GetSceneManager() == nullptr) return;
    BaseScene* currentScene = editor_->GetSceneManager()->GetCurrentScene();
    if (currentScene == nullptr) return;

    std::string currentJsonPath = "Resources/json/3Dobject/" + std::string(editor_->GetCurrentSceneFilenameBuffer());
    Object3d* selectedObject = editor_->GetSelectedObject();
    const std::size_t selectedCount = editor_->GetSelectedObjectCount();

    // ---------------------------------------------------------
    // 2. オブジェクト詳細 (Inspector本体)
    // ---------------------------------------------------------
    if (selectedObject == nullptr) {
        ImGui::TextDisabled(ICON_FA_EXCLAMATION_CIRCLE " オブジェクトが選択されていません");
        ImGui::TextDisabled("Hierarchyから選択してください");
        ImGui::Separator();
        // ゲッター経由での描画フラグ制御
        ImGui::Checkbox(ICON_FA_EYE " コライダー枠を描画", editor_->GetDrawCollidersPtr());
        ImGui::Checkbox(ICON_FA_FINGERPRINT " イベントIDを表示", editor_->GetDrawEventIDsPtr());
    }
    else {
        std::vector<Object3d*> inspectorTargets = CollectInspectorTargets(editor_, selectedObject);
        const bool isMultiSelection = inspectorTargets.size() > 1;

        if (isMultiSelection) {
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f),
                ICON_FA_CUBES " %zu個選択中 / 共通項目は一括編集されます", inspectorTargets.size());
            ImGui::TextDisabled("名前・親子・モデル分割など、代表Object専用の項目は代表Objectだけを編集します。");
            ImGui::Separator();
        }

        DrawPrefabInstancePanel(editor_, currentScene, selectedObject);

        // --- 名前表示 ---
        char nameBuffer[256];
        std::string currentName = selectedObject->GetName();
        if (currentName.empty()) currentName = "NoName";
        strcpy_s(nameBuffer, currentName.c_str());

        if (ImGui::InputText(ICON_FA_TAG " 名前", nameBuffer, sizeof(nameBuffer))) {
            selectedObject->SetName(std::string(nameBuffer));
        }
        ImGui::Spacing();

        if (editor_->GetIsPathEditMode()) {
            // 編集モード中は目立つ色（オレンジ）にして警告を表示
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.4f, 0.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.5f, 0.2f, 1.0f));
            if (ImGui::Button(ICON_FA_CHECK_CIRCLE " パス編集を完了してロックを解除 (Exit Edit Mode)", ImVec2(-1, 45))) {
                editor_->SetIsPathEditMode(false);
                if (selectedObject->recorder_) selectedObject->recorder_->DeselectPin();
            }
            ImGui::PopStyleColor(2);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " 現在パスを編集中です。");
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "    本体の移動・選択・削除はロックされています。");
        }
        else {
            if (ImGui::Button(ICON_FA_PENCIL_ALT " パスの軌跡を編集する (Enter Path Edit Mode)", ImVec2(-1, 35))) {
                editor_->SetIsPathEditMode(true);
            }
        }
        ImGui::Separator();

        // =========================================================================
        // 🚨 ここから下は「パス編集中」はロック（操作不能）にするエリア
        // =========================================================================
        ImGui::BeginDisabled(editor_->GetIsPathEditMode());
        ImGui::Spacing();

        if (ImGui::Button(ICON_FA_COPY " 複製 (Duplicate)")) {
            EditorCommandRegistry::GetInstance()->Execute(EditorCommandId::EditDuplicate);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_DOWNLOAD " 単体保存 (JSON更新)")) {
            editor_->SaveSingleObject();
        }
        ImGui::Spacing();

        // --- クラス名表示 ---
        ImGui::TextDisabled(ICON_FA_CUBES " クラス: %s", selectedObject->GetClassName().c_str());

        if (selectedObject->IsCameraObject()) {
            DrawCameraObjectInspector(editor_, currentScene, selectedObject);
            ImGui::EndDisabled();
            return;
        }

        const bool isManagedCharacter = GameplayStatusManager::IsManagedCharacter(selectedObject);

        static Object3d* tagLayerBufferOwner = nullptr;
        static char tagBuffer[128] = {};
        static char layerBuffer[128] = {};
        if (tagLayerBufferOwner != selectedObject) {
            tagLayerBufferOwner = selectedObject;
            strcpy_s(tagBuffer, selectedObject->GetTag().c_str());
            const std::string currentLayer = selectedObject->GetLayer().empty() ? "Default" : selectedObject->GetLayer();
            strcpy_s(layerBuffer, currentLayer.c_str());
        }

        ImGui::SeparatorText("Tag / Layer");
        ImGui::TextDisabled("Tagは役割名、Layerは処理対象の分類として使います。");
        const bool mixedTag = HasMixedInspectorValue(inspectorTargets, "identity.tag");
        if (ImGui::InputText("Tag", tagBuffer, sizeof(tagBuffer))) {
            const std::string newTag = tagBuffer;
            for (Object3d* target : inspectorTargets) {
                if (target) {
                    target->SetTag(newTag);
                }
            }
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }
        DrawMixedValueHint(mixedTag);

        const char* layerPresets[] = {
            "Default",
            "Player",
            "Enemy",
            "Stage",
            "Trigger",
            "Effect",
            "EditorOnly"
        };
        int layerPresetIndex = 0;
        const std::string selectedLayer = selectedObject->GetLayer().empty() ? "Default" : selectedObject->GetLayer();
        for (int i = 0; i < IM_ARRAYSIZE(layerPresets); ++i) {
            if (selectedLayer == layerPresets[i]) {
                layerPresetIndex = i;
                break;
            }
        }
        const bool mixedLayer = HasMixedInspectorValue(inspectorTargets, "identity.layer");
        if (ImGui::Combo("Layer Preset", &layerPresetIndex, layerPresets, IM_ARRAYSIZE(layerPresets))) {
            strcpy_s(layerBuffer, layerPresets[layerPresetIndex]);
            const std::string newLayer = layerBuffer;
            for (Object3d* target : inspectorTargets) {
                if (target) {
                    target->SetLayer(newLayer);
                }
            }
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }
        DrawMixedValueHint(mixedLayer);
        if (ImGui::InputText("Layer", layerBuffer, sizeof(layerBuffer))) {
            const std::string newLayer = layerBuffer[0] == '\0' ? "Default" : std::string(layerBuffer);
            if (layerBuffer[0] == '\0') {
                strcpy_s(layerBuffer, "Default");
            }
            for (Object3d* target : inspectorTargets) {
                if (target) {
                    target->SetLayer(newLayer);
                }
            }
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }
        DrawMixedValueHint(mixedLayer);


        DrawComponentPanel(editor_, selectedObject, inspectorTargets);

        const char* saveCategories[] = { "Object", "Player", "Enemy" };
        std::string currentCat = selectedObject->GetSaveCategory();
        int catIndex = 0;
        if (currentCat == "Player") catIndex = 1;
        else if (currentCat == "Enemy") catIndex = 2;

        const bool mixedSaveCategory = HasMixedInspectorValue(inspectorTargets, "identity.saveCategory");
        if (ImGui::Combo(ICON_FA_FOLDER " 保存先カテゴリ", &catIndex, saveCategories, IM_ARRAYSIZE(saveCategories))) {
            for (Object3d* object : inspectorTargets) {
                if (!object) continue;
                object->SetSaveCategory(saveCategories[catIndex]);
            }
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }
        DrawMixedValueHint(mixedSaveCategory);

        // --- 親の名前表示 ---
        if (selectedObject->GetParent()) {
            ImGui::TextDisabled(ICON_FA_SITEMAP " 親: %s", selectedObject->GetParent()->GetName().c_str());
            if (ImGui::Button(ICON_FA_UNLINK " 親を解除 (Unparent)")) {
                selectedObject->SetParent(nullptr, true);
            }
        }
        else {
            ImGui::TextDisabled(ICON_FA_SITEMAP " 親: なし");
        }

        // --- Model Asset (InvisibleBoxでない場合のみ表示) ---
        if (selectedObject->GetClassName() != "InvisibleBox") {
            ImGui::Separator();
            ImGui::Text(ICON_FA_CUBE " モデルアセット: %s", selectedObject->GetModelName().c_str());
            if (isManagedCharacter) {
                ImGui::TextDisabled("Player/Enemyのモデルはステータス管理のタイプ共通設定です。");
                if (ImGui::Button(ICON_FA_SLIDERS_H " ステータス管理を開く##ModelStatus", ImVec2(-1, 0)) && editor_->GetStatusTuningWindow()) {
                    EditorManager::GetInstance()->SetSelectedObject(editor_->GetStatusTuningWindow());
                }
            } else {
                ImGui::Button(ICON_FA_BOX_OPEN " [ ここにモデルをドロップして変更 ] ", ImVec2(-1, 30));

                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
                        const std::string modelName = ReadEditorAssetDragPath(payload->Data, payload->DataSize);
                        if (!modelName.empty()) {
                            ModelManager::GetInstance()->LoadModel(modelName);
                            selectedObject->SetModel(modelName);
                            DebugConsole::GetInstance()->AddLog("Switched model to: " + modelName);
                        }
                    }

                    // プリセットデータのドロップ受付
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PRESET_ASSET")) {
                        const char* presetName = (const char*)payload->Data;
                        PresetManager::GetInstance()->ApplyPresetToObject(presetName, selectedObject);
                        DebugConsole::GetInstance()->AddLog("Applied preset: " + std::string(presetName));
                    }
                    ImGui::EndDragDropTarget();
                }
            }

            Model* currentModel = selectedObject->GetModel();
            const int meshCount = currentModel ? static_cast<int>(currentModel->GetMeshCount()) : 0;
            if (selectedObject->IsMeshDrawFiltered()) {
                ImGui::TextDisabled("描画Mesh: %d", selectedObject->GetMeshDrawIndex());
            }
            if (meshCount > 1 && !selectedObject->IsMeshDrawFiltered()) {
                ImGui::TextDisabled("Mesh数: %d", meshCount);
                if (ImGui::Button(ICON_FA_CUBE " メッシュを子Objectに分割", ImVec2(-1, 28))) {
                    editor_->SplitSelectedModelIntoMeshChildren();
                }
            } else if (meshCount == 1) {
                ImGui::TextDisabled("Mesh数: 1");
            }
        }

        // --- 可視性設定 ---
        ImGui::Separator();
        bool isVisible = selectedObject->GetIsVisible();
        const bool mixedVisible = HasMixedInspectorValue(inspectorTargets, "rendering.visible");
        PushMixedCheckbox(mixedVisible);
        if (ImGui::Checkbox(ICON_FA_EYE " 表示 (ゲーム内)", &isVisible)) {
            for (Object3d* object : inspectorTargets) {
                if (!object) continue;
                object->SetIsVisible(isVisible);
            }
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }
        PopMixedCheckbox();
        bool isLocked = selectedObject->GetIsLocked();
        const bool mixedLocked = HasMixedInspectorValue(inspectorTargets, "editor.locked");
        PushMixedCheckbox(mixedLocked);
        if (ImGui::Checkbox(ICON_FA_LOCK " ロック (編集保護)", &isLocked)) {
            for (Object3d* object : inspectorTargets) {
                if (!object) continue;
                object->SetIsLocked(isLocked);
            }
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }
        PopMixedCheckbox();
        bool castShadow = selectedObject->GetCastShadow();
        const bool mixedCastShadow = HasMixedInspectorValue(inspectorTargets, "rendering.castShadow");
        PushMixedCheckbox(mixedCastShadow);
        if (ImGui::Checkbox(ICON_FA_LIGHTBULB " 影を落とす", &castShadow)) {
            for (Object3d* object : inspectorTargets) {
                if (!object) continue;
                object->SetCastShadow(castShadow);
            }
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }
        PopMixedCheckbox();

        // --- Transform編集 ---
        ImGui::Separator();
        ImGui::Text(ICON_FA_ARROWS_ALT " トランスフォーム (Transform)");
        Transform* transform = selectedObject->GetTransform();
        bool isTransformChanged = false;

        const Vector3 beforePos = transform->translate;
        const bool mixedPosition = HasMixedInspectorValue(inspectorTargets, "transform.position");
        if (ImGui::DragFloat3(ICON_FA_ARROWS_ALT " 座標 (Pos)", &transform->translate.x, 0.1f)) {
            ApplyPrimaryTransformDeltaToTargets(
                inspectorTargets,
                selectedObject,
                beforePos,
                transform->translate,
                transform->rotate,
                transform->rotate,
                transform->scale,
                transform->scale,
                true,
                false,
                false);
            isTransformChanged = true;
        }
        DrawMixedValueHint(mixedPosition);

        Vector3 rotDeg = { ToDegrees(transform->rotate.x), ToDegrees(transform->rotate.y), ToDegrees(transform->rotate.z) };
        const Vector3 beforeRot = transform->rotate;
        const bool mixedRotation = HasMixedInspectorValue(inspectorTargets, "transform.rotation");
        if (ImGui::DragFloat3(ICON_FA_SYNC " 回転 (Rot)", &rotDeg.x, 1.0f, -360.0f, 360.0f)) {
            transform->rotate = { ToRadians(rotDeg.x), ToRadians(rotDeg.y), ToRadians(rotDeg.z) };
            transform->isQuaternionMaster = false;
            ApplyPrimaryTransformDeltaToTargets(
                inspectorTargets,
                selectedObject,
                transform->translate,
                transform->translate,
                beforeRot,
                transform->rotate,
                transform->scale,
                transform->scale,
                false,
                true,
                false);
            isTransformChanged = true;
        }
        DrawMixedValueHint(mixedRotation);
        const Vector3 beforeScale = transform->scale;
        if (isManagedCharacter) {
            ImGui::TextDisabled(ICON_FA_EXPAND_ARROWS_ALT " スケール: %.3f, %.3f, %.3f（ステータス管理）",
                transform->scale.x, transform->scale.y, transform->scale.z);
        } else {
            const bool mixedScale = HasMixedInspectorValue(inspectorTargets, "transform.scale");
            if (ImGui::DragFloat3(ICON_FA_EXPAND_ARROWS_ALT " スケール (Scale)", &transform->scale.x, 0.05f)) {
                ApplyPrimaryTransformDeltaToTargets(
                    inspectorTargets,
                    selectedObject,
                    transform->translate,
                    transform->translate,
                    transform->rotate,
                    transform->rotate,
                    beforeScale,
                    transform->scale,
                    false,
                    false,
                    true);
                isTransformChanged = true;
            }
            DrawMixedValueHint(mixedScale);
        }

        if (isTransformChanged) {
            RefreshInspectorTargetMatrices(inspectorTargets);
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }


        // --- コライダー設定 ---
        ImGui::Separator();
        if (ImGui::CollapsingHeader(ICON_FA_SHIELD_ALT " コリジョン設定 (Collision)", ImGuiTreeNodeFlags_DefaultOpen)) {
            Object3d::ColliderConfig colConfig = selectedObject->GetColliderConfig();
            bool isColChanged = false;

            const char* typeNames[] = { "なし (None)", "球 (Sphere)", "箱 (AABB)", "回転箱 (OBB)", "円柱 (Cylinder)", "リング (Ring)", "地形 (Terrain)" };
            int currentTypeIndex = (int)colConfig.type;
            if (ImGui::Combo(ICON_FA_SHAPES " 形状タイプ", &currentTypeIndex, typeNames, IM_ARRAYSIZE(typeNames))) {
                colConfig.type = (ColliderType)currentTypeIndex;
                if ((colConfig.type == ColliderType::kOBB || colConfig.type == ColliderType::kRing) && colConfig.size.x == 0.0f) {
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
                }
                else if (colConfig.type == ColliderType::kRing) {
                    if (ImGui::DragFloat("外径 (Outer Radius)", &colConfig.size.x, 0.05f, 0.0f, 100.0f)) isColChanged = true;
                    if (ImGui::DragFloat("内径 (Inner Radius)", &colConfig.size.z, 0.05f, 0.0f, 100.0f)) isColChanged = true;
                    // 厚み(Y)
                    if (ImGui::DragFloat("厚み (Thickness)", &colConfig.size.y, 0.05f, 0.0f, 10.0f)) isColChanged = true;
                }
                else {
                    if (ImGui::DragFloat3("サイズ (Size)", &colConfig.size.x, 0.05f, 0.0f, 100.0f)) isColChanged = true;
                }

                if (colConfig.type == ColliderType::kOBB || colConfig.type == ColliderType::kRing) {
                    Vector3 rotDegObj = { ToDegrees(colConfig.rotation.x), ToDegrees(colConfig.rotation.y), ToDegrees(colConfig.rotation.z) };
                    if (ImGui::DragFloat3("回転 (Rotation)", &rotDegObj.x, 1.0f, -360.0f, 360.0f)) {
                        colConfig.rotation = { ToRadians(rotDegObj.x), ToRadians(rotDegObj.y), ToRadians(rotDegObj.z) };
                        isColChanged = true;
                    }
                }
                if (isColChanged) {
                    for (Object3d* object : inspectorTargets) {
                        if (!object) continue;
                        object->SetColliderConfig(colConfig);
                    }
                    MarkInspectorTargetsDirty(editor_, inspectorTargets);
                }
            }
            else {
                // なしの時も一応反映
                if (isColChanged) {
                    for (Object3d* object : inspectorTargets) {
                        if (!object) continue;
                        object->SetColliderConfig(colConfig);
                    }
                    MarkInspectorTargetsDirty(editor_, inspectorTargets);
                }
            }
            ImGui::Separator();
            if (ImGui::CollapsingHeader(ICON_FA_PALETTE " グラフィックス (Material)", ImGuiTreeNodeFlags_DefaultOpen)) {
                bool isGraphicsChanged = false;
                DrawMaterialInstancePanel(editor_, selectedObject, inspectorTargets);
                DrawDecalPanel(editor_, selectedObject, inspectorTargets);
                const char* matTypes[] = {
                                         "通常 (Standard)", "ガラス (Glass)", "氷・宝石 (Ice/Crystal)",
                                         "ホログラム (Hologram)", "消滅 (Dissolve)", "旧マグマ (Emissive)",
                                         "トゥーン調 (Cel Shaded)", "ローカルフォグ (Local Fog)",
                                         "水 (Water)", "新マグマ (Magma)", "分厚い氷 (Ice)", "炎 (Fire)",
                                         "レーザー (Laser)", "スライムジェル (Slime Gel)",
                                         "地面衝撃波 (Shockwave)", "水/マグマ接触 (Liquid Contact)",
                                         "ダメージ亀裂 (Damage Crack)", "上昇気流 (Updraft)",
                                         "スタン拘束 (Stun Bind)", "王冠解放 (Crown Unlock)",
                                         "毒胞子 (Poison Spore)", "雲 (Cloud)",
                                         "ゲートポータル (Gate Portal)",
                                         "アニメ調地形 (Stylized Terrain)",
                                         "ダッシュパネル (Dash Panel)",
                                         "スライム補正 (Slime Soft)",
                                         "風弾 (Wind Orb)",
                                         "プリズム結晶 (Prism Crystal)",
                                         "デカール (Surface Decal)"
                };
                int currentMatType = selectedObject->GetMaterialType();
                if (currentMatType < 0) currentMatType = 0;
                if (currentMatType > 28) currentMatType = 0;
                if (ImGui::Combo(ICON_FA_PAINT_BRUSH " 質感 (Material Type)", &currentMatType, matTypes, IM_ARRAYSIZE(matTypes))) {
                    for (Object3d* object : inspectorTargets) {
                        if (!object) continue;
                        object->SetMaterialType(currentMatType);
                    }
                    MarkInspectorTargetsDirty(editor_, inspectorTargets);
                    isGraphicsChanged = true;
                }

                if (currentMatType == 0) {
                    float metallic = selectedObject->GetMetallic();
                    if (ImGui::SliderFloat("金属度 (Metallic)", &metallic, 0.0f, 1.0f)) {
                        for (Object3d* object : inspectorTargets) {
                            if (!object) continue;
                            object->SetMetallic(metallic);
                        }
                        MarkInspectorTargetsDirty(editor_, inspectorTargets);
                        isGraphicsChanged = true;
                    }
                    float roughness = selectedObject->GetRoughness();
                    if (ImGui::SliderFloat("粗さ (Roughness)", &roughness, 0.0f, 1.0f)) {
                        for (Object3d* object : inspectorTargets) {
                            if (!object) continue;
                            object->SetRoughness(roughness);
                        }
                        MarkInspectorTargetsDirty(editor_, inspectorTargets);
                        isGraphicsChanged = true;
                    }
                }
                else if (currentMatType == 23) {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.45f, 1.0f), ICON_FA_TREE " --- Stylized Terrain Settings ---");
                    Vector4 terrainColor = selectedObject->GetColor();
                    if (ImGui::ColorEdit4("差し色 (Accent Color)", &terrainColor.x)) {
                        for (Object3d* object : inspectorTargets) {
                            if (!object) continue;
                            object->SetColor(terrainColor);
                        }
                        MarkInspectorTargetsDirty(editor_, inspectorTargets);
                        isGraphicsChanged = true;
                    }
                    float textureBlend = selectedObject->GetRoughness();
                    if (ImGui::SliderFloat("テクスチャ反映量 (Texture Blend)", &textureBlend, 0.0f, 1.0f)) {
                        for (Object3d* object : inspectorTargets) {
                            if (object) object->SetRoughness(textureBlend);
                        }
                        MarkInspectorTargetsDirty(editor_, inspectorTargets);
                        isGraphicsChanged = true;
                    }
                    float paintStrength = selectedObject->GetMetallic();
                    if (ImGui::SliderFloat("塗りの濃さ / 色ムラ (Paint Strength)", &paintStrength, 0.0f, 1.0f)) {
                        for (Object3d* object : inspectorTargets) {
                            if (object) object->SetMetallic(paintStrength);
                        }
                        MarkInspectorTargetsDirty(editor_, inspectorTargets);
                        isGraphicsChanged = true;
                    }
                    float reliefStrength = selectedObject->GetEnvIntensity();
                    if (ImGui::SliderFloat("立体感 / 凹凸強度 (Relief)", &reliefStrength, 0.0f, 2.0f)) {
                        for (Object3d* object : inspectorTargets) {
                            if (object) object->SetEnvIntensity(reliefStrength);
                        }
                        MarkInspectorTargetsDirty(editor_, inspectorTargets);
                        isGraphicsChanged = true;
                    }
                    ImGui::TextDisabled("側面の陰影・色面の差・法線マップ強度をまとめて調整します");
                }
                else if (currentMatType == 24) {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.35f, 0.95f, 1.0f, 1.0f), ICON_FA_FORWARD " --- Dash Panel Settings ---");
                    float flowSpeed = selectedObject->GetRoughness();
                    if (ImGui::SliderFloat("流れる速さ (Flow Speed)", &flowSpeed, 0.0f, 1.0f)) {
                        selectedObject->SetRoughness(flowSpeed); isGraphicsChanged = true;
                    }
                    float lineDensity = selectedObject->GetMetallic();
                    if (ImGui::SliderFloat("ライン密度 (Line Density)", &lineDensity, 0.0f, 1.0f)) {
                        selectedObject->SetMetallic(lineDensity); isGraphicsChanged = true;
                    }
                }
                else if (currentMatType == 27) {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.72f, 0.92f, 1.0f, 1.0f), ICON_FA_GEM " --- Prism Crystal Settings ---");
                    float facetSoftness = selectedObject->GetRoughness();
                    if (ImGui::SliderFloat("面反射の柔らかさ (Facet Softness)", &facetSoftness, 0.02f, 0.70f)) {
                        for (Object3d* object : inspectorTargets) {
                            if (object) object->SetRoughness(facetSoftness);
                        }
                        isGraphicsChanged = true;
                    }
                    float dispersionStrength = selectedObject->GetMetallic();
                    if (ImGui::SliderFloat("分光の強さ (Dispersion)", &dispersionStrength, 0.0f, 1.0f)) {
                        for (Object3d* object : inspectorTargets) {
                            if (object) object->SetMetallic(dispersionStrength);
                        }
                        isGraphicsChanged = true;
                    }
                    float environmentIntensity = selectedObject->GetEnvIntensity();
                    if (ImGui::SliderFloat("環境反射の強さ (Environment)", &environmentIntensity, 0.0f, 3.0f)) {
                        for (Object3d* object : inspectorTargets) {
                            if (!object) continue;
                            object->SetEnableEnvMap(true);
                            object->SetEnvIntensity(environmentIntensity);
                        }
                        isGraphicsChanged = true;
                    }
                }

                ImGui::Separator();
                ImGui::Text(ICON_FA_EXPAND_ARROWS_ALT " PBRテクスチャ繰り返し");
                Vector2 textureTiling = selectedObject->GetTextureTiling();
                if (ImGui::DragFloat2("繰り返し倍率 (Tiling)", &textureTiling.x, 0.05f, 0.01f, 100.0f, "%.2f")) {
                    selectedObject->SetTextureTiling(textureTiling);
                    isGraphicsChanged = true;
                }
                bool autoTextureTiling = selectedObject->GetAutoTextureTiling();
                if (ImGui::Checkbox("スケールに合わせて自動繰り返し", &autoTextureTiling)) {
                    selectedObject->SetAutoTextureTiling(autoTextureTiling);
                    isGraphicsChanged = true;
                }
                ImGui::TextDisabled("大きい床や壁でPBRテクスチャが伸びる時に使います");

                if (currentMatType == 7) {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), ICON_FA_SMOG " --- Local Fog Settings ---");
                    auto* fogData = selectedObject->GetLocalFogData();
                    if (fogData) {
                        ImGui::ColorEdit4("Fog Color (霧の色)", &fogData->fogColor.x);
                        ImGui::DragFloat("Density (濃さ)", &fogData->fogDensity, 0.01f, 0.0f, 10.0f);
                        ImGui::DragFloat("Edge Fade (境界線のボケ)", &fogData->edgeFade, 0.01f, 0.0f, 1.5f);
                        ImGui::DragFloat("Noise Speed (揺らぐ速さ)", &fogData->noiseSpeed, 0.01f, 0.0f, 10.0f);
                        ImGui::DragFloat("Noise Scale (模様の細かさ)", &fogData->noiseScale, 0.01f, 0.0f, 10.0f);
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), ICON_FA_SUN " --- Light Scattering ---");
                        ImGui::DragFloat("Scattering G (光の芯の強さ)", &fogData->scatteringG, 0.01f, 0.0f, 0.99f);
                        ImGui::DragFloat("Light Intensity (光の明るさ)", &fogData->scatteringIntensity, 0.01f, 0.0f, 5.0f);
                    }
                }
                if ((currentMatType >= 8 && currentMatType <= 22) || currentMatType == 26) {
                    ImGui::Separator();

                    if (selectedObject->GetMeshRenderer() && selectedObject->GetMeshRenderer()->GetWaterParamData()) {
                        auto* waterData = selectedObject->GetMeshRenderer()->GetWaterParamData();
                        if (currentMatType == 11) {
                            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.08f, 1.0f), ICON_FA_FIRE " --- Fire Settings ---");
                            const char* fireTypes[] = {
                                "炎の形 (Flame Shape)",
                                "炎の球 (Fire Ball)",
                                "まとい炎 (Wrapped Flame)",
                                "炎の流れ (Flame Stream)",
                                "立体かがり火 (Volumetric Brazier)"
                            };
                            int fireType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 4);
                            if (ImGui::Combo("炎タイプ (Fire Type)", &fireType, fireTypes, IM_ARRAYSIZE(fireTypes))) {
                                waterData->effectType = static_cast<float>(fireType);
                                isGraphicsChanged = true;
                            }
                            if (ImGui::DragFloat("揺らぎ速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 10.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("炎の細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 20.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("模様スケール (Pattern Scale)", &waterData->effectScale, 0.01f, 0.05f, 5.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("描画サイズ (Billboard Size)", &waterData->billboardScale, 0.01f, 0.05f, 3.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("炎の横幅 (Width)", &waterData->effectScaleX, 0.01f, 0.12f, 4.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("炎の高さ (Height)", &waterData->effectScaleY, 0.01f, 0.12f, 4.0f)) isGraphicsChanged = true;
                            if (fireType == 4 && ImGui::DragFloat("炎の奥行き (Depth)", &waterData->effectScaleZ, 0.01f, 0.12f, 4.0f)) isGraphicsChanged = true;
                            if (ImGui::SliderFloat("輪郭の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("炎の強さ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 5.0f)) isGraphicsChanged = true;
                        }
                        else if (currentMatType == 12) {
                            ImGui::TextColored(ImVec4(0.25f, 0.85f, 1.0f, 1.0f), ICON_FA_BOLT " --- Laser Settings ---");
                            if (ImGui::DragFloat("流れる速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 20.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("縞の細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 30.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("模様スケール (Pattern Scale)", &waterData->effectScale, 0.01f, 0.05f, 8.0f)) isGraphicsChanged = true;
                            if (ImGui::SliderFloat("外側の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("発光の強さ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 8.0f)) isGraphicsChanged = true;
                        }
                        else if (currentMatType == 13) {
                            ImGui::TextColored(ImVec4(0.15f, 1.0f, 0.8f, 1.0f), ICON_FA_WATER " --- Slime Gel Settings ---");
                            if (ImGui::DragFloat("ぷるぷる速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 10.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("変形量 (Wobble Height)", &waterData->waveHeight, 0.01f, 0.0f, 3.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("内部模様の細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 20.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("屈折の強さ (Refraction)", &waterData->effectScale, 0.01f, 0.05f, 4.0f)) isGraphicsChanged = true;
                            if (ImGui::SliderFloat("表面の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("透明感の強さ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 5.0f)) isGraphicsChanged = true;
                        }
                        else if (currentMatType == 14) {
                            ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), ICON_FA_BULLSEYE " --- Ground Shockwave Settings ---");
                            if (ImGui::DragFloat("広がる速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 10.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("ひびの細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 24.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("衝撃波の半径 (Radius Scale)", &waterData->effectScale, 0.01f, 0.05f, 10.0f)) isGraphicsChanged = true;
                            if (ImGui::SliderFloat("リング幅 (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("明るさ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 6.0f)) isGraphicsChanged = true;
                        }
                        else if (currentMatType == 15) {
                            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.18f, 1.0f), ICON_FA_BURN " --- Liquid Contact Settings ---");
                            const char* contactTypes[] = { "水の泡 (Foam)", "マグマ蒸気 (Steam)" };
                            int contactType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 1);
                            if (ImGui::Combo("接触タイプ (Contact Type)", &contactType, contactTypes, IM_ARRAYSIZE(contactTypes))) {
                                waterData->effectType = static_cast<float>(contactType);
                                isGraphicsChanged = true;
                            }
                            if (ImGui::DragFloat("流れる速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 10.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("泡/蒸気の細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 24.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("帯の幅 (Band Width)", &waterData->effectScale, 0.01f, 0.05f, 10.0f)) isGraphicsChanged = true;
                            if (ImGui::SliderFloat("境界の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("明るさ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 6.0f)) isGraphicsChanged = true;
                            ImGui::Separator();
                            ImGui::Text(ICON_FA_WIND " --- Flow Settings ---");
                            if (ImGui::DragFloat("Flow Speed X", &waterData->flowSpeedX, 0.01f, -50.0f, 50.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("Flow Speed Y", &waterData->flowSpeedY, 0.01f, -50.0f, 50.0f)) isGraphicsChanged = true;
                        }
                        else if (currentMatType == 16) {
                            ImGui::TextColored(ImVec4(1.0f, 0.32f, 0.18f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " --- Damage Crack Settings ---");
                            const char* crackTypes[] = { "ブロック亀裂 (Block Crack)", "ガラス亀裂 (Glass Crack)", "弱点コア亀裂 (Core Crack)" };
                            int crackType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 2);
                            if (ImGui::Combo("亀裂タイプ (Crack Type)", &crackType, crackTypes, IM_ARRAYSIZE(crackTypes))) {
                                waterData->effectType = static_cast<float>(crackType);
                                isGraphicsChanged = true;
                            }
                            if (ImGui::DragFloat("脈動速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 12.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("亀裂の深さ (Depth)", &waterData->waveHeight, 0.01f, 0.0f, 3.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("亀裂の細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 30.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("亀裂スケール (Crack Scale)", &waterData->effectScale, 0.01f, 0.05f, 8.0f)) isGraphicsChanged = true;
                            if (ImGui::SliderFloat("縁の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("発光の強さ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 8.0f)) isGraphicsChanged = true;
                        }
                        else if (currentMatType == 17) {
                            ImGui::TextColored(ImVec4(0.45f, 0.95f, 1.0f, 1.0f), ICON_FA_WIND " --- Updraft Settings ---");
                            const char* updraftTypes[] = { "上昇柱 (Column)", "渦リング (Vortex Ring)", "横風スラッシュ (Wind Slash)" };
                            int updraftType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 2);
                            if (ImGui::Combo("風タイプ (Wind Type)", &updraftType, updraftTypes, IM_ARRAYSIZE(updraftTypes))) {
                                waterData->effectType = static_cast<float>(updraftType);
                                isGraphicsChanged = true;
                            }
                            if (ImGui::DragFloat("流れる速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 16.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("揺らぎの高さ (Wobble)", &waterData->waveHeight, 0.01f, 0.0f, 4.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("筋の細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 30.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("渦の広さ (Vortex Scale)", &waterData->effectScale, 0.01f, 0.05f, 8.0f)) isGraphicsChanged = true;
                            if (ImGui::SliderFloat("境界の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("透明感/強さ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 8.0f)) isGraphicsChanged = true;
                        }
                        else if (currentMatType == 18) {
                            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.25f, 1.0f), ICON_FA_BOLT " --- Stun Bind Settings ---");
                            const char* stunTypes[] = { "拘束リング (Bind Rings)", "電撃ケージ (Electric Cage)", "スタン球 (Stun Orb)" };
                            int stunType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 2);
                            if (ImGui::Combo("拘束タイプ (Bind Type)", &stunType, stunTypes, IM_ARRAYSIZE(stunTypes))) {
                                waterData->effectType = static_cast<float>(stunType);
                                isGraphicsChanged = true;
                            }
                            if (ImGui::DragFloat("電撃速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 20.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("リング間隔 (Ring Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 32.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("拘束の太さ (Bind Width)", &waterData->effectScale, 0.01f, 0.05f, 8.0f)) isGraphicsChanged = true;
                            if (ImGui::SliderFloat("外側の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("発光の強さ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 10.0f)) isGraphicsChanged = true;
                        }
                        else if (currentMatType == 19) {
                            ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.25f, 1.0f), ICON_FA_MAGIC " --- Crown Unlock Settings ---");
                            const char* crownTypes[] = { "魔法陣 (Magic Circle)", "王冠バースト (Crown Burst)", "解放ポータル (Unlock Portal)" };
                            int crownType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 2);
                            if (ImGui::Combo("解放タイプ (Unlock Type)", &crownType, crownTypes, IM_ARRAYSIZE(crownTypes))) {
                                waterData->effectType = static_cast<float>(crownType);
                                isGraphicsChanged = true;
                            }
                            if (ImGui::DragFloat("演出速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 12.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("光線の数/細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 32.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("紋章スケール (Glyph Scale)", &waterData->effectScale, 0.01f, 0.05f, 8.0f)) isGraphicsChanged = true;
                            if (ImGui::SliderFloat("縁の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("神々しさ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 10.0f)) isGraphicsChanged = true;
                        }
                        else if (currentMatType == 20) {
                            ImGui::TextColored(ImVec4(0.55f, 1.0f, 0.25f, 1.0f), ICON_FA_SMOG " --- Poison Spore Settings ---");
                            const char* sporeTypes[] = { "毒霧 (Poison Mist)", "胞子雲 (Spore Cloud)", "毒リング (Poison Ring)" };
                            int sporeType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 2);
                            if (ImGui::Combo("毒タイプ (Poison Type)", &sporeType, sporeTypes, IM_ARRAYSIZE(sporeTypes))) {
                                waterData->effectType = static_cast<float>(sporeType);
                                isGraphicsChanged = true;
                            }
                            if (ImGui::DragFloat("漂う速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 12.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("霧の厚み (Density)", &waterData->waveHeight, 0.01f, 0.0f, 4.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("胞子の細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 30.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("模様スケール (Pattern Scale)", &waterData->effectScale, 0.01f, 0.05f, 8.0f)) isGraphicsChanged = true;
                            if (ImGui::SliderFloat("境界の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("毒の強さ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 8.0f)) isGraphicsChanged = true;
                        }
                        else if (currentMatType == 21) {
                            ImGui::TextColored(ImVec4(0.75f, 0.9f, 1.0f, 1.0f), ICON_FA_CLOUD " --- Cloud Settings ---");
                            const char* cloudTypes[] = { "雲の塊 (Puff)", "流れる雲 (Drift)", "足元の煙 (Ground Mist)" };
                            int cloudType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 2);
                            if (ImGui::Combo("雲タイプ (Cloud Type)", &cloudType, cloudTypes, IM_ARRAYSIZE(cloudTypes))) {
                                waterData->effectType = static_cast<float>(cloudType);
                                isGraphicsChanged = true;
                            }
                            if (ImGui::DragFloat("流れる速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 12.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("厚み (Density)", &waterData->waveHeight, 0.01f, 0.0f, 4.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("雲の細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 30.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("雲スケール (Cloud Scale)", &waterData->effectScale, 0.01f, 0.05f, 8.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("描画サイズ (Billboard Size)", &waterData->billboardScale, 0.01f, 0.05f, 4.0f)) isGraphicsChanged = true;
                            if (ImGui::SliderFloat("輪郭の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("明るさ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 5.0f)) isGraphicsChanged = true;
                        }
                        else if (currentMatType == 22) {
                            ImGui::TextColored(ImVec4(1.0f, 0.58f, 0.18f, 1.0f), ICON_FA_MAGIC " --- Gate Portal Settings ---");
                            const char* gateTypes[] = { "渦ポータル (Swirl Portal)", "暖色ゲート (Warm Gate)", "封印ゲート (Sealed Gate)" };
                            int gateType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 2);
                            if (ImGui::Combo("ゲートタイプ (Gate Type)", &gateType, gateTypes, IM_ARRAYSIZE(gateTypes))) {
                                waterData->effectType = static_cast<float>(gateType);
                                isGraphicsChanged = true;
                            }
                            if (ImGui::DragFloat("渦の速度 (Speed)", &waterData->waveSpeed, 0.01f, 0.05f, 12.0f, "%.3f")) isGraphicsChanged = true;
                            if (ImGui::DragFloat("奥行きの強さ (Depth)", &waterData->waveHeight, 0.005f, 0.05f, 5.0f, "%.3f")) isGraphicsChanged = true;
                            if (ImGui::DragFloat("渦の細かさ (Detail)", &waterData->waveFrequency, 0.01f, 0.1f, 36.0f, "%.3f")) isGraphicsChanged = true;
                            if (ImGui::DragFloat("模様スケール (Pattern Scale)", &waterData->effectScale, 0.001f, 0.01f, 8.0f, "%.3f")) isGraphicsChanged = true;
                            if (ImGui::DragFloat("全体サイズ (Portal Size)", &waterData->billboardScale, 0.001f, 0.01f, 8.0f, "%.3f")) isGraphicsChanged = true;
                            if (ImGui::DragFloat("横スケール (Scale X)", &waterData->effectScaleX, 0.001f, 0.05f, 6.0f, "%.3f")) isGraphicsChanged = true;
                            if (ImGui::DragFloat("縦スケール (Scale Y)", &waterData->effectScaleY, 0.001f, 0.05f, 6.0f, "%.3f")) isGraphicsChanged = true;
                            if (ImGui::DragFloat("厚みスケール (Scale Z)", &waterData->effectScaleZ, 0.001f, 0.0f, 3.0f, "%.3f")) isGraphicsChanged = true;
                            if (ImGui::SliderFloat("輪郭の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("発光の強さ (Intensity)", &waterData->effectIntensity, 0.01f, 0.05f, 8.0f, "%.3f")) isGraphicsChanged = true;
                        }
                        else if (currentMatType == 26) {
                            ImGui::TextColored(ImVec4(0.42f, 1.0f, 0.82f, 1.0f), ICON_FA_WIND " --- Wind Orb Settings ---");
                            const char* windOrbTypes[] = { "安定した風弾 (Stable)", "高速の渦 (Fast)", "圧縮した風 (Compressed)" };
                            int windOrbType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 2);
                            if (ImGui::Combo("風弾タイプ (Wind Orb Type)", &windOrbType, windOrbTypes, IM_ARRAYSIZE(windOrbTypes))) {
                                waterData->effectType = static_cast<float>(windOrbType);
                                isGraphicsChanged = true;
                            }
                            if (ImGui::DragFloat("帯の流速 (Band Speed)", &waterData->waveSpeed, 0.01f, 0.05f, 12.0f, "%.3f")) isGraphicsChanged = true;
                            if (ImGui::DragFloat("表面の揺れ (Surface Motion)", &waterData->waveHeight, 0.005f, 0.0f, 3.0f, "%.3f")) isGraphicsChanged = true;
                            if (ImGui::DragFloat("内部の細かさ (Inner Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 30.0f, "%.3f")) isGraphicsChanged = true;
                            if (ImGui::DragFloat("屈折の強さ (Refraction)", &waterData->effectScale, 0.005f, 0.05f, 3.0f, "%.3f")) isGraphicsChanged = true;
                            if (ImGui::SliderFloat("帯の柔らかさ (Band Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("発光の強さ (Intensity)", &waterData->effectIntensity, 0.01f, 0.05f, 5.0f, "%.3f")) isGraphicsChanged = true;
                        }
                        else if (currentMatType == 9) {
                            ImGui::TextColored(ImVec4(1.0f, 0.28f, 0.04f, 1.0f), ICON_FA_FIRE " --- Magma Settings ---");
                            if (ImGui::DragFloat("流れる速度 (Flow Speed)", &waterData->waveSpeed, 0.01f, 0.05f, 6.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("隆起の高さ (Heave Height)", &waterData->waveHeight, 0.01f, 0.0f, 4.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("塊の細かさ (Blob Detail)", &waterData->waveFrequency, 0.05f, 0.15f, 12.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("粘度/引き伸ばし (Viscosity)", &waterData->effectScale, 0.01f, 0.1f, 5.0f)) isGraphicsChanged = true;
                            if (ImGui::SliderFloat("黒いクラストの幅 (Crust Width)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("熱の強さ (Heat Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 5.0f)) isGraphicsChanged = true;
                            ImGui::Separator();
                            ImGui::Text(ICON_FA_WIND " --- Slow Flow Direction ---");
                            if (ImGui::DragFloat("Flow Speed X", &waterData->flowSpeedX, 0.01f, -5.0f, 5.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("Flow Speed Y", &waterData->flowSpeedY, 0.01f, -5.0f, 5.0f)) isGraphicsChanged = true;
                        }
                        else {
                            const char* settingTitle = (currentMatType == 8) ? ICON_FA_TINT " --- Water Settings ---" :
                                ICON_FA_SNOWFLAKE " --- Ice Settings ---";
                            ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), settingTitle);
                            if (currentMatType == 8) {
                                const char* waterMeshTypes[] = {
                                    "専用グリッド (Recommended)",
                                    "元モデルを使用 (Legacy)"
                                };
                                int waterMeshType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 1);
                                if (ImGui::Combo("水面形状 (Surface Mesh)", &waterMeshType, waterMeshTypes, IM_ARRAYSIZE(waterMeshTypes))) {
                                    waterData->effectType = static_cast<float>(waterMeshType);
                                    isGraphicsChanged = true;
                                }

                                if (ImGui::Button(ICON_FA_MAGIC " 明るいアニメ海プリセット")) {
                                    waterData->effectType = 0.0f;
                                    waterData->waveSpeed = 1.05f;
                                    waterData->waveHeight = 0.42f;
                                    waterData->waveFrequency = 4.20f;
                                    waterData->flowSpeedX = 0.035f;
                                    waterData->flowSpeedY = 0.018f;
                                    waterData->effectScale = 0.82f;
                                    waterData->effectSoftness = 0.48f;
                                    waterData->effectIntensity = 1.15f;
                                    waterData->billboardScale = 0.50f;
                                    waterData->effectScaleX = 8.0f;
                                    waterData->effectScaleY = 0.35f;
                                    waterData->effectScaleZ = 1.15f;
                                    isGraphicsChanged = true;
                                }
                                ImGui::SameLine();
                                if (ImGui::Button(ICON_FA_WATER " 穏やかな水プリセット")) {
                                    waterData->effectType = 0.0f;
                                    waterData->waveSpeed = 0.70f;
                                    waterData->waveHeight = 0.24f;
                                    waterData->waveFrequency = 3.10f;
                                    waterData->flowSpeedX = 0.020f;
                                    waterData->flowSpeedY = 0.012f;
                                    waterData->effectScale = 0.55f;
                                    waterData->effectSoftness = 0.36f;
                                    waterData->effectIntensity = 0.95f;
                                    waterData->billboardScale = 0.44f;
                                    waterData->effectScaleX = 10.0f;
                                    waterData->effectScaleY = 0.45f;
                                    waterData->effectScaleZ = 0.85f;
                                    isGraphicsChanged = true;
                                }
                                ImGui::TextDisabled("専用グリッドは元モデルに関係なく64分割の水面として描画します");
                            }
                            if (ImGui::DragFloat("Wave Speed (波の速さ)", &waterData->waveSpeed, 0.05f, 0.0f, 10.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("Wave Height (波の高さ)", &waterData->waveHeight, 0.05f, 0.0f, 10.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("Wave Frequency (波の細かさ)", &waterData->waveFrequency, 0.05f, 0.0f, 20.0f)) isGraphicsChanged = true;
                            ImGui::Separator();
                            ImGui::Text(ICON_FA_WIND " --- Flow Settings ---");
                            if (ImGui::DragFloat("Flow Speed X", &waterData->flowSpeedX, 0.01f, -50.0f, 50.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("Flow Speed Y", &waterData->flowSpeedY, 0.01f, -50.0f, 50.0f)) isGraphicsChanged = true;
                            if (currentMatType == 8) {
                                ImGui::Separator();
                                ImGui::Text(ICON_FA_TINT " --- Surface Settings ---");
                                if (ImGui::DragFloat("屈折の強さ (Refraction)", &waterData->effectScale, 0.01f, 0.0f, 3.0f)) isGraphicsChanged = true;
                                if (ImGui::SliderFloat("泡の広がり (Foam Width)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                                if (ImGui::DragFloat("水面の明るさ (Brightness)", &waterData->effectIntensity, 0.05f, 0.05f, 4.0f)) isGraphicsChanged = true;
                                if (ImGui::DragFloat("波模様の量 (Surface Pattern)", &waterData->billboardScale, 0.01f, 0.0f, 2.0f)) isGraphicsChanged = true;
                                if (ImGui::DragFloat("深さ色の範囲 (Depth Range)", &waterData->effectScaleX, 0.10f, 1.0f, 30.0f)) isGraphicsChanged = true;
                                if (ImGui::DragFloat("反射のきらめき (Sparkle)", &waterData->effectScaleY, 0.01f, 0.0f, 3.0f)) isGraphicsChanged = true;
                                if (ImGui::DragFloat("接触泡の強さ (Foam Intensity)", &waterData->effectScaleZ, 0.01f, 0.0f, 3.0f)) isGraphicsChanged = true;
                            }
                        }
                    }
                }
                ImGui::Separator();
                bool enableNormal = selectedObject->GetEnableNormalMap();
                if (ImGui::Checkbox(ICON_FA_MAP " 法線マップ (Normal Map) 有効化", &enableNormal)) {
                    selectedObject->SetEnableNormalMap(enableNormal); isGraphicsChanged = true;
                }

                ImGui::Separator();
                bool enableEnv = selectedObject->GetEnableEnvMap();
                if (ImGui::Checkbox(ICON_FA_GLOBE " 環境マップ (IBL) 有効化", &enableEnv)) {
                    selectedObject->SetEnableEnvMap(enableEnv); isGraphicsChanged = true;
                }
                if (enableEnv) {
                    float envIntensity = selectedObject->GetEnvIntensity();
                    if (ImGui::SliderFloat("環境マップ強度", &envIntensity, 0.0f, 5.0f)) {
                        selectedObject->SetEnvIntensity(envIntensity); isGraphicsChanged = true;
                    }
                }

                bool enableLighting = selectedObject->GetEnableLighting();
                if (ImGui::Checkbox(ICON_FA_LIGHTBULB " ライト影響を受ける", &enableLighting)) {
                    selectedObject->SetEnableLighting(enableLighting);
                    isGraphicsChanged = true;
                }

                bool castShadow = selectedObject->GetCastShadow();
                if (ImGui::Checkbox("影を落とす (Cast Shadow)", &castShadow)) {
                    selectedObject->SetCastShadow(castShadow);
                    isGraphicsChanged = true;
                }
                if (!castShadow) {
                    ImGui::TextDisabled("影マップ描画だけを無効化します。ライト影響は別設定です。");
                }

                static InspectorTextureCatalog textureCatalog;
                textureCatalog.EnsureLoaded();
                const auto& albedoPaths = textureCatalog.GetAlbedoPaths();
                const auto& normalPaths = textureCatalog.GetNormalPaths();
                const auto& armPaths = textureCatalog.GetOrmPaths();
                const auto& spriteTexturePaths = textureCatalog.GetSpritePaths();
                const auto& pbrPresets = textureCatalog.GetPbrPresets();

                ImGui::Separator();
                std::string currentTexturePath = selectedObject->GetTexturePath();
                std::string currentNormalPath = selectedObject->GetNormalMapPath();
                std::string currentOrmPathForPreset = selectedObject->GetOrmMapPath();

                std::string currentPresetName = "未設定 (3枚セットを選択)";
                for (const InspectorTextureCatalog::PbrPreset& preset : pbrPresets) {
                    if (preset.albedoPath == currentTexturePath &&
                        preset.normalPath == currentNormalPath &&
                        preset.ormPath == currentOrmPathForPreset) {
                        currentPresetName = preset.name;
                        break;
                    }
                }

                if (ImGui::BeginCombo(ICON_FA_IMAGE " PBRプリセット (Albedo/Normal/ORM)", currentPresetName.c_str())) {
                    if (pbrPresets.empty()) {
                        ImGui::TextDisabled("同名の3枚セットが見つかりません。");
                    }
                    for (const InspectorTextureCatalog::PbrPreset& preset : pbrPresets) {
                        const bool isSelected =
                            preset.albedoPath == currentTexturePath &&
                            preset.normalPath == currentNormalPath &&
                            preset.ormPath == currentOrmPathForPreset;
                        if (ImGui::Selectable(preset.name.c_str(), isSelected)) {
                            selectedObject->SetTexture(preset.albedoPath);
                            selectedObject->SetNormalMap(preset.normalPath);
                            selectedObject->SetOrmMap(preset.ormPath);
                            selectedObject->SetEnableNormalMap(true);
                            DebugConsole::GetInstance()->AddLog("PBR preset applied: " + preset.name);
                            isGraphicsChanged = true;
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                if (!pbrPresets.empty()) {
                    ImGui::TextDisabled("同じベース名の3枚をまとめて適用します。");
                }

                const char* previewTextureValue = currentTexturePath.empty() ? "デフォルト (モデル固有)" : currentTexturePath.c_str();

                if (ImGui::BeginCombo(ICON_FA_IMAGE " 基本画像 (Diffuse / Sprite)", previewTextureValue)) {
                    if (!spriteTexturePaths.empty()) {
                        ImGui::TextDisabled("Sprite");
                        for (const std::string& path : spriteTexturePaths) {
                            bool isSelected = (currentTexturePath == path);
                            if (ImGui::Selectable(path.c_str(), isSelected)) {
                                selectedObject->SetTexture(path); isGraphicsChanged = true;
                            }
                            if (isSelected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::Separator();
                    }

                    if (!albedoPaths.empty()) {
                        ImGui::TextDisabled("PBR Albedo");
                        for (const std::string& path : albedoPaths) {
                            bool isSelected = (currentTexturePath == path);
                            if (ImGui::Selectable(path.c_str(), isSelected)) {
                                selectedObject->SetTexture(path); isGraphicsChanged = true;
                            }
                            if (isSelected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::Separator();
                    }

                    if (ImGui::Selectable("デフォルトに戻す", currentTexturePath.empty())) {
                        selectedObject->SetTexture(""); isGraphicsChanged = true;
                    }
                    ImGui::EndCombo();
                }

                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SPRITE_FILE")) {
                        const char* spritePath = static_cast<const char*>(payload->Data);
                        if (spritePath) {
                            selectedObject->SetTexture("Resources/sprite/" + std::string(spritePath));
                            isGraphicsChanged = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_SYNC_ALT " 更新##TextureList")) {
                    textureCatalog.Refresh();
                }

                if (IsSpriteCardObject(selectedObject)) {
                    ImGui::Separator();
                    if (ImGui::CollapsingHeader(ICON_FA_IMAGE " 2.5Dスプライト板", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::TextDisabled("3D空間にSprite画像を置くための板です。画像は上の基本画像から変更できます。");
                        if (ImGui::Button(ICON_FA_MAGIC " 2.5D用に整える", ImVec2(-1, 28))) {
                            ConfigureSpriteCardObject(selectedObject);
                            currentMatType = 0;
                            isGraphicsChanged = true;
                        }

                        Transform* cardTransform = selectedObject->GetTransform();
                        Vector2 cardSize = { cardTransform->scale.x, cardTransform->scale.y };
                        if (ImGui::DragFloat2("表示サイズ (幅 / 高さ)", &cardSize.x, 0.02f, 0.01f, 100.0f, "%.2f")) {
                            cardTransform->scale.x = (std::max)(0.01f, cardSize.x);
                            cardTransform->scale.y = (std::max)(0.01f, cardSize.y);
                            cardTransform->scale.z = 1.0f;
                            isTransformChanged = true;
                        }

                        if (ImGui::Button(ICON_FA_UNDO " 正面向きに戻す")) {
                            selectedObject->SetRotation({ 0.0f, 0.0f, 0.0f });
                            isTransformChanged = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button(ICON_FA_SHIELD_ALT " 当たり判定なし")) {
                            Object3d::ColliderConfig colliderConfig = selectedObject->GetColliderConfig();
                            colliderConfig.type = ColliderType::kNone;
                            selectedObject->SetColliderConfig(colliderConfig);
                        }
                    }
                }

                if (enableNormal) {
                    std::string currentPath = selectedObject->GetNormalMapPath();
                    const char* previewValue = currentPath.empty() ? "未設定 (クリックで選択)" : currentPath.c_str();

                    if (ImGui::BeginCombo(ICON_FA_IMAGE " ノーマル画像", previewValue)) {
                        for (const std::string& path : normalPaths) {
                            bool isSelected = (currentPath == path);
                            if (ImGui::Selectable(path.c_str(), isSelected)) {
                                selectedObject->SetNormalMap(path); isGraphicsChanged = true;
                            }
                            if (isSelected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::Separator();
                        if (ImGui::Selectable("なし (クリア)", currentPath.empty())) {
                            selectedObject->SetNormalMap(""); isGraphicsChanged = true;
                        }
                        ImGui::EndCombo();
                    }

                    std::string currentOrmPath = selectedObject->GetOrmMapPath();
                    const char* previewOrmValue = currentOrmPath.empty() ? "未設定 (クリックで選択)" : currentOrmPath.c_str();

                    if (ImGui::BeginCombo(ICON_FA_IMAGE " ORMマップ (AO/粗さ/金属)", previewOrmValue)) {
                        for (const std::string& path : armPaths) {
                            bool isSelected = (currentOrmPath == path);
                            if (ImGui::Selectable(path.c_str(), isSelected)) {
                                selectedObject->SetOrmMap(path); isGraphicsChanged = true;
                            }
                            if (isSelected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::Separator();
                        if (ImGui::Selectable("なし (クリア)", currentOrmPath.empty())) {
                            selectedObject->SetOrmMap(""); isGraphicsChanged = true;
                        }
                        ImGui::EndCombo();
                    }
                }
                ImGui::Separator();
                const char* blendModes[] = { "なし (None)", "通常 (Normal)", "加算 (Add)", "減算 (Subtract)", "乗算 (Multiply)", "スクリーン (Screen)" };
                int currentBlend = static_cast<int>(selectedObject->GetBlendMode());

                if (ImGui::Combo(ICON_FA_ADJUST " 合成 (Blend Mode)", &currentBlend, blendModes, IM_ARRAYSIZE(blendModes))) {
                    selectedObject->SetBlendMode(static_cast<BlendMode>(currentBlend)); isGraphicsChanged = true;
                }

                if (currentMatType != 23) {
                    Vector4 color = selectedObject->GetColor();
                    if (ImGui::ColorEdit4(ICON_FA_FILL_DRIP " 色 (Color)", &color.x)) {
                        selectedObject->SetColor(color); isGraphicsChanged = true;
                    }
                }

                ImGui::Spacing();
                float emissive = selectedObject->GetEmissive();
                // 1.0 で光なし。HDR空間で限界突破させるため最大値は大きめ(50.0等)にしておく
                if (ImGui::DragFloat(ICON_FA_SUN " 発光強度 (Emissive)", &emissive, 0.1f, 1.0f, 50.0f, "%.1f")) {
                    selectedObject->SetEmissive(emissive);
                    isGraphicsChanged = true;
                }
            }

            ImGui::Separator();
            if (HasRegisteredComponent(selectedObject, "LOD") &&
                ImGui::CollapsingHeader(ICON_FA_COMPRESS_ARROWS_ALT " LOD / 軽量モデル")) {
                MeshRenderer* renderer = selectedObject->GetMeshRenderer();
                if (!renderer || selectedObject->GetModelName().empty()) {
                    ImGui::TextDisabled("モデルが設定されていません。");
                }
                else {
                    bool lodEnabled = selectedObject->IsLodEnabled();
                    if (ImGui::Checkbox("距離LODを使う", &lodEnabled)) {
                        selectedObject->SetLodEnabled(lodEnabled);
                    }

                    const int activeLodLevel = selectedObject->GetActiveLodLevel();
                    const float lodDistance = selectedObject->GetLodCameraDistance();
                    const std::string activeLodModel = selectedObject->GetActiveLodModelName();
                    ImGui::TextColored(
                        activeLodLevel == 0 ? ImVec4(0.75f, 0.9f, 1.0f, 1.0f) : ImVec4(0.45f, 1.0f, 0.55f, 1.0f),
                        "描画中: LOD%d / 距離 %.1f",
                        activeLodLevel,
                        lodDistance);
                    ImGui::TextDisabled("使用モデル: %s", activeLodModel.c_str());

                    if (ImGui::Button(ICON_FA_SYNC " LOD設定を再読込")) {
                        if (selectedObject->ReloadLodManifest()) {
                            DebugConsole::GetInstance()->AddLog("LOD manifest reloaded: " + selectedObject->GetModelName());
                        }
                        else {
                            DebugConsole::GetInstance()->AddLog("LOD manifest not found: " + selectedObject->GetModelName());
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_TRASH " LODを解除")) {
                        selectedObject->ClearLodLevels();
                    }

                    const auto& lodLevels = selectedObject->GetLodLevels();
                    if (lodLevels.empty()) {
                        ImGui::TextWrapped("LOD設定がありません。モデル最適化ツールでLODを生成するか、*_lod.jsonを配置してください。");
                    }
                    else if (ImGui::BeginTable("ObjectLodLevels", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                        ImGui::TableSetupColumn("LOD", ImGuiTableColumnFlags_WidthFixed, 48.0f);
                        ImGui::TableSetupColumn("切替距離", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                        ImGui::TableSetupColumn("モデル", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableHeadersRow();

                        for (const auto& lod : lodLevels) {
                            ImGui::TableNextRow();
                            if (lod.level == activeLodLevel) {
                                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(75, 160, 95, 95));
                            }
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("LOD%d", lod.level);

                            ImGui::TableSetColumnIndex(1);
                            float distance = lod.distance;
                            const std::string distanceLabel = "##lod_distance_" + std::to_string(lod.level);
                            if (ImGui::DragFloat(distanceLabel.c_str(), &distance, 0.5f, 0.0f, 5000.0f, "%.1f")) {
                                selectedObject->SetLodLevelDistance(lod.level, distance);
                            }

                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextWrapped("%s", lod.modelName.c_str());
                        }
                        ImGui::EndTable();
                    }
                }
            }

            ImGui::Separator();
            if (HasRegisteredComponent(selectedObject, "ParticleEmitter") &&
                ImGui::CollapsingHeader(ICON_FA_FIRE " パーティクル")) {
                // --- CPU Particle (Old) ---
                const auto& cpuParamsMap = ParticleManager::GetInstance()->GetParamsMap();
                std::vector<const char*> cpuItemNames;
                int currentCpuIndex = 0; int cpuIdx = 0;
                cpuItemNames.push_back("None (CPU)");

                std::string currentCpuName = selectedObject->GetParticleName();
                if (currentCpuName.empty()) currentCpuIndex = 0;

                for (const auto& [name, param] : cpuParamsMap) {
                    cpuItemNames.push_back(name.c_str());
                    if (name == currentCpuName) currentCpuIndex = cpuIdx + 1;
                    cpuIdx++;
                }

                if (ImGui::Combo("CPU Effect", &currentCpuIndex, cpuItemNames.data(), (int)cpuItemNames.size())) {
                    if (currentCpuIndex == 0) selectedObject->SetParticleName("");
                    else selectedObject->SetParticleName(cpuItemNames[currentCpuIndex]);
                }

                ImGui::Separator();

                // --- GPU Particle (New) ---
                const auto& gpuPresets = GPUParticleManager::GetInstance()->GetPresets();
                std::vector<const char*> gpuItemNames;
                int currentGpuIndex = 0; int gpuIdx = 0;
                gpuItemNames.push_back("None (GPU)");

                std::string currentGpuName = selectedObject->GetGPUParticleName();
                if (currentGpuName.empty()) currentGpuIndex = 0;

                for (const auto& [name, config] : gpuPresets) {
                    gpuItemNames.push_back(name.c_str());
                    if (name == currentGpuName) currentGpuIndex = gpuIdx + 1;
                    gpuIdx++;
                }

                if (ImGui::Combo("GPU Effect", &currentGpuIndex, gpuItemNames.data(), (int)gpuItemNames.size())) {
                    if (currentGpuIndex == 0) selectedObject->SetGPUParticleName("");
                    else selectedObject->SetGPUParticleName(gpuItemNames[currentGpuIndex]);
                }

                if (!currentGpuName.empty() && gpuPresets.find(currentGpuName) == gpuPresets.end()) {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Warning: GPU JSON not found!");
                }
            }

            ImGui::Separator();
            if (HasRegisteredComponent(selectedObject, "MeshEffect") &&
                ImGui::CollapsingHeader(ICON_FA_MAGIC " メッシュエフェクト (Mesh Effect)")) {
                std::vector<std::string> effectPaths;
                std::vector<std::string> effectDisplayNames;
                
                effectPaths.push_back(""); 
                effectDisplayNames.push_back("None");

                std::string effectDir = "Resources/json/effect";
                if (fs::exists(effectDir) && fs::is_directory(effectDir)) {
                    for (const auto& entry : fs::directory_iterator(effectDir)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".json") {
                            effectPaths.push_back(entry.path().generic_string());
                            effectDisplayNames.push_back(entry.path().filename().generic_string());
                        }
                    }
                }

                std::vector<const char*> itemNames;
                for (const auto& displayName : effectDisplayNames) {
                    itemNames.push_back(displayName.c_str());
                }

                // --- スロット1 ---
                std::string currentEff1 = selectedObject->GetMeshEffect1Name();
                int selectIdx1 = 0;
                for (size_t i = 0; i < effectPaths.size(); ++i) {
                    if (effectPaths[i] == currentEff1) {
                        selectIdx1 = static_cast<int>(i);
                        break;
                    }
                }

                if (ImGui::Combo("Slot 1 (Floor etc.)", &selectIdx1, itemNames.data(), (int)itemNames.size())) {
                    selectedObject->SetMeshEffect1Name(effectPaths[selectIdx1]);
                }

                // --- スロット2 ---
                std::string currentEff2 = selectedObject->GetMeshEffect2Name();
                int selectIdx2 = 0;
                for (size_t i = 0; i < effectPaths.size(); ++i) {
                    if (effectPaths[i] == currentEff2) {
                        selectIdx2 = static_cast<int>(i);
                        break;
                    }
                }

                if (ImGui::Combo("Slot 2 (Pillar etc.)", &selectIdx2, itemNames.data(), (int)itemNames.size())) {
                    selectedObject->SetMeshEffect2Name(effectPaths[selectIdx2]);
                }
            }

            ImGui::Separator();
            uint32_t currentAttr = selectedObject->GetCollisionAttribute();
            DrawAttributeSelector(ICON_FA_TAGS " 自分の属性 (Attribute)", &currentAttr);
            if (currentAttr != selectedObject->GetCollisionAttribute()) selectedObject->SetCollisionAttribute(currentAttr);

            uint32_t currentMask = selectedObject->GetCollisionMask();
            DrawAttributeSelector(ICON_FA_TAGS " 衝突対象 (Mask)", &currentMask);
            if (currentMask != selectedObject->GetCollisionMask()) selectedObject->SetCollisionMask(currentMask);
        }

        if (HasRegisteredComponent(selectedObject, "Animator")) {
            // ==========================================
            // 1. ボーンアニメーション設定
            // ==========================================
            ImGui::Separator();
            ImGui::Text(ICON_FA_BONE " 【ボーンアニメーション】");
            const std::vector<Object3d*> animatorTargets =
                CollectRegisteredComponentTargets(inspectorTargets, "Animator");

        std::string controllerPath = selectedObject->GetAnimatorControllerPath();
        bool hasMixedController = false;
        for (Object3d* target : animatorTargets) {
            if (target && target->GetAnimatorControllerPath() != controllerPath) {
                hasMixedController = true;
                break;
            }
        }
        std::string controllerPreview = "(なし)";
        if (hasMixedController) {
            controllerPreview = "(複数値)";
        } else if (!controllerPath.empty()) {
            controllerPreview = fs::path(controllerPath).stem().string();
        }
        if (ImGui::BeginCombo("Animator Controller##BoneAnim", controllerPreview.c_str())) {
            const bool noneSelected = !hasMixedController && controllerPath.empty();
            if (ImGui::Selectable("(なし)", noneSelected)) {
                const auto beforeStates = editor_->CaptureObjectStates(animatorTargets);
                for (Object3d* target : animatorTargets) {
                    if (target) {
                        target->ClearAnimatorController();
                    }
                }
                editor_->RegisterObjectsEdited(beforeStates, "Animator Controller Clear");
            }

            std::vector<std::string> controllerFiles;
            const fs::path controllerDirectory = "Resources/json/animator";
            if (fs::exists(controllerDirectory) && fs::is_directory(controllerDirectory)) {
                for (const auto& entry : fs::directory_iterator(controllerDirectory)) {
                    if (entry.is_regular_file() && ToLowerAscii(entry.path().extension().string()) == ".json") {
                        controllerFiles.push_back(entry.path().stem().string());
                    }
                }
            }
            std::sort(controllerFiles.begin(), controllerFiles.end());
            for (const std::string& fileName : controllerFiles) {
                const bool isSelected = !hasMixedController && fs::path(controllerPath).stem().string() == fileName;
                if (ImGui::Selectable(fileName.c_str(), isSelected)) {
                    const auto beforeStates = editor_->CaptureObjectStates(animatorTargets);
                    bool assigned = false;
                    for (Object3d* target : animatorTargets) {
                        if (target && target->SetAnimatorController(fileName)) {
                            assigned = true;
                        }
                    }
                    if (assigned) {
                        editor_->RegisterObjectsEdited(beforeStates, "Animator Controller Assign");
                    } else {
                        DebugConsole::GetInstance()->AddLog("Animator Controllerの読み込みに失敗しました: " + fileName);
                    }
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (selectedObject->HasAnimatorController()) {
            const std::string currentState = selectedObject->GetAnimatorCurrentStateName();
            ImGui::TextDisabled("現在State: %s", currentState.empty() ? "(未再生)" : currentState.c_str());
        }
        if (ImGui::Button("Animator Controller Editorを開く##BoneAnim")) {
            editor_->GetAnimatorControllerEditor()->SetPreviewTarget(selectedObject);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetAnimatorControllerEditor());
        }
        ImGui::TextDisabled("Controller未設定時は下の旧単発クリップ設定を使用します。現行制作ではControllerを推奨します。");

        char animNameBuf[64];
        strncpy_s(animNameBuf, selectedObject->animName_.c_str(), sizeof(animNameBuf));
        if (ImGui::InputText("アニメ名##BoneAnim", animNameBuf, sizeof(animNameBuf))) {
            selectedObject->animName_ = animNameBuf;
        }
            ImGui::Checkbox("ループ再生##BoneAnim", &selectedObject->isAnimLoop_);
        }

        if (HasRegisteredComponent(selectedObject, "PathMover")) {
            DrawPathMoverSection(selectedObject);
        }

        DrawGameplayDataSection(selectedObject);
        ImGui::EndDisabled();
        ImGui::Separator();

        if (ImGui::Button(ICON_FA_TRASH_ALT " オブジェクト削除", ImVec2(-1, 0))) {
            EditorCommandRegistry::GetInstance()->Execute(EditorCommandId::EditDelete);
            EditorManager::GetInstance()->ClearSelection();
        }

        // --- Gizmo 操作切替 ---
        ImGui::Separator();
        ImGui::Text(ICON_FA_HAND_POINTER " ギズモ操作モード:");
        static ImGuizmo::OPERATION currentOperation = ImGuizmo::TRANSLATE;
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) {
            if (ImGui::IsKeyPressed(ImGuiKey_T)) currentOperation = ImGuizmo::TRANSLATE;
            if (ImGui::IsKeyPressed(ImGuiKey_R)) currentOperation = ImGuizmo::ROTATE;
            if (ImGui::IsKeyPressed(ImGuiKey_S)) currentOperation = ImGuizmo::SCALE;
        }
        if (ImGui::RadioButton("移動", currentOperation == ImGuizmo::TRANSLATE)) currentOperation = ImGuizmo::TRANSLATE; ImGui::SameLine();
        if (ImGui::RadioButton("回転", currentOperation == ImGuizmo::ROTATE)) currentOperation = ImGuizmo::ROTATE; ImGui::SameLine();
        if (ImGui::RadioButton("拡大縮小", currentOperation == ImGuizmo::SCALE)) currentOperation = ImGuizmo::SCALE;
    }
#endif
}

// -------------------------------------------------------------
// UIヘルパー関数群
// -------------------------------------------------------------
