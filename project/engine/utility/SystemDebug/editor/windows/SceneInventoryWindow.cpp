#include "SceneInventoryWindow.h"

#include "BaseScene.h"
#include "CollisionConfig.h"
#include "DebugEditor.h"
#include "IconsFontAwesome5.h"
#include "Object3d.h"
#include "SceneManager.h"
#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <map>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace {
bool IsEditorOnlyObject(const Object3d& object) {
    const std::string& name = object.GetName();
    return object.IsEditorInternal() || name.rfind("__Editor_", 0) == 0 || object.GetClassName() == "EditorOnly";
}

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool ContainsInsensitive(const std::string& text, const std::string& query) {
    if (query.empty()) return true;
    return ToLowerAscii(text).find(ToLowerAscii(query)) != std::string::npos;
}

bool ContainsAny(const std::string& text, std::initializer_list<const char*> words) {
    const std::string lower = ToLowerAscii(text);
    for (const char* word : words) {
        if (lower.find(word) != std::string::npos) return true;
    }
    return false;
}

bool IsTerrainVisual(const Object3d& object) {
    const std::string identity = object.GetName() + " " + object.GetModelName();
    if (ContainsAny(identity, { "backdrop", "background", "decor", "tree", "pillar", "brazier", "sign", "marker" })) {
        return false;
    }
    return ContainsAny(identity, {
        "terrain", "ground", "island", "courtyard", "cliff", "orchard", "waterworks",
        "arena", "rampart", "keep", "bridge", "platform", "floor", "landing", "plaza",
        "foundation", "causeway", "path", "road", "terrace", "valley", "westland", "eastland"
    });
}

std::string GetTypeDisplayName(const std::string& type) {
    static const std::unordered_map<std::string, std::string> kNames = {
        { "AppearingFloor", "出現床" },
        { "BlinkBlock", "点滅ブロック" },
        { "BreakableBlock", "破壊可能壁・ブロック" },
        { "ChainCollapseFloor", "連鎖して落ちる床" },
        { "ChikuwaBlock", "ちくわブロック" },
        { "DashPanel", "ダッシュパネル" },
        { "EventReceiver", "イベント受信オブジェクト" },
        { "ArenaEncounter", "中ボス遭遇管理" },
        { "GameplayVolume", "ゲームプレイボリューム" },
        { "PrismBarrier", "プリズム障壁" },
        { "FallingSpike", "落下する棘" },
        { "FireCannon", "炎の砲台" },
        { "HazardRideFloor", "妨害付き輸送床" },
        { "HookAnchor", "フックアンカー" },
        { "IceFloor", "氷床" },
        { "LaserNode", "レーザーノード" },
        { "MagmaGeyser", "マグマ噴出口" },
        { "MagmaHazard", "マグマ地形" },
        { "MovingFloor", "移動床" },
        { "OneWayFloor", "一方通行床" },
        { "PhaseFlipFloor", "時間反転床" },
        { "RotatingFloor", "回転床" },
        { "SeesawFloor", "シーソー床" },
        { "SinkingFloor", "沈む床" },
        { "StageGate", "ステージゲート" },
        { "TimedSwitch", "時間スイッチ" },
        { "Trampoline", "ジャンプ床" },
        { "Coin", "コイン" },
        { "StarCoin", "スターコイン" },
        { "Slime", "ピンクスライム" },
        { "FireSlime", "炎スライム" },
        { "ThunderSlime", "雷スライム" },
        { "WindSlime", "風スライム" },
        { "PrismSlime", "クリスタルスライム" },
        { "MagmaSlime", "マグマスライム" },
        { "GiantSlime", "巨大スライム" },
        { "RingBurner", "リングバーナー" },
        { "Bomber", "ボムスライム" },
        { "Bomb", "ボム" },
        { "Mushroom", "キノコ" },
        { "Goblin", "ゴブリン" },
    };

    auto it = kNames.find(type);
    return it != kNames.end() ? it->second : type;
}

const char* GetColliderShapeName(ColliderType type) {
    switch (type) {
    case ColliderType::kNone: return "なし";
    case ColliderType::kSphere: return "Sphere";
    case ColliderType::kAABB: return "AABB";
    case ColliderType::kOBB: return "OBB";
    case ColliderType::kCylinder: return "Cylinder";
    case ColliderType::kRing: return "Ring";
    case ColliderType::kTerrain: return "Terrain";
    default: return "不明";
    }
}
}

void SceneInventoryWindow::Initialize(SceneManager* sceneManager, DebugEditor* editor) {
    sceneManager_ = sceneManager;
    editor_ = editor;
}

void SceneInventoryWindow::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text(ICON_FA_CHART_BAR " シーン配置集計");
    ImGui::TextDisabled("現在のシーンに何が何個配置されているかを用途別に確認できます。");
    ImGui::Separator();

    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) {
        ImGui::TextDisabled("現在のシーンがありません");
        return;
    }

    RebuildEntries();

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##SceneInventorySearch", "名前 / 種類 / モデル / Classで検索", searchBuffer_, sizeof(searchBuffer_));
    ImGui::Checkbox("実行時に非表示のObjectも含める", &includeHidden_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("出現待ちの床や落下する棘など、配置済みでも現在非表示のObjectを集計します。");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Editor補助Objectも含める", &includeEditorObjects_);

    if (ImGui::Button(ICON_FA_COPY " 集計をコピー")) {
        CopySummaryToClipboard();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("配置総数: %d", static_cast<int>(entries_.size()));

    DrawSummary();
    ImGui::Separator();

    if (ImGui::BeginTabBar("SceneInventoryTabs")) {
        if (ImGui::BeginTabItem("カテゴリ別")) {
            DrawGroupedList(GroupMode::Category);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("種類別")) {
            DrawGroupedList(GroupMode::Type);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("モデル別")) {
            DrawGroupedList(GroupMode::Model);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("当たり判定")) {
            DrawGroupedList(GroupMode::Collision);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
#endif
}

void SceneInventoryWindow::RebuildEntries() {
    entries_.clear();
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
    entries_.reserve(objects.size());

    for (const auto& objectPtr : objects) {
        Object3d* object = objectPtr.get();
        if (!object) continue;

        const bool editorOnly = IsEditorOnlyObject(*object);
        if (!includeEditorObjects_ && editorOnly) continue;
        if (!includeHidden_ && !object->GetIsVisible()) continue;

        Entry entry;
        entry.object = object;
        entry.editorOnly = editorOnly;
        entry.visible = object->GetIsVisible();
        entry.category = ClassifyCategory(*object);
        entry.type = ClassifyType(*object, entry.category);
        entry.model = object->GetModelName().empty() ? "（モデルなし）" : object->GetModelName();
        entry.collision = ClassifyCollision(*object);
        entries_.push_back(std::move(entry));
    }
}

void SceneInventoryWindow::DrawSummary() const {
#ifdef USE_IMGUI
    static const char* kCategories[] = {
        "通常地形", "背景・装飾", "当たり判定のみ", "ギミック",
        "敵", "アイテム・収集物", "プレイヤー・カメラ", "エフェクト", "Editor補助", "その他"
    };

    std::map<std::string, int> counts;
    for (const Entry& entry : entries_) {
        if (MatchesSearch(entry)) ++counts[entry.category];
    }

    ImGui::Spacing();
    if (ImGui::BeginTable("SceneInventorySummary", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
        for (const char* category : kCategories) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(category);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.35f, 0.9f, 1.0f, 1.0f), "%d", counts[category]);
        }
        ImGui::EndTable();
    }
#endif
}

void SceneInventoryWindow::DrawGroupedList(GroupMode mode) {
#ifdef USE_IMGUI
    const std::vector<Group> groups = BuildGroups(mode);
    if (groups.empty()) {
        ImGui::TextDisabled("条件に一致するObjectはありません");
        return;
    }

    ImGui::TextDisabled("グループ数: %d / Object数: %d", static_cast<int>(groups.size()),
        static_cast<int>(std::count_if(entries_.begin(), entries_.end(), [this](const Entry& entry) { return MatchesSearch(entry); })));

    ImGui::BeginChild("SceneInventoryGroups", ImVec2(0.0f, 0.0f), false);
    for (const Group& group : groups) {
        ImGui::PushID(group.label.c_str());
        const bool open = ImGui::TreeNodeEx("##Group", ImGuiTreeNodeFlags_SpanAvailWidth,
            "%s  (%d)%s", group.label.c_str(), static_cast<int>(group.entries.size()),
            group.hiddenCount > 0 ? "  [非表示あり]" : "");
        if (open) {
            DrawObjectRows(group);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
#endif
}

void SceneInventoryWindow::DrawObjectRows(const Group& group) {
#ifdef USE_IMGUI
    if (!ImGui::BeginTable("Objects", 5,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX)) {
        return;
    }

    ImGui::TableSetupColumn("確認", ImGuiTableColumnFlags_WidthFixed, 78.0f);
    ImGui::TableSetupColumn("Object名", ImGuiTableColumnFlags_WidthFixed, 210.0f);
    ImGui::TableSetupColumn("用途 / 種類", ImGuiTableColumnFlags_WidthFixed, 190.0f);
    ImGui::TableSetupColumn("モデル", ImGuiTableColumnFlags_WidthStretch, 260.0f);
    ImGui::TableSetupColumn("状態", ImGuiTableColumnFlags_WidthFixed, 115.0f);
    ImGui::TableHeadersRow();

    for (const Entry* entry : group.entries) {
        if (!entry || !entry->object) continue;

        ImGui::PushID(entry->object);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::SmallButton("フォーカス") && editor_) {
            editor_->FocusSceneObject(entry->object);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Inspectorを開かず、Objectの正面へSceneカメラを移動します");
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(entry->object->GetName().c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Class: %s\nLayer: %s\nTag: %s", entry->object->GetClassName().c_str(),
                entry->object->GetLayer().c_str(), entry->object->GetTag().empty() ? "（なし）" : entry->object->GetTag().c_str());
        }
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%s / %s", entry->category.c_str(), entry->type.c_str());
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(entry->model.c_str());
        ImGui::TableSetColumnIndex(4);
        if (!entry->visible) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "非表示");
        } else {
            ImGui::TextUnformatted("表示中");
        }
        ImGui::SameLine();
        ImGui::TextDisabled(entry->object->IsStatic() ? "固定" : "動的");
        ImGui::PopID();
    }

    ImGui::EndTable();
#endif
}

void SceneInventoryWindow::CopySummaryToClipboard() const {
#ifdef USE_IMGUI
    std::map<std::string, int> categoryCounts;
    std::map<std::string, int> typeCounts;
    for (const Entry& entry : entries_) {
        if (!MatchesSearch(entry)) continue;
        ++categoryCounts[entry.category];
        ++typeCounts[entry.category + " / " + entry.type];
    }

    std::ostringstream text;
    text << "カテゴリ\t個数\n";
    for (const auto& [label, count] : categoryCounts) text << label << '\t' << count << '\n';
    text << "\n種類\t個数\n";
    for (const auto& [label, count] : typeCounts) text << label << '\t' << count << '\n';
    ImGui::SetClipboardText(text.str().c_str());
#endif
}

std::vector<SceneInventoryWindow::Group> SceneInventoryWindow::BuildGroups(GroupMode mode) const {
    std::map<std::string, Group> grouped;

    for (const Entry& entry : entries_) {
        std::string label;
        switch (mode) {
        case GroupMode::Category:
            label = entry.category;
            break;
        case GroupMode::Type:
            label = entry.category + " / " + entry.type;
            break;
        case GroupMode::Model:
            label = entry.model;
            break;
        case GroupMode::Collision:
            label = entry.collision;
            break;
        }

        if (!MatchesSearch(entry, label)) continue;
        Group& group = grouped[label];
        group.label = label;
        group.entries.push_back(&entry);
        if (!entry.visible) ++group.hiddenCount;
    }

    std::vector<Group> result;
    result.reserve(grouped.size());
    for (auto& [label, group] : grouped) result.push_back(std::move(group));
    std::sort(result.begin(), result.end(), [](const Group& lhs, const Group& rhs) {
        if (lhs.entries.size() != rhs.entries.size()) return lhs.entries.size() > rhs.entries.size();
        return lhs.label < rhs.label;
    });
    return result;
}

bool SceneInventoryWindow::MatchesSearch(const Entry& entry, const std::string& groupLabel) const {
    const std::string query = searchBuffer_;
    if (query.empty()) return true;
    if (ContainsInsensitive(groupLabel, query) || ContainsInsensitive(entry.category, query) ||
        ContainsInsensitive(entry.type, query) || ContainsInsensitive(entry.model, query) ||
        ContainsInsensitive(entry.collision, query)) {
        return true;
    }
    if (!entry.object) return false;
    return ContainsInsensitive(entry.object->GetName(), query) ||
        ContainsInsensitive(entry.object->GetClassName(), query) ||
        ContainsInsensitive(entry.object->GetLayer(), query) ||
        ContainsInsensitive(entry.object->GetTag(), query);
}

std::string SceneInventoryWindow::ClassifyCategory(const Object3d& object) const {
    const std::string className = object.GetClassName();
    const std::string saveCategory = object.GetSaveCategory();
    const std::string gimmickType = object.GetGimmickType();
    const std::string itemType = object.GetItemType();
    const std::string name = object.GetName();

    if (IsEditorOnlyObject(object)) return "Editor補助";
    if (className == "InvisibleBox") return "当たり判定のみ";
    if (className == "Player" || saveCategory == "Player" || object.IsCameraObject()) return "プレイヤー・カメラ";
    if (saveCategory == "Enemy" || !object.GetEnemyType().empty()) return "敵";
    if (!itemType.empty() || saveCategory == "Item" || gimmickType == "Coin" ||
        ContainsAny(name, { "starcoin", "stagecoin", "collectible" })) {
        return "アイテム・収集物";
    }
    if (!gimmickType.empty() || className == "Gimmick") return "ギミック";
    if (ContainsAny(className, { "particle", "effect", "vfx" }) ||
        ContainsAny(saveCategory, { "particle", "effect", "vfx" })) {
        return "エフェクト";
    }
    if ((object.GetCollisionAttribute() & kGround) != 0 || object.GetColliderConfig().type == ColliderType::kTerrain ||
        IsTerrainVisual(object)) {
        return "通常地形";
    }
    if (saveCategory == "Object" && (object.IsStatic() || ContainsAny(object.GetModelName(), { "stages/", "terrain", "ground" }))) {
        return "背景・装飾";
    }
    return "その他";
}

std::string SceneInventoryWindow::ClassifyType(const Object3d& object, const std::string& category) const {
    if (category == "当たり判定のみ") return ClassifyCollision(object);
    if (category == "敵" && !object.GetEnemyType().empty()) return GetTypeDisplayName(object.GetEnemyType());
    if (category == "ギミック" && !object.GetGimmickType().empty()) return GetTypeDisplayName(object.GetGimmickType());
    if (category == "アイテム・収集物") {
        if (!object.GetItemType().empty()) return GetTypeDisplayName(object.GetItemType());
        if (ContainsAny(object.GetName(), { "starcoin", "stagecoin" })) return "スターコイン";
        if (!object.GetGimmickType().empty()) return GetTypeDisplayName(object.GetGimmickType());
        return "収集物";
    }
    if (category == "プレイヤー・カメラ") return object.IsCameraObject() ? "カメラ" : "プレイヤー";
    if (category == "通常地形") {
        if (object.GetColliderConfig().type == ColliderType::kTerrain) return "高さ付き地形";
        if (object.GetColliderConfig().type != ColliderType::kNone) return object.IsStatic() ? "固定地形（判定あり）" : "動的地形（判定あり）";
        return object.IsStatic() ? "固定地形（見た目）" : "動的地形（見た目）";
    }
    if (category == "背景・装飾") return object.IsStatic() ? "固定背景・装飾" : "動的背景・装飾";
    return object.GetClassName().empty() ? "未分類" : object.GetClassName();
}

std::string SceneInventoryWindow::ClassifyCollision(const Object3d& object) const {
    const ColliderConfig& config = object.GetColliderConfig();
    const uint32_t attribute = object.GetCollisionAttribute();
    const std::string& name = object.GetName();

    if (config.type == ColliderType::kNone) return "当たり判定なし";
    if ((attribute & kTrigger) != 0 || ContainsAny(name, { "trigger", "eventarea", "goalarea" })) {
        return std::string("トリガー / ") + GetColliderShapeName(config.type);
    }
    if (config.type == ColliderType::kTerrain) return "地形コライダー / Terrain";

    const float horizontalMin = (std::max)(0.001f, (std::min)(std::abs(config.size.x), std::abs(config.size.z)));
    const float horizontalMax = (std::max)(std::abs(config.size.x), std::abs(config.size.z));
    const float height = std::abs(config.size.y);
    const bool namedWall = ContainsAny(name, { "wall", "barrier", "boundary", "guard", "fence" });
    const bool namedFloor = ContainsAny(name, { "floor", "ground", "landing", "pad", "step", "bridge", "terrain" });

    if (object.GetClassName() == "InvisibleBox") {
        if (namedWall || (height > horizontalMin * 1.25f && horizontalMax > horizontalMin * 1.75f)) {
            return std::string("透明な壁 / ") + GetColliderShapeName(config.type);
        }
        if (namedFloor || height <= horizontalMin * 0.4f) {
            return std::string("透明な床 / ") + GetColliderShapeName(config.type);
        }
        return std::string("透明な判定ボリューム / ") + GetColliderShapeName(config.type);
    }

    std::string role = (attribute & kGround) != 0 ? "地形" : "通常";
    return role + " / " + GetColliderShapeName(config.type);
}
