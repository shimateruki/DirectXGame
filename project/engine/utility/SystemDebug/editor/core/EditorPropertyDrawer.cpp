#include "EditorPropertyDrawer.h"

#ifdef USE_IMGUI
#include "imgui.h"
#include "imgui_internal.h"
#endif

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace {

using json = nlohmann::json;

constexpr float kRadiansToDegrees = 57.29577951308232f;
constexpr float kDegreesToRadians = 0.017453292519943295f;

float ReadNumber(const json& value, float fallback = 0.0f) {
    return value.is_number() ? value.get<float>() : fallback;
}

float ReadArrayNumber(const json& value, std::size_t index, float fallback = 0.0f) {
    if (!value.is_array() || index >= value.size() || !value[index].is_number()) {
        return fallback;
    }
    return value[index].get<float>();
}

}

bool EditorPropertyDrawer::DrawValue(
    const EditorPropertyDescriptor& property,
    nlohmann::json& value,
    const char* id,
    const EditorPropertyDrawOptions& options) {
#ifdef USE_IMGUI
    const float width = options.width != 0.0f
        ? options.width
        : (options.compact ? -FLT_MIN : 430.0f);
    ImGui::SetNextItemWidth(width);
    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, options.mixed);

    bool changed = false;
    const EditorPropertyUiHints& ui = property.ui;
    const char* format = ui.format.empty() ? "%.3f" : ui.format.c_str();

    switch (property.type) {
    case EditorPropertyType::Bool: {
        bool current = value.is_boolean() ? value.get<bool>() : false;
        if (ImGui::Checkbox(id, &current)) {
            value = current;
            changed = true;
        }
        break;
    }
    case EditorPropertyType::Integer: {
        std::int64_t current = value.is_number_unsigned() || value.is_number_integer()
            ? value.get<std::int64_t>()
            : 0;
        if (!property.enumLabels.empty()) {
            int enumValue = std::clamp(
                static_cast<int>(current),
                0,
                static_cast<int>(property.enumLabels.size()) - 1);
            std::vector<const char*> labels;
            labels.reserve(property.enumLabels.size());
            for (const std::string& label : property.enumLabels) {
                labels.push_back(label.c_str());
            }
            if (ImGui::Combo(id, &enumValue, labels.data(), static_cast<int>(labels.size()))) {
                value = enumValue;
                changed = true;
            }
        }
        else if (ImGui::InputScalar(id, ImGuiDataType_S64, &current)) {
            value = current;
            changed = true;
        }
        break;
    }
    case EditorPropertyType::Number: {
        float current = ReadNumber(value);
        if (ui.displayAsDegrees) {
            current *= kRadiansToDegrees;
        }
        if (ui.useSlider) {
            changed = ImGui::SliderFloat(id, &current, ui.minValue, ui.maxValue, format);
        }
        else {
            changed = ImGui::DragFloat(id, &current, ui.speed, ui.minValue, ui.maxValue, format);
        }
        if (changed) {
            if (ui.displayAsDegrees) {
                current *= kDegreesToRadians;
            }
            value = current;
        }
        break;
    }
    case EditorPropertyType::String: {
        std::array<char, 512> buffer = {};
        const std::string current = value.is_string() ? value.get<std::string>() : std::string();
        std::snprintf(buffer.data(), buffer.size(), "%s", current.c_str());
        if (ImGui::InputText(id, buffer.data(), buffer.size())) {
            value = std::string(buffer.data());
            changed = true;
        }
        break;
    }
    case EditorPropertyType::Vector2: {
        float current[2] = { ReadArrayNumber(value, 0), ReadArrayNumber(value, 1) };
        changed = ImGui::DragFloat2(
            id, current, ui.speed, ui.minValue, ui.maxValue, format);
        if (changed) {
            value = json::array({ current[0], current[1] });
        }
        break;
    }
    case EditorPropertyType::Vector3: {
        float current[3] = {
            ReadArrayNumber(value, 0),
            ReadArrayNumber(value, 1),
            ReadArrayNumber(value, 2),
        };
        if (ui.displayAsDegrees) {
            for (float& component : current) {
                component *= kRadiansToDegrees;
            }
        }
        changed = ImGui::DragFloat3(
            id, current, ui.speed, ui.minValue, ui.maxValue, format);
        if (changed) {
            if (ui.displayAsDegrees) {
                for (float& component : current) {
                    component *= kDegreesToRadians;
                }
            }
            value = json::array({ current[0], current[1], current[2] });
        }
        break;
    }
    case EditorPropertyType::Vector4: {
        float current[4] = {
            ReadArrayNumber(value, 0),
            ReadArrayNumber(value, 1),
            ReadArrayNumber(value, 2),
            ReadArrayNumber(value, 3, 1.0f),
        };
        changed = ui.useColorPicker
            ? ImGui::ColorEdit4(id, current, ImGuiColorEditFlags_Float)
            : ImGui::DragFloat4(id, current, ui.speed, ui.minValue, ui.maxValue, format);
        if (changed) {
            value = json::array({ current[0], current[1], current[2], current[3] });
        }
        break;
    }
    }

    ImGui::PopItemFlag();
    return changed;
#else
    (void)property;
    (void)value;
    (void)id;
    (void)options;
    return false;
#endif
}
