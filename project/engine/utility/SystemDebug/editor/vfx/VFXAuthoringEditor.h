#pragma once

#include "engine/graphics/particle/VFXAuthoring.h"
#include "engine/utility/SystemDebug/editor/core/EditorPropertyTransaction.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#ifdef USE_IMGUI
#include "imgui.h"

namespace VFXAuthoringEditor {

inline const char* GetEasingName(VFXCurveEasing easing) {
    switch (easing) {
    case VFXCurveEasing::EaseIn: return "Ease In";
    case VFXCurveEasing::EaseOut: return "Ease Out";
    case VFXCurveEasing::EaseInOut: return "Ease In Out";
    case VFXCurveEasing::SmoothStep: return "Smooth Step";
    case VFXCurveEasing::Linear:
    default: return "Linear";
    }
}

template <class TKey>
float FindInsertionTime(const std::vector<TKey>& keys) {
    if (keys.size() < 2) {
        return keys.empty() ? 0.0f : 1.0f;
    }

    float insertionTime = 0.5f;
    float largestGap = -1.0f;
    for (std::size_t index = 1; index < keys.size(); ++index) {
        const float gap = keys[index].time - keys[index - 1].time;
        if (gap > largestGap) {
            largestGap = gap;
            insertionTime = (keys[index - 1].time + keys[index].time) * 0.5f;
        }
    }
    return std::clamp(insertionTime, 0.001f, 0.999f);
}

inline bool DrawFloatCurve(
    const char* id,
    VFXFloatCurve& curve,
    float minimumValue,
    float maximumValue,
    int maximumKeys,
    const std::function<void(const VFXFloatCurve&)>& apply,
    bool allowKeyCountChange = true,
    bool allowPerKeyEasing = true) {
    bool changed = false;
    ImGui::PushID(id);

    std::vector<float> preview(64);
    for (std::size_t index = 0; index < preview.size(); ++index) {
        preview[index] = curve.Evaluate(
            static_cast<float>(index) / static_cast<float>(preview.size() - 1),
            1.0f);
    }
    ImGui::PlotLines(
        "##Preview",
        preview.data(),
        static_cast<int>(preview.size()),
        0,
        nullptr,
        minimumValue,
        maximumValue,
        ImVec2(ImGui::GetContentRegionAvail().x, 90.0f));

    for (std::size_t index = 0; index < curve.keys.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        ImGui::TextDisabled("Key %d", static_cast<int>(index + 1));
        ImGui::SameLine();

        VFXFloatCurve before = curve;
        ImGui::SetNextItemWidth(82.0f);
        bool timeChanged = ImGui::DragFloat("Time", &curve.keys[index].time, 0.005f, 0.0f, 1.0f, "%.3f");
        if (index == 0) curve.keys[index].time = 0.0f;
        if (index + 1 == curve.keys.size()) curve.keys[index].time = 1.0f;
        if (index > 0 && index + 1 < curve.keys.size()) {
            curve.keys[index].time = std::clamp(
                curve.keys[index].time,
                curve.keys[index - 1].time + 0.001f,
                curve.keys[index + 1].time - 0.001f);
        }
        if (timeChanged) {
            apply(curve);
            changed = true;
        }
        EditorPropertyTransaction::TrackLastItem(
            std::string(id) + ": Key Time",
            before,
            curve,
            apply);

        ImGui::SameLine();
        before = curve;
        ImGui::SetNextItemWidth(92.0f);
        if (ImGui::DragFloat("Value", &curve.keys[index].value, 0.01f, minimumValue, maximumValue, "%.3f")) {
            curve.keys[index].value = std::clamp(curve.keys[index].value, minimumValue, maximumValue);
            apply(curve);
            changed = true;
        }
        EditorPropertyTransaction::TrackLastItem(
            std::string(id) + ": Key Value",
            before,
            curve,
            apply);

        if (allowPerKeyEasing && index + 1 < curve.keys.size()) {
            ImGui::SameLine();
            int easing = static_cast<int>(curve.keys[index].easing);
            ImGui::SetNextItemWidth(105.0f);
            before = curve;
            if (ImGui::Combo("Easing", &easing, "Linear\0Ease In\0Ease Out\0Ease In Out\0Smooth Step\0")) {
                curve.keys[index].easing = static_cast<VFXCurveEasing>(easing);
                apply(curve);
                EditorPropertyTransaction::RegisterDiscrete(
                    std::string(id) + ": Easing",
                    before,
                    curve,
                    apply);
                changed = true;
            }
        }

        if (allowKeyCountChange && curve.keys.size() > 2 && index > 0 && index + 1 < curve.keys.size()) {
            ImGui::SameLine();
            before = curve;
            if (ImGui::SmallButton("削除")) {
                curve.keys.erase(curve.keys.begin() + static_cast<std::ptrdiff_t>(index));
                apply(curve);
                EditorPropertyTransaction::RegisterDiscrete(
                    std::string(id) + ": Delete Key",
                    before,
                    curve,
                    apply);
                changed = true;
                ImGui::PopID();
                break;
            }
        }
        ImGui::PopID();
    }

    if (allowKeyCountChange && static_cast<int>(curve.keys.size()) < maximumKeys && ImGui::Button("キーを追加")) {
        const VFXFloatCurve before = curve;
        const float time = FindInsertionTime(curve.keys);
        curve.keys.push_back({ time, curve.Evaluate(time, 1.0f), VFXCurveEasing::Linear });
        curve.Normalize();
        apply(curve);
        EditorPropertyTransaction::RegisterDiscrete(
            std::string(id) + ": Add Key",
            before,
            curve,
            apply);
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

inline bool DrawColorGradient(
    const char* id,
    VFXColorGradient& gradient,
    int maximumKeys,
    const std::function<void(const VFXColorGradient&)>& apply,
    bool allowKeyCountChange = true) {
    bool changed = false;
    ImGui::PushID(id);
    for (std::size_t index = 0; index < gradient.keys.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        ImGui::TextDisabled("Color %d", static_cast<int>(index + 1));
        ImGui::SameLine();

        VFXColorGradient before = gradient;
        ImGui::SetNextItemWidth(82.0f);
        bool timeChanged = ImGui::DragFloat("Time", &gradient.keys[index].time, 0.005f, 0.0f, 1.0f, "%.3f");
        if (index == 0) gradient.keys[index].time = 0.0f;
        if (index + 1 == gradient.keys.size()) gradient.keys[index].time = 1.0f;
        if (index > 0 && index + 1 < gradient.keys.size()) {
            gradient.keys[index].time = std::clamp(
                gradient.keys[index].time,
                gradient.keys[index - 1].time + 0.001f,
                gradient.keys[index + 1].time - 0.001f);
        }
        if (timeChanged) {
            apply(gradient);
            changed = true;
        }
        EditorPropertyTransaction::TrackLastItem(
            std::string(id) + ": Color Time",
            before,
            gradient,
            apply);

        ImGui::SameLine();
        before = gradient;
        if (ImGui::ColorEdit4("Color", &gradient.keys[index].color.x, ImGuiColorEditFlags_Float)) {
            apply(gradient);
            changed = true;
        }
        EditorPropertyTransaction::TrackLastItem(
            std::string(id) + ": Color",
            before,
            gradient,
            apply);

        if (allowKeyCountChange && gradient.keys.size() > 2 && index > 0 && index + 1 < gradient.keys.size()) {
            ImGui::SameLine();
            before = gradient;
            if (ImGui::SmallButton("削除")) {
                gradient.keys.erase(gradient.keys.begin() + static_cast<std::ptrdiff_t>(index));
                apply(gradient);
                EditorPropertyTransaction::RegisterDiscrete(
                    std::string(id) + ": Delete Color",
                    before,
                    gradient,
                    apply);
                changed = true;
                ImGui::PopID();
                break;
            }
        }
        ImGui::PopID();
    }

    if (allowKeyCountChange && static_cast<int>(gradient.keys.size()) < maximumKeys && ImGui::Button("色キーを追加")) {
        const VFXColorGradient before = gradient;
        const float time = FindInsertionTime(gradient.keys);
        gradient.keys.push_back({ time, gradient.Evaluate(time), VFXCurveEasing::Linear });
        gradient.Normalize();
        apply(gradient);
        EditorPropertyTransaction::RegisterDiscrete(
            std::string(id) + ": Add Color",
            before,
            gradient,
            apply);
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

inline bool DrawLodSettings(
    const char* id,
    VFXLodSettings& settings,
    const std::function<void(const VFXLodSettings&)>& apply,
    int minimumParticleLimit = 1,
    int maximumParticleLimit = 262144) {
    bool changed = false;
    ImGui::PushID(id);

    VFXLodSettings before = settings;
    if (ImGui::Checkbox("距離LODを有効化", &settings.enabled)) {
        settings.Sanitize();
        apply(settings);
        EditorPropertyTransaction::RegisterDiscrete(
            std::string(id) + ": Enable LOD",
            before,
            settings,
            apply);
        changed = true;
    }

    ImGui::BeginDisabled(!settings.enabled);
    before = settings;
    if (ImGui::DragFloat("近距離", &settings.nearDistance, 0.25f, 0.0f, 1000.0f, "%.2f")) {
        settings.Sanitize();
        apply(settings);
        changed = true;
    }
    EditorPropertyTransaction::TrackLastItem(std::string(id) + ": Near Distance", before, settings, apply);

    before = settings;
    if (ImGui::DragFloat("遠距離", &settings.farDistance, 0.25f, 0.01f, 2000.0f, "%.2f")) {
        settings.Sanitize();
        apply(settings);
        changed = true;
    }
    EditorPropertyTransaction::TrackLastItem(std::string(id) + ": Far Distance", before, settings, apply);

    before = settings;
    if (ImGui::SliderFloat("遠距離の発生率", &settings.farEmissionScale, 0.0f, 1.0f, "%.2f")) {
        settings.Sanitize();
        apply(settings);
        changed = true;
    }
    EditorPropertyTransaction::TrackLastItem(std::string(id) + ": Far Emission", before, settings, apply);

    before = settings;
    if (ImGui::DragInt("同時生存上限 (0 = System上限)", &settings.maxAliveParticles, 8.0f, 0, maximumParticleLimit)) {
        settings.Sanitize();
        if (settings.maxAliveParticles > 0) {
            settings.maxAliveParticles = std::clamp(
                settings.maxAliveParticles, (std::max)(minimumParticleLimit, 1), maximumParticleLimit);
        }
        apply(settings);
        changed = true;
    }
    EditorPropertyTransaction::TrackLastItem(std::string(id) + ": Max Alive", before, settings, apply);
    ImGui::EndDisabled();

    ImGui::PopID();
    return changed;
}

} // namespace VFXAuthoringEditor
#endif
