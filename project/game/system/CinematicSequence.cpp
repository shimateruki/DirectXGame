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
    cameraShots.clear();
    animationClips.clear();
    audioClips.clear();
    signals.clear();
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
    std::sort(cameraShots.begin(), cameraShots.end(), [](const CinematicCameraShot& a, const CinematicCameraShot& b) {
        return a.startTime < b.startTime;
    });
    std::sort(animationClips.begin(), animationClips.end(), [](const CinematicAnimationClipData& a, const CinematicAnimationClipData& b) {
        return a.startTime < b.startTime;
    });
    std::sort(audioClips.begin(), audioClips.end(), [](const CinematicAudioClipData& a, const CinematicAudioClipData& b) {
        return a.startTime < b.startTime;
    });
    std::sort(signals.begin(), signals.end(), [](const CinematicSignalMarker& a, const CinematicSignalMarker& b) {
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
    for (const auto& shot : cameraShots) {
        result = std::max(result, shot.startTime + std::max(0.05f, shot.duration));
    }
    for (const auto& clip : animationClips) {
        result = std::max(result, clip.startTime + std::max(0.05f, clip.duration));
    }
    for (const auto& clip : audioClips) {
        result = std::max(result, clip.startTime + std::max(0.05f, clip.duration));
    }
    for (const auto& signal : signals) {
        result = std::max(result, signal.time);
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
    root["cameraShots"] = json::array();
    root["animationClips"] = json::array();
    root["audioClips"] = json::array();
    root["signals"] = json::array();
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

    for (const auto& shot : cameraShots) {
        json item;
        item["name"] = shot.name;
        WriteBinding(item, shot.binding);
        item["startTime"] = shot.startTime;
        item["duration"] = shot.duration;
        item["blendInDuration"] = shot.blendInDuration;
        item["blendOutDuration"] = shot.blendOutDuration;
        item["easing"] = shot.easing;
        item["enabled"] = shot.enabled;
        item["muted"] = shot.muted;
        root["cameraShots"].push_back(item);
    }

    for (const auto& clip : animationClips) {
        json item;
        item["name"] = clip.name;
        WriteBinding(item, clip.binding);
        item["driver"] = clip.driver;
        item["clipName"] = clip.clipName;
        item["startTime"] = clip.startTime;
        item["duration"] = clip.duration;
        item["playbackSpeed"] = clip.playbackSpeed;
        item["blendInDuration"] = clip.blendInDuration;
        item["easing"] = clip.easing;
        item["loop"] = clip.loop;
        item["restoreOnStop"] = clip.restoreOnStop;
        item["enabled"] = clip.enabled;
        item["muted"] = clip.muted;
        root["animationClips"].push_back(item);
    }

    for (const auto& clip : audioClips) {
        root["audioClips"].push_back({
            { "name", clip.name },
            { "audioId", clip.audioId },
            { "startTime", clip.startTime },
            { "duration", clip.duration },
            { "volume", clip.volume },
            { "loop", clip.loop },
            { "enabled", clip.enabled },
            { "muted", clip.muted }
        });
    }

    for (const auto& signal : signals) {
        json item;
        item["time"] = signal.time;
        item["name"] = signal.name;
        item["signal"] = signal.signal;
        item["payload"] = signal.payload;
        item["enabled"] = signal.enabled;
        WriteBinding(item, signal.binding);
        root["signals"].push_back(item);
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

        for (const auto& item : root.value("cameraShots", json::array())) {
            CinematicCameraShot shot;
            ReadBinding(item, shot.binding);
            shot.name = item.value("name", item.value("targetName", "Camera Shot"));
            shot.startTime = item.value("startTime", 0.0f);
            shot.duration = item.value("duration", 1.0f);
            shot.blendInDuration = item.value("blendInDuration", 0.3f);
            shot.blendOutDuration = item.value("blendOutDuration", 0.3f);
            shot.easing = item.value("easing", 4);
            shot.enabled = item.value("enabled", true);
            shot.muted = item.value("muted", false);
            cameraShots.push_back(shot);
        }

        for (const auto& item : root.value("animationClips", json::array())) {
            CinematicAnimationClipData clip;
            ReadBinding(item, clip.binding);
            clip.name = item.value("name", "Animation Clip");
            clip.driver = item.value("driver", "Model");
            clip.clipName = item.value("clipName", "");
            clip.startTime = item.value("startTime", 0.0f);
            clip.duration = item.value("duration", 1.0f);
            clip.playbackSpeed = item.value("playbackSpeed", 1.0f);
            clip.blendInDuration = item.value("blendInDuration", 0.12f);
            clip.easing = item.value("easing", 4);
            clip.loop = item.value("loop", false);
            clip.restoreOnStop = item.value("restoreOnStop", true);
            clip.enabled = item.value("enabled", true);
            clip.muted = item.value("muted", false);
            animationClips.push_back(clip);
        }

        for (const auto& item : root.value("audioClips", json::array())) {
            CinematicAudioClipData clip;
            clip.name = item.value("name", "Audio Clip");
            clip.audioId = item.value("audioId", "");
            clip.startTime = item.value("startTime", 0.0f);
            clip.duration = item.value("duration", 0.1f);
            clip.volume = item.value("volume", 1.0f);
            clip.loop = item.value("loop", false);
            clip.enabled = item.value("enabled", true);
            clip.muted = item.value("muted", false);
            audioClips.push_back(clip);
        }

        for (const auto& item : root.value("signals", json::array())) {
            CinematicSignalMarker signal;
            signal.time = item.value("time", 0.0f);
            signal.name = item.value("name", "Signal");
            signal.signal = item.value("signal", "");
            signal.payload = item.value("payload", "");
            signal.enabled = item.value("enabled", true);
            ReadBinding(item, signal.binding);
            signals.push_back(signal);
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
