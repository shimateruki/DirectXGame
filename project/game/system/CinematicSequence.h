#pragma once

#include "engine/utility/math/Math.h"

#include <string>
#include <vector>
#include <array>

// Timeline上のトラックとシーンObjectを、名前またはイベントIDで結び付けます。
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
    bool anchorRelative = false;
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

// Camera Objectを使用する1つのカメラカットです。
// カメラ自身の追従・注視設定はCamera Object側、カット固有の時間とBlendはTimeline側で管理します。
struct CinematicCameraShot {
    std::string name;
    CinematicObjectBinding binding;
    float startTime = 0.0f;
    float duration = 1.0f;
    float blendInDuration = 0.3f;
    float blendOutDuration = 0.3f;
    int easing = 4;
    bool enabled = true;
    bool muted = false;
};

// Runtime側のAnimation Driverへ渡すAnimation Clipです。
struct CinematicAnimationClipData {
    std::string name;
    CinematicObjectBinding binding;
    std::string driver;
    std::string clipName;
    float startTime = 0.0f;
    float duration = 1.0f;
    float playbackSpeed = 1.0f;
    float blendInDuration = 0.12f;
    int easing = 4;
    bool loop = false;
    bool restoreOnStop = true;
    bool enabled = true;
    bool muted = false;
};

struct CinematicAudioClipData {
    std::string name;
    std::string audioId;
    float startTime = 0.0f;
    float duration = 0.1f;
    float volume = 1.0f;
    bool loop = false;
    bool enabled = true;
    bool muted = false;
};

// シーン固有処理をTimelineの時刻へ接続する名前付きSignalです。
struct CinematicSignalMarker {
    float time = 0.0f;
    std::string name;
    std::string signal;
    std::string payload;
    CinematicObjectBinding binding;
    bool enabled = true;
};

struct CinematicEventMarker {
    float time = 0.0f;
    int eventId = 0;
    std::string name;
    CinematicObjectBinding binding;
};

class CinematicSequence {
public:
    static constexpr int kCurrentVersion = 6;

    void Clear();
    void Sort();
    float GetAuthoredDuration() const;
    bool Save(const std::string& filePath) const;
    bool Load(const std::string& filePath);

    int version = kCurrentVersion;
    std::string name = "cinematic_sequence";
    float duration = 0.0f;
    // ゲート等を共通原点にする。大きさや傾きは引き継がず、水平方向だけ使用する。
    CinematicObjectBinding anchor;
    float anchorYawOffset = 0.0f;
    // 役割名で作った演出を実シーンへ接続する。プレビュー専用Object名は保存しない。
    std::array<CinematicObjectBinding, 3> roleBindings;
    // 制作空間の仮モデル。ゲーム側の対象モデルは変更しない。
    std::array<std::string, 2> previewModels = { "Characters/slime_pink", "Primitives/cube" };
    std::vector<CinematicTransformTrack> transformTracks;
    std::vector<CinematicVFXTrackData> vfxTracks;
    std::vector<CinematicCameraShot> cameraShots;
    std::vector<CinematicAnimationClipData> animationClips;
    std::vector<CinematicAudioClipData> audioClips;
    std::vector<CinematicSignalMarker> signals;
    std::vector<CinematicEventMarker> events;
};

// エディターの軌跡表示・キー記録と再生器で同じ座標変換を使用する。
Vector3 CinematicAnchorPosition(const Vector3& local, const Vector3& origin, float yaw);
Vector3 CinematicAnchorLocalPosition(const Vector3& world, const Vector3& origin, float yaw);
