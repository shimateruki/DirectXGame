#pragma once

#include "EditorPropertyRegistry.h"
#include "Object3d.h"
#include <cmath>
#include <limits>
#include <type_traits>

// 変数名を保存IDにします。変数名を変える場合は、このマクロを外して旧IDを明記してください。
#define ENGINE_PROPERTY(member) #member, &std::remove_reference_t<decltype(*this)>::member

namespace engine {

/// クラスのメンバーを既存のProperty台帳へ登録するための小さな記述用APIです。
/// 台帳にはメンバーポインターだけを渡し、最初に作ったObjectのアドレスは保持しません。
template<class Owner>
class ObjectPropertyDefinition {
public:
    using Condition = bool (*)(const Owner&);

    struct Options {
        float speed = 0.05f;
        const char* format = "";
        const char* parameterId = "";
        int integerDisplayOffset = 0;
        Condition enabled = nullptr;
        std::vector<nlohmann::json> values;
        const char* assetDirectory = "";
        EditorPropertyAssetEditor assetEditor = EditorPropertyAssetEditor::None;
        float (*maximum)(const Owner&) = nullptr;
        std::string (*help)(const Owner&) = nullptr;
        nlohmann::json (*displayValue)(const Owner&) = nullptr;
    };

    ObjectPropertyDefinition(const Owner& defaults, const char* typeId, const char* label, const char* description = "")
        : defaults_(defaults), componentId_(std::string("Game_") + typeId) {
        auto* registry = EditorPropertyRegistry::GetInstance();
        EditorComponentDescriptor component;
        component.typeId = componentId_;
        component.displayName = label;
        component.description = description;
        component.required = true;
        component.applicable = [](const Object3d& object) { return dynamic_cast<const Owner*>(&object) != nullptr; };
        component.present = component.applicable;
        enabled_ = registry->RegisterComponent(std::move(component));
    }

    /// 専用変数を公開します。日本語の表示名を変えてもidが同じなら保存互換を保ちます。
    template<class Value>
    void Expose(const char* label, const char* id, Value Owner::* member,
                float minimum = 0, float maximum = 0, Condition visible = nullptr) {
        if (!enabled_) return;
        auto property = Describe<Value>(label, id, minimum, maximum, visible);
        property.flags = property.flags | EditorPropertyFlags::ClassStorage;
        property.serializedPath = "components." + componentId_ + "." + id;
        property.defaultValue = defaults_.*member;
        property.getter = [member](const Object3d& object) -> nlohmann::json {
            return static_cast<const Owner&>(object).*member;
        };
        property.setter = [member](Object3d& object, const nlohmann::json& json) {
            return Assign(static_cast<Owner&>(object).*member, json);
        };
        EditorPropertyRegistry::GetInstance()->Register(std::move(property));
    }

    /// 旧paramの変数をそのまま公開します。専用変数へコピーせず、既存JSONの正本を保ちます。
    template<class Value>
    void Parameter(const char* label, const char* id, Value Object3d::EntityParameter::* member,
                   float minimum = 0, float maximum = 0, Condition visible = nullptr,
                   std::vector<std::string> choices = {}, const Options& options = {}) {
        if (!enabled_) return;
        auto property = Describe<Value>(label, id, minimum, maximum, visible);
        property.serializedPath = std::string("param.") + (options.parameterId[0] ? options.parameterId : id);
        const Object3d::EntityParameter fallback{};
        property.defaultValue = defaults_.GetParameters() ? (*defaults_.GetParameters()).*member : fallback.*member;
        property.enumLabels = std::move(choices);
        ApplyOptions(property, options);
        const Value defaultValue = property.defaultValue.get<Value>();
        property.getter = [member, defaultValue](const Object3d& object) -> nlohmann::json {
            return object.GetParameters() ? (*object.GetParameters()).*member : defaultValue;
        };
        property.setter = [member](Object3d& object, const nlohmann::json& json) {
            Value value{};
            if (!Assign(value, json)) return false;
            if (!object.GetParameters()) object.ResetParameters();
            object.SetParameter(member, std::move(value));
            return true;
        };
        EditorPropertyRegistry::GetInstance()->Register(std::move(property));
    }

    /// 既存の保存先を使う特殊項目です。変換や複数項目の更新はクラス側で記述します。
    template<class Value>
    void Accessor(const char* label, const char* id, const char* serializedPath,
                  std::function<Value(const Owner&)> getter,
                  std::function<void(Owner&, const Value&)> setter,
                  float minimum = 0, float maximum = 0, Condition visible = nullptr,
                  std::vector<std::string> choices = {}, const Options& options = {}) {
        if (!enabled_) return;
        auto property = Describe<Value>(label, id, minimum, maximum, visible);
        property.serializedPath = serializedPath;
        property.defaultValue = getter(defaults_);
        property.enumLabels = std::move(choices);
        ApplyOptions(property, options);
        property.getter = [getter](const Object3d& object) -> nlohmann::json {
            return getter(static_cast<const Owner&>(object));
        };
        property.setter = [setter](Object3d& object, const nlohmann::json& json) {
            Value value{};
            if (!Assign(value, json)) return false;
            setter(static_cast<Owner&>(object), value);
            return true;
        };
        EditorPropertyRegistry::GetInstance()->Register(std::move(property));
    }

private:
    static void ApplyOptions(EditorPropertyDescriptor& property, const Options& options) {
        property.ui.speed = options.speed;
        property.ui.format = options.format;
        property.ui.integerDisplayOffset = options.integerDisplayOffset;
        property.ui.assetDirectory = options.assetDirectory;
        property.ui.assetEditor = options.assetEditor;
        property.enumValues = options.values;
        if (options.enabled) property.enabled = [condition = options.enabled](const Object3d& object) {
            return condition(static_cast<const Owner&>(object));
        };
        if (options.maximum) property.maximum = [maximum = options.maximum](const Object3d& object) {
            return maximum(static_cast<const Owner&>(object));
        };
        if (options.displayValue) property.displayValue = [display = options.displayValue](const Object3d& object) {
            return display(static_cast<const Owner&>(object));
        };
        if (options.help) property.help = [help = options.help](const Object3d& object) {
            return help(static_cast<const Owner&>(object));
        };
    }

    template<class Value>
    EditorPropertyDescriptor Describe(const char* label, const char* id, float minimum, float maximum, Condition visible) const {
        static_assert(std::is_same_v<Value, bool> || std::is_same_v<Value, int> ||
                      std::is_same_v<Value, float> || std::is_same_v<Value, std::string>,
                      "Expose currently supports bool, int, float and string");
        EditorPropertyDescriptor property;
        property.path = "class." + componentId_ + "." + id;
        property.componentTypeId = componentId_;
        property.displayName = label;
        property.flags = EditorPropertyFlags::MultiEdit | EditorPropertyFlags::PrefabOverride;
        property.applicable = [](const Object3d& object) { return dynamic_cast<const Owner*>(&object) != nullptr; };
        if (visible) {
            property.visible = [visible](const Object3d& object) { return visible(static_cast<const Owner&>(object)); };
        }
        if constexpr (std::is_same_v<Value, bool>) property.type = EditorPropertyType::Bool;
        else if constexpr (std::is_same_v<Value, int>) property.type = EditorPropertyType::Integer;
        else if constexpr (std::is_same_v<Value, float>) property.type = EditorPropertyType::Number;
        else property.type = EditorPropertyType::String;
        property.ui.configured = true;
        property.ui.speed = 0.05f;
        property.ui.minValue = minimum;
        property.ui.maxValue = maximum;
        if constexpr (std::is_same_v<Value, int>) property.ui.useSlider = maximum > minimum;
        return property;
    }

    template<class Value>
    static bool Assign(Value& target, const nlohmann::json& json) {
        // 保存済みの範囲外値は勝手に丸めません。UIの調整範囲と保存互換は別に扱います。
        if constexpr (std::is_same_v<Value, bool>) {
            if (!json.is_boolean()) return false;
        } else if constexpr (std::is_same_v<Value, std::string>) {
            if (!json.is_string()) return false;
        } else if constexpr (std::is_same_v<Value, int>) {
            if (!json.is_number_integer() || json.get<double>() < (std::numeric_limits<int>::min)() ||
                json.get<double>() > (std::numeric_limits<int>::max)()) return false;
        } else {
            if (!json.is_number() || !std::isfinite(json.get<double>()) ||
                std::abs(json.get<double>()) > (std::numeric_limits<float>::max)()) return false;
        }
        target = json.get<Value>();
        return true;
    }

    // 既定値を登録時に読むだけの参照です。この記述用Object自体は台帳に保存しません。
    const Owner& defaults_;
    std::string componentId_;
    bool enabled_ = false; // 同じクラスの二個目以降は登録を繰り返しません。
};
} // namespace engine
