#pragma once

#include "ObjectComponent.h"

#include <string>

// 担当：記録経路の名前、ループ・相対再生・乗車条件を保持する。
// 関連：ObjectComponent（共通型）、Object3D（保持）、GhostRecorder（経路再生）
// 主な入口：GetPathName / SetPathName / GetPlaybackMode
class PathMoverComponent final : public ObjectComponent {
public:
    static constexpr std::string_view kTypeId = "PathMover"; // 保存と実行時の型判定に使う固定ID。

    /// <summary>保存と型判定に使うComponent種別を返す。</summary>
    std::string_view GetTypeId() const override { return kTypeId; }

    // 排他的な再生条件。旧データは常時再生として扱う。
    // Alwaysは常時、WhileRidingは乗車中だけ、RideToEndは乗車を契機に終端まで進む指定。
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
    // 値を保持するだけで、ここでは0〜1への制限や経路の再計算をしない。
    void SetPhaseOffset(float phase) { phaseOffset_ = phase; }

    // 乗車型の床は演出タイムラインを実行する。空なら従来のPath再生を維持する。
    const std::string& GetRideSequence() const { return rideSequence_; }

    void SetRideSequence(const std::string& path) { rideSequence_ = path; }

private:
    PlaybackMode playbackMode_ = PlaybackMode::Always; // 常時・乗車中・乗車後終端までの再生条件。
    std::string pathName_; // 記録経路の名前。空なら未設定。
    bool loop_ = false; // 経路の終端から先頭へ戻すか。
    bool relative_ = false; // 記録経路を基準姿勢に対する相対座標で使うか。
    float phaseOffset_ = 0.0f; // ループ開始位置の割合。保持時には範囲制限しない。
    std::string rideSequence_; // 乗車時の演出シーケンス。空なら通常の経路再生。
};
