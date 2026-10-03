#pragma once

#include "ObjectComponent.h"

#include <string>

/// <summary>Ghost Recorderで再生する経路名と再生方法を保持する。</summary>
class PathMoverComponent final : public ObjectComponent {
public:
    static constexpr std::string_view kTypeId = "PathMover";

    /// <summary>保存と型判定に使うComponent種別を返す。</summary>
    std::string_view GetTypeId() const override { return kTypeId; }

    // 経路名、ループ、相対座標再生の設定。
    const std::string& GetPathName() const { return pathName_; }
    void SetPathName(const std::string& name) { pathName_ = name; }

    bool IsLoop() const { return loop_; }
    void SetLoop(bool loop) { loop_ = loop; }

    bool IsRelative() const { return relative_; }
    void SetRelative(bool relative) { relative_ = relative; }

    // 乗車型の床は演出タイムラインを実行する。空なら従来のPath再生を維持する。
    const std::string& GetRideSequence() const { return rideSequence_; }
    void SetRideSequence(const std::string& path) { rideSequence_ = path; }

private:
    std::string pathName_;
    bool loop_ = false;
    bool relative_ = false;
    std::string rideSequence_;
};
