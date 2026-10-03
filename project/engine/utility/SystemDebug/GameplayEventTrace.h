#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

/// <summary>演出イベントが処理経路のどこまで進んだかを表す。</summary>
enum class GameplayEventTracePhase {
    Requested,
    Started,
    Fired,
    Completed,
    Cancelled,
    Warning,
    Failed,
};

/// <summary>一つの演出イベントに関する時刻と発生元の記録。</summary>
struct GameplayEventTraceEntry {
    std::uint64_t sequence = 0;
    double timestampSeconds = 0.0;
    GameplayEventTracePhase phase = GameplayEventTracePhase::Requested;
    std::string category;
    std::string source;
    std::string asset;
    std::string detail;
};

/// <summary>Animation EventからVFX・音声・カメラへ渡る処理を時系列で記録する。</summary>
class GameplayEventTrace {
public:
    /// <summary>共有のイベント記録インスタンスを取得する。</summary>
    static GameplayEventTrace* GetInstance();

    /// <summary>演出イベントの段階と発生元を時刻付きで記録する。</summary>
    /// <param name="phase">処理の段階。</param>
    /// <param name="category">イベントの種類。</param>
    /// <param name="source">発火元。</param>
    /// <param name="asset">対象の演出アセット。</param>
    /// <param name="detail">補足情報。</param>
    void Record(
        GameplayEventTracePhase phase,
        std::string category,
        std::string source,
        std::string asset,
        std::string detail = {});

    /// <summary>記録を破棄し、計測の基準時刻を戻す。</summary>
    void Clear();

    /// <summary>イベント記録画面を開く。</summary>
    void Open() { isOpen_ = true; }

    /// <summary>イベント記録画面が開いているかを返す。</summary>
    bool IsOpen() const { return isOpen_; }

    /// <summary>イベントの記録を有効・無効にする。</summary>
    void SetCaptureEnabled(bool enabled) { captureEnabled_.store(enabled); }

    /// <summary>イベントの記録が有効かを返す。</summary>
    bool IsCaptureEnabled() const { return captureEnabled_.load(); }

    /// <summary>ImGuiが有効な場合に記録画面を描画する。</summary>
    void DrawImGui();

    /// <summary>処理段階の表示名を返す。</summary>
    static const char* GetPhaseName(GameplayEventTracePhase phase);

private:
    GameplayEventTrace();

    // 記録数を制限し、長時間のプレイでもメモリを増やし続けない。
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
