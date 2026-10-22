#include "engine/animation/cinematic/CinematicSequence.h"
#include "json.hpp"
#include <stdexcept>
#include <cmath>

namespace engine {

using json = nlohmann::json;

namespace {
engine::Vector3 V3(const json& a) { return {a.at(0).get<float>(), a.at(1).get<float>(), a.at(2).get<float>()}; }
engine::Vector4 V4(const json& a) { return {a.at(0).get<float>(), a.at(1).get<float>(), a.at(2).get<float>(), a.at(3).get<float>()}; }
json J3(const engine::Vector3& v) { return {v.x, v.y, v.z}; }
json J4(const engine::Vector4& v) { return {v.x, v.y, v.z, v.w}; }

// 非有限値や不正な配列長をGPUへ渡さず、読込時にエラーへまとめる。
void CheckRow(const json& row, size_t count) {
    if (!row.is_array() || row.size() != count)
        throw std::runtime_error("Invalid cinematic sample size");
    for (const auto& value : row)
        if (!value.is_number() || !std::isfinite(value.get<float>()))
            throw std::runtime_error("Invalid cinematic sample value");
}
}

void SaveCinematicPresentation(json& root, const CinematicPresentation& p) {
    auto& data = root["presentation"];
    data = {{"reference", p.reference}, {"lookToward", p.lookToward}, {"hideObjects", p.hideObjects},
            {"objects", json::array()}, {"view", json::array()}, {"particles", json::array()}};
    if (p.viewWorldSpace) data["viewWorldSpace"] = true;
    for (const auto& clip : p.objects) {
        json item = {{"name", clip.name}, {"model", clip.model}, {"source", clip.source},
            {"positionSpace", clip.positionSpace}, {"pivot", J3(clip.pivot)}, {"spawn", clip.spawn},
            {"additive", clip.additive}, {"lit", clip.lit}, {"enabled", clip.enabled},
            {"inheritScale", clip.inheritScale}, {"inheritRotation", clip.inheritRotation},
            {"clipScaleHeight", clip.clipScaleHeight}, {"floorHeight", clip.floorHeight},
            {"clipCenterOffset", clip.clipCenterOffset},
            {"clipEdgeWidth", clip.clipEdgeWidth}, {"clipDissolve", clip.clipDissolve},
            {"clipColor", J4(clip.clipColor)}, {"keys", json::array()}};
        if (!clip.animate) item["animate"] = false;
        if (clip.followPosition) item["followPosition"] = true;
        if (clip.followRotation) item["followRotation"] = true;
        if (clip.followScale) item["followScale"] = true;
        if (clip.inheritVisibility) item["inheritVisibility"] = true;
        if (clip.rotationMask.x != 1 || clip.rotationMask.y != 1 || clip.rotationMask.z != 1)
            item["rotationMask"] = J3(clip.rotationMask);
        for (const auto& k : clip.keys)
            item["keys"].push_back({k.time, k.position.x, k.position.y, k.position.z,
                k.rotation.x, k.rotation.y, k.rotation.z, k.scale.x, k.scale.y, k.scale.z,
                k.color.x, k.color.y, k.color.z, k.color.w, k.emissive, k.clipHeight,
                k.bounce, k.visible ? 1 : 0, k.clipEnabled ? 1 : 0});
        data["objects"].push_back(std::move(item));
    }
    for (const auto& k : p.view) {
        data["view"].push_back({k.time, k.focusMix, k.focusOffset.x, k.focusOffset.y, k.focusOffset.z,
            k.scaledFocusOffset.x, k.scaledFocusOffset.y, k.scaledFocusOffset.z,
            k.eyeOffset.x, k.eyeOffset.y, k.eyeOffset.z, k.jitter.x, k.jitter.y, k.jitter.z, k.fov, k.bars});
        if (!k.reference.empty() || !k.lookToward.empty() || !k.eyeReference.empty()) {
            data["view"].back().push_back(k.reference);
            data["view"].back().push_back(k.lookToward);
            if (!k.eyeReference.empty()) data["view"].back().push_back(k.eyeReference);
        }
    }
    for (const auto& clip : p.particles)
        data["particles"].push_back({{"target", clip.target}, {"preset", clip.preset},
            {"directionMode", clip.directionMode}, {"startTime", clip.startTime}, {"endTime", clip.endTime},
            {"interval", clip.interval}, {"scale", clip.scale}, {"direction", J3(clip.direction)}});
}

void LoadCinematicPresentation(const json& root, CinematicPresentation& p) {
    if (!root.contains("presentation"))
        return;
    const auto& data = root.at("presentation");
    p.reference = data.value("reference", "");
    p.lookToward = data.value("lookToward", "");
    p.viewWorldSpace = data.value("viewWorldSpace", false);
    p.hideObjects = data.value("hideObjects", std::vector<std::string>{});
    for (const auto& item : data.value("objects", json::array())) {
        CinematicObjectClip clip;
        clip.name = item.at("name").get<std::string>();
        clip.source = item.at("source").get<std::string>();
        clip.model = item.value("model", "");
        clip.positionSpace = item.value("positionSpace", "world");
        if (clip.positionSpace != "world" && clip.positionSpace != "model" && clip.positionSpace != "radial")
            throw std::runtime_error("Unknown cinematic position space");
        clip.pivot = V3(item.value("pivot", json::array({0, 0, 0})));
        clip.spawn = item.value("spawn", true);
        clip.additive = item.value("additive", false);
        clip.lit = item.value("lit", true);
        clip.enabled = item.value("enabled", true);
        clip.animate = item.value("animate", true);
        clip.followPosition = item.value("followPosition", false);
        clip.followRotation = item.value("followRotation", false);
        clip.followScale = item.value("followScale", false);
        clip.inheritVisibility = item.value("inheritVisibility", false);
        clip.rotationMask = V3(item.value("rotationMask", json::array({1, 1, 1})));
        clip.inheritScale = item.value("inheritScale", true);
        clip.inheritRotation = item.value("inheritRotation", true);
        clip.clipScaleHeight = item.value("clipScaleHeight", false);
        clip.floorHeight = item.value("floorHeight", -100000.0f);
        clip.clipCenterOffset = item.value("clipCenterOffset", 0.0f);
        clip.clipEdgeWidth = item.value("clipEdgeWidth", 0.075f);
        clip.clipDissolve = item.value("clipDissolve", 0.20f);
        clip.clipColor = V4(item.value("clipColor", json::array({1, 1, 1, 1})));
        for (const auto& row : item.at("keys")) {
            CheckRow(row, 19);
            CinematicVisualKey k;
            k.time = row[0]; k.position = {row[1], row[2], row[3]};
            k.rotation = {row[4], row[5], row[6]}; k.scale = {row[7], row[8], row[9]};
            k.color = {row[10], row[11], row[12], row[13]};
            k.emissive = row[14]; k.clipHeight = row[15]; k.bounce = row[16];
            k.visible = row[17].get<float>() != 0; k.clipEnabled = row[18].get<float>() != 0;
            clip.keys.push_back(k);
        }
        p.objects.push_back(std::move(clip));
    }
    for (const auto& row : data.value("view", json::array())) {
        if (!row.is_array() || (row.size() != 16 && row.size() != 18 && row.size() != 19))
            throw std::runtime_error("Invalid cinematic view sample size");
        CheckRow(json(row.begin(), row.begin() + 16), 16);
        CinematicViewKey k;
        k.time = row[0]; k.focusMix = row[1]; k.focusOffset = {row[2], row[3], row[4]};
        k.scaledFocusOffset = {row[5], row[6], row[7]}; k.eyeOffset = {row[8], row[9], row[10]};
        k.jitter = {row[11], row[12], row[13]}; k.fov = row[14]; k.bars = row[15];
        if (row.size() >= 18) {
            k.reference = row[16].get<std::string>();
            k.lookToward = row[17].get<std::string>();
            if (row.size() == 19) k.eyeReference = row[18].get<std::string>();
        }
        p.view.push_back(k);
    }
    for (const auto& item : data.value("particles", json::array())) {
        CinematicParticleClip clip;
        clip.target = item.at("target").get<std::string>(); clip.preset = item.at("preset").get<std::string>();
        clip.directionMode = item.value("directionMode", "fixed");
        clip.startTime = item.value("startTime", 0.0f); clip.endTime = item.value("endTime", 1.0f);
        clip.interval = item.value("interval", 1.0f / 9.0f); clip.scale = item.value("scale", 1.0f);
        clip.direction = V3(item.value("direction", json::array({0, 1, 0})));
        if (!std::isfinite(clip.interval) || clip.interval < 0.01f || clip.endTime < clip.startTime)
            throw std::runtime_error("Invalid cinematic particle interval");
        p.particles.push_back(clip);
    }
}

} // namespace engine
