#pragma once
// 分割実装だけが使う補助関数の宣言です。状態を重複して持たせません。
#include "engine/utility/SystemDebug/editor/core/preset/PresetManager.h"

#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "EditorPropertyRegistry.h"
#include "EditorTransactionManager.h"
#include "game/actor/enemy/spawn/EnemyFactory.h"
#include "GameplayStatusManager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fs = std::filesystem;

namespace PresetManagerImplementation {

std::string ReadString(const json& source, const char* key, const std::string& fallback = "");

std::string ToLowerAscii(std::string value);

bool ContainsGroundKeyword(const std::string& value);

bool ShouldApplyGroundDefaults(engine::Object3d* object);

void ApplyGroundDefaultsIfNeeded(engine::Object3d* object);

std::string ResolveEnemyType(const json& node);

std::uint64_t HashText(const std::string& text);

std::string MakeHexId(const std::string& prefix, std::uint64_t value);

std::string MakeStableId(const std::string& prefix, const std::string& seed);

std::string MakeUniqueId(const std::string& prefix);

json* FindSourceNodeRecursive(json& node, const std::string& sourceObjectId);
const json* FindSourceNodeRecursive(const json& node, const std::string& sourceObjectId);

bool JsonValuesEqual(const json& lhs, const json& rhs);

void RemoveSceneObjectGuids(json& node);

json BuildPresetNode(engine::Object3d* object, std::unordered_set<const engine::Object3d*>& visited);

json BuildPrefabNode(
    engine::Object3d* object,
    const std::string& assetId,
    std::unordered_set<const engine::Object3d*>& visited);

void EnsurePrefabObjectIds(json& node, const std::string& assetId, const std::string& hierarchyPath);

void EnsurePrefabVariantFields(json& asset);

// Prefab 内の元ノード位置。親 ID と同階層内の順番を保持して差分適用に使います。
struct PrefabNodeLocation {
    const json* node = nullptr;
    std::string parentObjectId;
    std::size_t order = 0;
};

void CollectPrefabNodeLocations(
    const json& node,
    const std::string& parentObjectId,
    std::size_t order,
    std::unordered_map<std::string, PrefabNodeLocation>& locations);

int CountPrefabNodes(const json& node);

void RemovePrefabChildrenWithKnownIds(json& node, const std::unordered_set<std::string>& knownIds);

bool RemovePrefabNodeRecursive(json& root, const std::string& sourceObjectId);

bool TakePrefabNodeRecursive(json& root, const std::string& sourceObjectId, json& removedNode);

bool InsertPrefabChild(json& root, const std::string& parentObjectId, std::size_t order, json child);

void ApplyJsonMergePatch(json& target, const json& patch);

json BuildJsonMergePatch(const json& base, const json& edited, bool& changed);

bool EraseJsonPath(json& source, const std::string& path);

void MigrateLegacyComponentOverrides(json& asset);

json NormalizePrefabAsset(const std::string& prefabName, const json& value);

// 一回の Prefab 展開で共有するアセット ID とインスタンス ID です。
struct PrefabCreationContext {
    std::string prefabName;
    std::string assetId;
    std::string instanceId;
};

void CreateObjectFromPresetNode(
    const json& node,
    Object3dCommon* common,
    engine::Object3d* parent,
    std::vector<std::unique_ptr<engine::Object3d>>& outObjects,
    const PrefabCreationContext* prefabContext = nullptr);

json* FindSourceNodeRecursive(json& node, const std::string& sourceObjectId);

const json* FindSourceNodeRecursive(const json& node, const std::string& sourceObjectId);

const char* GetSerializedPropertyKey(const std::string& propertyPath);

const char* GetCanonicalComponentPropertyKey(const std::string& propertyPath);

const json* FindJsonPath(const json& source, const std::string& path);

json* EnsureJsonPath(json& source, const std::string& path);

json GetMissingSourceDefault(const std::string& propertyPath);

json ReadSourceProperty(const json& sourceNode, const std::string& propertyPath);

bool WriteSourceProperty(json& sourceNode, const std::string& propertyPath, const json& value);

bool IsPrefabComponentDescriptor(const EditorComponentDescriptor& component);

void RemoveComponentPropertyOverrides(
    json& asset,
    const std::string& sourceObjectId,
    const std::string& componentTypeId);

bool WriteComponentStateFromObject(
    json& sourceNode,
    engine::Object3d* object,
    const EditorComponentDescriptor& component,
    bool present);

bool ApplySerializedComponentState(
    engine::Object3d* object,
    const json& sourceNode,
    const EditorComponentDescriptor& component);

void RemoveKnownComponentPresenceMarkers(json& node);

bool IsInstancePlacementProperty(const engine::Object3d::PrefabInstanceInfo& info, const std::string& propertyPath);

void RegisterPrefabAssetTransaction(
    PresetManager* manager,
    const std::string& prefabName,
    const json& before,
    const json& after,
    const std::string& label);

}
