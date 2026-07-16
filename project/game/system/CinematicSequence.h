#pragma once

#include "engine/utility/math/Math.h"

#include <string>
#include <vector>

// ムービー編集データをランタイム再生処理やImGuiから分離して保持します。
struct CinematicObjectBinding {
    std::string targetName;
    int targetEventId = -1;
};

struct CinematicTransformKey {
    float time = 0.0f;
    Vector3 position = { 0.0f, 0.0f, 0.0f };
    Vector3 rotation = { 0.0f, 0.0f, 0.0f };
    Vector3 scale = { 1.0f, 1.0f, 1.0f };
    int easingToNext = 4;
};

struct CinematicTransformTrack {
    std::string name;
    CinematicObjectBinding binding;
    std::string legacyPathFile;
    float startTime = 0.0f;
    bool enabled = true;
    bool muted = false;
    bool locked = false;
    bool relative = false;
    bool holdLast = true;
    std::vector<CinematicTransformKey> keys;
};

struct CinematicVFXTrackData {
    std::string name;
    CinematicObjectBinding binding;
    std::string sequenceName;
    float startTime = 0.0f;
    float duration = 0.1f;
    bool enabled = true;
    bool muted = false;
};

struct CinematicEventMarker {
    float time = 0.0f;
    int eventId = 0;
    std::string name;
    CinematicObjectBinding binding;
};

class CinematicSequence {
public:
    static constexpr int kCurrentVersion = 3;

    void Clear();
    void Sort();
    float GetAuthoredDuration() const;
    bool Save(const std::string& filePath) const;
    bool Load(const std::string& filePath);

    int version = kCurrentVersion;
    std::string name = "cinematic_sequence";
    float duration = 0.0f;
    std::vector<CinematicTransformTrack> transformTracks;
    std::vector<CinematicVFXTrackData> vfxTracks;
    std::vector<CinematicEventMarker> events;
};
