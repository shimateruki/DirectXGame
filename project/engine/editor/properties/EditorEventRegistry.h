#pragma once

#include "EditorPropertyRegistry.h"
#include "Object3D.h"

#include <algorithm>
#include <memory>
#include <set>
#include <utility>

/// 保存済みの番号と表示名、選択時だけ適用する初期設定をまとめます。
/// 固有の編集項目には、既存のProperty定義と保存パスをそのまま使えます。
// 関連：EditorEventRegistry（この型を使う機能の本体）
struct EditorEventDescriptor {
    int value;
    std::string displayName;
    std::function<void(engine::Object3D&)> initialize;
    std::vector<EditorPropertyDescriptor> properties;
};

// 担当：ゲーム定義の接触イベントを既存のComponent・Property UIへ登録する。
// 関連：EditorEventDescriptor（定義）、EditorPropertyRegistry（登録先）、Object3D（設定対象）
// 主な入口：Register
class EditorEventRegistry {
public:
    static constexpr const char* kComponentType = "ContactEvent";
    static constexpr const char* kTypePath = "event.type";

    /// 起動時に一度、ゲーム側で定義した一覧を登録します。
    /// 番号は配列の添字ではなく保存値です。空の一覧や重複した定義は登録しません。
    static bool Register(std::vector<EditorEventDescriptor> events) {
        auto* registry = EditorPropertyRegistry::GetInstance();
        registry->InitializeBuiltInProperties();
        if (events.empty() || registry->FindComponent(kComponentType) || registry->Find(kTypePath))
            return false;

        // 登録途中で不正な定義が見つからないよう、全件を先に検証します。
        std::set<int> values;
        std::set<std::string> paths{ kTypePath };
        for (const auto& event : events) {
            if (event.displayName.empty() || !values.insert(event.value).second)
                return false;
            for (const auto& property : event.properties) {
                if (property.path.empty() || !property.getter || !property.setter ||
                    registry->Find(property.path) || !paths.insert(property.path).second)
                    return false;
            }
        }

        EditorComponentDescriptor component;
        component.typeId = kComponentType;
        component.displayName = "接触イベント";
        component.description = "接触時に実行するイベントを選びます。";
        component.displayOrder = 85;
        component.required = true;
        component.applicable = [](const engine::Object3D&) { return true; };
        component.present = component.applicable;
        component.serializedPresent = [](const nlohmann::json&) { return true; };
        if (!registry->RegisterComponent(std::move(component)))
            return false;

        // Setterが初期設定を保持します。表示と保存は既存のProperty機構へ任せます。
        auto choices = std::make_shared<const std::vector<EditorEventDescriptor>>(std::move(events));
        EditorPropertyDescriptor selector;
        selector.path = kTypePath;
        selector.displayName = "イベント種類";
        selector.category = "Gameplay";
        selector.type = EditorPropertyType::Integer;
        selector.flags = EditorPropertyFlags::MultiEdit | EditorPropertyFlags::PrefabOverride;
        selector.componentTypeId = kComponentType;
        selector.serializedPath = "eventType";
        selector.defaultValue = 0;
        selector.getter = [](const engine::Object3D& object) {
            return nlohmann::json(static_cast<int>(object.GetEventType()));
        };
        selector.setter = [choices](engine::Object3D& object, const nlohmann::json& value) {
            if (!value.is_number_integer())
                return false;
            const auto selected = std::find_if(choices->begin(), choices->end(), [&](const auto& event) {
                return value == event.value;
            });
            if (selected == choices->end() || static_cast<int>(object.GetEventType()) == selected->value)
                return false;

            object.SetEventType(static_cast<decltype(object.GetEventType())>(selected->value));
            if (selected->initialize)
                selected->initialize(object);
            return true;
        };
        for (const auto& event : *choices) {
            selector.enumLabels.push_back(event.displayName);
            selector.enumValues.push_back(event.value);
        }
        if (!registry->Register(std::move(selector)))
            return false;

        // 入力欄の表示条件だけをイベント番号へ結び付け、保存時には旧値を消しません。
        for (const auto& event : *choices) {
            for (auto property : event.properties) {
                const auto visible = property.visible;
                property.componentTypeId = kComponentType;
                property.visible = [value = event.value, visible](const engine::Object3D& object) {
                    return static_cast<int>(object.GetEventType()) == value && (!visible || visible(object));
                };
                if (!registry->Register(std::move(property)))
                    return false;
            }
        }
        return true;
    }
};
