#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

enum class GameplayEventTracePhase {
    Requested,
    Started,
    Fired,
    Completed,
    Cancelled,
    Warning,
    Failed,
};

struct GameplayEventTraceEntry {
    std::uint64_t sequence = 0;
    double timestampSeconds = 0.0;
    GameplayEventTracePhase phase = GameplayEventTracePhase::Requested;
    std::string category;
    std::string source;
    std::string asset;
    std::string detail;
};

// Animation EventからVFX、Audio、Camera等へ渡る演出イベントを時系列で記録します。
class GameplayEventTrace {
public:
    static GameplayEventTrace* GetInstance();

    void Record(
        GameplayEventTracePhase phase,
        std::string category,
        std::string source,
        std::string asset,
        std::string detail = {});
    void Clear();

    void Open() { isOpen_ = true; }
    bool IsOpen() const { return isOpen_; }
    void SetCaptureEnabled(bool enabled) { captureEnabled_.store(enabled); }
    bool IsCaptureEnabled() const { return captureEnabled_.load(); }

    void DrawImGui();
    static const char* GetPhaseName(GameplayEventTracePhase phase);

private:
    GameplayEventTrace();

    static constexpr std::size_t kMaxEntries = 1024;
    std::chrono::steady_clock::time_point startTime_;
    mutable std::mutex mutex_;
    std::deque<GameplayEventTraceEntry> entries_;
    std::uint64_t nextSequence_ = 1;
    std::atomic_bool captureEnabled_ = true;
    bool isOpen_ = false;
    bool autoScroll_ = true;
    char filter_[128]{};
    std::uint64_t selectedSequence_ = 0;
};
