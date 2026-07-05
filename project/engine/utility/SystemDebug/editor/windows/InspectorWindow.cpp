#include "InspectorWindow.h"
#include "DebugEditor.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "Object3d.h"
#include "imgui.h"
#include "IconsFontAwesome5.h"
#include "EditorManager.h"
#include "ParticleManager.h"
#include "GPUParticleManager.h"
#include "ModelManager.h"
#include "GhostRecorder.h"
#include "DebugConsole.h"
#include "ImGuizmo.h"
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
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

std::string NormalizeAssetPath(const fs::path& path) {
    return path.generic_string();
}

bool IsSupportedTextureFile(const fs::path& path) {
    const std::string ext = ToLowerAscii(path.extension().string());
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".dds";
}

struct PbrTexturePreset {
    std::string name;
    std::string albedoPath;
    std::string normalPath;
    std::string ormPath;

    bool IsComplete() const {
        return !albedoPath.empty() && !normalPath.empty() && !ormPath.empty();
    }
};

bool IsDdsTexture(const std::string& path) {
    return ToLowerAscii(fs::path(path).extension().string()) == ".dds";
}

void AssignPreferredTexture(std::string& slot, const std::string& candidate) {
    if (slot.empty() || (!IsDdsTexture(slot) && IsDdsTexture(candidate))) {
        slot = candidate;
    }
}

std::string BuildPbrPresetKey(const fs::path& path) {
    std::string stem = ToLowerAscii(path.stem().string());
    for (char& c : stem) {
        if (c == '-' || c == ' ') {
            c = '_';
        }
    }

    static const std::set<std::string> ignoredTokens = {
        "1k", "2k", "4k", "8k", "dx", "gl", "directx", "opengl",
        "diff", "diffuse", "albedo", "basecolor", "base", "color", "colour",
        "nor", "normal", "arm", "orm", "ao", "roughness", "metallic", "metalness"
    };

    std::string key;
    size_t start = 0;
    while (start <= stem.size()) {
        const size_t end = stem.find('_', start);
        const std::string token = stem.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!token.empty() && ignoredTokens.count(token) == 0) {
            if (!key.empty()) {
                key += "_";
            }
            key += token;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    if (key.empty()) {
        key = stem;
    }
    return key;
}

std::string BuildPbrPresetName(const std::string& key) {
    std::string name = key;
    for (char& c : name) {
        if (c == '_') {
            c = ' ';
        }
    }
    if (!name.empty()) {
        name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    }
    return name;
}

void RefreshTextureLists(
    std::vector<std::string>& albedoPaths,
    std::vector<std::string>& normalPaths,
    std::vector<std::string>& armPaths,
    std::vector<std::string>& spritePaths,
    std::vector<PbrTexturePreset>& pbrPresets) {
    albedoPaths.clear();
    normalPaths.clear();
    armPaths.clear();
    spritePaths.clear();
    pbrPresets.clear();

    const std::string pbrDir = "Resources/texture/PBR/";
    if (fs::exists(pbrDir)) {
        std::vector<std::string> allFiles;
        std::set<std::string> ddsBaseNames;
        std::map<std::string, PbrTexturePreset> presetMap;

        for (const auto& entry : fs::recursive_directory_iterator(pbrDir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const fs::path path = entry.path();
            if (!IsSupportedTextureFile(path)) {
                continue;
            }

            const std::string normalized = NormalizeAssetPath(path);
            allFiles.push_back(normalized);
            if (ToLowerAscii(path.extension().string()) == ".dds") {
                ddsBaseNames.insert(NormalizeAssetPath(path.parent_path() / path.stem()));
            }
        }

        for (const std::string& pathString : allFiles) {
            const fs::path p(pathString);
            const std::string ext = ToLowerAscii(p.extension().string());
            const std::string base = NormalizeAssetPath(p.parent_path() / p.stem());
            if (ext != ".dds" && ddsBaseNames.count(base) != 0) {
                continue;
            }

            if (pathString.find("/Albedo/") != std::string::npos) albedoPaths.push_back(pathString);
            else if (pathString.find("/Normal/") != std::string::npos) normalPaths.push_back(pathString);
            else if (pathString.find("/ARM/") != std::string::npos) armPaths.push_back(pathString);

            const std::string key = BuildPbrPresetKey(p);
            PbrTexturePreset& preset = presetMap[key];
            preset.name = BuildPbrPresetName(key);

            if (pathString.find("/Albedo/") != std::string::npos) {
                AssignPreferredTexture(preset.albedoPath, pathString);
            }
            else if (pathString.find("/Normal/") != std::string::npos) {
                AssignPreferredTexture(preset.normalPath, pathString);
            }
            else if (pathString.find("/ARM/") != std::string::npos) {
                AssignPreferredTexture(preset.ormPath, pathString);
            }
            else if (pathString.find("/Default/") != std::string::npos) {
                const std::string stem = ToLowerAscii(p.stem().string());
                if (stem.find("albedo") != std::string::npos || stem.find("diff") != std::string::npos) {
                    AssignPreferredTexture(preset.albedoPath, pathString);
                }
                else if (stem.find("normal") != std::string::npos || stem.find("nor") != std::string::npos) {
                    AssignPreferredTexture(preset.normalPath, pathString);
                }
                else if (stem.find("arm") != std::string::npos || stem.find("orm") != std::string::npos) {
                    AssignPreferredTexture(preset.ormPath, pathString);
                }
            }
        }

        for (const auto& [key, preset] : presetMap) {
            if (preset.IsComplete()) {
                pbrPresets.push_back(preset);
            }
        }
    }

    const std::string spriteDir = "Resources/sprite/";
    if (fs::exists(spriteDir)) {
        for (const auto& entry : fs::recursive_directory_iterator(spriteDir)) {
            if (!entry.is_regular_file() || !IsSupportedTextureFile(entry.path())) {
                continue;
            }
            spritePaths.push_back(NormalizeAssetPath(entry.path()));
        }
    }

    auto sortPaths = [](std::vector<std::string>& paths) {
        std::sort(paths.begin(), paths.end());
    };
    sortPaths(albedoPaths);
    sortPaths(normalPaths);
    sortPaths(armPaths);
    sortPaths(spritePaths);
    std::sort(pbrPresets.begin(), pbrPresets.end(), [](const PbrTexturePreset& a, const PbrTexturePreset& b) {
        return a.name < b.name;
    });
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

enum class PseudoComponentKind {
    Collision,
    Particle,
    Lod,
    MeshEffect,
    BoneAnimation,
    PathMove,
    LinkIds,
};

bool HasPseudoComponent(const Object3d* object, PseudoComponentKind kind) {
    if (!object) {
        return false;
    }

    switch (kind) {
    case PseudoComponentKind::Collision: {
        const Object3d::ColliderConfig& config = object->GetColliderConfig();
        return config.type != ColliderType::kNone || object->GetCollisionAttribute() != 0 || object->GetCollisionMask() != 0;
    }
    case PseudoComponentKind::Particle:
        return !object->GetParticleName().empty() || !object->GetGPUParticleName().empty();
    case PseudoComponentKind::Lod:
        return object->IsLodEnabled() || object->HasLodLevels();
    case PseudoComponentKind::MeshEffect:
        return !object->GetMeshEffect1Name().empty() || !object->GetMeshEffect2Name().empty();
    case PseudoComponentKind::BoneAnimation:
        return !object->animName_.empty();
    case PseudoComponentKind::PathMove:
        return !object->recordPathName_.empty();
    case PseudoComponentKind::LinkIds:
        return object->GetEventID() != 0 || object->GetTargetID() != 0;
    }
    return false;
}

const char* GetPseudoComponentName(PseudoComponentKind kind) {
    switch (kind) {
    case PseudoComponentKind::Collision: return "Collision";
    case PseudoComponentKind::Particle: return "Particle";
    case PseudoComponentKind::Lod: return "LOD";
    case PseudoComponentKind::MeshEffect: return "Mesh Effect";
    case PseudoComponentKind::BoneAnimation: return "Bone Animation";
    case PseudoComponentKind::PathMove: return "Path Move";
    case PseudoComponentKind::LinkIds: return "Link IDs";
    }
    return "Unknown";
}

const char* GetPseudoComponentDescription(PseudoComponentKind kind) {
    switch (kind) {
    case PseudoComponentKind::Collision: return "当たり判定、Trigger、Collision Maskを管理します。";
    case PseudoComponentKind::Particle: return "Objectに追従するCPU/GPU Particleを管理します。";
    case PseudoComponentKind::Lod: return "距離で軽量モデルへ切り替える設定です。";
    case PseudoComponentKind::MeshEffect: return "メッシュに重ねる演出エフェクトを管理します。";
    case PseudoComponentKind::BoneAnimation: return "モデルのボーンアニメーション名とループ設定です。";
    case PseudoComponentKind::PathMove: return "GhostRecorderの移動パス再生設定です。";
    case PseudoComponentKind::LinkIds: return "ギミック連携用のEvent/Target IDです。";
    }
    return "";
}

std::string FindFirstMeshEffectPath() {
    const std::string effectDir = "Resources/json/effect";
    if (!fs::exists(effectDir) || !fs::is_directory(effectDir)) {
        return "";
    }

    std::vector<std::string> candidates;
    for (const auto& entry : fs::directory_iterator(effectDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            candidates.push_back(entry.path().generic_string());
        }
    }

    std::sort(candidates.begin(), candidates.end());
    return candidates.empty() ? "" : candidates.front();
}

void AddDefaultParticleComponent(Object3d* object) {
    if (!object) {
        return;
    }

    const auto& gpuPresets = GPUParticleManager::GetInstance()->GetPresets();
    if (!gpuPresets.empty()) {
        object->SetGPUParticleName(gpuPresets.begin()->first);
        object->SetParticleName("");
        return;
    }

    const auto& cpuParams = ParticleManager::GetInstance()->GetParamsMap();
    if (!cpuParams.empty()) {
        object->SetParticleName(cpuParams.begin()->first);
        object->SetGPUParticleName("");
        return;
    }

    DebugConsole::GetInstance()->AddLog("Particle component add skipped: particle preset was not found.");
}

void AddPseudoComponentToObject(Object3d* object, PseudoComponentKind kind, bool triggerCollision = false) {
    if (!object) {
        return;
    }

    switch (kind) {
    case PseudoComponentKind::Collision: {
        Object3d::ColliderConfig config = object->GetColliderConfig();
        config.type = ColliderType::kAABB;
        if (std::fabs(config.size.x) < 0.001f || std::fabs(config.size.y) < 0.001f || std::fabs(config.size.z) < 0.001f) {
            config.size = { 1.0f, 1.0f, 1.0f };
        }
        object->SetColliderConfig(config);
        if (triggerCollision) {
            object->SetCollisionAttribute(CollisionAttribute::kTrigger);
            object->SetCollisionMask(CollisionAttribute::kPlayer);
            object->SetStatic(false);
        }
        else {
            object->SetCollisionAttribute(CollisionAttribute::kGround);
            object->SetCollisionMask(0xFFFFFFFFu);
            object->SetStatic(true);
        }
        break;
    }
    case PseudoComponentKind::Particle:
        AddDefaultParticleComponent(object);
        break;
    case PseudoComponentKind::Lod:
        object->SetLodEnabled(true);
        if (!object->HasLodLevels()) {
            object->ReloadLodManifest();
        }
        break;
    case PseudoComponentKind::MeshEffect: {
        if (object->GetMeshEffect1Name().empty()) {
            const std::string effectPath = FindFirstMeshEffectPath();
            if (!effectPath.empty()) {
                object->SetMeshEffect1Name(effectPath);
            }
            else {
                DebugConsole::GetInstance()->AddLog("Mesh Effect component add skipped: effect json was not found.");
            }
        }
        break;
    }
    case PseudoComponentKind::BoneAnimation:
        if (object->animName_.empty()) {
            object->animName_ = "Idle";
        }
        object->isAnimLoop_ = true;
        break;
    case PseudoComponentKind::PathMove:
        DebugConsole::GetInstance()->AddLog("Path Move component is enabled by selecting a path in the Path Move section.");
        break;
    case PseudoComponentKind::LinkIds:
        DebugConsole::GetInstance()->AddLog("Link IDs component is enabled by setting Event ID or Target ID.");
        break;
    }
}

void RemovePseudoComponentFromObject(Object3d* object, PseudoComponentKind kind) {
    if (!object) {
        return;
    }

    switch (kind) {
    case PseudoComponentKind::Collision: {
        Object3d::ColliderConfig config = object->GetColliderConfig();
        config.type = ColliderType::kNone;
        object->SetColliderConfig(config);
        object->SetCollisionAttribute(0);
        object->SetCollisionMask(0);
        object->SetStatic(false);
        break;
    }
    case PseudoComponentKind::Particle:
        object->SetParticleName("");
        object->SetGPUParticleName("");
        break;
    case PseudoComponentKind::Lod:
        object->SetLodEnabled(false);
        object->ClearLodLevels();
        break;
    case PseudoComponentKind::MeshEffect:
        object->SetMeshEffect1Name("");
        object->SetMeshEffect2Name("");
        break;
    case PseudoComponentKind::BoneAnimation:
        object->animName_.clear();
        object->isAnimLoop_ = false;
        break;
    case PseudoComponentKind::PathMove:
        object->recordPathName_.clear();
        if (object->recorder_) {
            object->recorder_->Stop();
        }
        break;
    case PseudoComponentKind::LinkIds:
        object->SetEventID(0);
        object->SetTargetID(0);
        break;
    }
}

void ApplyPseudoComponentToTargets(
    DebugEditor* editor,
    const std::vector<Object3d*>& targets,
    PseudoComponentKind kind,
    bool add,
    bool triggerCollision = false) {
    for (Object3d* target : targets) {
        if (!target) {
            continue;
        }
        if (add) {
            AddPseudoComponentToObject(target, kind, triggerCollision);
        }
        else {
            RemovePseudoComponentFromObject(target, kind);
        }
    }

    MarkInspectorTargetsDirty(editor, targets);
    RefreshInspectorTargetMatrices(targets);
}

void DrawPseudoComponentRow(
    DebugEditor* editor,
    const std::vector<Object3d*>& targets,
    const char* name,
    const char* description,
    bool removable,
    PseudoComponentKind kind) {
    ImGui::PushID(name);
    ImGui::BeginGroup();
    ImGui::TextUnformatted(name);
    ImGui::TextDisabled("%s", description);
    ImGui::EndGroup();

    if (removable) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            ApplyPseudoComponentToTargets(editor, targets, kind, false);
        }
    }
    else {
        ImGui::SameLine();
        ImGui::TextDisabled("固定");
    }

    ImGui::Separator();
    ImGui::PopID();
}

void DrawAddComponentMenu(DebugEditor* editor, const std::vector<Object3d*>& targets, Object3d* primary) {
    if (!ImGui::BeginPopup("AddPseudoComponentMenu")) {
        return;
    }

    ImGui::TextDisabled("追加するComponent");
    ImGui::Separator();

    const bool hasCollision = HasPseudoComponent(primary, PseudoComponentKind::Collision);
    if (ImGui::MenuItem("Box Collider (Ground)", nullptr, false, !hasCollision)) {
        ApplyPseudoComponentToTargets(editor, targets, PseudoComponentKind::Collision, true, false);
    }
    if (ImGui::MenuItem("Trigger Collider", nullptr, false, true)) {
        ApplyPseudoComponentToTargets(editor, targets, PseudoComponentKind::Collision, true, true);
    }
    if (ImGui::MenuItem("Particle", nullptr, false, !HasPseudoComponent(primary, PseudoComponentKind::Particle))) {
        ApplyPseudoComponentToTargets(editor, targets, PseudoComponentKind::Particle, true);
    }
    if (ImGui::MenuItem("LOD", nullptr, false, !HasPseudoComponent(primary, PseudoComponentKind::Lod))) {
        ApplyPseudoComponentToTargets(editor, targets, PseudoComponentKind::Lod, true);
    }
    if (ImGui::MenuItem("Mesh Effect", nullptr, false, !HasPseudoComponent(primary, PseudoComponentKind::MeshEffect))) {
        ApplyPseudoComponentToTargets(editor, targets, PseudoComponentKind::MeshEffect, true);
    }
    if (ImGui::MenuItem("Bone Animation", nullptr, false, !HasPseudoComponent(primary, PseudoComponentKind::BoneAnimation))) {
        ApplyPseudoComponentToTargets(editor, targets, PseudoComponentKind::BoneAnimation, true);
    }

    ImGui::Separator();
    ImGui::TextWrapped("Path MoveとLink IDsは下の専用セクションで値を入れるとComponentとして有効になります。");
    ImGui::EndPopup();
}

void DrawPseudoComponentPanel(DebugEditor* editor, Object3d* primary, const std::vector<Object3d*>& targets) {
    if (!primary) {
        return;
    }

    ImGui::SeparatorText("Components");
    ImGui::TextDisabled("Object3Dの既存機能をUnity風のComponentとして追加/削除します。");
    ImGui::TextDisabled("v1はPrefab化しやすいコピー型Componentです。元Prefabとのリンク管理はまだ行いません。");

    if (ImGui::Button("Add Component", ImVec2(-1.0f, 30.0f))) {
        ImGui::OpenPopup("AddPseudoComponentMenu");
    }
    DrawAddComponentMenu(editor, targets, primary);

    ImGui::Spacing();
    DrawPseudoComponentRow(editor, targets, "Transform", "位置・回転・スケール。Object3Dの必須Componentです。", false, PseudoComponentKind::Collision);

    const bool hasRenderer = primary->GetClassName() != "InvisibleBox" && !primary->GetModelName().empty();
    if (hasRenderer) {
        DrawPseudoComponentRow(editor, targets, "Renderer / Material", "モデル、マテリアル、影、PBRテクスチャを管理します。", false, PseudoComponentKind::Collision);
    }
    else {
        ImGui::TextDisabled("Renderer / Material: モデル未設定またはInvisibleBoxのため非表示扱いです。");
        ImGui::Separator();
    }

    const PseudoComponentKind optionalKinds[] = {
        PseudoComponentKind::Collision,
        PseudoComponentKind::Particle,
        PseudoComponentKind::Lod,
        PseudoComponentKind::MeshEffect,
        PseudoComponentKind::BoneAnimation,
        PseudoComponentKind::PathMove,
        PseudoComponentKind::LinkIds,
    };

    bool hasOptionalComponent = false;
    for (PseudoComponentKind kind : optionalKinds) {
        if (!HasPseudoComponent(primary, kind)) {
            continue;
        }
        hasOptionalComponent = true;
        DrawPseudoComponentRow(
            editor,
            targets,
            GetPseudoComponentName(kind),
            GetPseudoComponentDescription(kind),
            true,
            kind);
    }

    if (!hasOptionalComponent) {
        ImGui::TextDisabled("追加済みの任意Componentはありません。Add Componentから追加できます。");
    }
}}

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
            editor_->DuplicateSelected();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_DOWNLOAD " 単体保存 (JSON更新)")) {
            editor_->SaveSingleObject();
        }
        ImGui::Spacing();

        // --- クラス名表示 ---
        ImGui::TextDisabled(ICON_FA_CUBES " クラス: %s", selectedObject->GetClassName().c_str());

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
        if (ImGui::InputText("Tag", tagBuffer, sizeof(tagBuffer))) {
            const std::string newTag = tagBuffer;
            for (Object3d* target : inspectorTargets) {
                if (target) {
                    target->SetTag(newTag);
                }
            }
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }

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


        DrawPseudoComponentPanel(editor_, selectedObject, inspectorTargets);

        const char* saveCategories[] = { "Object", "Player", "Enemy" };
        std::string currentCat = selectedObject->GetSaveCategory();
        int catIndex = 0;
        if (currentCat == "Player") catIndex = 1;
        else if (currentCat == "Enemy") catIndex = 2;

        if (ImGui::Combo(ICON_FA_FOLDER " 保存先カテゴリ", &catIndex, saveCategories, IM_ARRAYSIZE(saveCategories))) {
            for (Object3d* object : inspectorTargets) {
                if (!object) continue;
                object->SetSaveCategory(saveCategories[catIndex]);
            }
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }

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
            ImGui::Button(ICON_FA_BOX_OPEN " [ ここにモデルをドロップして変更 ] ", ImVec2(-1, 30));

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
                    const char* modelName = (const char*)payload->Data;
                    ModelManager::GetInstance()->LoadModel(modelName);
                    selectedObject->SetModel(modelName);
                    DebugConsole::GetInstance()->AddLog("Switched model to: " + std::string(modelName));
                }
                
                // プリセットデータのドロップ受付
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PRESET_ASSET")) {
                    const char* presetName = (const char*)payload->Data;
                    PresetManager::GetInstance()->ApplyPresetToObject(presetName, selectedObject);
                    DebugConsole::GetInstance()->AddLog("Applied preset: " + std::string(presetName));
                }
                ImGui::EndDragDropTarget();
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
        if (ImGui::Checkbox(ICON_FA_EYE " 表示 (ゲーム内)", &isVisible)) {
            for (Object3d* object : inspectorTargets) {
                if (!object) continue;
                object->SetIsVisible(isVisible);
            }
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }
        bool isLocked = selectedObject->GetIsLocked();
        if (ImGui::Checkbox(ICON_FA_LOCK " ロック (編集保護)", &isLocked)) {
            for (Object3d* object : inspectorTargets) {
                if (!object) continue;
                object->SetIsLocked(isLocked);
            }
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }
        bool castShadow = selectedObject->GetCastShadow();
        if (ImGui::Checkbox(ICON_FA_LIGHTBULB " 影を落とす", &castShadow)) {
            for (Object3d* object : inspectorTargets) {
                if (!object) continue;
                object->SetCastShadow(castShadow);
            }
            MarkInspectorTargetsDirty(editor_, inspectorTargets);
        }

        // --- Transform編集 ---
        ImGui::Separator();
        ImGui::Text(ICON_FA_ARROWS_ALT " トランスフォーム (Transform)");
        Transform* transform = selectedObject->GetTransform();
        bool isTransformChanged = false;

        const Vector3 beforePos = transform->translate;
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

        Vector3 rotDeg = { ToDegrees(transform->rotate.x), ToDegrees(transform->rotate.y), ToDegrees(transform->rotate.z) };
        const Vector3 beforeRot = transform->rotate;
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
        const Vector3 beforeScale = transform->scale;
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
                                         "ダッシュパネル (Dash Panel)"
                };
                int currentMatType = selectedObject->GetMaterialType();
                if (currentMatType < 0) currentMatType = 0;
                if (currentMatType > 24) currentMatType = 0;
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
                        selectedObject->SetRoughness(textureBlend); isGraphicsChanged = true;
                    }
                    float paintStrength = selectedObject->GetMetallic();
                    if (ImGui::SliderFloat("塗りの濃さ / 色ムラ (Paint Strength)", &paintStrength, 0.0f, 1.0f)) {
                        selectedObject->SetMetallic(paintStrength); isGraphicsChanged = true;
                    }
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
                if (currentMatType >= 8 && currentMatType <= 22) {
                    ImGui::Separator();

                    if (selectedObject->GetMeshRenderer() && selectedObject->GetMeshRenderer()->GetWaterParamData()) {
                        auto* waterData = selectedObject->GetMeshRenderer()->GetWaterParamData();
                        if (currentMatType == 11) {
                            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.08f, 1.0f), ICON_FA_FIRE " --- Fire Settings ---");
                            const char* fireTypes[] = { "炎の形 (Flame Shape)", "炎の球 (Fire Ball)" };
                            int fireType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 1);
                            if (ImGui::Combo("炎タイプ (Fire Type)", &fireType, fireTypes, IM_ARRAYSIZE(fireTypes))) {
                                waterData->effectType = static_cast<float>(fireType);
                                isGraphicsChanged = true;
                            }
                            if (ImGui::DragFloat("揺らぎ速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 10.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("炎の細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 20.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("模様スケール (Pattern Scale)", &waterData->effectScale, 0.01f, 0.05f, 5.0f)) isGraphicsChanged = true;
                            if (ImGui::DragFloat("描画サイズ (Billboard Size)", &waterData->billboardScale, 0.01f, 0.05f, 3.0f)) isGraphicsChanged = true;
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

                static std::vector<std::string> albedoPaths;
                static std::vector<std::string> normalPaths;
                static std::vector<std::string> armPaths;
                static std::vector<std::string> spriteTexturePaths;
                static std::vector<PbrTexturePreset> pbrPresets;
                static bool isTextureListInitialized = false;

                if (!isTextureListInitialized) {
                    RefreshTextureLists(albedoPaths, normalPaths, armPaths, spriteTexturePaths, pbrPresets);
                    isTextureListInitialized = true;
                }

                ImGui::Separator();
                std::string currentTexturePath = selectedObject->GetTexturePath();
                std::string currentNormalPath = selectedObject->GetNormalMapPath();
                std::string currentOrmPathForPreset = selectedObject->GetOrmMapPath();

                std::string currentPresetName = "未設定 (3枚セットを選択)";
                for (const PbrTexturePreset& preset : pbrPresets) {
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
                    for (const PbrTexturePreset& preset : pbrPresets) {
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
                    isTextureListInitialized = false;
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
            if (ImGui::CollapsingHeader(ICON_FA_COMPRESS_ARROWS_ALT " LOD / 軽量モデル")) {
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
            if (ImGui::CollapsingHeader(ICON_FA_FIRE " パーティクル")) {
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
            if (ImGui::CollapsingHeader(ICON_FA_MAGIC " メッシュエフェクト (Mesh Effect)")) {
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

        // --- Gimmick (ID設定) ---
        ImGui::Separator();
        if (ImGui::CollapsingHeader(ICON_FA_LINK " ギミック設定 (Link IDs)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("イベント連携ID:");
            int tID = selectedObject->GetTargetID();
            if (ImGui::InputInt("送信先ID (Target)", &tID)) selectedObject->SetTargetID(tID);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("トリガーを作動させたい相手のIDを指定してください");

            int eID = selectedObject->GetEventID();
            if (ImGui::InputInt("自分ID (Event)", &eID)) selectedObject->SetEventID(eID);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("ギミック等から起動されるための、自分のIDを指定してください");
        }

        // ==========================================
        // 1. ボーンアニメーション設定
        // ==========================================
        ImGui::Separator();
        ImGui::Text(ICON_FA_BONE " 【ボーンアニメーション】");
        char animNameBuf[64];
        strncpy_s(animNameBuf, selectedObject->animName_.c_str(), sizeof(animNameBuf));
        if (ImGui::InputText("アニメ名##BoneAnim", animNameBuf, sizeof(animNameBuf))) {
            selectedObject->animName_ = animNameBuf;
        }
        ImGui::Checkbox("ループ再生##BoneAnim", &selectedObject->isAnimLoop_);

        // ==========================================
        // 2. パス移動 (GhostRecorder) 設定
        // ==========================================
        ImGui::Separator();
        ImGui::Text(ICON_FA_GHOST " 【パス移動 (GhostRecorder)】");
        std::string currentRecordPreview = selectedObject->recordPathName_.empty() ? "(なし)" : selectedObject->recordPathName_;

        if (ImGui::BeginCombo("パスデータ", currentRecordPreview.c_str())) {
            bool isNoneSelected = selectedObject->recordPathName_.empty();
            if (ImGui::Selectable("(なし)", isNoneSelected)) {
                selectedObject->recordPathName_ = "";
                if (selectedObject->recorder_) selectedObject->recorder_->Stop();
            }
            if (isNoneSelected) ImGui::SetItemDefaultFocus();

            std::string dirPath = "Resources/json/animation/";
            if (fs::exists(dirPath) && fs::is_directory(dirPath)) {
                for (const auto& entry : fs::directory_iterator(dirPath)) {
                    if (entry.path().extension() == ".json") {
                        std::string fileName = entry.path().stem().string();
                        bool isSelected = (selectedObject->recordPathName_ == fileName);

                        if (ImGui::Selectable(fileName.c_str(), isSelected)) {
                            selectedObject->recordPathName_ = fileName;
                            if (selectedObject->recorder_) {
                                bool isCinematic = (selectedObject->GetClassName() == "CinematicCamera");
                                selectedObject->recorder_->Play(selectedObject->recordPathName_, selectedObject->isRecordLoop_, selectedObject->isRecordRelative_, isCinematic);
                            }
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::Checkbox("ループ再生##Record", &selectedObject->isRecordLoop_)) {
            if (selectedObject->recorder_ && !selectedObject->recordPathName_.empty()) {
                bool isCinematic = (selectedObject->GetClassName() == "CinematicCamera");
                selectedObject->recorder_->Play(selectedObject->recordPathName_, selectedObject->isRecordLoop_, selectedObject->isRecordRelative_, isCinematic);
            }
        }
        if (ImGui::Checkbox("相対座標モード##Record", &selectedObject->isRecordRelative_)) {
            if (selectedObject->recorder_ && !selectedObject->recordPathName_.empty()) {
                bool isCinematic = (selectedObject->GetClassName() == "CinematicCamera");
                selectedObject->recorder_->Play(selectedObject->recordPathName_, selectedObject->isRecordLoop_, selectedObject->isRecordRelative_, isCinematic);
            }
        }
        if (ImGui::Button("テスト再生##Record")) {
            if (selectedObject->recorder_ && !selectedObject->recordPathName_.empty()) {
                bool isCinematic = (selectedObject->GetClassName() == "CinematicCamera");
                selectedObject->recorder_->Play(selectedObject->recordPathName_, selectedObject->isRecordLoop_, selectedObject->isRecordRelative_, isCinematic);
            }
        }

        // --- Game Data (Stats) ---
        ImGui::Separator();
        if (ImGui::CollapsingHeader(ICON_FA_GAMEPAD " ゲームデータ (Stats)", ImGuiTreeNodeFlags_DefaultOpen)) {
            EventType currentType = selectedObject->GetEventType();
            int currentItemIndex = static_cast<int>(currentType);
            const char* eventNames[] = { "なし", "ダメージ", "ワープ", "映像演出 (橋落ち)", "中間地点 (Checkpoint)", "ゴール", "ステージセレクト", "スターコイン (StarCoin)" };
            if (ImGui::Combo(ICON_FA_FLAG " イベント種類", &currentItemIndex, eventNames, IM_ARRAYSIZE(eventNames))) {
                const EventType selectedEventType = static_cast<EventType>(currentItemIndex);
                selectedObject->SetEventType(selectedEventType);
                if (selectedEventType == EventType::Goal) {
                    selectedObject->SetCollisionAttribute(CollisionAttribute::kTrigger);
                    selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);

                    if (selectedObject->GetColliderType() == ColliderType::kNone) {
                        Object3d::ColliderConfig colConfig;
                        colConfig.type = ColliderType::kSphere;
                        colConfig.size = { 1.2f, 1.2f, 1.2f };
                        selectedObject->SetColliderConfig(colConfig);
                        selectedObject->SetCollisionRadius(1.2f);
                    }
                }
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "--- Object Type Settings ---");
            const char* classItems[] = { "Model", "Spawner", "Player", "Enemy", "Gimmick", "Item", "InvisibleBox", "Block" };
            std::string currentClass = selectedObject->GetClassName();
            int currentClassIndex = 0;
            for (int i = 0; i < IM_ARRAYSIZE(classItems); i++) {
                if (currentClass == classItems[i]) { currentClassIndex = i; break; }
            }

            if (ImGui::Combo(ICON_FA_CUBES " Class Type", &currentClassIndex, classItems, IM_ARRAYSIZE(classItems))) {
                selectedObject->SetClassName(classItems[currentClassIndex]);
                if (std::string(classItems[currentClassIndex]) == "Spawner") {
                    if (selectedObject->GetName().find("Object") != std::string::npos) selectedObject->SetName("Spawner_New");
                    if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
                }
                else if (std::string(classItems[currentClassIndex]) == "Item") {
                    if (selectedObject->GetName().find("Object") != std::string::npos) selectedObject->SetName("Item_Heal");
                    if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
                    selectedObject->SetItemType("Heal");
                    selectedObject->param_->itemType = "Heal";
                    selectedObject->param_->healAmount = 1.0f;
                    selectedObject->SetModel("Item/heart.gltf");
                    selectedObject->SetColor({ 1.0f, 0.15f, 0.35f, 1.0f });
                    selectedObject->SetEmissive(1.8f);
                    selectedObject->SetScale({ 0.8f, 0.8f, 0.8f });
                    selectedObject->SetCollisionAttribute(CollisionAttribute::kTrigger);
                    selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);
                    selectedObject->SetStatic(false);

                    Object3d::ColliderConfig colConfig;
                    colConfig.type = ColliderType::kSphere;
                    colConfig.size = { 1.2f, 1.2f, 1.2f };
                    selectedObject->SetColliderConfig(colConfig);
                    selectedObject->SetCollisionRadius(1.2f);
                }
            }

            if (selectedObject->GetClassName() == "Spawner") DrawSpawnerSettings();

            ImGui::Spacing();
            if (selectedObject->GetClassName() == "Enemy") {
                ImGui::Indent(); DrawEnemyTypeSelector(); ImGui::Unindent();
            }
            if (selectedObject->GetClassName() == "Gimmick") {
                ImGui::Indent(); DrawGimmickTypeSelector(); ImGui::Unindent();
            }
            if (selectedObject->GetClassName() == "Item") {
                ImGui::Indent(); DrawItemTypeSelector(); ImGui::Unindent();
            }

            if (selectedObject->GetClassName() == "Enemy" || selectedObject->GetClassName() == "Player") {
                if (!selectedObject->param_.has_value()) {
                    if (ImGui::Button(ICON_FA_PLUS_CIRCLE " ステータスを追加", ImVec2(-1, 0))) selectedObject->param_.emplace();
                }
                else {
                    auto& p = selectedObject->param_.value();
                    ImGui::Text("キャラクター・ステータス:");
                    ImGui::Indent();
                    const float oldMaxHp = p.maxHp;
                    const bool wasFullHp = std::abs(p.hp - p.maxHp) <= 0.001f;
                    ImGui::DragFloat(ICON_FA_HEART " HP (体力)", &p.hp, 1.0f, 0.0f, 9999.0f);
                    if (ImGui::DragFloat(ICON_FA_HEARTBEAT " Max HP", &p.maxHp, 1.0f, 1.0f, 9999.0f)) {
                        p.maxHp = (std::max)(p.maxHp, 1.0f);
                        if (wasFullHp || std::abs(p.hp - oldMaxHp) <= 0.001f) {
                            p.hp = p.maxHp;
                        }
                    }
                    p.maxHp = (std::max)(p.maxHp, 1.0f);
                    p.hp = (std::max)(p.hp, 0.0f);
                    if (p.hp > p.maxHp) {
                        p.maxHp = p.hp;
                    }
                    ImGui::DragFloat(ICON_FA_BOLT " 攻撃力倍率", &p.attackPower, 0.05f, 0.0f, 100.0f);
                    p.attackPower = (std::max)(p.attackPower, 0.0f);
                    ImGui::DragFloat(ICON_FA_TACHOMETER_ALT " 速度 (Speed)", &p.speed, 0.1f, 0.0f, 100.0f);
                    ImGui::DragFloat(ICON_FA_ARROW_DOWN " 重力 (Gravity)", &p.gravity, 0.01f, -10.0f, 10.0f);
                    ImGui::DragFloat(ICON_FA_ARROW_UP " ジャンプ力", &p.jumpPower, 0.1f, 0.0f, 100.0f);
                    ImGui::DragFloat(ICON_FA_SEARCH " 検知範囲 (Detection)", &p.detectionRange, 0.5f, 0.0f, 500.0f);
                    ImGui::Unindent();

                    ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.6f, 0.6f));
                    if (ImGui::Button(ICON_FA_TRASH_ALT " ステータスを削除", ImVec2(-1, 0))) selectedObject->param_ = std::nullopt;
                    ImGui::PopStyleColor();
                }
            }
            else if (selectedObject->GetClassName() == "Gimmick") {
                if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
                auto& p = selectedObject->param_.value();
                
                ImGui::Text(ICON_FA_TOOLS " ギミック設定:");
                ImGui::Indent();
                
                std::string gType = selectedObject->GetGimmickType();
                if (gType == "Trampoline") {
                    ImGui::DragFloat(ICON_FA_ARROW_UP " ジャンプ力 (Jump Power)", &p.jumpPower, 1.0f, 0.0f, 100.0f);
                }
                else if (gType == "MovingFloor") {
                    ImGui::DragFloat(ICON_FA_TACHOMETER_ALT " 移動速度 (Speed)", &p.speed, 0.1f, 0.0f, 100.0f);
                }
                else if (gType == "ChikuwaBlock") {
                    ImGui::DragFloat(ICON_FA_HOURGLASS_HALF " 震え時間 (Shake)", &p.shakeDuration, 0.1f, 0.0f, 10.0f, "%.1f s");
                    ImGui::DragFloat(ICON_FA_ARROW_DOWN " 落下時間 (Fall)", &p.fallDuration, 0.1f, 0.0f, 10.0f, "%.1f s");
                    ImGui::DragFloat(ICON_FA_RECYCLE " リスポーン間隔", &p.interval, 0.1f, 0.0f, 10.0f, "%.1f s");
                    ImGui::DragFloat(ICON_FA_WEIGHT_HANGING " 落下速度 (Gravity)", &p.gravity, 1.0f, 0.0f, 200.0f);
                }
                else if (gType == "BlinkBlock") {
                    ImGui::Text(ICON_FA_PALETTE " ブロックの色設定:");
                    ImGui::RadioButton("青 (Blue: Jump Even)", &p.colorType, 0);
                    ImGui::SameLine();
                    ImGui::RadioButton("赤 (Red: Jump Odd)", &p.colorType, 1);
                }
                else if (gType == "Switch") {
                    const char* switchModes[] = { "押している間だけ", "押すたび切替", "一定時間だけ" };
                    ImGui::Combo("スイッチ方式", &p.switchMode, switchModes, IM_ARRAYSIZE(switchModes));
                    if (p.switchMode == 2) {
                        ImGui::DragFloat(ICON_FA_CLOCK " 有効時間", &p.interval, 0.1f, 0.1f, 60.0f, "%.1f s");
                    }
                    ImGui::TextDisabled("Target ID と受信側の My Event ID を合わせてください");
                }
                else if (gType == "EventReceiver") {
                    const char* actionModes[] = { "出現", "Y方向に移動", "X方向に移動", "Z方向に移動", "有効化", "無効化" };
                    ImGui::Combo("動作モード", &p.actionMode, actionModes, IM_ARRAYSIZE(actionModes));

                    if (p.actionMode >= 1 && p.actionMode <= 3) {
                        ImGui::DragFloat(ICON_FA_ARROWS_ALT " 移動量", &p.moveAmount, 0.1f, -500.0f, 500.0f);
                        ImGui::DragFloat(ICON_FA_TACHOMETER_ALT " 移動速度", &p.moveSpeed, 0.1f, 0.1f, 60.0f);
                    }

                    ImGui::Checkbox("開始時に有効", &p.startActive);
                    ImGui::Checkbox("OFFで元に戻す", &p.returnOnOff);
                    ImGui::TextDisabled("My Event ID とスイッチの Target ID を合わせてください");
                }
                else if (gType == "HookPullBlock") {
                    ImGui::DragFloat(ICON_FA_TACHOMETER_ALT " 引っ張り速度", &p.speed, 0.5f, 1.0f, 120.0f);
                    ImGui::DragFloat(ICON_FA_WEIGHT_HANGING " 重力", &p.gravity, 1.0f, 0.0f, 200.0f);
                    ImGui::TextDisabled("フックを当てるとプレイヤー側へ引き寄せます");
                }
                else if (gType == "OneWayFloor") {
                    ImGui::TextDisabled("上から着地した時だけ足場になります");
                }
                else if (gType == "LiquidLevel") {
                    const char* liquidTypes[] = { "水", "マグマ" };
                    ImGui::Combo("液体の種類", &p.colorType, liquidTypes, IM_ARRAYSIZE(liquidTypes));
                    ImGui::DragFloat(ICON_FA_ARROWS_ALT_V " 上下量", &p.moveAmount, 0.1f, -500.0f, 500.0f);
                    ImGui::DragFloat(ICON_FA_TACHOMETER_ALT " 上下速度", &p.moveSpeed, 0.1f, 0.1f, 60.0f);
                    ImGui::Checkbox("開始時に上昇", &p.startActive);
                    ImGui::Checkbox("OFFで元に戻す", &p.returnOnOff);
                    ImGui::TextDisabled("スイッチの Target ID とこの My Event ID を合わせてください");
                }
                else if (gType == "ChainCollapseFloor") {
                    ImGui::DragFloat(ICON_FA_HOURGLASS_HALF " 揺れ時間", &p.shakeDuration, 0.05f, 0.0f, 10.0f, "%.2f s");
                    ImGui::DragFloat(ICON_FA_LINK " 連鎖までの時間", &p.interval, 0.01f, 0.0f, 10.0f, "%.2f s");
                    ImGui::DragFloat(ICON_FA_ARROW_DOWN " 落下時間", &p.fallDuration, 0.05f, 0.1f, 10.0f, "%.2f s");
                    ImGui::DragFloat(ICON_FA_WEIGHT_HANGING " 重力", &p.gravity, 1.0f, 0.0f, 200.0f);
                    ImGui::TextDisabled("Target ID に次の床の My Event ID を入れると連鎖します");
                }
                else if (gType == "RotatingFloor" || gType == "RotatingPillar") {
                    const char* axes[] = { "X", "Y", "Z" };
                    ImGui::Combo("回転軸", &p.actionMode, axes, IM_ARRAYSIZE(axes));
                    ImGui::DragFloat(ICON_FA_SYNC_ALT " 回転速度 (度/秒)", &p.speed, 1.0f, -720.0f, 720.0f);
                    ImGui::Checkbox("開始時に回転", &p.startActive);
                    ImGui::Checkbox("OFFで停止", &p.returnOnOff);
                    ImGui::TextDisabled("スイッチ連動で回転の開始/停止ができます");
                }
                else if (gType == "PhaseFlipFloor") {
                    int floorNumber = p.colorType + 1;
                    int phaseCount = (std::max)(1, p.maxCount);
                    ImGui::DragInt("床番号", &floorNumber, 1, 1, phaseCount);
                    p.colorType = (std::clamp)(floorNumber, 1, phaseCount) - 1;

                    ImGui::DragInt("全体の床数", &p.maxCount, 1, 1, 16);
                    if (p.colorType >= p.maxCount) p.colorType = p.maxCount - 1;

                    ImGui::DragFloat(ICON_FA_CLOCK " 1フェーズの時間", &p.interval, 0.05f, 0.1f, 30.0f, "%.2f s");
                    ImGui::Checkbox("正方向に回転", &p.startActive);
                    ImGui::TextDisabled("床番号 1 -> 2 -> 3 ... の順に、当たり判定を残したまま180度回転します");
                }
                else if (gType == "FireCannon") {
                    const char* aimModes[] = { "前方固定", "プレイヤーを狙う" };
                    ImGui::Combo("発射方向", &p.actionMode, aimModes, IM_ARRAYSIZE(aimModes));
                    ImGui::DragFloat(ICON_FA_FIRE " 火球速度", &p.speed, 0.5f, 1.0f, 120.0f);
                    ImGui::DragFloat(ICON_FA_CLOCK " 発射間隔", &p.interval, 0.05f, 0.08f, 20.0f, "%.2f s");
                    ImGui::DragFloat(ICON_FA_CIRCLE " 火球サイズ", &p.moveAmount, 0.01f, 0.1f, 5.0f);
                    ImGui::DragFloat(ICON_FA_SEARCH " 索敵範囲", &p.detectionRange, 0.5f, 0.0f, 300.0f);
                    ImGui::DragFloat(ICON_FA_SYNC_ALT " 旋回速度 (度/秒)", &p.moveSpeed, 1.0f, 1.0f, 720.0f);
                    ImGui::Checkbox("開始時に有効", &p.startActive);
                    ImGui::Checkbox("OFFで停止", &p.returnOnOff);
                    ImGui::TextDisabled("前方固定はローカルZ+方向に火球を撃ちます");
                }
                else if (gType == "LaserEmitter") {
                    ImGui::DragFloat(ICON_FA_BOLT " ダメージ量", &p.speed, 0.5f, 0.0f, 100.0f);
                    ImGui::DragFloat(ICON_FA_CLOCK " ダメージ間隔", &p.interval, 0.05f, 0.05f, 10.0f, "%.2f s");
                    ImGui::DragFloat(ICON_FA_ARROWS_ALT_H " レーザーの太さ", &p.moveAmount, 0.01f, 0.03f, 5.0f);
                    ImGui::Checkbox("開始時に有効", &p.startActive);
                    ImGui::Checkbox("OFFで停止", &p.returnOnOff);
                    ImGui::TextDisabled("Target ID に終点ノードの My Event ID を入れると接続します");
                }
                else if (gType == "LaserNode") {
                    ImGui::DragFloat(ICON_FA_BOLT " ダメージ量", &p.speed, 0.5f, 0.0f, 100.0f);
                    ImGui::DragFloat(ICON_FA_CLOCK " ダメージ間隔", &p.interval, 0.05f, 0.05f, 10.0f, "%.2f s");
                    ImGui::DragFloat(ICON_FA_ARROWS_ALT_H " レーザーの太さ", &p.moveAmount, 0.01f, 0.03f, 5.0f);
                    ImGui::Checkbox("開始時に有効", &p.startActive);
                    ImGui::Checkbox("OFFで停止", &p.returnOnOff);
                    ImGui::TextDisabled("Target ID に次の LaserNode の My Event ID を入れると、その間にレーザーが出ます");
                }
                else if (gType == "StageGate") {
                    const char* gateModes[] = { "ステージセレクト用ノード", "シーン転移ゲート", "ステージ開始ゲート" };
                    p.actionMode = (std::clamp)(p.actionMode, 0, 2);
                    ImGui::Combo("ゲートモード", &p.actionMode, gateModes, IM_ARRAYSIZE(gateModes));
                    ImGui::Checkbox("開始時に有効", &p.startActive);

                    if (p.actionMode == 0) {
                        int stageIndex = selectedObject->GetTargetID();
                        if (ImGui::InputInt("ステージ番号 (Target ID)", &stageIndex)) {
                            selectedObject->SetTargetID(stageIndex);
                        }
                        ImGui::TextDisabled("ステージセレクトで近づいて決定した時だけ使われます");
                    }
                    else if (p.actionMode == 1) {
                        const char* sceneValues[] = { "TITLE", "TUTORIAL", "SELECT", "GAMEPLAY", "GAMECLEAR", "GAMEOVER", "SETTING", "PREVIEW" };
                        const char* sceneLabels[] = { "タイトル", "チュートリアル", "ステージセレクト", "ゲーム本編", "ゲームクリア", "ゲームオーバー", "設定", "プレビュー" };
                        int sceneIndex = 2;
                        for (int i = 0; i < IM_ARRAYSIZE(sceneValues); ++i) {
                            if (p.targetScene == sceneValues[i]) {
                                sceneIndex = i;
                                break;
                            }
                        }
                        if (ImGui::Combo("転移先シーン", &sceneIndex, sceneLabels, IM_ARRAYSIZE(sceneLabels))) {
                            p.targetScene = sceneValues[sceneIndex];
                        }
                        if (p.targetScene.empty()) {
                            p.targetScene = "SELECT";
                        }
                        ImGui::TextDisabled("プレイヤーが触れると指定シーンへ遷移します");
                    }
                    else if (p.actionMode == 2) {
                        int stageIndex = selectedObject->GetTargetID();
                        if (ImGui::InputInt("開始ステージ番号 (Target ID)", &stageIndex)) {
                            selectedObject->SetTargetID(stageIndex);
                        }
                        ImGui::TextDisabled("プレイヤーが触れると指定ステージをセットしてゲーム本編へ遷移します");
                    }
                }
                else {
                    ImGui::TextDisabled("(この種類には個別設定がありません)");
                }
                ImGui::Unindent();
            }
            else if (selectedObject->GetClassName() == "Item") {
                if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
                auto& p = selectedObject->param_.value();

                ImGui::Text(ICON_FA_HEART " アイテム設定:");
                ImGui::Indent();

                std::string itemType = selectedObject->GetItemType();
                if (itemType == "Heal") {
                    ImGui::DragFloat(ICON_FA_HEARTBEAT " 回復量", &p.healAmount, 0.1f, 0.0f, 999.0f);
                    ImGui::TextDisabled("プレイヤーが触れるとHPを回復して消えます");
                }
                else {
                    ImGui::TextDisabled("(この種類には個別設定がありません)");
                }

                ImGui::Unindent();
            }
        }

        ImGui::EndDisabled();
        ImGui::Separator();

        if (ImGui::Button(ICON_FA_TRASH_ALT " オブジェクト削除", ImVec2(-1, 0))) {
            editor_->DeleteSelected();
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
void InspectorWindow::DrawSpawnerSettings() {
#ifdef USE_IMGUI
    Object3d* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject) return;

    ImGui::Separator();
    ImGui::Indent();
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "[ Spawner Config ]");

    if (!selectedObject->param_.has_value()) {
        selectedObject->param_.emplace();
    }
    auto& p = selectedObject->param_.value();

    static char typeBuf[64] = "";
    if (typeBuf[0] == '\0') {
        strcpy_s(typeBuf, sizeof(typeBuf), p.enemyType.c_str());
    }

    const char* enemyTypes[] = { "Slime", "Bomb", "Bomber", "Mushroom", "GiantSlime", "FireSlime", "ThunderSlime", "Bat", "BeamDrone" };
    int currentTypeIndex = -1;
    for (int i = 0; i < IM_ARRAYSIZE(enemyTypes); i++) {
        if (p.enemyType == enemyTypes[i]) currentTypeIndex = i;
    }

    if (ImGui::Combo("Spawn Type", &currentTypeIndex, enemyTypes, IM_ARRAYSIZE(enemyTypes))) {
        p.enemyType = enemyTypes[currentTypeIndex];
        strcpy_s(typeBuf, sizeof(typeBuf), p.enemyType.c_str());
    }

    ImGui::DragFloat("Interval (sec)", &p.interval, 0.1f, 0.1f, 60.0f, "%.1f s");
    ImGui::InputInt("Max Count", &p.maxCount);

    ImGui::Unindent();
#endif
}

void InspectorWindow::DrawEnemyTypeSelector() {
#ifdef USE_IMGUI
    Object3d* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject) return;

    const char* enemyTypes[] = { "Slime", "BossCore", "Bomb", "Bomber", "Mushroom", "GiantSlime", "FireSlime", "ThunderSlime", "Bat", "BeamDrone" };
    std::string currentType = selectedObject->GetEnemyType();

    int currentIndex = -1;
    for (int i = 0; i < IM_ARRAYSIZE(enemyTypes); i++) {
        if (currentType == enemyTypes[i]) {
            currentIndex = i;
            break;
        }
    }

    const char* previewValue = (currentIndex >= 0) ? enemyTypes[currentIndex] : "(未設定)";

    if (ImGui::BeginCombo("敵の種族 (Enemy Type)", previewValue)) {
        for (int i = 0; i < IM_ARRAYSIZE(enemyTypes); i++) {
            bool isSelected = (currentIndex == i);
            if (ImGui::Selectable(enemyTypes[i], isSelected)) {
                selectedObject->SetEnemyType(enemyTypes[i]);
                selectedObject->SetName("Enemy_" + std::string(enemyTypes[i]));
                if (std::string(enemyTypes[i]) == "Bat") {
                    selectedObject->SetModel("Characters/bat");
                    selectedObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                    selectedObject->SetScale({ 0.6f, 0.6f, 0.6f });
                    selectedObject->animName_ = "ArmatureAction";
                    selectedObject->isAnimLoop_ = true;
                    selectedObject->SetCollisionAttribute(CollisionAttribute::kEnemy);
                    selectedObject->SetCollisionMask(CollisionAttribute::kPlayerAttack | CollisionAttribute::kAttributePlayerBullet);
                    selectedObject->SetColliderType(ColliderType::kSphere);
                    selectedObject->SetCollisionRadius(0.85f);
                }
                else if (std::string(enemyTypes[i]) == "BeamDrone") {
                    selectedObject->SetModel("Characters/eye");
                    selectedObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                    selectedObject->SetEmissive(1.4f);
                    selectedObject->SetScale({ 0.85f, 0.85f, 0.85f });
                    selectedObject->animName_.clear();
                    selectedObject->isAnimLoop_ = false;
                    selectedObject->SetCollisionAttribute(CollisionAttribute::kEnemy);
                    selectedObject->SetCollisionMask(CollisionAttribute::kPlayer | CollisionAttribute::kPlayerAttack | CollisionAttribute::kAttributePlayerBullet);
                    selectedObject->SetColliderType(ColliderType::kSphere);
                    selectedObject->SetCollisionRadius(1.1f);
                }
                else if (std::string(enemyTypes[i]) == "GiantSlime") {
                    selectedObject->SetModel("Characters/slime");
                    selectedObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                    selectedObject->SetEmissive(1.0f);
                    selectedObject->SetScale({ 2.8f, 2.8f, 2.8f });
                    selectedObject->animName_.clear();
                    selectedObject->isAnimLoop_ = false;
                    selectedObject->SetCollisionAttribute(CollisionAttribute::kEnemy);
                    selectedObject->SetCollisionMask(CollisionAttribute::kPlayer | CollisionAttribute::kGround | CollisionAttribute::kPlayerAttack | CollisionAttribute::kAttributePlayerBullet);
                    selectedObject->SetColliderType(ColliderType::kSphere);
                    selectedObject->SetCollisionRadius(2.2f);
                }
                else if (std::string(enemyTypes[i]) == "FireSlime") {
                    selectedObject->SetModel("Characters/slime_red");
                    selectedObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                    selectedObject->SetEmissive(1.15f);
                    selectedObject->SetScale({ 0.95f, 0.95f, 0.95f });
                    selectedObject->animName_.clear();
                    selectedObject->isAnimLoop_ = false;
                    selectedObject->SetCollisionAttribute(CollisionAttribute::kEnemy);
                    selectedObject->SetCollisionMask(CollisionAttribute::kPlayer | CollisionAttribute::kGround | CollisionAttribute::kPlayerAttack | CollisionAttribute::kAttributePlayerBullet);
                    selectedObject->SetColliderType(ColliderType::kSphere);
                    selectedObject->SetCollisionRadius(0.95f);
                }
                else if (std::string(enemyTypes[i]) == "ThunderSlime") {
                    selectedObject->SetModel("Characters/slime_yellow");
                    selectedObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                    selectedObject->SetEmissive(1.2f);
                    selectedObject->SetScale({ 0.95f, 0.95f, 0.95f });
                    selectedObject->animName_.clear();
                    selectedObject->isAnimLoop_ = false;
                    selectedObject->SetCollisionAttribute(CollisionAttribute::kEnemy);
                    selectedObject->SetCollisionMask(CollisionAttribute::kPlayer | CollisionAttribute::kGround | CollisionAttribute::kPlayerAttack | CollisionAttribute::kAttributePlayerBullet);
                    selectedObject->SetColliderType(ColliderType::kSphere);
                    selectedObject->SetCollisionRadius(0.95f);
                }
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ロード時に生成される敵クラスを指定します。\nEmptyの場合はただの箱になります。");
#endif
}

void InspectorWindow::DrawGimmickTypeSelector() {
#ifdef USE_IMGUI
    Object3d* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject) return;

    const char* gimmickTypes[] = { "Default", "MovingFloor", "Trampoline", "ChikuwaBlock", "BlinkBlock", "BreakableBlock", "Coin", "HookAnchor", "SinkingFloor", "SeesawFloor", "DashPanel", "IceFloor", "TimedSwitch", "AppearingFloor", "Switch", "EventReceiver", "HookPullBlock", "OneWayFloor", "LiquidLevel", "ChainCollapseFloor", "RotatingFloor", "RotatingPillar", "PhaseFlipFloor", "FireCannon", "StageGate", "LaserEmitter", "LaserNode" };
    const char* gimmickTypeLabels[] = {
        "通常",
        "移動床",
        "トランポリン",
        "ちくわブロック",
        "点滅ブロック",
        "破壊ブロック",
        "コイン",
        "フックアンカー",
        "沈む床",
        "シーソー床",
        "ダッシュパネル",
        "氷の床",
        "時限スイッチ床",
        "出現床",
        "汎用スイッチ",
        "イベント受信ギミック",
        "フックで引っ張るブロック",
        "一方通行床",
        "水位・マグマ上下",
        "連鎖崩れ床",
        "回転床",
        "回転柱",
        "順番反転床",
        "火球砲台",
        "ステージゲート",
        "レーザー発生器",
        "レーザー接続ノード"
    };
    std::string currentType = selectedObject->GetGimmickType();

    int currentIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(gimmickTypes); i++) {
        if (currentType == gimmickTypes[i]) {
            currentIndex = i;
            break;
        }
    }

    if (ImGui::Combo("ギミックの種類", &currentIndex, gimmickTypeLabels, IM_ARRAYSIZE(gimmickTypeLabels))) {
        std::string selectedGimmickType = gimmickTypes[currentIndex];
        selectedObject->SetGimmickType(selectedGimmickType);
        
        if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
        selectedObject->param_->gimmickType = selectedGimmickType;
        
        // 各ギミックに合わせた初期状態（エディタ上のデフォルト初期値）を設定
        if (selectedGimmickType == "BreakableBlock") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_BreakableBlock");
            selectedObject->SetModel("Stages/bomb_break_block");
            selectedObject->SetTexture("Resources/3DModel/Stages/bomb_break_block/bomb_break_block_albedo.png");
            selectedObject->SetEnableNormalMap(true);
            selectedObject->SetNormalMap("Resources/3DModel/Stages/bomb_break_block/bomb_break_block_normal.png");
            selectedObject->SetOrmMap("Resources/3DModel/Stages/bomb_break_block/bomb_break_block_orm.png");
            selectedObject->SetMaterialType(0);
            selectedObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
            selectedObject->SetMetallic(0.0f);
            selectedObject->SetRoughness(0.72f);
            selectedObject->SetEnableEnvMap(false);
            selectedObject->SetEmissive(1.0f);
            selectedObject->SetScale({ 1.0f, 1.0f, 1.0f });
            
            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);
            
            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "Coin") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_Coin");
            selectedObject->SetModel("Primitives/sphere");
            selectedObject->SetColor({ 1.0f, 0.9f, 0.0f, 1.0f }); // ゴールドイエロー
            selectedObject->SetScale({ 0.6f, 0.6f, 0.15f }); // 薄いコインの形
            
            selectedObject->SetCollisionAttribute(CollisionAttribute::kTrigger);
            selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);
            
            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kSphere;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
            selectedObject->SetCollisionRadius(1.0f);
        }
        else if (selectedGimmickType == "HookAnchor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_HookAnchor");
            selectedObject->SetModel("Primitives/sphere");
            selectedObject->SetColor({ 0.2f, 0.85f, 1.0f, 1.0f });
            selectedObject->SetScale({ 1.2f, 1.2f, 1.2f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kHookAnchor);
            selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kSphere;
            colConfig.size = { 2.5f, 2.5f, 2.5f };
            selectedObject->SetColliderConfig(colConfig);
            selectedObject->SetCollisionRadius(2.5f);
        }
        else if (selectedGimmickType == "SinkingFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_SinkingFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.55f, 0.75f, 1.0f, 1.0f });
            selectedObject->SetScale({ 1.0f, 1.0f, 1.0f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "SeesawFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_SeesawFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.9f, 0.75f, 0.35f, 1.0f });
            selectedObject->SetScale({ 4.0f, 0.35f, 1.4f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "DashPanel") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_DashPanel");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetMaterialType(24);
            selectedObject->SetBlendMode(BlendMode::kNone);
            selectedObject->SetColor({ 0.25f, 0.95f, 1.0f, 1.0f });
            selectedObject->SetRoughness(0.62f);
            selectedObject->SetMetallic(0.56f);
            selectedObject->SetEmissive(1.0f);
            selectedObject->SetTextureTiling({ 1.0f, 1.0f });
            selectedObject->SetAutoTextureTiling(false);
            selectedObject->SetScale({ 2.0f, 0.25f, 1.2f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "IceFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_IceFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.65f, 0.9f, 1.0f, 0.9f });
            selectedObject->SetScale({ 1.0f, 1.0f, 1.0f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "TimedSwitch") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_TimedSwitch");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.25f, 0.75f, 1.0f, 1.0f });
            selectedObject->SetScale({ 1.5f, 0.25f, 1.5f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "AppearingFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_AppearingFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.55f, 1.0f, 0.7f, 1.0f });
            selectedObject->SetScale({ 2.0f, 0.35f, 2.0f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "Switch") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_Switch");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.25f, 0.75f, 1.0f, 1.0f });
            selectedObject->SetScale({ 1.5f, 0.25f, 1.5f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->switchMode = 0;
            selectedObject->param_->interval = 3.0f;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "EventReceiver") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_EventReceiver");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.65f, 1.0f, 0.65f, 1.0f });
            selectedObject->SetScale({ 2.0f, 0.35f, 2.0f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->actionMode = 0;
            selectedObject->param_->moveAmount = 10.0f;
            selectedObject->param_->moveSpeed = 6.0f;
            selectedObject->param_->startActive = false;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "HookPullBlock") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_HookPullBlock");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.55f, 0.85f, 1.0f, 1.0f });
            selectedObject->SetScale({ 1.0f, 1.0f, 1.0f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->speed = 42.0f;
            selectedObject->param_->gravity = 50.0f;
            selectedObject->param_->maxFallSpeed = 60.0f;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "OneWayFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_OneWayFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.85f, 0.9f, 0.65f, 0.9f });
            selectedObject->SetScale({ 2.5f, 0.22f, 2.5f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "LiquidLevel") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_LiquidLevel");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.45f, 0.85f, 1.0f, 0.65f });
            selectedObject->SetScale({ 4.0f, 0.08f, 4.0f });
            selectedObject->SetMaterialType(8);
            selectedObject->SetCollisionAttribute(0);
            selectedObject->SetCollisionMask(0);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->colorType = 0;
            selectedObject->param_->moveAmount = 6.0f;
            selectedObject->param_->moveSpeed = 3.0f;
            selectedObject->param_->startActive = false;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "ChainCollapseFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_ChainCollapseFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.75f, 0.92f, 1.0f, 0.82f });
            selectedObject->SetScale({ 2.0f, 0.25f, 2.0f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->shakeDuration = 0.45f;
            selectedObject->param_->fallDuration = 1.4f;
            selectedObject->param_->interval = 0.18f;
            selectedObject->param_->gravity = 48.0f;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "RotatingFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_RotatingFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.95f, 0.65f, 0.35f, 1.0f });
            selectedObject->SetScale({ 3.0f, 0.3f, 1.2f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->speed = 45.0f;
            selectedObject->param_->actionMode = 1;
            selectedObject->param_->startActive = true;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "RotatingPillar") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_RotatingPillar");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.95f, 0.65f, 0.35f, 1.0f });
            selectedObject->SetScale({ 0.75f, 3.0f, 0.75f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->speed = 60.0f;
            selectedObject->param_->actionMode = 1;
            selectedObject->param_->startActive = true;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "PhaseFlipFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_PhaseFlipFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.35f, 0.75f, 1.0f, 1.0f });
            selectedObject->SetScale({ 2.0f, 0.25f, 2.0f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->colorType = 0;
            selectedObject->param_->maxCount = 3;
            selectedObject->param_->interval = 1.0f;
            selectedObject->param_->startActive = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "FireCannon") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_FireCannon");
            selectedObject->SetModel("Primitives/cube");
            selectedObject->SetColor({ 0.22f, 0.11f, 0.08f, 1.0f });
            selectedObject->SetBlendMode(BlendMode::kNormal);
            selectedObject->SetMaterialType(0);
            selectedObject->SetEmissive(1.1f);
            selectedObject->SetScale({ 0.55f, 0.55f, 1.1f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->speed = 13.0f;
            selectedObject->param_->interval = 1.35f;
            selectedObject->param_->moveAmount = 0.55f;
            selectedObject->param_->moveSpeed = 360.0f;
            selectedObject->param_->detectionRange = 45.0f;
            selectedObject->param_->actionMode = 1;
            selectedObject->param_->startActive = true;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "StageGate") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_StageGate");
            selectedObject->SetModel("Gimmicks/portal_surface");
            selectedObject->SetColor({ 0.35f, 0.75f, 1.0f, 1.0f });
            selectedObject->SetBlendMode(BlendMode::kNormal);
            selectedObject->SetMaterialType(22);
            selectedObject->SetEmissive(1.8f);
            selectedObject->SetEnableEnvMap(false);
            selectedObject->SetScale({ 1.4f, 1.4f, 1.4f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kTrigger);
            selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->gimmickType = "StageGate";
            selectedObject->param_->actionMode = 0;
            selectedObject->param_->targetScene = "SELECT";
            selectedObject->param_->startActive = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kSphere;
            colConfig.size = { 4.0f, 4.0f, 4.0f };
            selectedObject->SetColliderConfig(colConfig);
            selectedObject->SetCollisionRadius(4.0f);
        }
        else if (selectedGimmickType == "LaserEmitter") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_LaserEmitter");
            selectedObject->SetModel("Primitives/cube");
            selectedObject->SetColor({ 1.0f, 0.08f, 0.05f, 0.9f });
            selectedObject->SetBlendMode(BlendMode::kAdd);
            selectedObject->SetMaterialType(12);
            selectedObject->SetTexture("Resources/sprite/common/white.png");
            selectedObject->SetEmissive(6.0f);
            selectedObject->SetScale({ 0.25f, 0.25f, 1.0f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kTrigger);
            selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->speed = 10.0f;
            selectedObject->param_->interval = 0.7f;
            selectedObject->param_->moveAmount = 0.14f;
            selectedObject->param_->startActive = true;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "LaserNode") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_LaserNode");
            selectedObject->SetModel("Primitives/sphere");
            selectedObject->SetColor({ 1.0f, 0.18f, 0.08f, 1.0f });
            selectedObject->SetBlendMode(BlendMode::kAdd);
            selectedObject->SetMaterialType(3);
            selectedObject->SetTexture("Resources/sprite/common/white.png");
            selectedObject->SetEmissive(3.5f);
            selectedObject->SetScale({ 0.35f, 0.35f, 0.35f });
            selectedObject->SetCollisionAttribute(0);
            selectedObject->SetCollisionMask(0);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->speed = 10.0f;
            selectedObject->param_->interval = 0.7f;
            selectedObject->param_->moveAmount = 0.14f;
            selectedObject->param_->startActive = true;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kSphere;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
            selectedObject->SetCollisionRadius(1.0f);
        }
        else if (selectedGimmickType == "Default") {
            selectedObject->SetClassName("Default");
            selectedObject->SetName("Cube");
        }
        else {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_" + selectedGimmickType);
        }
    }
    
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ロード時に生成されるギミッククラスを指定します。");
#endif
}

void InspectorWindow::DrawItemTypeSelector() {
#ifdef USE_IMGUI
    Object3d* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject) return;

    const char* itemTypes[] = { "Heal" };
    const char* itemTypeLabels[] = { "体力回復" };
    std::string currentType = selectedObject->GetItemType();
    if (currentType.empty()) {
        currentType = "Heal";
        selectedObject->SetItemType(currentType);
        if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
        selectedObject->param_->itemType = currentType;
        selectedObject->param_->healAmount = 1.0f;
        selectedObject->SetName("Item_Heal");
        selectedObject->SetModel("Item/heart.gltf");
        selectedObject->SetColor({ 1.0f, 0.15f, 0.35f, 1.0f });
        selectedObject->SetEmissive(1.8f);
        selectedObject->SetScale({ 0.8f, 0.8f, 0.8f });
        selectedObject->SetCollisionAttribute(CollisionAttribute::kTrigger);
        selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);
        selectedObject->SetStatic(false);

        Object3d::ColliderConfig colConfig;
        colConfig.type = ColliderType::kSphere;
        colConfig.size = { 1.2f, 1.2f, 1.2f };
        selectedObject->SetColliderConfig(colConfig);
        selectedObject->SetCollisionRadius(1.2f);
    }

    int currentIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(itemTypes); i++) {
        if (currentType == itemTypes[i]) {
            currentIndex = i;
            break;
        }
    }

    if (ImGui::Combo("アイテムの種類", &currentIndex, itemTypeLabels, IM_ARRAYSIZE(itemTypeLabels))) {
        std::string selectedItemType = itemTypes[currentIndex];
        selectedObject->SetClassName("Item");
        selectedObject->SetItemType(selectedItemType);

        if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
        selectedObject->param_->itemType = selectedItemType;

        if (selectedItemType == "Heal") {
            selectedObject->SetName("Item_Heal");
            selectedObject->SetModel("Item/heart.gltf");
            selectedObject->SetColor({ 1.0f, 0.15f, 0.35f, 1.0f });
            selectedObject->SetEmissive(1.8f);
            selectedObject->SetScale({ 0.8f, 0.8f, 0.8f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kTrigger);
            selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);
            selectedObject->SetStatic(false);

            selectedObject->param_->healAmount = 1.0f;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kSphere;
            colConfig.size = { 1.2f, 1.2f, 1.2f };
            selectedObject->SetColliderConfig(colConfig);
            selectedObject->SetCollisionRadius(1.2f);
        }
    }

    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ロード時に生成されるアイテムクラスを指定します。");
#endif
}

void InspectorWindow::DrawAttributeSelector(const char* label, uint32_t* attribute) {
#ifdef USE_IMGUI
    if (ImGui::TreeNode(label)) {
        int flags = static_cast<int>(*attribute);
        ImGui::CheckboxFlags("プレイヤー (Player)", &flags, 1 << 0);
        ImGui::CheckboxFlags("敵 (Enemy)", &flags, 1 << 1);
        ImGui::CheckboxFlags("床・地形 (Ground)", &flags, 1 << 2);
        ImGui::CheckboxFlags("弾 (Bullet)", &flags, 1 << 3);
        ImGui::CheckboxFlags("トリガー (Trigger)", &flags, 1 << 4);
        ImGui::CheckboxFlags("プレイヤー攻撃 (PlayerAttack)", &flags, 1 << 6);
        ImGui::CheckboxFlags("敵攻撃 (EnemyAttack)", &flags, 1 << 7);
        *attribute = static_cast<uint32_t>(flags);
        ImGui::TreePop();
    }
#endif
}
