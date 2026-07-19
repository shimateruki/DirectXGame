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
        return object->HasParticleEmitterComponent();
    case PseudoComponentKind::Lod:
        return object->IsLodEnabled() || object->HasLodLevels();
    case PseudoComponentKind::MeshEffect:
        return object->HasMeshEffectComponent();
    case PseudoComponentKind::BoneAnimation:
        return !object->animName_.empty() || object->HasAnimatorController();
    case PseudoComponentKind::PathMove:
        return object->HasPathMoverComponent();
    case PseudoComponentKind::LinkIds:
        return object->HasGameplayLinkComponent();
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
    case PseudoComponentKind::BoneAnimation: return "単発クリップまたはAnimator Controllerでボーンアニメーションを管理します。";
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

    object->EnsureParticleEmitterComponent();
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
        object->EnsureMeshEffectComponent();
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
        object->EnsurePathMoverComponent();
        DebugConsole::GetInstance()->AddLog("Path Move component is enabled by selecting a path in the Path Move section.");
        break;
    case PseudoComponentKind::LinkIds:
        object->EnsureGameplayLinkComponent();
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
        object->RemoveParticleEmitterComponent();
        break;
    case PseudoComponentKind::Lod:
        object->SetLodEnabled(false);
        object->ClearLodLevels();
        break;
    case PseudoComponentKind::MeshEffect:
        object->RemoveMeshEffectComponent();
        break;
    case PseudoComponentKind::BoneAnimation:
        object->animName_.clear();
        object->isAnimLoop_ = false;
        object->ClearAnimatorController();
        break;
    case PseudoComponentKind::PathMove:
        object->RemovePathMoverComponent();
        break;
    case PseudoComponentKind::LinkIds:
        object->RemoveGameplayLinkComponent();
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

