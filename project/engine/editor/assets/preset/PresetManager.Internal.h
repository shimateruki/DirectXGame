#pragma once

// 担当：Prefabの識別子・JSON階層・継承差分を扱う内部関数。
// 関連：PresetManagerの分割.cpp、EditorPropertyRegistry（項目定義）
// 分割実装だけが使う補助関数の宣言です。状態を重複して持たせません。
#include "engine/editor/assets/preset/PresetManager.h"

#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "EditorPropertyRegistry.h"
#include "EditorTransactionManager.h"
#include "game/actor/enemy/spawn/EnemyFactory.h"
#include "game/system/rules/GameplayStatusManager.h"

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

// 古い汎用Paramの敵種別より、明示されたオブジェクト種別を優先します。
std::string ResolveEnemyType(const json& node);

std::uint64_t HashText(const std::string& text);

std::string MakeHexId(const std::string& prefix, std::uint64_t value);

// 同じ prefix と seed から再現可能なIDを作る。新しい配置個体のID生成とは区別する。
std::string MakeStableId(const std::string& prefix, const std::string& seed);

std::string MakeUniqueId(const std::string& prefix);

json* FindSourceNodeRecursive(json& node, const std::string& sourceObjectId);
const json* FindSourceNodeRecursive(const json& node, const std::string& sourceObjectId);

// 浮動小数の保存誤差を許容しつつ、配列の要素は順番どおりに比較する。
bool JsonValuesEqual(const json& lhs, const json& rhs);

// 再配置用の定義へシーン個体のGUIDを持ち込まないよう、子ノードまで除去する。
void RemoveSceneObjectGuids(json& node);

// 親子参照の循環や重複を避け、Scene固有GUIDを除いて再利用可能なツリーを作ります。
json BuildPresetNode(engine::Object3D* object, std::unordered_set<const engine::Object3D*>& visited);

json BuildPrefabNode(
    engine::Object3D* object,
    const std::string& assetId,
    std::unordered_set<const engine::Object3D*>& visited);

// Prefab階層内のノードへ、差分対応に使う元IDを補う。
void EnsurePrefabObjectIds(json& node, const std::string& assetId, const std::string& hierarchyPath);

// 旧Variantの不足フィールドを空の差分として補い、現在の読込処理へ渡します。
void EnsurePrefabVariantFields(json& asset);

// Prefab 内の元ノード位置。親 ID と同階層内の順番を保持して差分適用に使います。
// 関連：PresetManager（元ノードの検索・差分適用）
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

// 子を再帰的に数え、Prefabの展開規模を把握する。
int CountPrefabNodes(const json& node);

// 既知IDの子はOverrideから除き、Prefab本体から継承できる状態を維持する。
void RemovePrefabChildrenWithKnownIds(json& node, const std::unordered_set<std::string>& knownIds);

// Prefab内の識別子を階層から探し、最初に一致した子だけを削除する。
bool RemovePrefabNodeRecursive(json& root, const std::string& sourceObjectId);

// ノードを移動用に取り出して元の親から外し、識別子を保持したまま再配置できるようにする。
bool TakePrefabNodeRecursive(json& root, const std::string& sourceObjectId, json& removedNode);

// 指定順が子の総数を超える場合は末尾へ追加する。
bool InsertPrefabChild(json& root, const std::string& parentObjectId, std::size_t order, json child);

// nullは削除、オブジェクトは再帰的に統合し、それ以外は値を置き換える。
void ApplyJsonMergePatch(json& target, const json& patch);

// Prefab元との差分だけを保存し、元データの更新をインスタンスへ引き継げるようにする。
json BuildJsonMergePatch(const json& base, const json& edited, bool& changed);

// 子の差分を消した結果、空になった親オブジェクトも畳む。
bool EraseJsonPath(json& source, const std::string& path);

// 旧nodeOverrides内の存在フラグを新しいcomponentOverridesへ移し、差分の意味を保つ。
void MigrateLegacyComponentOverrides(json& asset);

// 旧形式のルート直書きを包み、以後の処理が単一のPrefab形式を読めるようにする。
json NormalizePrefabAsset(const std::string& prefabName, const json& value);

// 一回の Prefab 展開で共有するアセット ID とインスタンス ID です。
// 関連：PresetManager（Prefabの展開・Instance識別）
struct PrefabCreationContext {
    std::string prefabName;
    std::string assetId;
    std::string instanceId;
};

void CreateObjectFromPresetNode(
    const json& node,
    engine::Object3DCommon* common,
    engine::Object3D* parent,
    std::vector<std::unique_ptr<engine::Object3D>>& outObjects,
    const PrefabCreationContext* prefabContext = nullptr);

json* FindSourceNodeRecursive(json& node, const std::string& sourceObjectId);

const json* FindSourceNodeRecursive(const json& node, const std::string& sourceObjectId);

const char* GetSerializedPropertyKey(const std::string& propertyPath);

const char* GetCanonicalComponentPropertyKey(const std::string& propertyPath);

// ドット区切りのパスを段階的にたどり、途中でキーが欠けたら探索を止める。
const json* FindJsonPath(const json& source, const std::string& path);

// Prefab差分を保存するため、途中のObjectがない場合は生成して末端の値を返します。
json* EnsureJsonPath(json& source, const std::string& path);

json GetMissingSourceDefault(const std::string& propertyPath);

// Registryで決まる保存Pathを解決し、旧Assetで省略された値は型ごとの既定値に戻します。
json ReadSourceProperty(const json& sourceNode, const std::string& propertyPath);

// Propertyパスで指定された元Prefabの値を書き換える。
bool WriteSourceProperty(json& sourceNode, const std::string& propertyPath, const json& value);

bool IsPrefabComponentDescriptor(const EditorComponentDescriptor& component);

void RemoveComponentPropertyOverrides(
    json& asset,
    const std::string& sourceObjectId,
    const std::string& componentTypeId);

bool WriteComponentStateFromObject(
    json& sourceNode,
    engine::Object3D* object,
    const EditorComponentDescriptor& component,
    bool present);

bool ApplySerializedComponentState(
    engine::Object3D* object,
    const json& sourceNode,
    const EditorComponentDescriptor& component);

// 既知Componentの存在指定をJSONから除き、差分比較用に整える。
void RemoveKnownComponentPresenceMarkers(json& node);

bool IsInstancePlacementProperty(const engine::Object3D::PrefabInstanceInfo& info, const std::string& propertyPath);

void RegisterPrefabAssetTransaction(
    PresetManager* manager,
    const std::string& prefabName,
    const json& before,
    const json& after,
    const std::string& label);

}
