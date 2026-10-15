#include "BuiltInCreatePresetRegistry.h"

#include "EditorObjectFactoryRegistry.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "engine/utility/SystemDebug/editor/core/preset/PresetManager.h"
#include "json.hpp"

#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using json = nlohmann::json;

namespace {
// 自動更新される組み込み定義でも、ユーザーの表示名・用途・お気に入りを保ちます。
void PreservePresetCatalogFields(const json& current, json& replacement) {
    for (auto it = current.begin(); it != current.end(); ++it) {
        if (it.key().rfind("preset", 0) == 0) replacement[it.key()] = it.value();
    }
}

bool RegisterOrRefreshEnemyPreset(const std::string& presetName, std::unique_ptr<engine::Object3d> object, const std::string& displayName) {
    if (!object) {
        return false;
    }

    PresetManager* manager = PresetManager::GetInstance();
    if (manager->HasPreset(presetName)) {
        const json& current = manager->GetPreset(presetName);
        if (!current.value("builtinCreateTemplate", false)) {
            return false;
        }
    }

    // 組み込み定義だけを更新対象とし、ユーザーが同名で作った Preset は上書きしません。
    object->SetName(displayName);
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();

    json data = object->ExportToJson();
    data["name"] = displayName;
    data["builtinCreateTemplate"] = true;

    // 再生成しても表示用メタデータは保ち、内容が同じなら不要な保存を避けます。
    if (manager->HasPreset(presetName)) PreservePresetCatalogFields(manager->GetPreset(presetName), data);
    if (manager->HasPreset(presetName) && manager->GetPreset(presetName) == data) {
        return false;
    }

    manager->GetPreset(presetName) = data;
    return true;
}

bool RegisterPresetOnce(const std::string& presetName, std::unique_ptr<engine::Object3d> object, const std::string& displayName) {
    // 既存のユーザープリセットを保護し、未登録の内蔵テンプレートだけ追加する。
    if (!object || PresetManager::GetInstance()->HasPreset(presetName)) {
        return false;
    }

    object->SetName(displayName);
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();

    json data = object->ExportToJson();
    data["name"] = displayName;
    data["builtinCreateTemplate"] = true;
    PresetManager::GetInstance()->GetPreset(presetName) = data;
    return true;
}

bool RegisterOrRefreshPresetData(const std::string& presetName, json data) {
    // 内蔵テンプレートだけ更新し、カタログ側の表示情報は維持する。
    PresetManager* manager = PresetManager::GetInstance();
    if (manager->HasPreset(presetName)) {
        const json& current = manager->GetPreset(presetName);
        if (!current.value("builtinCreateTemplate", false)) {
            return false;
        }
        PreservePresetCatalogFields(current, data);
        if (current == data) {
            return false;
        }
    }

    manager->GetPreset(presetName) = std::move(data);
    return true;
}

enum class Stage1PresetVariant {
    Direct,
    FixedPlatform,
};

// ステージ配置から生成ライブラリーへ取り込む定義。経路・対象 ID を引き継ぐかも個別に指定する。
struct Stage1PresetDefinition {
    const char* presetName;
    const char* sourceObjectName;
    const char* displayName;
    const char* section;
    const char* description;
    int sectionOrder;
    int presetOrder;
    bool preserveRecorder = false;
    bool preserveTargetId = false;
    Stage1PresetVariant variant = Stage1PresetVariant::Direct;
};

std::unordered_map<std::string, json> LoadStage1PresetSources() {
    std::unordered_map<std::string, json> sources;
    std::ifstream file("Resources/json/3Dobject/stage1_object.json", std::ios::binary);
    if (!file.is_open()) {
        return sources;
    }

    try {
        json root;
        file >> root;
        if (!root.contains("objects") || !root["objects"].is_array()) {
            return sources;
        }
        // 配置名をキーに、後続の Preset 定義が参照する元オブジェクトを探せるようにします。
        for (const json& object : root["objects"]) {
            if (!object.is_object()) {
                continue;
            }
            const std::string name = object.value("name", "");
            if (!name.empty()) {
                sources.emplace(name, object);
            }
        }
    } catch (...) {
        sources.clear();
    }
    return sources;
}

// ステージ1の実配置からScene固有IDと位置を外し、再配置可能な内蔵Presetへ変換します。
// 経路とリンク先IDだけは定義側の指定に従って保持します。
json BuildStage1PresetData(const json& source, const Stage1PresetDefinition& definition) {
    json data = source;

    // Scene固有の識別子と配置値を外し、安全な新規配置テンプレートへ変換する。
    data.erase("guid");
    data.erase("prefabInstance");
    data.erase("prefabObjectId");
    data["name"] = definition.displayName;
    data["position"] = { 0.0f, 0.0f, 0.0f };
    data["translate"] = { 0.0f, 0.0f, 0.0f };
    data["rotation"] = { 0.0f, 0.0f, 0.0f };
    data["rotate"] = { 0.0f, 0.0f, 0.0f };
    data["quaternion"] = { 0.0f, 0.0f, 0.0f, 1.0f };
    data["parentGuid"] = "";
    data["parentName"] = "";
    data["isLocked"] = false;
    data["myEventID"] = -1;
    // 収集物などで ID を持ち越す場合を除き、新規配置が既存 Event と接続しないよう解除します。
    if (!definition.preserveTargetId) {
        data["targetID"] = -1;
        if (data.contains("components") && data["components"].is_object() &&
            data["components"].contains("GameplayLink") &&
            data["components"]["GameplayLink"].is_object()) {
            data["components"]["GameplayLink"]["targetId"] = -1;
        }
    }

    // 移動床だけは記録経路を再利用します。その他は元ステージの軌道を引き継ぎません。
    if (!definition.preserveRecorder) {
        data["recorder"] = {
            { "isRecordLoop", false },
            { "isRecordRelative", false },
            { "recordPathName", "" }
        };
    }

    // 元が移動床でも固定足場 Preset では振る舞いと Recorder を取り除きます。
    if (definition.variant == Stage1PresetVariant::FixedPlatform) {
        data["type"] = "Model";
        data["gimmickType"] = "";
        data["enemyType"] = "";
        data["itemType"] = "";
        data["isStatic"] = true;
        data.erase("param");
        data["recorder"] = {
            { "isRecordLoop", false },
            { "isRecordRelative", false },
            { "recordPathName", "" }
        };
    }

    data["builtinCreateTemplate"] = true;
    data["presetCollection"] = "Stage1";
    data["presetSection"] = definition.section;
    data["presetSectionOrder"] = definition.sectionOrder;
    data["presetOrder"] = definition.presetOrder;
    data["presetDescription"] = definition.description;
    data["presetSourceObject"] = definition.sourceObjectName;
    data["presetRecommended"] = true;
    return data;
}

// 元配置が存在する定義だけ登録・更新し、保存が必要な変更の有無を返します。
bool RegisterStage1LibraryPresets() {
    const std::unordered_map<std::string, json> sources = LoadStage1PresetSources();
    if (sources.empty()) {
        return false;
    }

    // カタログの表示順と元配置名をここで対応付けます。元配置がなければ該当 Preset は作りません。
    const std::vector<Stage1PresetDefinition> definitions = {
        {
            "Stage1/Terrain/FixedGardenPlatform",
            "Stage1_V4_MovingIntro_A",
            "庭園足場（固定）",
            "地形・足場",
            "標準サイズの見える足場。当たり判定込みで安全地帯や着地点に使えます。",
            100,
            100,
            false,
            false,
            Stage1PresetVariant::FixedPlatform
        },
        {
            "Stage1/Gimmick/MovingPlatformHorizontal",
            "Stage1_V4_MovingIntro_A",
            "移動床（横移動）",
            "移動・床ギミック",
            "ステージ1の横移動記録を設定済み。配置後にGhost Recorderで軌道を調整できます。",
            200,
            200,
            true
        },
        {
            "Stage1/Gimmick/MovingPlatformVertical",
            "Stage1_V4_MovingIntro_C",
            "昇降リフト（上下移動）",
            "移動・床ギミック",
            "上下移動用のリフトモデルと記録を設定済み。高低差ルート向けです。",
            200,
            210,
            true
        },
        {
            "Stage1/Gimmick/BlinkPlatform",
            "Stage1_V4_BlinkFloor_Intro_01",
            "点滅床",
            "移動・床ギミック",
            "一定周期でON/OFFする庭園床。複数枚を並べて周期ルートを作れます。",
            200,
            220
        },
        {
            "Stage1/Gimmick/LinkedCollapsePlatform",
            "Stage1_V4_LinkedFloor_Approach_01",
            "連動崩れ床",
            "移動・床ギミック",
            "自分IDと送信先IDを設定して順番に崩す床。連動ルート用です。",
            200,
            230
        },
        {
            "Stage1/Gimmick/DashPanel",
            "Stage1_V4_DashPanel_01",
            "ダッシュパネル",
            "移動・床ギミック",
            "進行方向が分かる専用モデルと加速設定を持つダッシュパネルです。",
            200,
            240
        },
        {
            "Stage1/Gimmick/GiantBreakGate",
            "Stage1_V4_GiantRushGate",
            "巨大スライム破壊ゲート",
            "攻略・進行",
            "巨大スライムの突進能力で壊す専用ゲート。能力ルートの出口向けです。",
            300,
            300
        },
        {
            "Stage1/Progress/Checkpoint",
            "Stage1_V4_Checkpoint_PreBoss",
            "チェックポイント",
            "攻略・進行",
            "復帰地点として使う発光チェックポイント。イベント設定を含みます。",
            300,
            310
        },
        {
            "Stage1/Progress/EntranceGate",
            "Stage1_EntranceGate",
            "ステージ入口ゲート",
            "攻略・進行",
            "ステージ開始演出で使用している王冠ゲートです。",
            300,
            320
        },
        {
            "Stage1/Progress/PrismBarrier",
            "Stage1_V4_PrismBarrier_East",
            "中ボス封鎖バリア",
            "攻略・進行",
            "クリスタルスライム戦で通路を封鎖する大型バリア。イベントIDは配置後に設定します。",
            300,
            330
        },
        {
            "Stage1/Collectible/Coin",
            "Stage1_V4_MovingGuide_01",
            "コイン（ルート誘導）",
            "収集物・誘導",
            "ステージ1標準サイズのコイン。進行方向やジャンプ軌道の誘導に使います。",
            400,
            400
        },
        {
            "Stage1/Collectible/StarCoin01",
            "Stage1_StarCoin_01",
            "スターコイン 1",
            "収集物・誘導",
            "ステージ内スターの1個目。取得番号1のリンク設定を保持しています。",
            400,
            410,
            false,
            true
        },
        {
            "Stage1/Collectible/StarCoin02",
            "Stage1_StarCoin_02",
            "スターコイン 2",
            "収集物・誘導",
            "ステージ内スターの2個目。取得番号2のリンク設定を保持しています。",
            400,
            420,
            false,
            true
        },
        {
            "Stage1/Collectible/StarCoin03",
            "Stage1_StarCoin_03",
            "スターコイン 3",
            "収集物・誘導",
            "ステージ内スターの3個目。取得番号3のリンク設定を保持しています。",
            400,
            430,
            false,
            true
        },
        {
            "Stage1/Decoration/SoftTree",
            "Stage1_Decor_V4_01",
            "庭園の樹木",
            "背景・装飾",
            "ルートの境界や視線誘導に使う、当たり判定なしの庭園樹木です。",
            500,
            500
        },
        {
            "Stage1/Decoration/RuinPillar",
            "Stage1_Decor_V4_02",
            "遺跡の柱",
            "背景・装飾",
            "高低差や曲がり角の目印に使う、当たり判定なしの遺跡柱です。",
            500,
            510
        },
        {
            "Stage1/Decoration/Brazier",
            "Stage1_EntryDecor_Brazier_1",
            "遺跡の篝火台",
            "背景・装飾",
            "入口や重要地点を強調する篝火台。炎エフェクトは別途配置します。",
            500,
            520
        },
    };

    // 選択した元データだけをテンプレート化し、変更の有無を上位の保存処理へ返します。
    bool changed = false;
    for (const Stage1PresetDefinition& definition : definitions) {
        const auto source = sources.find(definition.sourceObjectName);
        if (source == sources.end()) {
            continue;
        }
        changed |= RegisterOrRefreshPresetData(
            definition.presetName,
            BuildStage1PresetData(source->second, definition));
    }
    return changed;
}

std::unique_ptr<engine::Object3d> CreateSpriteCardPreset(Object3dCommon* common) {
    auto object = std::make_unique<engine::Object3d>();
    object->Initialize(common);
    object->SetClassName("Model");
    object->SetModel("Primitives/plane");
    object->SetName("SpriteCard_2_5D");
    object->SetScale({ 1.8f, 1.8f, 1.0f });
    object->SetTexture("Resources/sprite/common/white.png");
    object->SetBlendMode(BlendMode::kNormal);
    object->SetMaterialType(0);
    object->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    object->SetEnableLighting(false);
    object->SetEnableEnvMap(false);
    object->SetEmissive(1.35f);
    object->SetCollisionAttribute(0);
    object->SetCollisionMask(0);

    // 2.5D 板は描画専用なので、既定の Collider と衝突カテゴリを持たせません。
    ColliderConfig colliderConfig = object->GetColliderConfig();
    colliderConfig.type = ColliderType::kNone;
    object->SetColliderConfig(colliderConfig);

    return object;
}
}

void BuiltInCreatePresetRegistry::EnsureRegistered(Object3dCommon* common) {
    if (!common) {
        return;
    }

    bool changed = false;

    // Enemy は実体を再生成して組み込み Preset を更新します。
    const std::vector<std::pair<std::string, std::string>> enemies = {
        { "FireSlime", "ファイアスライム" },
        { "ThunderSlime", "サンダースライム" },
        { "WindSlime", "風スライム" },
        { "Slime", "ピンクスライム" },
        { "Bomb", "ボム" },
        { "Bomber", "ボムスライム" },
        { "GiantSlime", "巨大スライム" },
        { "PrismSlime", "プリズムスライム（中ボス）" },
        { "FalseKingSlime", "偽王スライム（ステージ3ボス）" },
        { "RingBurner", "リングバーナー" },
    };
    for (const auto& [type, label] : enemies) {
        changed |= RegisterOrRefreshEnemyPreset("Builtin/Enemy/" + type,
            EditorObjectFactoryRegistry::Create(EditorObjectFactoryRegistry::Kind::Enemy, type, common), label);
    }

    // Gimmick は初回だけ登録し、Editor 上で編集した内容を以後の起動で保持します。
    const std::vector<std::pair<std::string, std::string>> gimmicks = {
        { "FireCannon", "火炎砲台" },
        { "BreakableBlock", "破壊ブロック" },
        { "MovingFloor", "動く床" },
        { "HazardRideFloor", "妨害付き輸送床" },
        { "Trampoline", "ジャンプ床" },
        { "SinkingFloor", "沈む床" },
        { "SeesawFloor", "シーソー床" },
        { "DashPanel", "ダッシュパネル" },
        { "TimedSwitch", "時間スイッチ" },
        { "AppearingFloor", "出現床" },
        { "Switch", "汎用スイッチ" },
        { "EventReceiver", "イベント受信" },
        { "ArenaEncounter", "中ボス遭遇管理" },
        { "GameplayVolume", "ゲームプレイボリューム" },
        { "CopyMemoryStation", "コピー記憶台" },
        { "PrismBarrier", "プリズム障壁" },
        { "BossGate", "ボス闘技場・黒格子ゲート" },
        { "HookPullBlock", "フック可動ブロック" },
        { "LiquidLevel", "水位/マグマ上下" },
        { "MagmaHazard", "マグマダメージ床" },
        { "MagmaGeyser", "周期式マグマ噴出口" },
        { "ChainCollapseFloor", "連鎖崩れ床" },
        { "RotatingFloor", "回転床" },
        { "RotatingPillar", "回転柱" },
        { "PhaseFlipFloor", "時間反転床" },
        { "StageGate", "ステージゲート" },
    };
    for (const auto& [type, label] : gimmicks) {
        changed |= RegisterPresetOnce("Builtin/Gimmick/" + type,
            EditorObjectFactoryRegistry::Create(EditorObjectFactoryRegistry::Kind::Gimmick, type, common), label);
    }

    // 単体 Preset と Stage1 の配置由来 Preset を追加し、実変更があったときだけ書き出します。
    changed |= RegisterPresetOnce("Builtin/Item/Heal",
        EditorObjectFactoryRegistry::Create(EditorObjectFactoryRegistry::Kind::Item, "Heal", common), "体力回復");
    changed |= RegisterPresetOnce("Builtin/Utility/SpriteCard2_5D", CreateSpriteCardPreset(common), "2.5Dスプライト板");
    changed |= RegisterStage1LibraryPresets();

    if (changed) {
        PresetManager::GetInstance()->SaveAll();
    }
}
