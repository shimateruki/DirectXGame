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
#include <filesystem>

namespace {

using json = nlohmann::json;

constexpr float kRadiansToDegrees = 57.29577951308232f;
constexpr float kDegreesToRadians = 0.017453292519943295f;

// 数値以外の旧データや未設定値でも編集UIを開けるよう、既定値へ置き換えます。
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

// 登録済みの型に合わせて入力 UI を選び、変更があった場合だけ呼び出し元へ通知します。
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
    // 複数選択で値が異なるときはImGuiの混合値表示を使い、入力が確定した項目だけ書き戻す。
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
        current += ui.integerDisplayOffset;
        if (!property.enumLabels.empty()) {
            std::string preview = value.dump();
            const auto choiceValue = [&](std::size_t index) -> json {
                return property.enumValues.size() == property.enumLabels.size()
                    ? property.enumValues[index] : json(index);
            };
            for (std::size_t i = 0; i < property.enumLabels.size(); ++i)
                if (choiceValue(i) == value) preview = property.enumLabels[i];
            if (ImGui::BeginCombo(id, preview.c_str())) {
                for (std::size_t i = 0; i < property.enumLabels.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Selectable(property.enumLabels[i].c_str(), value == choiceValue(i))) {
                        value = choiceValue(i);
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
        }
        else if (ui.useSlider && ui.maxValue > ui.minValue) {
            // 登録側で範囲を指定した整数も、共通UIだけで調整できます。
            const std::int64_t minimum = static_cast<std::int64_t>(ui.minValue) + ui.integerDisplayOffset;
            const std::int64_t maximum = static_cast<std::int64_t>(ui.maxValue) + ui.integerDisplayOffset;
            if (ImGui::SliderScalar(id, ImGuiDataType_S64, &current, &minimum, &maximum, "%lld")) {
                value = current - ui.integerDisplayOffset;
                changed = true;
            }
        }
        else if (ImGui::InputScalar(id, ImGuiDataType_S64, &current)) {
            value = current - ui.integerDisplayOffset;
            changed = true;
        }

        break;
    }

    case EditorPropertyType::Number: {
        float current = ReadNumber(value);

        // 内部はラジアンのまま保持し、Inspectorで角度として見せるときだけ度へ換算する。
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
        const std::string current = value.is_string() ? value.get<std::string>() : std::string();
        if (property.enumLabels.size() == property.enumValues.size() && !property.enumValues.empty()) {
            std::string preview = current.empty() ? "(未設定)" : current;
            for (std::size_t i = 0; i < property.enumValues.size(); ++i)
                if (value == property.enumValues[i]) preview = property.enumLabels[i];
            if (ImGui::BeginCombo(id, preview.c_str())) {
                for (std::size_t i = 0; i < property.enumValues.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Selectable(property.enumLabels[i].c_str(), value == property.enumValues[i])) {
                        value = property.enumValues[i];
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
        } else if (!ui.assetDirectory.empty()) {
            namespace fs = std::filesystem;
            const auto preview = current.empty() ? std::string("(なし)") : fs::path(current).stem().string();
            if (ImGui::BeginCombo(id, preview.c_str())) {
                if (ImGui::Selectable("(なし)", current.empty())) { value = ""; changed = true; }
                std::vector<fs::path> files;
                std::error_code error;
                for (fs::directory_iterator it(ui.assetDirectory, error), end; !error && it != end; it.increment(error)) {
                    if (it->is_regular_file(error) && it->path().extension() == ".json") files.push_back(it->path());
                }
                std::sort(files.begin(), files.end());
                for (const auto& file : files) {
                    const auto path = file.generic_string();
                    if (ImGui::Selectable(file.stem().string().c_str(), current == path)) { value = path; changed = true; }
                }
                ImGui::EndCombo();
            }
            std::error_code error;
            if (!current.empty() && !fs::is_regular_file(current, error))
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f), "指定されたJSONが見つかりません");
        } else {
            std::array<char, 512> buffer = {};
            std::snprintf(buffer.data(), buffer.size(), "%s", current.c_str());
            if (ImGui::InputText(id, buffer.data(), buffer.size())) {
                value = std::string(buffer.data());
                changed = true;
            }
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
        // 回転ベクトルも各成分を表示用に換算し、保存時には元の単位へ戻す。
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
