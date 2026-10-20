#pragma once

#include "ObjectComponent.h"

#include <string>

/// <summary>Ghost Recorderで再生する経路名と再生方法を保持する。</summary>
class PathMoverComponent final : public ObjectComponent {
public:
    static constexpr std::string_view kTypeId = "PathMover";

    /// <summary>保存と型判定に使うComponent種別を返す。</summary>
    std::string_view GetTypeId() const override { return kTypeId; }

    // 排他的な再生条件。旧データは常時再生として扱う。
    enum class PlaybackMode { Always, WhileRiding, RideToEnd };
    PlaybackMode GetPlaybackMode() const { return playbackMode_; }
    void SetPlaybackMode(PlaybackMode mode) { playbackMode_ = mode; }
    std::string GetPlaybackModeName() const {
        switch (playbackMode_) {
        case PlaybackMode::WhileRiding: return "whileRiding";
        case PlaybackMode::RideToEnd: return "rideToEnd";
        default: return "always";
        }
    }
    void SetPlaybackModeName(const std::string& name) {
        // 不明な値も旧版と同じ動作へ戻す。
        playbackMode_ = name == "whileRiding" ? PlaybackMode::WhileRiding :
            name == "rideToEnd" ? PlaybackMode::RideToEnd : PlaybackMode::Always;
    }

    // 経路名、ループ、相対座標再生の設定。
    const std::string& GetPathName() const { return pathName_; }

    void SetPathName(const std::string& name) { pathName_ = name; }

    bool IsLoop() const { return loop_; }

    void SetLoop(bool loop) { loop_ = loop; }

    bool IsRelative() const { return relative_; }

    void SetRelative(bool relative) { relative_ = relative; }

    // ループの開始位置を周期の割合でずらし、同じパスを使う障害物の動きを揃えすぎない。
    float GetPhaseOffset() const { return phaseOffset_; }
    void SetPhaseOffset(float phase) { phaseOffset_ = phase; }

    // 乗車型の床は演出タイムラインを実行する。空なら従来のPath再生を維持する。
    const std::string& GetRideSequence() const { return rideSequence_; }

    void SetRideSequence(const std::string& path) { rideSequence_ = path; }

private:
    PlaybackMode playbackMode_ = PlaybackMode::Always;
    std::string pathName_;
    bool loop_ = false;
    bool relative_ = false;
    float phaseOffset_ = 0.0f;
    std::string rideSequence_;
};
