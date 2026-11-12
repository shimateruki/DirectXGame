#pragma once
#ifdef USE_IMGUI
#include "DebugEditor.h"
#include "SceneManager.h"
#include "WinApp.h"
#include "game/system/progress/StageManager.h"
#include "game/system/scene/GimmickFactory.h"
#include "game/system/scene/SceneEntityParameterCodec.h"
#include "game/actor/gimmick/movement/GimmickSinkingFloor.h"
#include "engine/editor/properties/ObjectPropertyDefinition.h"
#include <fstream>
#include <stdexcept>
#include <cmath>
#include <set>
#include "GimmickFactoryCompatibilityFixture.h"
#include "EditorPropertyJsonBinding.h"

// 専用環境変数で起動したプロセスだけで検証します。Scene JSONそのものは書き換えません。
namespace GimmickPropertyRuntimeProbe {
inline bool Enabled() { return GetEnvironmentVariableA("CG2_GIMMICK_PROPERTY_TEST", nullptr, 0) > 0; }
inline void Require(bool success, const char* message) { if (!success) throw std::runtime_error(message); }
inline bool Near(float left, float right) { return std::abs(left - right) < 0.0001f; }

// 別クラスでもエンジンへ型名分岐を追加せず、同じAPIだけで公開できることを確認します。
class Fixture : public engine::Object3D {
public:
    void Bind() {
        engine::ObjectPropertyDefinition<Fixture> properties(*this, "ProbeFixture", "検証用");
        properties.Expose("有効", ENGINE_PROPERTY(enabled_));
        properties.Expose("個数", ENGINE_PROPERTY(count_), 0, 5);
        properties.Expose("名前", ENGINE_PROPERTY(text_));
    }
private:
    bool enabled_ = true;
    int count_ = 2;
    std::string text_ = "保存互換";
};

inline void CheckSettings(engine::Object3DCommon* common, DebugEditor* editor, BaseScene* scene, std::ofstream& log) {
    using json = nlohmann::json;
    auto* registry = EditorPropertyRegistry::GetInstance();
    const std::string prefix = "class.Game_SinkingFloor.";
    auto floor = GimmickFactory::GetInstance()->CreateGimmick("SinkingFloor", common);
    Require(dynamic_cast<GimmickSinkingFloor*>(floor.get()) != nullptr, "factory dynamic type");
    registry->InitializeBuiltInProperties();
    const auto propertyCount = registry->GetProperties().size();
    auto another = GimmickFactory::GetInstance()->CreateGimmick("SinkingFloor", common);
    Require(propertyCount == registry->GetProperties().size(), "duplicate property registration");
    Require(registry->FindComponent("Game_SinkingFloor")->displayName == "沈む床", "Japanese label");
    Require(registry->GetPropertiesForComponent("Game_SinkingFloor").size() == 6, "property count");
    Require(!registry->IsApplicable(another.get(), "class.Game_ProbeFixture.count_"), "foreign property applicability");

    // 既存LevelLoaderと同じ順序で、旧componentsとparamを復元します。
    const json legacy = {{"type", "Gimmick"}, {"gimmickType", "SinkingFloor"},
                         {"param", {{"actionMode", 1}, {"speed", 3.75f}, {"moveAmount", 4.25f},
                                    {"interval", 1.2f}, {"shakeDuration", 0.7f}}}};
    floor->DeserializeFeatureComponents(legacy);
    SceneEntityParameterCodec::Apply(*floor, legacy["param"]);
    Require(floor->GetParameters()->actionMode == 1 && Near(floor->GetParameters()->moveAmount, 4.25f) &&
            Near(floor->GetParameters()->speed, 3.75f) && Near(floor->GetParameters()->shakeDuration, 0.7f), "legacy param migration");
    Require(Near(registry->GetValue(floor.get(), prefix + "sinkDepth_").get<float>(), 2.0f), "legacy shallow default");
    Require(!registry->Find(prefix + "sinkDepth_")->visible(*floor) &&
            registry->Find(prefix + "moveAmount")->visible(*floor), "deep mode visibility");
    registry->SetValue(floor.get(), prefix + "actionMode", 0);
    Require(registry->Find(prefix + "sinkDepth_")->visible(*floor) &&
            !registry->Find(prefix + "moveAmount")->visible(*floor) && Near(floor->GetParameters()->moveAmount, 4.25f), "mode switch lost hidden setting");
    registry->SetValue(floor.get(), prefix + "actionMode", 1);
    Require(registry->SetValue(floor.get(), prefix + "sinkDepth_", 3.5f), "set private depth");
    Require(registry->SetValue(floor.get(), prefix + "sinkSpeed_", 7.25f), "set private response");
    Require(!registry->SetValue(floor.get(), prefix + "sinkDepth_", "invalid"), "reject malformed number");
    Require(registry->SetValue(floor.get(), prefix + "speed", 4.5f) && Near(floor->GetParameters()->speed, 4.5f), "legacy single source");
    auto saved = floor->ExportToJson();
    saved["components"]["Game_SinkingFloor"]["future"] = {1, 2, 3};
    floor->ImportFromJson(json::parse(saved.dump()));
    Require(floor->ExportToJson()["components"]["Game_SinkingFloor"]["future"] == json::array({1, 2, 3}), "unknown field loss");
    Require(Near(registry->GetValue(floor.get(), prefix + "sinkSpeed_").get<float>(), 7.25f), "private round trip");
    Require(floor->ExportToJson()["param"]["shakeDuration"] == legacy["param"]["shakeDuration"], "unused legacy field loss");

    // 非表示の浅いモード設定も複製し、複製先の編集で元個体を変えないことを確かめます。
    auto clone = floor->Clone();
    Require(clone && clone->GetPersistentGuid() != floor->GetPersistentGuid(), "clone identity");
    Require(Near(registry->GetValue(clone.get(), prefix + "sinkDepth_").get<float>(), 3.5f), "clone private setting");
    registry->SetValue(clone.get(), prefix + "sinkDepth_", 8.0f);
    Require(Near(registry->GetValue(floor.get(), prefix + "sinkDepth_").get<float>(), 3.5f), "clone shares binding");
    auto replay = floor->CaptureReplayState();
    registry->SetValue(floor.get(), prefix + "sinkDepth_", 10.0f);
    floor->RestoreReplayState(replay);
    Require(Near(registry->GetValue(floor.get(), prefix + "sinkDepth_").get<float>(), 3.5f), "replay private setting");
    floor.reset();
    Require(Near(registry->GetValue(clone.get(), prefix + "sinkDepth_").get<float>(), 8.0f), "binding captured destroyed instance");

    auto* raw = scene->AddObjectImmediate(std::move(clone));
    editor->SetSelectedObject(raw);
    const auto before = editor->CaptureObjectState(raw);
    registry->SetValue(raw, prefix + "sinkDepth_", 9.0f);
    editor->RegisterObjectEdited(raw, before, "検証・深さ変更");
    editor->PerformUndo();
    Require(Near(registry->GetValue(raw, prefix + "sinkDepth_").get<float>(), 8.0f), "property undo");
    editor->PerformRedo();
    Require(Near(registry->GetValue(raw, prefix + "sinkDepth_").get<float>(), 9.0f), "property redo");
    SceneSerializer serializer;
    serializer.Initialize(editor);
    const auto targets = serializer.BuildSingleObjectSaveTargets(raw, "stage1");
    bool found = false;
    for (const auto& target : targets) for (const auto& row : target.data.value("objects", json::array()))
        if (row.value("guid", std::string{}) == raw->GetPersistentGuid())
            found = Near(row["components"]["Game_SinkingFloor"]["sinkDepth_"].get<float>(), 9.0f);
    Require(found, "SceneSerializer omitted class settings");
    log << "OK legacy JSON, separate instances, malformed input, hidden values, unknown fields, clone, Replay, property Undo/Redo, SceneSerializer" << std::endl;

    Fixture fixture;
    fixture.Initialize(common);
    fixture.Bind();
    Require(registry->SetValue(&fixture, "class.Game_ProbeFixture.count_", 4), "second class registration");
    Require(!registry->SetValue(&fixture, "class.Game_ProbeFixture.count_", 4.5), "integer accepted float");
    Require(registry->SetValue(&fixture, "class.Game_ProbeFixture.text_", "日本語"), "UTF8 string");
    json payload = json::object();
    registry->WriteClassProperties(fixture, payload);
    Require(payload["Game_ProbeFixture"]["text_"] == "日本語", "UTF8 persistence");

    // 未知IDを読み込んだ仮ギミックも、旧経路で削除・再作成できることを確認します。
    auto legacyUnknown = GimmickFactory::GetInstance()->CreateGimmick("__LegacyUnknown", common);
    const auto legacyGuid = legacyUnknown->EnsurePersistentGuid();
    editor->AddEditorObject(std::move(legacyUnknown), "検証・旧未知ID");
    editor->PerformUndo();
    editor->PerformRedo();
    bool restoredUnknown = false;
    for (const auto& object : scene->GetObjects())
        if (object->GetPersistentGuid() == legacyGuid && object->GetGimmickType() == "__LegacyUnknown") restoredUnknown = true;
    Require(restoredUnknown, "legacy unknown placeholder redo");
    log << "OK second class, UTF8, legacy unknown placeholder restoration" << std::endl;
}


// 全生成IDを旧Factoryの設定と比較します。旧データは専用worktreeのコピーを読みます。
inline void CheckAllGimmicks(engine::Object3DCommon* common, DebugEditor* editor, BaseScene* scene, std::ofstream& log) {
    using json = nlohmann::json;
    auto* factory = GimmickFactory::GetInstance();
    auto* registry = EditorPropertyRegistry::GetInstance();
    const auto types = factory->GetRegisteredTypes();
    Require(types.size() == 31, "registered type count including Default and aliases");
    Require(EditorObjectFactoryRegistry::GetTypes(EditorObjectFactoryRegistry::Kind::Gimmick).size() == types.size(), "editor type list drift");
    std::set<std::string> seen;
    std::size_t fieldCount = 0;
    for (const auto& type : types) {
        Require(seen.insert(type.id).second, "duplicate type id");
        auto object = factory->CreateGimmick(type.id, common);
        auto legacy = LegacyGimmickCreation::Create(type.id, common);
        auto currentJson = object->ExportToJson();
        auto oldJson = legacy->ExportToJson();
        currentJson.erase("guid"); oldJson.erase("guid");
        if (currentJson != oldJson) {
            log << "FACTORY DIFF " << type.id << ": " << json::diff(oldJson, currentJson).dump() << std::endl;
            Require(false, "factory defaults changed");
        }
        Require(type.matches && type.matches(*object) && typeid(*object) == typeid(*legacy), "factory runtime class");
        Require(factory->GetCreationType(*object) == type.id, "factory alias identity");
        const auto count = registry->GetProperties().size();
        auto second = factory->CreateGimmick(type.id, common);
        Require(count == registry->GetProperties().size(), "class registered properties twice");
        const auto defaults = object->ExportToJson();
        const auto defaultParams = SceneEntityParameterCodec::Serialize(*object);
        auto presetWithValues = defaults;
        presetWithValues["param"]["speed"] = 77.0f;
        EditorPropertyJsonBinding::Restore(*object, defaults, presetWithValues);
        Require(object->GetParameters() && Near(object->GetParameters()->speed, 77.0f), "preset context failed to load values");
        EditorPropertyJsonBinding::Restore(*object, defaults, json{{"type", "Gimmick"}, {"gimmickType", type.id}});
        Require(SceneEntityParameterCodec::Serialize(*object) == defaultParams, "preset context carried absent parameters from previous preset");
        const auto beforeRead = object->ExportToJson();
        for (const auto* component : registry->GetComponentsForObject(object.get())) {
            if (component->typeId.rfind("Game_", 0) != 0) continue;
            for (const auto* property : registry->GetPropertiesForComponent(component->typeId)) {
                Require(registry->IsApplicable(object.get(), property->path), "class field not applicable");
                registry->GetValue(object.get(), property->path);
                if (property->visible) property->visible(*object);
                if (property->enabled) property->enabled(*object);
                if (property->maximum) property->maximum(*object);
                if (property->help) property->help(*object);
                if (property->displayValue) property->displayValue(*object);
                ++fieldCount;
            }
        }
        Require(beforeRead == object->ExportToJson(), "property inspection mutated configuration");
        for (const auto* component : registry->GetComponentsForObject(object.get())) {
            if (component->typeId.rfind("Game_", 0) != 0) continue;
            for (const auto* property : registry->GetPropertiesForComponent(component->typeId)) {
                const auto savedBeforeEdit = object->ExportToJson();
                const auto current = registry->GetValue(object.get(), property->path);
                const json invalid = current.is_string() ? json(17) : json("invalid");
                Require(!registry->SetValue(object.get(), property->path, invalid), "class property accepted wrong JSON type");
                json edited;
                if (!property->enumValues.empty()) edited = property->enumValues.front();
                else if (!property->enumLabels.empty()) edited = 0;
                else if (current.is_boolean()) edited = !current.get<bool>();
                else if (current.is_number_integer()) edited = static_cast<int>(property->ui.minValue);
                else if (current.is_number()) edited = property->ui.minValue + 0.25f;
                else edited = "__registration_probe";
                auto presetData = savedBeforeEdit;
                presetData["param"]["futureAuthoringSetting"] = "keep";
                presetData["children"] = json::array({{{"name", "keep-child"}}});
                presetData["components"]["FutureComponent"]["payload"] = 913;
                // 同じ値の場合もあるため、Applyの返値ではなく実値と保存結果を確認します。
                EditorPropertyJsonBinding::Apply(*object, *property, presetData, edited);
                Require(presetData["param"]["futureAuthoringSetting"] == "keep" &&
                    presetData["children"][0]["name"] == "keep-child" &&
                    presetData["components"]["FutureComponent"]["payload"] == 913, "preset adapter lost unknown data");
                const auto expected = registry->GetValue(object.get(), property->path);
                second->ImportFromJson(presetData);
                if (registry->GetValue(second.get(), property->path) != expected) {
                    log << "FIELD DIFF " << type.id << " " << property->path << std::endl;
                    Require(false, "edited property round trip");
                }
                object->ImportFromJson(savedBeforeEdit);
            }
        }
        // 読込・複製・Replayで非表示項目も含めて旧paramをそのまま保持します。
        if (object->GetParameters()) {
            object->SetParameter(&engine::EntityParameter::hp, 83.25f);
            object->SetParameter(&engine::EntityParameter::volumePayload, "__unused_legacy_payload");
        }
        auto saved = object->ExportToJson();
        second->ImportFromJson(saved);
        if (SceneEntityParameterCodec::Serialize(*object) != SceneEntityParameterCodec::Serialize(*second))
            log << "PARAM DIFF " << type.id << ": " << json::diff(SceneEntityParameterCodec::Serialize(*object), SceneEntityParameterCodec::Serialize(*second)).dump() << std::endl;
        Require(SceneEntityParameterCodec::Serialize(*object) == SceneEntityParameterCodec::Serialize(*second), "param round trip");
        auto clone = object->Clone();
        Require(clone && typeid(*clone) == typeid(*object), "clone runtime class");
        Require(clone->GetGimmickType() == type.id && SceneEntityParameterCodec::Serialize(*clone) == SceneEntityParameterCodec::Serialize(*object), "clone alias or params");
        const auto params = SceneEntityParameterCodec::Serialize(*object);
        const auto replay = object->CaptureReplayState();
        if (object->GetParameters()) object->SetParameter(&engine::EntityParameter::speed, object->GetParameters()->speed + (12.5f));
        object->RestoreReplayState(replay);
        Require(params == SceneEntityParameterCodec::Serialize(*object), "replay params");
        log << "OK factory, read-only inspection, roundtrip, clone, replay: " << type.id << std::endl;
    }
    // 旧シーンの各種類から一例ずつ取り、現行Codecで往復します。
    std::set<std::string> legacySeen;
    for (const char* file : {"stage1_object.json", "stage2_object.json", "stage3_object.json", "tutorial_object.json", "stageSelect_object.json"}) {
        std::ifstream stream(std::string("Resources/json/3Dobject/") + file);
        if (!stream) continue;
        const auto source = json::parse(stream);
        for (const auto& record : source.value("objects", json::array())) {
            if (record.value("type", "") != "Gimmick") continue;
            const std::string id = record.value("gimmickType", "");
            if (!factory->IsKnownType(id) || !legacySeen.insert(id).second) continue;
            auto original = LegacyGimmickCreation::Create(id, common);
            auto migrated = factory->CreateGimmick(id, common);
            original->ImportFromJson(record);
            migrated->ImportFromJson(record);
            Require(SceneEntityParameterCodec::Serialize(*original) == SceneEntityParameterCodec::Serialize(*migrated), "legacy stage parameters changed");
            const auto expected = SceneEntityParameterCodec::Serialize(*migrated);
            const auto exported = migrated->ExportToJson();
            auto restored = factory->CreateGimmick(id, common);
            restored->ImportFromJson(exported);
            Require(SceneEntityParameterCodec::Serialize(*restored) == expected, "legacy stage round trip");
        }
    }
    // 旧4枠を表示するだけでは消さず、選択を変更した場合だけ単体へ移行します。
    auto copy = factory->CreateGimmick("CopyMemoryStation", common);
    copy->SetParameter(&engine::EntityParameter::copyMemoryTypeA, {}); copy->SetParameter(&engine::EntityParameter::copyMemoryTypeB, "WindSlime");
    copy->SetParameter(&engine::EntityParameter::copyMemoryTypeC, "Bomber"); copy->SetParameter(&engine::EntityParameter::copyMemoryTypeD, "Slime");
    const auto copyBefore = SceneEntityParameterCodec::Serialize(*copy);
    Require(registry->GetValue(copy.get(), "class.Game_CopyMemoryStation.copyMemoryTypeA") == "" &&
        registry->Find("class.Game_CopyMemoryStation.copyMemoryTypeA")->displayValue(*copy) == "WindSlime", "legacy copy display and canonical storage");
    Require(copyBefore == SceneEntityParameterCodec::Serialize(*copy), "legacy copy changed on read");
    auto otherCopy = factory->CreateGimmick("CopyMemoryStation", common);
    otherCopy->SetParameter(&engine::EntityParameter::copyMemoryTypeA, {}); otherCopy->SetParameter(&engine::EntityParameter::copyMemoryTypeB, "ThunderSlime");
    Require(registry->HasMixedValue({copy.get(), otherCopy.get()}, "class.Game_CopyMemoryStation.copyMemoryTypeA"), "legacy copy mixed display");
    Require(registry->SetValue(copy.get(), "class.Game_CopyMemoryStation.copyMemoryTypeA", "FireSlime"), "copy selection");
    Require(copy->GetParameters()->copyMemoryTypeB.empty() && copy->GetParameters()->copyMemoryTypeC.empty() && copy->GetParameters()->copyMemoryTypeD.empty(), "copy selection legacy cleanup");
    auto phase = factory->CreateGimmick("PhaseFlipFloor", common);
    phase->SetParameter(&engine::EntityParameter::maxCount, 3); phase->SetParameter(&engine::EntityParameter::colorType, 2);
    Require(registry->GetValue(phase.get(), "class.Game_PhaseFlipFloor.colorType") == 2 &&
        registry->Find("class.Game_PhaseFlipFloor.colorType")->ui.integerDisplayOffset == 1, "one-based floor display and canonical storage");
    registry->SetValue(phase.get(), "class.Game_PhaseFlipFloor.maxCount", 2);
    Require(phase->GetParameters()->maxCount == 2 && phase->GetParameters()->colorType == 1, "phase count edit clamp");
    auto water = factory->CreateGimmick("WaterGeyser", common);
    Require(!registry->Find("class.Game_MagmaGeyser.actionMode")->visible(*water) &&
        registry->Find("class.Game_MagmaGeyser.detectionRange_water")->visible(*water), "water alias UI");
    auto volume = factory->CreateGimmick("GameplayVolume", common);
    const auto* returns = registry->Find("class.Game_GameplayVolume.returnOnOff");
    volume->SetParameter(&engine::EntityParameter::volumeMode, 1);
    Require(returns->enabled && !returns->enabled(*volume), "volume unsupported return enabled");
    volume->SetParameter(&engine::EntityParameter::volumeMode, 4);
    Require(returns->enabled(*volume), "volume BGM return disabled");
    volume->SetParameter(&engine::EntityParameter::volumeTriggerOnExit, true);
    Require(!returns->enabled(*volume), "volume exit conflict enabled");
    // 通常材質の噴出口でも、泡の調整値を旧waterParamへ保存して復元します。
    auto magma = factory->CreateGimmick("MagmaGeyser", common);
    Require(magma->GetMaterialType() == 0, "magma render material changed");
    const std::string boiling = "class.Game_MagmaGeyser.boiling_effectScaleX";
    Require(registry->SetValue(magma.get(), boiling, 1.65f), "boiling edit");
    auto magmaSave = magma->ExportToJson();
    Require(magmaSave.contains("waterParam") && Near(magmaSave["waterParam"]["effectScaleX"].get<float>(), 1.65f), "boiling save at standard material");
    auto restoredMagma = factory->CreateGimmick("MagmaGeyser", common);
    restoredMagma->ImportFromJson(magmaSave);
    Require(Near(registry->GetValue(restoredMagma.get(), boiling).get<float>(), 1.65f), "boiling import");
    auto magmaClone = magma->Clone();
    Require(Near(registry->GetValue(magmaClone.get(), boiling).get<float>(), 1.65f), "boiling clone");
    const auto magmaBefore = editor->CaptureObjectState(magma.get());
    auto* magmaRaw = scene->AddObjectImmediate(std::move(magma));
    editor->SetSelectedObject(magmaRaw);
    registry->SetValue(magmaRaw, boiling, 0.8f);
    editor->RegisterObjectEdited(magmaRaw, magmaBefore, "検証・泡の調整");
    editor->PerformUndo();
    Require(Near(registry->GetValue(magmaRaw, boiling).get<float>(), 1.65f), "boiling undo");
    editor->PerformRedo();
    Require(Near(registry->GetValue(magmaRaw, boiling).get<float>(), 0.8f), "boiling redo");
    SceneSerializer serializer;
    serializer.Initialize(editor);
    const auto targets = serializer.BuildSingleObjectSaveTargets(magmaRaw, "stage1.json");
    bool materialSaved = false;
    for (const auto& target : targets) {
        if (!target.data.contains("objects")) continue;
        for (const auto& record : target.data["objects"]) {
            if (record.value("guid", "") == magmaRaw->GetPersistentGuid())
                materialSaved = record.contains("waterParam") && Near(record["waterParam"]["effectScaleX"].get<float>(), 0.8f);
        }
    }
    Require(materialSaved, "scene serializer omitted boiling setting");
    log << "OK standard-material boiling import, clone, undo/redo and scene save candidate" << std::endl;
    const auto* destinations = registry->Find("class.Game_StageGate.targetScene");
    Require(destinations->enumLabels.size() == 5 && destinations->enumValues.size() == 5, "gate scene choice mismatch");
    // 全ての型を実クラスへ交換し、Undo/Redoが元の型と調整値へ戻ることを確かめます。
    auto start = factory->CreateGimmick("MovingFloor", common);
    start->SetTranslate({10000, 3, 10000});
    auto* raw = scene->AddObjectImmediate(std::move(start));
    const auto guid = raw->EnsurePersistentGuid();
    const auto find = [&]() -> engine::Object3D* {
        for (const auto& object : scene->GetObjects()) if (object->GetPersistentGuid() == guid) return object.get();
        return nullptr;
    };
    editor->SetSelectedObject(raw);
    for (const auto& type : types) {
        if (type.id == "Default") continue;
        const auto beforeType = typeid(*raw).name();
        const auto beforeParams = SceneEntityParameterCodec::Serialize(*raw);
        Require(editor->ChangeObjectType(raw, EditorObjectFactoryRegistry::Kind::Gimmick, type.id), "registered type change failed");
        raw = find();
        Require(raw && type.matches(*raw), "type change left wrong runtime class");
        editor->PerformUndo(); raw = find();
        Require(raw && std::string(typeid(*raw).name()) == beforeType && SceneEntityParameterCodec::Serialize(*raw) == beforeParams, "all type undo");
        editor->PerformRedo(); raw = find();
        Require(raw && type.matches(*raw) && raw->GetGimmickType() == type.id, "all type redo");
    }
    log << "OK preset JSON adapter preserves unknown parameters, components and children" << std::endl;
    log << "OK all-type property reads=" << fieldCount << ", legacy stage types=" << legacySeen.size()
        << ", copy legacy slots, phase numbering, alias visibility, volume guards, scene choices and all-type Undo/Redo" << std::endl;
}

inline std::string CheckTypeChange(engine::Object3DCommon* common, DebugEditor* editor, BaseScene* scene) {
    using Registry = EditorObjectFactoryRegistry;
    auto object = std::make_unique<engine::Object3D>();
    object->Initialize(common);
    object->SetModel("Stages/block");
    object->SetClassName("Gimmick");
    object->SetGimmickType("Default");
    object->SetName("__PropertyProbe");
    object->SetTranslate({10000, 5, 10000});
    auto* raw = scene->AddObjectImmediate(std::move(object));
    auto parent = std::make_unique<engine::Object3D>();
    parent->Initialize(common);
    auto* parentRaw = scene->AddObjectImmediate(std::move(parent));
    raw->SetParent(parentRaw);
    auto child = std::make_unique<engine::Object3D>();
    child->Initialize(common);
    child->SetTranslate({1, 2, 3});
    child->SetParent(raw);
    auto* childRaw = scene->AddObjectImmediate(std::move(child));
    const auto guid = raw->EnsurePersistentGuid();
    const auto count = scene->GetObjects().size();
    auto find = [&]() -> engine::Object3D* {
        for (const auto& candidate : scene->GetObjects()) if (candidate->GetPersistentGuid() == guid) return candidate.get();
        return nullptr;
    };
    editor->SetSelectedObject(raw);
    Require(!editor->ChangeObjectType(raw, Registry::Kind::Gimmick, "__UnknownType"), "unknown type replaced object");
    Require(editor->ChangeObjectType(raw, Registry::Kind::Gimmick, "SinkingFloor"), "type replacement");
    raw = find();
    Require(dynamic_cast<GimmickSinkingFloor*>(raw) && raw->GetParent() == parentRaw &&
            childRaw->GetParent() == raw && Near(childRaw->GetTranslate().y, 2) &&
            Near(raw->GetTranslate().y, 5) && raw->GetName() == "__PropertyProbe" && scene->GetObjects().size() == count,
            "replacement identity/hierarchy/transform");
    editor->PerformUndo();
    raw = find();
    Require(raw && typeid(*raw) == typeid(engine::Object3D) && childRaw->GetParent() == raw, "type undo");
    editor->PerformRedo();
    raw = find();
    Require(dynamic_cast<GimmickSinkingFloor*>(raw) && childRaw->GetParent() == raw, "type redo");
    Require(editor->ChangeObjectType(raw, Registry::Kind::Gimmick, "MovingFloor"), "switch away from registered class");
    raw = find();
    Require(raw && !dynamic_cast<GimmickSinkingFloor*>(raw), "old dynamic type remained");
    // 同じ種類名のまま実体だけを直す場合は、旧JSONの調整値を失わないようにします。
    raw->SetGimmickType("SinkingFloor");
    raw->ResetParameters();
    raw->SetParameter(&engine::EntityParameter::actionMode, 1);
    raw->SetParameter(&engine::EntityParameter::moveAmount, 6.3f);
    Require(editor->ChangeObjectType(raw, Registry::Kind::Gimmick, "SinkingFloor"), "metadata repair");
    raw = find();
    Require(raw && raw->GetParameters()->actionMode == 1 && Near(raw->GetParameters()->moveAmount, 6.3f), "metadata repair lost parameter");
    editor->PerformUndo();
    raw = find();
    Require(raw && !dynamic_cast<GimmickSinkingFloor*>(raw) && childRaw->GetParent() == raw, "metadata repair undo");
    editor->PerformRedo();
    raw = find();
    Require(dynamic_cast<GimmickSinkingFloor*>(raw) && Near(raw->GetParameters()->moveAmount, 6.3f), "metadata repair redo");
    Require(editor->ChangeObjectType(raw, Registry::Kind::Gimmick, "MovingFloor"), "second switch away");
    raw = find();
    editor->RequestObjectTypeChange(raw, Registry::Kind::Gimmick, "SinkingFloor");
    Require(!dynamic_cast<GimmickSinkingFloor*>(raw), "queued replacement executed during drawing");
    return guid;
}

inline void AfterDraw(SceneManager* manager, DebugEditor* editor, const std::string& path) {
    static std::ofstream log(path, std::ios::trunc);
    static int phase = 0;
    static std::string guid;
    static bool done = false;
    if (done || !manager || !editor || manager->IsTransitioning()) return;
    try {
        if (phase == 0) {
            StageManager::GetInstance()->SetCurrentStageById("stage1");
            // 今回はFactory・保存・Editor操作のテストです。大量の装飾読込には依存させません。
            if (manager->GetCurrentSceneName() != "TITLE") manager->ChangeScene("TITLE");
            phase = 1;
            return;
        }
        if (manager->GetCurrentSceneName() != "TITLE") return;
        manager->SetIsPlaying(false);
        auto* scene = manager->GetCurrentScene();
        if (phase == 1) {
            auto* common = scene->GetObject3DCommon();
            log << "HOST: TITLE, isolated factory/serialization/editor compatibility tests" << std::endl;
            CheckSettings(common, editor, scene, log);
            CheckAllGimmicks(common, editor, scene, log);
            guid = CheckTypeChange(common, editor, scene);
            phase = 2;
            return;
        }
        engine::Object3D* changed = nullptr;
        for (const auto& object : scene->GetObjects()) if (object->GetPersistentGuid() == guid) changed = object.get();
        Require(dynamic_cast<GimmickSinkingFloor*>(changed) && editor->GetSelectedObject() == changed, "deferred type change or selection");
        log << "OK actual class replacement, GUID, parent/children, transform, selection, type Undo/Redo and deferred Inspector change" << std::endl;
        log << "PASS: all gimmick registrations, legacy data compatibility and shared UI" << std::endl;
    } catch (const std::exception& error) {
        log << "FAIL: " << error.what() << std::endl;
    }
    done = true;
    WinApp::CloseNow();
}
} // namespace GimmickPropertyRuntimeProbe
#endif
