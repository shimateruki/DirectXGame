#include "EditorPropertyRegistry.h"
#include "EditorPropertyRegistry.BuiltIn.h"
#include "Object3d.h"

#include <algorithm>
#include <utility>

using nlohmann::json;
using EditorPropertyBuiltIn::CommonFlags;

void EditorPropertyRegistry::RegisterBuiltInRenderAndComponentProperties() {
    // 描画材質とコンポーネントの編集項目を登録し、対象型ごとの違いを明示します。
    const auto renderableOnly = [](const Object3d& object) {
        return !object.IsCameraObject() && object.GetMeshRenderer() != nullptr;
    };

    Register({
        "rendering.model", "モデル", "Rendering", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetModelName()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetModel(value.get<std::string>());
            return true;
        },
        renderableOnly,
    });
    Register({
        "rendering.blendMode", "合成方式", "Rendering", EditorPropertyType::Integer, CommonFlags(),
        [](const Object3d& object) { return json(static_cast<int>(object.GetBlendMode())); },
        [](Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            const int mode = std::clamp(value.get<int>(), 0, static_cast<int>(BlendMode::kCountOfBlendMode) - 1);
            object.SetBlendMode(static_cast<BlendMode>(mode));
            return true;
        },
        renderableOnly,
        { "なし", "通常", "加算", "減算", "乗算", "スクリーン" },
    });
    Register({
        "rendering.materialType", "マテリアルType", "Rendering", EditorPropertyType::Integer, CommonFlags(),
        [](const Object3d& object) { return json(object.GetMaterialType()); },
        [](Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            object.SetMaterialType(value.get<int>());
            return true;
        },
        renderableOnly,
    });
    Register({
        "rendering.texture", "基本Texture", "Rendering", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetTexturePath()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetTexture(value.get<std::string>());
            return true;
        },
        renderableOnly,
    });
    Register({
        "rendering.textureTiling", "Texture Tiling", "Rendering", EditorPropertyType::Vector2, CommonFlags(true),
        [](const Object3d& object) {
            const Vector2 value = object.GetTextureTiling();
            return json::array({ value.x, value.y });
        },
        [](Object3d& object, const json& value) {
            if (!value.is_array() || value.size() < 2) return false;
            object.SetTextureTiling({ value[0].get<float>(), value[1].get<float>() });
            return true;
        },
        renderableOnly,
    });
    Register({
        "rendering.autoTextureTiling", "自動Tiling", "Rendering", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.GetAutoTextureTiling()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.SetAutoTextureTiling(value.get<bool>());
            return true;
        },
        renderableOnly,
    });
    Register({
        "rendering.lighting", "Lighting", "Rendering", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.GetEnableLighting()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.SetEnableLighting(value.get<bool>());
            return true;
        },
        renderableOnly,
    });
    Register({
        "rendering.environmentMap", "環境Map", "Rendering", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.GetEnableEnvMap()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.SetEnableEnvMap(value.get<bool>());
            return true;
        },
        renderableOnly,
    });
    Register({
        "rendering.environmentIntensity", "環境反射強度", "Rendering", EditorPropertyType::Number, CommonFlags(true),
        [](const Object3d& object) { return json(object.GetEnvIntensity()); },
        [](Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.SetEnvIntensity((std::max)(value.get<float>(), 0.0f));
            return true;
        },
        renderableOnly,
    });
    Register({
        "rendering.normalMapEnabled", "Normal Map", "Rendering", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.GetEnableNormalMap()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.SetEnableNormalMap(value.get<bool>());
            return true;
        },
        renderableOnly,
    });
    Register({
        "rendering.normalMap", "Normal Texture", "Rendering", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetNormalMapPath()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetNormalMap(value.get<std::string>());
            return true;
        },
        renderableOnly,
    });
    Register({
        "rendering.ormMap", "ORM Texture", "Rendering", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetOrmMapPath()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetOrmMap(value.get<std::string>());
            return true;
        },
        renderableOnly,
    });

    Register({
        "component.collision.type", "Collider Type", "Component", EditorPropertyType::Integer, CommonFlags(),
        [](const Object3d& object) { return json(static_cast<int>(object.GetColliderConfig().type)); },
        [](Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            Object3d::ColliderConfig config = object.GetColliderConfig();
            config.type = static_cast<ColliderType>(std::clamp(
                value.get<int>(), static_cast<int>(ColliderType::kNone), static_cast<int>(ColliderType::kTerrain)));
            object.SetColliderConfig(config);
            return true;
        },
        {},
        { "なし", "Sphere", "AABB", "OBB", "Cylinder", "Ring", "Terrain" },
    });
    Register({
        "component.collision.center", "Collider中心", "Component", EditorPropertyType::Vector3, CommonFlags(true),
        [](const Object3d& object) {
            const Vector3& value = object.GetColliderConfig().center;
            return json::array({ value.x, value.y, value.z });
        },
        [](Object3d& object, const json& value) {
            if (!value.is_array() || value.size() < 3) return false;
            Object3d::ColliderConfig config = object.GetColliderConfig();
            config.center = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
            object.SetColliderConfig(config);
            return true;
        },
    });
    Register({
        "component.collision.size", "Colliderサイズ", "Component", EditorPropertyType::Vector3, CommonFlags(true),
        [](const Object3d& object) {
            const Vector3& value = object.GetColliderConfig().size;
            return json::array({ value.x, value.y, value.z });
        },
        [](Object3d& object, const json& value) {
            if (!value.is_array() || value.size() < 3) return false;
            Object3d::ColliderConfig config = object.GetColliderConfig();
            config.size = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
            object.SetColliderConfig(config);
            return true;
        },
    });
    Register({
        "component.collision.rotation", "Collider回転", "Component", EditorPropertyType::Vector3, CommonFlags(true),
        [](const Object3d& object) {
            const Vector3& value = object.GetColliderConfig().rotation;
            return json::array({ value.x, value.y, value.z });
        },
        [](Object3d& object, const json& value) {
            if (!value.is_array() || value.size() < 3) return false;
            Object3d::ColliderConfig config = object.GetColliderConfig();
            config.rotation = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
            object.SetColliderConfig(config);
            return true;
        },
    });
    Register({
        "component.collision.static", "Static Collision", "Component", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.IsStatic()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.SetStatic(value.get<bool>());
            return true;
        },
    });
    Register({
        "component.particle.cpu", "CPU Particle", "Component", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetParticleName()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetParticleName(value.get<std::string>());
            return true;
        },
    });
    Register({
        "component.particle.gpu", "GPU Particle", "Component", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetGPUParticleName()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetGPUParticleName(value.get<std::string>());
            return true;
        },
    });
    Register({
        "component.lod.enabled", "LOD有効", "Component", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.IsLodEnabled()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.SetLodEnabled(value.get<bool>());
            return true;
        },
        renderableOnly,
    });
    Register({
        "component.lod.levels", "LOD Levels", "Component", EditorPropertyType::String,
        EditorPropertyFlags::PrefabOverride,
        [](const Object3d& object) {
            json levels = json::array();
            for (const auto& level : object.GetLodLevels()) {
                levels.push_back({
                    { "level", level.level },
                    { "modelName", level.modelName },
                    { "distance", level.distance },
                });
            }
            return levels;
        },
        [](Object3d& object, const json& value) {
            if (!value.is_array()) return false;
            std::vector<Object3d::LodLevel> levels;
            for (const auto& source : value) {
                if (!source.is_object()) continue;
                Object3d::LodLevel level;
                level.level = source.value("level", 0);
                level.modelName = source.value("modelName", "");
                level.distance = source.value("distance", 0.0f);
                if (level.level > 0 && !level.modelName.empty()) {
                    levels.push_back(std::move(level));
                }
            }
            object.SetLodLevels(levels);
            return true;
        },
        renderableOnly,
    });
    Register({
        "component.meshEffect.primary", "Mesh Effect 1", "Component", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetMeshEffect1Name()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetMeshEffect1Name(value.get<std::string>());
            return true;
        },
    });
    Register({
        "component.meshEffect.secondary", "Mesh Effect 2", "Component", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetMeshEffect2Name()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetMeshEffect2Name(value.get<std::string>());
            return true;
        },
    });
    Register({
        "component.animation.name", "Animation", "Component", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.animName_); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.animName_ = value.get<std::string>();
            return true;
        },
    });
    Register({
        "component.animation.loop", "Animation Loop", "Component", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.isAnimLoop_); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.isAnimLoop_ = value.get<bool>();
            return true;
        },
    });
    Register({
        "component.animation.controller", "Animator Controller", "Component", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetAnimatorControllerPath()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            return object.SetAnimatorController(value.get<std::string>());
        },
    });
    Register({
        "component.path.name", "Ghost Path", "Component", EditorPropertyType::String, CommonFlags(),
        [](const Object3d& object) { return json(object.GetRecordPathName()); },
        [](Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.SetRecordPathName(value.get<std::string>());
            return true;
        },
    });
    Register({
        "component.path.loop", "Path Loop", "Component", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.IsRecordLoop()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.SetRecordLoop(value.get<bool>());
            return true;
        },
    });
    Register({
        "component.path.relative", "Path相対座標", "Component", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.IsRecordRelative()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.SetRecordRelative(value.get<bool>());
            return true;
        },
    });
    Register({
        "component.link.eventId", "Event ID", "Component", EditorPropertyType::Integer, CommonFlags(),
        [](const Object3d& object) { return json(object.GetEventID()); },
        [](Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            object.SetEventID(value.get<int>());
            return true;
        },
    });
    Register({
        "component.link.targetId", "Target ID", "Component", EditorPropertyType::Integer, CommonFlags(),
        [](const Object3d& object) { return json(object.GetTargetID()); },
        [](Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            object.SetTargetID(value.get<int>());
            return true;
        },
    });
    Register({
        "component.nav.enabled", "経路探索を有効化", "Component", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(object.GetNavAgentComponent() && object.GetNavAgentComponent()->IsEnabled()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.EnsureNavAgentComponent()->SetEnabled(value.get<bool>());
            return true;
        },
    });
    Register({
        "component.nav.cellSize", "セルサイズ", "Component", EditorPropertyType::Number, CommonFlags(),
        [](const Object3d& object) { return json(object.GetNavAgentComponent() ? object.GetNavAgentComponent()->GetCellSize() : 1.0f); },
        [](Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.EnsureNavAgentComponent()->SetCellSize(value.get<float>());
            return true;
        },
    });
    Register({
        "component.nav.agentRadius", "Agent半径", "Component", EditorPropertyType::Number, CommonFlags(),
        [](const Object3d& object) { return json(object.GetNavAgentComponent() ? object.GetNavAgentComponent()->GetAgentRadius() : 0.55f); },
        [](Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.EnsureNavAgentComponent()->SetAgentRadius(value.get<float>());
            return true;
        },
    });
    Register({
        "component.nav.agentHeight", "Agent高さ", "Component", EditorPropertyType::Number, CommonFlags(),
        [](const Object3d& object) { return json(object.GetNavAgentComponent() ? object.GetNavAgentComponent()->GetAgentHeight() : 1.5f); },
        [](Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.EnsureNavAgentComponent()->SetAgentHeight(value.get<float>());
            return true;
        },
    });
    Register({
        "component.nav.searchPadding", "探索余白", "Component", EditorPropertyType::Number, CommonFlags(),
        [](const Object3d& object) { return json(object.GetNavAgentComponent() ? object.GetNavAgentComponent()->GetSearchPadding() : 5.0f); },
        [](Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.EnsureNavAgentComponent()->SetSearchPadding(value.get<float>());
            return true;
        },
    });
    Register({
        "component.nav.repathInterval", "再探索間隔", "Component", EditorPropertyType::Number, CommonFlags(),
        [](const Object3d& object) { return json(object.GetNavAgentComponent() ? object.GetNavAgentComponent()->GetRepathInterval() : 0.35f); },
        [](Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.EnsureNavAgentComponent()->SetRepathInterval(value.get<float>());
            return true;
        },
    });
    Register({
        "component.nav.stoppingDistance", "停止距離", "Component", EditorPropertyType::Number, CommonFlags(),
        [](const Object3d& object) { return json(object.GetNavAgentComponent() ? object.GetNavAgentComponent()->GetStoppingDistance() : 0.25f); },
        [](Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.EnsureNavAgentComponent()->SetStoppingDistance(value.get<float>());
            return true;
        },
    });
    Register({
        "component.nav.obstacleMask", "障害物Mask", "Component", EditorPropertyType::Integer, CommonFlags(),
        [](const Object3d& object) { return json(object.GetNavAgentComponent() ? object.GetNavAgentComponent()->GetObstacleMask() : kAllSolid); },
        [](Object3d& object, const json& value) {
            if (!value.is_number_integer() && !value.is_number_unsigned()) return false;
            object.EnsureNavAgentComponent()->SetObstacleMask(value.get<std::uint32_t>());
            return true;
        },
    });
    Register({
        "component.nav.allowDiagonal", "斜め移動", "Component", EditorPropertyType::Bool, CommonFlags(),
        [](const Object3d& object) { return json(!object.GetNavAgentComponent() || object.GetNavAgentComponent()->AllowsDiagonal()); },
        [](Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.EnsureNavAgentComponent()->SetAllowDiagonal(value.get<bool>());
            return true;
        },
    });
}
