#define NOMINMAX
#include "CinematicSequence.h"

#include "json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;

namespace {
Vector3 ReadVector3(const json& value, const Vector3& fallback) {
    if (!value.is_array() || value.size() < 3) {
        return fallback;
    }
    return {
        value[0].get<float>(),
        value[1].get<float>(),
        value[2].get<float>()
    };
}

json WriteVector3(const Vector3& value) {
    return json::array({ value.x, value.y, value.z });
}

void ReadBinding(const json& value, CinematicObjectBinding& binding) {
    binding.targetName = value.value("targetName", "");
    binding.targetEventId = value.value("targetEventId", -1);
}

void WriteBinding(json& value, const CinematicObjectBinding& binding) {
    value["targetName"] = binding.targetName;
    value["targetEventId"] = binding.targetEventId;
}
}

void CinematicSequence::Clear() {
    version = kCurrentVersion;
    duration = 0.0f;
    transformTracks.clear();
    vfxTracks.clear();
    events.clear();
}

void CinematicSequence::Sort() {
    for (auto& track : transformTracks) {
        std::sort(track.keys.begin(), track.keys.end(), [](const CinematicTransformKey& a, const CinematicTransformKey& b) {
            return a.time < b.time;
        });
    }
    std::sort(events.begin(), events.end(), [](const CinematicEventMarker& a, const CinematicEventMarker& b) {
        return a.time < b.time;
    });
}

float CinematicSequence::GetAuthoredDuration() const {
    float result = std::max(0.0f, duration);
    for (const auto& track : transformTracks) {
        if (!track.keys.empty()) {
            result = std::max(result, track.startTime + std::max(0.0f, track.keys.back().time));
        }
    }
    for (const auto& track : vfxTracks) {
        result = std::max(result, track.startTime + std::max(0.1f, track.duration));
    }
    for (const auto& event : events) {
        result = std::max(result, event.time);
    }
    return std::max(result, 0.1f);
}

bool CinematicSequence::Save(const std::string& filePath) const {
    json root;
    root["version"] = kCurrentVersion;
    root["name"] = name;
    root["duration"] = duration;
    root["tracks"] = json::array();
    root["vfxCues"] = json::array();
    root["events"] = json::array();

    for (const auto& track : transformTracks) {
        json item;
        item["name"] = track.name;
        WriteBinding(item, track.binding);
        item["pathFileName"] = track.legacyPathFile;
        item["startTime"] = track.startTime;
        item["delayTime"] = track.startTime;
        item["enabled"] = track.enabled;
        item["muted"] = track.muted;
        item["locked"] = track.locked;
        item["relative"] = track.relative;
        item["holdLast"] = track.holdLast;
        item["keys"] = json::array();
        for (const auto& key : track.keys) {
            item["keys"].push_back({
                { "time", key.time },
                { "position", WriteVector3(key.position) },
                { "rotation", WriteVector3(key.rotation) },
                { "scale", WriteVector3(key.scale) },
                { "easingToNext", key.easingToNext }
            });
        }
        root["tracks"].push_back(item);
    }

    for (const auto& track : vfxTracks) {
        json item;
        item["name"] = track.name;
        WriteBinding(item, track.binding);
        item["sequenceName"] = track.sequenceName;
        item["startTime"] = track.startTime;
        item["delayTime"] = track.startTime;
        item["duration"] = track.duration;
        item["enabled"] = track.enabled;
        item["muted"] = track.muted;
        root["vfxCues"].push_back(item);
    }

    for (const auto& marker : events) {
        json item;
        item["time"] = marker.time;
        item["eventId"] = marker.eventId;
        item["name"] = marker.name;
        WriteBinding(item, marker.binding);
        root["events"].push_back(item);
    }

    const std::filesystem::path path(filePath);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    file << root.dump(4);
    return file.good();
}

bool CinematicSequence::Load(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    json root;
    try {
        file >> root;
    } catch (...) {
        return false;
    }

    try {
        Clear();
        version = root.value("version", 1);
        name = root.value("name", std::filesystem::path(filePath).stem().string());
        duration = root.value("duration", 0.0f);

        const json transformItems = root.value("tracks", json::array());
        for (const auto& item : transformItems) {
            CinematicTransformTrack track;
            ReadBinding(item, track.binding);
            track.name = item.value("name", track.binding.targetName);
            track.legacyPathFile = item.value("pathFileName", "");
            track.startTime = item.value("startTime", item.value("delayTime", 0.0f));
            track.enabled = item.value("enabled", true);
            track.muted = item.value("muted", false);
            track.locked = item.value("locked", false);
            track.relative = item.value("relative", false);
            track.holdLast = item.value("holdLast", true);
            for (const auto& keyItem : item.value("keys", json::array())) {
                CinematicTransformKey key;
                key.time = keyItem.value("time", 0.0f);
                key.position = ReadVector3(keyItem.value("position", json::array()), key.position);
                key.rotation = ReadVector3(keyItem.value("rotation", json::array()), key.rotation);
                key.scale = ReadVector3(keyItem.value("scale", json::array()), key.scale);
                key.easingToNext = keyItem.value("easingToNext", 4);
                track.keys.push_back(key);
            }
            transformTracks.push_back(track);
        }

        const json vfxItems = root.value("vfxCues", root.value("vfxTracks", json::array()));
        for (const auto& item : vfxItems) {
            CinematicVFXTrackData track;
            ReadBinding(item, track.binding);
            track.name = item.value("name", item.value("sequenceName", "VFX"));
            track.sequenceName = item.value("sequenceName", "");
            track.startTime = item.value("startTime", item.value("delayTime", 0.0f));
            track.duration = item.value("duration", 0.1f);
            track.enabled = item.value("enabled", true);
            track.muted = item.value("muted", false);
            vfxTracks.push_back(track);
        }

        for (const auto& item : root.value("events", json::array())) {
            CinematicEventMarker marker;
            marker.time = item.value("time", 0.0f);
            marker.eventId = item.value("eventId", 0);
            marker.name = item.value("name", "");
            ReadBinding(item, marker.binding);
            events.push_back(marker);
        }
    } catch (...) {
        Clear();
        return false;
    }

    Sort();
    return true;
}
