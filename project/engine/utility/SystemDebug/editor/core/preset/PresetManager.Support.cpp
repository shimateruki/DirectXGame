#include "engine/utility/SystemDebug/editor/core/preset/PresetManager.Internal.h"

using namespace PresetManagerImplementation;

// 補助処理の定義を一か所に保持し、共有状態と呼び出し順を維持します。
namespace PresetManagerImplementation {
std::string ReadString(const json& source, const char* key, const std::string& fallback) {
    if (source.is_object() && source.contains(key) && source.at(key).is_string()) {
        return source.at(key).get<std::string>();
    }
    return fallback;
}

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

// モデル名が地形・床に使われる語を含むかを判定します。
bool ContainsGroundKeyword(const std::string& value) {
    return value.find("block") != std::string::npos ||
        value.find("floor") != std::string::npos ||
        value.find("ground") != std::string::npos ||
        value.find("platform") != std::string::npos ||
        value.find("terrain") != std::string::npos ||
        value.find("wall") != std::string::npos;
}

// 専用衝突設定を持つ役者を除き、地形用の既定衝突設定が必要か判定します。
bool ShouldApplyGroundDefaults(engine::Object3d* object) {
    // 地形プリセットの初期判定だけを補完し、ギミックなど専用判定を持つクラスは除く。
    if (!object) {
        return false;
    }

    const std::string className = ToLowerAscii(object->GetClassName());
    if (className == "enemy" || className == "player" || className == "gimmick" ||
        className == "item" || className == "gpuparticle" || className == "cinematiccamera" ||
        className == "spritecard" || className == "meshroot" || className == "meshpart") {
        return false;
    }

    const std::string modelName = ToLowerAscii(object->GetModelName());
    if (modelName.empty()) {
        return false;
    }
    if (modelName.rfind("characters/", 0) == 0 || modelName.rfind("item/", 0) == 0) {
        return false;
    }
    if (modelName == "primitives/plane" || modelName.find("portal") != std::string::npos) {
        return false;
    }

    return modelName.rfind("stages/", 0) == 0 ||
        modelName == "primitives/cube" || ContainsGroundKeyword(modelName);
}

// 未設定の地形Colliderと衝突属性だけを補い、明示設定は維持します。
void ApplyGroundDefaultsIfNeeded(engine::Object3d* object) {
    // 地形プリセットで未設定の衝突情報だけ補い、個別設定は上書きしない。
    if (!ShouldApplyGroundDefaults(object)) {
        return;
    }

    auto colliderConfig = object->GetColliderConfig();
    if (colliderConfig.type == ColliderType::kNone) {
        colliderConfig.type = ColliderType::kAABB;
        object->SetColliderConfig(colliderConfig);
    }
    if (object->GetCollisionAttribute() == 0) {
        object->SetCollisionAttribute(CollisionCategories::Get("ground"));
    }
    if (object->GetCollisionMask() == 0) {
        object->SetCollisionMask(0xFFFFFFFFu);
    }
}

std::string ResolveEnemyType(const json& node) {
    // 古い汎用Paramの敵種別より、明示されたオブジェクト種別を優先します。
    const std::string type = ReadString(node, "type");
    if (!type.empty() && type != "Enemy")
        return {};
    std::string enemyType = ReadString(node, "enemyType");
    if (enemyType.empty() && node.contains("param") && node["param"].is_object()) {
        enemyType = ReadString(node["param"], "enemyType");
    }
    return enemyType;
}

std::uint64_t HashText(const std::string& text) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char value : text) {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string MakeHexId(const std::string& prefix, std::uint64_t value) {
    std::ostringstream stream;
    stream << prefix << '-' << std::hex << std::setw(16) << std::setfill('0') << value;
    return stream.str();
}

std::string MakeStableId(const std::string& prefix, const std::string& seed) {
    return MakeHexId(prefix, HashText(seed));
}

std::string MakeUniqueId(const std::string& prefix) {
    static std::atomic<std::uint64_t> counter = 0;
    const auto ticks = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return MakeHexId(prefix, ticks ^ (++counter * 0x9E3779B97F4A7C15ull));
}

// Prefab差分用にJSONを比較します。数値には小さい保存誤差を許容します。
bool JsonValuesEqual(const json& lhs, const json& rhs) {
    // 浮動小数の保存誤差を許容しつつ、配列の要素は順番どおりに比較する。
    if (lhs.is_number() && rhs.is_number()) {
        return std::fabs(lhs.get<double>() - rhs.get<double>()) <= 0.00001;
    }
    if (lhs.is_array() && rhs.is_array()) {
        if (lhs.size() != rhs.size()) {
            return false;
        }
        for (std::size_t index = 0; index < lhs.size(); ++index) {
            if (!JsonValuesEqual(lhs[index], rhs[index])) {
                return false;
            }
        }
        return true;
    }
    return lhs == rhs;
}

// Assetへ移すノードと子孫から、シーン固有のGUIDを除きます。
void RemoveSceneObjectGuids(json& node) {
    if (!node.is_object()) return;
    node.erase("guid");
    if (!node.contains("children") || !node["children"].is_array()) return;
    for (json& child : node["children"]) {
        RemoveSceneObjectGuids(child);
    }
}

// 親子Objectを再帰的に保存用JSONへ変換し、循環参照を避けます。
json BuildPresetNode(engine::Object3d* object, std::unordered_set<const engine::Object3d*>& visited) {
    // 親子参照の循環や重複を避け、Scene固有GUIDを除いて再利用可能なツリーを作ります。
    if (!object || visited.count(object) != 0) {
        return json::object();
    }

    visited.insert(object);
    json node = object->ExportToJson();
    node.erase("guid");
    node.erase("prefabInstance");
    node.erase("prefabObjectId");
    json children = json::array();

    for (engine::Object3d* child : object->GetChildren()) {
        json childNode = BuildPresetNode(child, visited);
        if (!childNode.empty()) {
            children.push_back(std::move(childNode));
        }
    }

    if (!children.empty()) {
        node["children"] = std::move(children);
    }
    return node;
}

// Prefabの安定IDを残しながらObject階層をAsset用ノードへ変換します。
json BuildPrefabNode(
    engine::Object3d* object,
    const std::string& assetId,
    std::unordered_set<const engine::Object3d*>& visited) {
    if (!object || visited.count(object) != 0) {
        return json::object();
    }

    // 元Prefabから来たObjectは安定IDを引き継ぎ、追加したObjectだけ新IDを発行します。
    visited.insert(object);
    json node = object->ExportToJson();
    node.erase("guid");
    node.erase("prefabInstance");

    const auto& instance = object->GetPrefabInstanceInfo();
    if (instance.assetId == assetId && !instance.sourceObjectId.empty()) {
        node["prefabObjectId"] = instance.sourceObjectId;
    } else {
        node["prefabObjectId"] = MakeUniqueId("object");
    }

    json children = json::array();
    for (engine::Object3d* child : object->GetChildren()) {
        json childNode = BuildPrefabNode(child, assetId, visited);
        if (!childNode.empty()) {
            children.push_back(std::move(childNode));
        }
    }
    if (!children.empty()) {
        node["children"] = std::move(children);
    }
    return node;
}

// Assetと階層位置から、IDのないPrefabノードへ安定IDを補います。
void EnsurePrefabObjectIds(json& node, const std::string& assetId, const std::string& hierarchyPath) {
    if (!node.is_object()) {
        return;
    }
    // 旧AssetでIDが欠ける場合だけ、Asset IDと階層位置から再現可能なIDを補います。
    if (!node.contains("prefabObjectId") || !node["prefabObjectId"].is_string() ||
        node["prefabObjectId"].get<std::string>().empty()) {
        node["prefabObjectId"] = MakeStableId("object", assetId + ':' + hierarchyPath);
    }
    node.erase("prefabInstance");
    // Prefab AssetはScene上の実体GUIDを所有しません。
    node.erase("guid");

    if (!node.contains("children") || !node["children"].is_array()) {
        return;
    }
    for (std::size_t index = 0; index < node["children"].size(); ++index) {
        EnsurePrefabObjectIds(node["children"][index], assetId,
            hierarchyPath + '/' + std::to_string(index));
    }
}

// 旧Variantに不足する差分フィールドを空の配列・オブジェクトで補います。
void EnsurePrefabVariantFields(json& asset) {
    // 旧Variantの不足フィールドを空の差分として補い、現在の読込処理へ渡します。
    if (!asset.contains("removedObjectIds") || !asset["removedObjectIds"].is_array()) {
        asset["removedObjectIds"] = json::array();
    }
    if (!asset.contains("addedChildren") || !asset["addedChildren"].is_array()) {
        asset["addedChildren"] = json::array();
    }
    if (!asset.contains("reparentedObjects") || !asset["reparentedObjects"].is_array()) {
        asset["reparentedObjects"] = json::array();
    }
    if (!asset.contains("nodeOverrides") || !asset["nodeOverrides"].is_object()) {
        asset["nodeOverrides"] = json::object();
    }
    if (!asset.contains("componentOverrides") || !asset["componentOverrides"].is_object()) {
        asset["componentOverrides"] = json::object();
    }
}

// Prefab階層のObject ID、親ID、並び順を一覧へ採取します。
void CollectPrefabNodeLocations(
    const json& node,
    const std::string& parentObjectId,
    std::size_t order,
    std::unordered_map<std::string, PrefabNodeLocation>& locations) {
    if (!node.is_object()) {
        return;
    }

    const std::string objectId = node.value("prefabObjectId", "");
    if (!objectId.empty()) {
        locations[objectId] = { &node, parentObjectId, order };
    }

    if (!node.contains("children") || !node["children"].is_array()) {
        return;
    }
    for (std::size_t childIndex = 0; childIndex < node["children"].size(); ++childIndex) {
        CollectPrefabNodeLocations(node["children"][childIndex], objectId, childIndex, locations);
    }
}

// 現ノードと子孫を再帰的に数えます。
int CountPrefabNodes(const json& node) {
    // 子を再帰的に数え、Prefabの展開規模を把握する。
    if (!node.is_object()) {
        return 0;
    }
    int count = 1;
    if (node.contains("children") && node["children"].is_array()) {
        for (const auto& child : node["children"]) {
            count += CountPrefabNodes(child);
        }
    }
    return count;
}

// Prefab元から継承できる既知IDの子をOverride側から除きます。
void RemovePrefabChildrenWithKnownIds(json& node, const std::unordered_set<std::string>& knownIds) {
    // 既知IDの子はOverrideから除き、Prefab本体から継承できる状態を維持する。
    if (!node.is_object() || !node.contains("children") || !node["children"].is_array()) {
        return;
    }

    auto& children = node["children"];
    for (auto it = children.begin(); it != children.end();) {
        const std::string childId = it->is_object() ? it->value("prefabObjectId", "") : "";
        if (!childId.empty() && knownIds.find(childId) != knownIds.end()) {
            it = children.erase(it);
        } else {
            RemovePrefabChildrenWithKnownIds(*it, knownIds);
            ++it;
        }
    }
    if (children.empty()) {
        node.erase("children");
    }
}

// 指定IDの子孫ノードを階層から一つ削除し、削除できた場合にtrueを返します。
bool RemovePrefabNodeRecursive(json& root, const std::string& sourceObjectId) {
    // Prefab内の識別子を階層から探し、最初に一致した子だけを削除する。
    if (!root.is_object() || !root.contains("children") || !root["children"].is_array()) {
        return false;
    }

    auto& children = root["children"];
    for (auto childIt = children.begin(); childIt != children.end(); ++childIt) {
        if (childIt->is_object() && childIt->value("prefabObjectId", "") == sourceObjectId) {
            children.erase(childIt);
            return true;
        }
        if (RemovePrefabNodeRecursive(*childIt, sourceObjectId)) {
            return true;
        }
    }
    return false;
}

// 指定IDの子孫ノードを取り出して元の親から外し、移動先へ渡します。
bool TakePrefabNodeRecursive(json& root, const std::string& sourceObjectId, json& removedNode) {
    // ノードを移動用に取り出して元の親から外し、識別子を保持したまま再配置できるようにする。
    if (!root.is_object() || !root.contains("children") || !root["children"].is_array()) {
        return false;
    }

    auto& children = root["children"];
    for (auto childIt = children.begin(); childIt != children.end(); ++childIt) {
        if (childIt->is_object() && childIt->value("prefabObjectId", "") == sourceObjectId) {
            removedNode = std::move(*childIt);
            children.erase(childIt);
            return true;
        }
        if (TakePrefabNodeRecursive(*childIt, sourceObjectId, removedNode)) {
            return true;
        }
    }
    return false;
}

// 指定親の子一覧へ順序を指定して挿入します。親がなければfalseを返します。
bool InsertPrefabChild(json& root, const std::string& parentObjectId, std::size_t order, json child) {
    // 指定順が子の総数を超える場合は末尾へ追加する。
    json* parent = FindSourceNodeRecursive(root, parentObjectId);
    if (!parent) {
        return false;
    }
    if (!parent->contains("children") || !(*parent)["children"].is_array()) {
        (*parent)["children"] = json::array();
    }

    auto& children = (*parent)["children"];
    const std::size_t insertOrder = (std::min)(order, children.size());
    children.insert(children.begin() + static_cast<json::difference_type>(insertOrder), std::move(child));
    return true;
}

// nullを削除として扱い、Prefabの差分JSONを対象へ再帰的に適用します。
void ApplyJsonMergePatch(json& target, const json& patch) {
    // nullは削除、オブジェクトは再帰的に統合し、それ以外は値を置き換える。
    if (!patch.is_object()) {
        target = patch;
        return;
    }
    if (!target.is_object()) {
        target = json::object();
    }

    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (it.value().is_null()) {
            target.erase(it.key());
        } else {
            ApplyJsonMergePatch(target[it.key()], it.value());
        }
    }
}

// 元Assetと編集後の値から、変更された項目だけの差分JSONを作ります。
json BuildJsonMergePatch(const json& base, const json& edited, bool& changed) {
    // Prefab元との差分だけを保存し、元データの更新をインスタンスへ引き継げるようにする。
    if (JsonValuesEqual(base, edited)) {
        changed = false;
        return json::object();
    }
    if (!base.is_object() || !edited.is_object()) {
        changed = true;
        return edited;
    }

    json patch = json::object();
    // 元にあって編集後にないキーはnullを削除命令として記録する。
    for (auto baseIt = base.begin(); baseIt != base.end(); ++baseIt) {
        if (!edited.contains(baseIt.key())) {
            patch[baseIt.key()] = nullptr;
        }
    }
    for (auto editedIt = edited.begin(); editedIt != edited.end(); ++editedIt) {
        // オブジェクトの子要素だけ再帰的に比較し、値全体の複製を抑える。
        if (!base.contains(editedIt.key())) {
            patch[editedIt.key()] = editedIt.value();
            continue;
        }
        bool childChanged = false;
        json childPatch = BuildJsonMergePatch(base.at(editedIt.key()), editedIt.value(), childChanged);
        if (childChanged) {
            patch[editedIt.key()] = std::move(childPatch);
        }
    }
    changed = !patch.empty();
    return patch;
}

// ドット区切りのプロパティを削除し、空になった親ノードも畳みます。
bool EraseJsonPath(json& source, const std::string& path) {
    // 子の差分を消した結果、空になった親オブジェクトも畳む。
    const std::size_t separator = path.find('.');
    if (separator == std::string::npos) {
        return source.is_object() && source.erase(path) > 0;
    }
    const std::string head = path.substr(0, separator);
    if (!source.is_object() || !source.contains(head)) {
        return false;
    }
    const bool erased = EraseJsonPath(source[head], path.substr(separator + 1));
    if (erased && source.contains(head) && source[head].is_object() && source[head].empty()) {
        source.erase(head);
    }
    return erased;
}

// 旧形式のComponent存在フラグを現在のOverride形式へ移します。
void MigrateLegacyComponentOverrides(json& asset) {
    // 旧nodeOverrides内の存在フラグを新しいcomponentOverridesへ移し、差分の意味を保つ。
    EnsurePrefabVariantFields(asset);
    json& nodeOverrides = asset["nodeOverrides"];
    if (!nodeOverrides.is_object()) {
        return;
    }

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    for (auto nodeIt = nodeOverrides.begin(); nodeIt != nodeOverrides.end();) {
        json& patch = nodeIt.value();
        if (!patch.is_object() || !patch.contains("components") || !patch["components"].is_object()) {
            ++nodeIt;
            continue;
        }

        json& serializedComponents = patch["components"];
        // 取り外し可能なComponentだけを対象にし、プロパティ差分は元のノードに残す。
        for (const EditorComponentDescriptor& component : registry->GetComponents()) {
            if (!component.removable || !component.serializedPresent || !component.setSerializedPresent ||
                !serializedComponents.contains(component.typeId) ||
                !serializedComponents[component.typeId].is_object()) {
                continue;
            }
            json& componentPatch = serializedComponents[component.typeId];
            if (componentPatch.contains("_editorPresent") &&
                componentPatch["_editorPresent"].is_boolean()) {
                asset["componentOverrides"][nodeIt.key()][component.typeId] =
                    componentPatch["_editorPresent"];
                componentPatch.erase("_editorPresent");
            }
            if (componentPatch.empty()) {
                serializedComponents.erase(component.typeId);
            }
        }
        if (serializedComponents.empty()) {
            patch.erase("components");
        }
        if (patch.empty()) {
            nodeIt = nodeOverrides.erase(nodeIt);
        } else {
            ++nodeIt;
        }
    }
}

// 旧形式も現在のPrefab Asset構造へ包み直し、IDと差分フィールドを補います。
json NormalizePrefabAsset(const std::string& prefabName, const json& value) {
    // 旧形式のルート直書きを包み、以後の処理が単一のPrefab形式を読めるようにする。
    json asset;
    if (value.is_object() && value.contains("root") && value["root"].is_object()) {
        asset = value;
    } else {
        asset["root"] = value;
    }

    asset["version"] = 3;
    std::string assetId = asset.value("assetId", "");
    if (assetId.empty()) {
        assetId = MakeStableId("prefab", prefabName);
        asset["assetId"] = assetId;
    }
    if (!asset.contains("root") || !asset["root"].is_object()) {
        asset["root"] = json::object();
    }
    if (!asset.contains("baseAssetId") || !asset["baseAssetId"].is_string()) {
        asset["baseAssetId"] = "";
    }
    if (!asset.contains("propertyOverrides") || !asset["propertyOverrides"].is_object()) {
        asset["propertyOverrides"] = json::object();
    }
    EnsurePrefabVariantFields(asset);
    // 旧Component形式を変換してから、親子ノードの安定IDを補う。
    MigrateLegacyComponentOverrides(asset);
    EnsurePrefabObjectIds(asset["root"], assetId, "root");
    for (std::size_t index = 0; index < asset["addedChildren"].size(); ++index) {
        json& entry = asset["addedChildren"][index];
        if (entry.is_object() && entry.contains("node") && entry["node"].is_object()) {
            EnsurePrefabObjectIds(entry["node"], assetId, "added/" + std::to_string(index));
        }
    }
    return asset;
}

// Preset/Prefabノードから実体と子孫を生成し、所有権をoutObjectsへ移します。
void CreateObjectFromPresetNode(
    const json& node,
    Object3dCommon* common,
    engine::Object3d* parent,
    std::vector<std::unique_ptr<engine::Object3d>>& outObjects,
    const PrefabCreationContext* prefabContext) {
    if (!node.is_object() || !common) {
        return;
    }

    // EnemyはFactoryで派生型を生成し、それ以外は通常Objectとして作ります。
    const std::string enemyType = ResolveEnemyType(node);
    std::unique_ptr<engine::Object3d> object;
    if (!enemyType.empty()) {
        object = EnemyFactory::GetInstance()->CreateEnemy(enemyType, common);
    }
    if (!object) {
        object = std::make_unique<engine::Object3d>();
        object->Initialize(common);
    }
    object->ImportFromJson(node);
    // Preset/Prefabの生成物は、Asset内データとは別のScene実体です。
    object->RegeneratePersistentGuid();
    // JSON読込後に敵種別とStatusを再適用し、生成した実体の戦闘値を揃えます。
    if (!enemyType.empty()) {
        object->SetClassName("Enemy");
        object->SetEnemyType(enemyType);
        if (!object->param_.has_value()) {
            object->param_.emplace();
        }
        object->param_->enemyType = enemyType;
        auto* statusManager = GameplayStatusManager::GetInstance();
        statusManager->Initialize();
        statusManager->ApplyEnemyStatus(object.get(), true);
    }
    // 明示された当たり判定と衝突属性を優先し、旧テンプレートの省略値だけ補完します。
    if (!node.contains("collider") && !node.contains("collisionAttribute") && !node.contains("collisionMask"))
        ApplyGroundDefaultsIfNeeded(object.get());
    if (node.contains("name") && node["name"].is_string()) {
        object->SetName(node.value("presetDisplayName", node["name"].get<std::string>()));
    }

    // Prefab配置では元Asset内のObject IDとScene実体のInstance IDを分けて保持します。
    if (prefabContext) {
        engine::Object3d::PrefabInstanceInfo info;
        info.prefabName = prefabContext->prefabName;
        info.assetId = prefabContext->assetId;
        info.instanceId = prefabContext->instanceId;
        info.sourceObjectId = node.value("prefabObjectId", "");
        info.isRoot = parent == nullptr;
        object->SetPrefabInstanceInfo(info);
    }

    // 親子関係と行列を確定してから所有権を出力配列へ移し、子ノードを再帰生成します。
    engine::Object3d* raw = object.get();
    if (parent) {
        raw->SetParent(parent);
    }
    raw->UpdateLocalMatrix();
    raw->UpdateWorldMatrix();
    outObjects.push_back(std::move(object));

    if (!node.contains("children") || !node["children"].is_array()) {
        return;
    }
    for (const auto& childNode : node["children"]) {
        CreateObjectFromPresetNode(childNode, common, raw, outObjects, prefabContext);
    }
}

json* FindSourceNodeRecursive(json& node, const std::string& sourceObjectId) {
    // Prefab内の固定IDを子までたどり、編集できるノードを返す。
    if (!node.is_object()) {
        return nullptr;
    }
    if (node.value("prefabObjectId", "") == sourceObjectId) {
        return &node;
    }
    if (node.contains("children") && node["children"].is_array()) {
        for (auto& child : node["children"]) {
            if (json* result = FindSourceNodeRecursive(child, sourceObjectId)) {
                return result;
            }
        }
    }
    return nullptr;
}

const json* FindSourceNodeRecursive(const json& node, const std::string& sourceObjectId) {
    // 読み取り専用のPrefab階層でも同じ固定IDで該当ノードを探す。
    if (!node.is_object()) {
        return nullptr;
    }
    if (node.value("prefabObjectId", "") == sourceObjectId) {
        return &node;
    }
    if (node.contains("children") && node["children"].is_array()) {
        for (const auto& child : node["children"]) {
            if (const json* result = FindSourceNodeRecursive(child, sourceObjectId)) {
                return result;
            }
        }
    }
    return nullptr;
}

const char* GetSerializedPropertyKey(const std::string& propertyPath) {
    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    if (const EditorPropertyDescriptor* descriptor = registry->Find(propertyPath)) {
        return descriptor->serializedPath.empty() ? nullptr : descriptor->serializedPath.c_str();
    }

    // Registry導入前の外部Propertyに対する移行期間用Fallbackです。
    // カメラとGameplayの古い保存キーもここで読み書きできる状態を保ちます。
    if (propertyPath == "identity.name") return "name";
    if (propertyPath == "identity.tag") return "tag";
    if (propertyPath == "identity.layer") return "layer";
    if (propertyPath == "identity.saveCategory") return "saveCategory";
    if (propertyPath == "transform.position") return "translate";
    if (propertyPath == "transform.rotation") return "rotate";
    if (propertyPath == "transform.scale") return "scale";
    if (propertyPath == "rendering.visible") return "isVisible";
    if (propertyPath == "rendering.castShadow") return "castShadow";
    if (propertyPath == "rendering.color") return "color";
    if (propertyPath == "rendering.metallic") return "metallic";
    if (propertyPath == "rendering.roughness") return "roughness";
    if (propertyPath == "rendering.emissive") return "emissive";
    if (propertyPath == "editor.locked") return "isLocked";
    if (propertyPath == "collision.attribute") return "collisionAttribute";
    if (propertyPath == "collision.mask") return "collisionMask";
    if (propertyPath == "rendering.model") return "modelName";
    if (propertyPath == "rendering.blendMode") return "blendMode";
    if (propertyPath == "rendering.materialType") return "materialType";
    if (propertyPath == "rendering.texture") return "texturePath";
    if (propertyPath == "rendering.textureTiling") return "textureTiling";
    if (propertyPath == "rendering.autoTextureTiling") return "autoTextureTiling";
    if (propertyPath == "rendering.lighting") return "enableLighting";
    if (propertyPath == "rendering.environmentMap") return "enableEnvMap";
    if (propertyPath == "rendering.environmentIntensity") return "envIntensity";
    if (propertyPath == "rendering.normalMapEnabled") return "enableNormalMap";
    if (propertyPath == "rendering.normalMap") return "normalMapPath";
    if (propertyPath == "rendering.ormMap") return "ormMapPath";
    if (propertyPath == "component.collision.type") return "collider.type";
    if (propertyPath == "component.collision.center") return "collider.center";
    if (propertyPath == "component.collision.size") return "collider.size";
    if (propertyPath == "component.collision.rotation") return "collider.rotation";
    if (propertyPath == "component.collision.static") return "isStatic";
    if (propertyPath == "component.particle.cpu") return "particleName";
    if (propertyPath == "component.particle.gpu") return "gpuParticleName";
    if (propertyPath == "component.lod.enabled") return "lod.enabled";
    if (propertyPath == "component.lod.levels") return "lod.levels";
    if (propertyPath == "component.meshEffect.primary") return "meshEffect1";
    if (propertyPath == "component.meshEffect.secondary") return "meshEffect2";
    if (propertyPath == "component.animation.name") return "animation.animName";
    if (propertyPath == "component.animation.loop") return "animation.isAnimLoop";
    if (propertyPath == "component.animation.controller") return "animation.animatorController";
    if (propertyPath == "component.path.name") return "recorder.recordPathName";
    if (propertyPath == "component.path.loop") return "recorder.isRecordLoop";
    if (propertyPath == "component.path.relative") return "recorder.isRecordRelative";
    if (propertyPath == "component.link.eventId") return "myEventID";
    if (propertyPath == "component.link.targetId") return "targetID";
    if (propertyPath == "camera.enabled") return "camera.enabled";
    if (propertyPath == "camera.role") return "camera.role";
    if (propertyPath == "camera.fov") return "camera.fovY";
    if (propertyPath == "camera.nearClip") return "camera.nearClip";
    if (propertyPath == "camera.farClip") return "camera.farClip";
    if (propertyPath == "camera.eyeSource") return "camera.eyeSource";
    if (propertyPath == "camera.eyeObject") return "camera.eyeObjectName";
    if (propertyPath == "camera.eyeOffset") return "camera.eyeOffset";
    if (propertyPath == "camera.eyeFollowMode") return "camera.eyeFollowMode";
    if (propertyPath == "camera.eyeFollowResponse") return "camera.eyeFollowResponse";
    if (propertyPath == "camera.targetMode") return "camera.targetMode";
    if (propertyPath == "camera.targetObject") return "camera.targetObjectName";
    if (propertyPath == "camera.targetOffset") return "camera.targetOffset";
    if (propertyPath == "camera.fixedTarget") return "camera.fixedTarget";
    if (propertyPath == "camera.forwardDistance") return "camera.forwardDistance";
    if (propertyPath == "camera.targetFollowMode") return "camera.targetFollowMode";
    if (propertyPath == "camera.targetFollowResponse") return "camera.targetFollowResponse";
    if (propertyPath == "camera.blendIn") return "camera.blendInDuration";
    if (propertyPath == "camera.blendOut") return "camera.blendOutDuration";
    if (propertyPath == "camera.easing") return "camera.easing";
    if (propertyPath == "gameplay.healAmount") return "param.healAmount";
    if (propertyPath == "gameplay.interval") return "param.interval";
    if (propertyPath == "gameplay.maxCount") return "param.maxCount";
    if (propertyPath == "gameplay.detectionRange") return "param.detectionRange";
    if (propertyPath == "gameplay.shakeDuration") return "param.shakeDuration";
    if (propertyPath == "gameplay.fallDuration") return "param.fallDuration";
    if (propertyPath == "gameplay.colorType") return "param.colorType";
    if (propertyPath == "gameplay.switchMode") return "param.switchMode";
    if (propertyPath == "gameplay.actionMode") return "param.actionMode";
    if (propertyPath == "gameplay.targetScene") return "param.targetScene";
    if (propertyPath == "gameplay.moveAmount") return "param.moveAmount";
    if (propertyPath == "gameplay.moveSpeed") return "param.moveSpeed";
    if (propertyPath == "gameplay.startActive") return "param.startActive";
    if (propertyPath == "gameplay.returnOnOff") return "param.returnOnOff";
    return nullptr;
}

const char* GetCanonicalComponentPropertyKey(const std::string& propertyPath) {
    // 旧形式の編集プロパティ名を現行Componentの保存パスへ写す。
    if (propertyPath == "component.particle.cpu") return "components.ParticleEmitter.cpuParticle";
    if (propertyPath == "component.particle.gpu") return "components.ParticleEmitter.gpuParticle";
    if (propertyPath == "component.meshEffect.primary") return "components.MeshEffect.primary";
    if (propertyPath == "component.meshEffect.secondary") return "components.MeshEffect.secondary";
    if (propertyPath == "component.path.name") return "components.PathMover.path";
    if (propertyPath == "component.path.loop") return "components.PathMover.loop";
    if (propertyPath == "component.path.relative") return "components.PathMover.relative";
    if (propertyPath == "component.link.eventId") return "components.GameplayLink.eventId";
    if (propertyPath == "component.link.targetId") return "components.GameplayLink.targetId";
    return nullptr;
}

const json* FindJsonPath(const json& source, const std::string& path) {
    // ドット区切りのパスを段階的にたどり、途中でキーが欠けたら探索を止める。
    const json* current = &source;
    std::size_t begin = 0;
    while (begin < path.size()) {
        const std::size_t separator = path.find('.', begin);
        const std::string key = path.substr(begin, separator == std::string::npos ? std::string::npos : separator - begin);
        if (!current->is_object() || !current->contains(key)) {
            return nullptr;
        }
        current = &current->at(key);
        if (separator == std::string::npos) {
            return current;
        }
        begin = separator + 1;
    }
    return current;
}

json* EnsureJsonPath(json& source, const std::string& path) {
    // Prefab差分を保存するため、途中のObjectがない場合は生成して末端の値を返します。
    json* current = &source;
    std::size_t begin = 0;
    while (begin < path.size()) {
        const std::size_t separator = path.find('.', begin);
        const std::string key = path.substr(begin, separator == std::string::npos ? std::string::npos : separator - begin);
        if (separator == std::string::npos) {
            return &(*current)[key];
        }
        json& next = (*current)[key];
        if (!next.is_object()) {
            next = json::object();
        }
        current = &next;
        begin = separator + 1;
    }
    return nullptr;
}

// 元Assetで省略されたプロパティをRegistryまたは旧形式の既定値で補います。
json GetMissingSourceDefault(const std::string& propertyPath) {
    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    registry->InitializeBuiltInProperties();
    if (const EditorPropertyDescriptor* descriptor = registry->Find(propertyPath)) {
        return descriptor->defaultValue;
    }

    // Registry未登録の旧Propertyだけを従来値で扱います。
    if (propertyPath == "component.collision.static" ||
        propertyPath == "component.lod.enabled" ||
        propertyPath == "component.path.loop" ||
        propertyPath == "component.path.relative") {
        return false;
    }
    if (propertyPath == "component.lod.levels") {
        return json::array();
    }
    if (propertyPath == "component.particle.cpu" ||
        propertyPath == "component.particle.gpu" ||
        propertyPath == "component.meshEffect.primary" ||
        propertyPath == "component.meshEffect.secondary" ||
        propertyPath == "component.animation.name" ||
        propertyPath == "component.animation.controller" ||
        propertyPath == "component.path.name") {
        return "";
    }
    return json();
}

json ReadSourceProperty(const json& sourceNode, const std::string& propertyPath) {
    // Registryで決まる保存Pathを解決し、旧Assetで省略された値は型ごとの既定値に戻します。
    const char* key = GetSerializedPropertyKey(propertyPath);
    if (!key) {
        return json();
    }
    const json* value = FindJsonPath(sourceNode, key);
    return value ? *value : GetMissingSourceDefault(propertyPath);
}

// Registryの保存Pathへ値を書き、必要なら正規のComponentキーにも同期します。
bool WriteSourceProperty(json& sourceNode, const std::string& propertyPath, const json& value) {
    const char* key = GetSerializedPropertyKey(propertyPath);
    if (!key) {
        return false;
    }
    json* target = EnsureJsonPath(sourceNode, key);
    if (!target) {
        return false;
    }
    *target = value;
    if (const char* canonicalKey = GetCanonicalComponentPropertyKey(propertyPath)) {
        if (json* canonicalTarget = EnsureJsonPath(sourceNode, canonicalKey)) {
            *canonicalTarget = value;
        }
    }
    if (propertyPath == "transform.rotation") {
        // Euler角のOverrideを適用した時は、古いQuaternionが優先されないようにします。
        sourceNode.erase("quaternion");
    }
    return true;
}

// Componentの有無をPrefabへ保存・復元できる定義か判定します。
bool IsPrefabComponentDescriptor(const EditorComponentDescriptor& component) {
    return component.removable && component.serializedPresent && component.setSerializedPresent;
}

// Componentを取り除く際に、そのComponentに属するプロパティ差分を消します。
void RemoveComponentPropertyOverrides(
    json& asset,
    const std::string& sourceObjectId,
    const std::string& componentTypeId) {
    if (!asset.contains("propertyOverrides") || !asset["propertyOverrides"].is_object() ||
        !asset["propertyOverrides"].contains(sourceObjectId) ||
        !asset["propertyOverrides"][sourceObjectId].is_object()) {
        return;
    }

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    json& objectOverrides = asset["propertyOverrides"][sourceObjectId];
    for (const EditorPropertyDescriptor* property : registry->GetPropertiesForComponent(componentTypeId)) {
        if (property) {
            objectOverrides.erase(property->path);
        }
    }
    if (objectOverrides.empty()) {
        asset["propertyOverrides"].erase(sourceObjectId);
    }
}

// Object上のComponentの有無と保存対象プロパティをPrefabノードへ反映します。
bool WriteComponentStateFromObject(
    json& sourceNode,
    engine::Object3d* object,
    const EditorComponentDescriptor& component,
    bool present) {
    if (!object || !component.setSerializedPresent) {
        return false;
    }

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    if (present) {
        // 追加したComponentのPrefab上書き対象だけを保存し、実行時専用値を混ぜない。
        for (const EditorPropertyDescriptor* property : registry->GetPropertiesForComponent(component.typeId)) {
            if (!property || !HasEditorPropertyFlag(property->flags, EditorPropertyFlags::PrefabOverride) ||
                property->serializedPath.empty()) {
                continue;
            }
            const json value = registry->GetValue(object, property->path);
            if (!value.is_null()) {
                WriteSourceProperty(sourceNode, property->path, value);
            }
        }
    }
    component.setSerializedPresent(sourceNode, present);
    return true;
}

// 保存されたComponentの有無を先に合わせ、その後でプロパティ値を戻します。
bool ApplySerializedComponentState(
    engine::Object3d* object,
    const json& sourceNode,
    const EditorComponentDescriptor& component) {
    if (!object || !component.serializedPresent) {
        return false;
    }

    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    const bool desiredPresent = component.serializedPresent(sourceNode);
    const bool currentPresent = registry->IsComponentPresent(object, component.typeId);
    // 値の適用より先にComponentの有無を揃え、存在しないComponentへの書き込みを避ける。
    if (desiredPresent != currentPresent) {
        const bool changed = desiredPresent
            ? registry->AddComponent(object, component.typeId)
            : registry->RemoveComponent(object, component.typeId);
        if (!changed) {
            return false;
        }
    }

    if (desiredPresent) {
        for (const EditorPropertyDescriptor* property : registry->GetPropertiesForComponent(component.typeId)) {
            if (!property || property->serializedPath.empty()) {
                continue;
            }
            const json value = ReadSourceProperty(sourceNode, property->path);
            if (!value.is_null()) {
                registry->SetValue(object, property->path, value);
            }
        }
    }
    return true;
}

// Registryで扱うComponentの旧存在マーカーを保存ノードから取り除きます。
void RemoveKnownComponentPresenceMarkers(json& node) {
    EditorPropertyRegistry* registry = EditorPropertyRegistry::GetInstance();
    for (const EditorComponentDescriptor& component : registry->GetComponents()) {
        if (IsPrefabComponentDescriptor(component)) {
            EraseJsonPath(node, "components." + component.typeId + "._editorPresent");
            EraseJsonPath(node, "components." + component.typeId + ".version");
        }
    }
}

// インスタンスの配置位置・回転としてPrefab元から独立させるプロパティか判定します。
bool IsInstancePlacementProperty(const engine::Object3d::PrefabInstanceInfo& info, const std::string& propertyPath) {
    return info.isRoot &&
        (propertyPath == "transform.position" || propertyPath == "transform.rotation");
}

// Prefab Assetの変更前後をEditor共通のUndo/Redo履歴へ登録します。
void RegisterPrefabAssetTransaction(
    PresetManager* manager,
    const std::string& prefabName,
    const json& before,
    const json& after,
    const std::string& label) {
    if (!manager || before == after) {
        return;
    }

    EditorTransaction transaction;
    transaction.label = label;
    transaction.undo = [manager, prefabName, before]() {
        manager->GetPrefab(prefabName) = before;
        manager->RefreshPrefabInheritance();
        manager->SavePrefabs();
    };
    transaction.redo = [manager, prefabName, after]() {
        manager->GetPrefab(prefabName) = after;
        manager->RefreshPrefabInheritance();
        manager->SavePrefabs();
    };
    EditorTransactionManager::GetInstance()->Register(std::move(transaction));
}
}
