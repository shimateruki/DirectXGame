#pragma once

// 担当：複数選択・Prefab差分・カメラなどのInspector内部処理を共有する。
// 関連：InspectorWindowの分割.cpp、DebugEditor / EditorPropertyRegistry
// 分割実装だけが使う補助関数の宣言です。状態を重複して持たせません。
#include "engine/editor/scene/inspector/InspectorWindow.h"
#include <numbers>
#include "engine/editor/scene/inspector/InspectorTextureCatalog.h"
#include "DebugEditor.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "Object3D.h"
#include "engine/graphics/3d/material/MaterialInstance.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "IconsFontAwesome5.h"
#include "EditorManager.h"
#include "ParticleManager.h"
#include "GpuParticleManager.h"
#include "ModelManager.h"
#include "GhostRecorder.h"
#include "engine/editor/graphics/camera/CameraEditor.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "game/system/rules/GameplayStatusManager.h"
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

#include "engine/editor/assets/preset/PresetManager.h"

// 度をラジアンへ変換する。
static float ToRadians(float degrees) { return degrees * (std::numbers::pi_v<float> / 180.0f); }
// ラジアンを度へ変換する。
static float ToDegrees(float radians) { return radians * (180.0f / std::numbers::pi_v<float>); }
namespace fs = std::filesystem;

namespace InspectorWindowImplementation {
std::string ToLowerAscii(std::string text);

// 複数選択を含む現在の編集対象を集めます。戻り値はシーン所有オブジェクトへの非所有参照です。
std::vector<engine::Object3D*> CollectInspectorTargets(DebugEditor* editor, engine::Object3D* primary);

// 複数選択の編集を保存対象へ反映するため、生存しているObjectだけ変更済みにする。
void MarkInspectorTargetsDirty(DebugEditor* editor, const std::vector<engine::Object3D*>& targets);

// 編集値をワールド行列へ反映し、選択枠や子Objectの表示を更新する。
void RefreshInspectorTargetMatrices(const std::vector<engine::Object3D*>& targets);

// ゼロ付近の元スケールを考慮して、変更前後の比率を求める。
float SafeScaleRatio(float after, float before);

// 主対象で行った移動・回転・拡大率の差分を、他の選択対象へ相対的に適用します。
void ApplyPrimaryTransformDeltaToTargets(
    const std::vector<engine::Object3D*>& targets,
    engine::Object3D* primary,
    const engine::Vector3& beforePosition,
    const engine::Vector3& afterPosition,
    const engine::Vector3& beforeRotation,
    const engine::Vector3& afterRotation,
    const engine::Vector3& beforeScale,
    const engine::Vector3& afterScale,
    bool applyPosition,
    bool applyRotation,
    bool applyScale);

bool IsSpriteCardObject(const engine::Object3D* object);

// 画像を表示する板状Objectの共通設定を適用する。
void ConfigureSpriteCardObject(engine::Object3D* object);

// 複数選択へ同じComponent操作を適用し、変更があったときだけDirtyと行列を更新する。
void ApplyRegisteredComponentToTargets(
    DebugEditor* editor,
    const std::vector<engine::Object3D*>& targets,
    const std::string& componentTypeId,
    bool add);

void ApplyRegisteredComponentPresetToTargets(
    DebugEditor* editor,
    const std::vector<engine::Object3D*>& targets,
    const std::string& componentTypeId,
    const std::string& presetId);

void DrawRegisteredComponentRow(
    DebugEditor* editor,
    const std::vector<engine::Object3D*>& targets,
    const EditorComponentDescriptor& component);

void DrawRegisteredAddComponentMenu(
    DebugEditor* editor,
    const std::vector<engine::Object3D*>& targets,
    engine::Object3D* primary);

bool HasRegisteredComponent(const engine::Object3D* object, const char* componentTypeId);

std::vector<engine::Object3D*> CollectRegisteredComponentTargets(
    const std::vector<engine::Object3D*>& targets,
    const char* componentTypeId);

bool IsPrefabPropertyOverridden(const engine::Object3D* object, const std::string& propertyPath);

void DrawAutomaticComponentProperties(
    DebugEditor* editor,
    engine::Object3D* primary,
    const std::vector<engine::Object3D*>& targets,
    const EditorComponentDescriptor& component);

// 共通Component定義を用いて単体・複数選択のInspectorを同じ手順で描画する。
void DrawComponentPanel(DebugEditor* editor, engine::Object3D* primary, const std::vector<engine::Object3D*>& targets);}

namespace InspectorWindowImplementation {

// カメラ自身とEditor専用Objectを除き、注視対象として有効な名前だけ列挙する。
bool DrawSceneObjectNameCombo(const char* label, BaseScene* scene, engine::Object3D* cameraObject, std::string& objectName);

bool HasMixedInspectorValue(const std::vector<engine::Object3D*>& targets, const char* propertyPath);

// 複数選択で値が異なる項目に、混在の案内を表示する。
void DrawMixedValueHint(bool mixed);

// 複数対象で値が異なるCheckboxの表示フラグを積む。描画後に PopMixedCheckbox と対で戻す。
void PushMixedCheckbox(bool mixed);

// 混在値チェックボックス用に積んだUI状態を戻す。
void PopMixedCheckbox();

// Camera Objectの視点・領域設定を編集する。
void DrawCameraObjectInspector(DebugEditor* editor, BaseScene* scene, engine::Object3D* cameraObject);

// インスペクターの対象を現シーンの所有オブジェクトから集める。
std::vector<engine::Object3D*> CollectSceneObjects(BaseScene* scene);

// 同一Prefabインスタンスに属する子だけを編集対象として集める。
std::vector<engine::Object3D*> CollectPrefabInstanceObjects(BaseScene* scene, const std::string& instanceId);

// Prefab Instanceの差分表示と適用・復元操作を描画する。
void DrawPrefabInstancePanel(DebugEditor* editor, BaseScene* scene, engine::Object3D* object);

std::string NormalizeMaterialInstanceSavePath(const char* text);

void DrawMaterialInstancePanel(
    DebugEditor* editor,
    engine::Object3D* selectedObject,
    const std::vector<engine::Object3D*>& targets);

void DrawDecalPanel(
    DebugEditor* editor,
    engine::Object3D* selectedObject,
    const std::vector<engine::Object3D*>& targets);

}
