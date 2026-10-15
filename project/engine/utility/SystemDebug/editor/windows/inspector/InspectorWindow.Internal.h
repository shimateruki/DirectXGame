#pragma once
// 分割実装だけが使う補助関数の宣言です。状態を重複して持たせません。
#include "engine/utility/SystemDebug/editor/windows/inspector/InspectorWindow.h"
#include "engine/utility/SystemDebug/editor/windows/inspector/InspectorTextureCatalog.h"
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
#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.h"
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
#include "engine/utility/SystemDebug/editor/core/preset/PresetManager.h"
static const float PI = (float)M_PI;
static float ToRadians(float degrees) { return degrees * (PI / 180.0f); }
static float ToDegrees(float radians) { return radians * (180.0f / PI); }
namespace fs = std::filesystem;

namespace InspectorWindowImplementation {
std::string ToLowerAscii(std::string text);

// 複数選択を含む現在の編集対象を集めます。戻り値はシーン所有オブジェクトへの非所有参照です。
std::vector<engine::Object3d*> CollectInspectorTargets(DebugEditor* editor, engine::Object3d* primary);

void MarkInspectorTargetsDirty(DebugEditor* editor, const std::vector<engine::Object3d*>& targets);

void RefreshInspectorTargetMatrices(const std::vector<engine::Object3d*>& targets);

float SafeScaleRatio(float after, float before);

// 主対象で行った移動・回転・拡大率の差分を、他の選択対象へ相対的に適用します。
void ApplyPrimaryTransformDeltaToTargets(
    const std::vector<engine::Object3d*>& targets,
    engine::Object3d* primary,
    const engine::Vector3& beforePosition,
    const engine::Vector3& afterPosition,
    const engine::Vector3& beforeRotation,
    const engine::Vector3& afterRotation,
    const engine::Vector3& beforeScale,
    const engine::Vector3& afterScale,
    bool applyPosition,
    bool applyRotation,
    bool applyScale);

bool IsSpriteCardObject(const engine::Object3d* object);

void ConfigureSpriteCardObject(engine::Object3d* object);

void ApplyRegisteredComponentToTargets(
    DebugEditor* editor,
    const std::vector<engine::Object3d*>& targets,
    const std::string& componentTypeId,
    bool add);

void ApplyRegisteredComponentPresetToTargets(
    DebugEditor* editor,
    const std::vector<engine::Object3d*>& targets,
    const std::string& componentTypeId,
    const std::string& presetId);

void DrawRegisteredComponentRow(
    DebugEditor* editor,
    const std::vector<engine::Object3d*>& targets,
    const EditorComponentDescriptor& component);

void DrawRegisteredAddComponentMenu(
    DebugEditor* editor,
    const std::vector<engine::Object3d*>& targets,
    engine::Object3d* primary);

bool HasRegisteredComponent(const engine::Object3d* object, const char* componentTypeId);

std::vector<engine::Object3d*> CollectRegisteredComponentTargets(
    const std::vector<engine::Object3d*>& targets,
    const char* componentTypeId);

bool IsPrefabPropertyOverridden(const engine::Object3d* object, const std::string& propertyPath);

void DrawAutomaticComponentProperties(
    DebugEditor* editor,
    engine::Object3d* primary,
    const std::vector<engine::Object3d*>& targets,
    const EditorComponentDescriptor& component);

void DrawComponentPanel(DebugEditor* editor, engine::Object3d* primary, const std::vector<engine::Object3d*>& targets);}

namespace InspectorWindowImplementation {

bool DrawSceneObjectNameCombo(const char* label, BaseScene* scene, engine::Object3d* cameraObject, std::string& objectName);

bool HasMixedInspectorValue(const std::vector<engine::Object3d*>& targets, const char* propertyPath);

void DrawMixedValueHint(bool mixed);

void PushMixedCheckbox(bool mixed);

void PopMixedCheckbox();

void DrawCameraObjectInspector(DebugEditor* editor, BaseScene* scene, engine::Object3d* cameraObject);

std::vector<engine::Object3d*> CollectSceneObjects(BaseScene* scene);

std::vector<engine::Object3d*> CollectPrefabInstanceObjects(BaseScene* scene, const std::string& instanceId);

void DrawPrefabInstancePanel(DebugEditor* editor, BaseScene* scene, engine::Object3d* object);

std::string NormalizeMaterialInstanceSavePath(const char* text);

void DrawMaterialInstancePanel(
    DebugEditor* editor,
    engine::Object3d* selectedObject,
    const std::vector<engine::Object3d*>& targets);

void DrawDecalPanel(
    DebugEditor* editor,
    engine::Object3d* selectedObject,
    const std::vector<engine::Object3d*>& targets);

}
