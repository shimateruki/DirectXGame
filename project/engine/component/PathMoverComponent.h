#pragma once

#include "ObjectComponent.h"

#include <string>

// PathMoverComponentは、Ghost Recorderで再生するPath設定を所有します。
class PathMoverComponent final : public ObjectComponent {
public:
    static constexpr std::string_view kTypeId = "PathMover";

    std::string_view GetTypeId() const override { return kTypeId; }

    const std::string& GetPathName() const { return pathName_; }
    void SetPathName(const std::string& name) { pathName_ = name; }

    bool IsLoop() const { return loop_; }
    void SetLoop(bool loop) { loop_ = loop; }

    bool IsRelative() const { return relative_; }
    void SetRelative(bool relative) { relative_ = relative; }

private:
    std::string pathName_;
    bool loop_ = false;
    bool relative_ = false;
};
