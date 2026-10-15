#pragma once
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <array>
#include <memory>
#include <mutex>

namespace engine { class Object3d; }

#ifdef USE_IMGUI
#include "imgui.h"
#endif

/// <summary>
/// ロード時間とCPU/GPU計測値を集計し、デバッグ画面へ表示する。
/// </summary>
class ProfilerManager {
public:
    /// <summary>共有の計測マネージャを取得する。</summary>
    static ProfilerManager* GetInstance();

    /// <summary>計測履歴と表示状態を初期化する。</summary>
    void Initialize();

    /// <summary>カテゴリ別のロード所要時間を記録する。</summary>
    /// <param name="category">集計先カテゴリ。</param>
    /// <param name="name">計測した処理の名前。</param>
    /// <param name="timeMs">所要時間（ミリ秒）。</param>
    void RecordLoadTime(const std::string& category, const std::string& name, float timeMs);

    /// <summary>GPU処理の所要時間を履歴へ記録する。</summary>
    void RecordGpuTime(const std::string& name, float timeMs);

    /// <summary>CPU処理の所要時間を履歴へ記録する。</summary>
    void RecordCpuTime(const std::string& name, float timeMs);

    /// <summary>指定したGPU計測の最新値をミリ秒で返す。</summary>
    float GetLatestGpuTime(const std::string& name) const;

    /// <summary>指定したCPU計測の最新値をミリ秒で返す。</summary>
    float GetLatestCpuTime(const std::string& name) const;

    /// <summary>ImGuiが有効な場合に計測画面を描画する。</summary>
    void DrawImGui();

    /// <summary>計測画面を開く。</summary>
    void Open() { isOpen_ = true; }

    /// <summary>計測画面を閉じる。</summary>
    void Close() { isOpen_ = false; }

    /// <summary>計測画面が開いているかを返す。</summary>
    bool IsOpen() const { return isOpen_; }

    /// <summary>オブジェクト別計測の表示対象を設定する。</summary>
    /// <remarks>リストは所有しないため、表示中は呼び出し側で保持する。</remarks>
    void SetObjectList(const std::vector<std::unique_ptr<engine::Object3d>>* objects) { currentObjects_ = objects; }

    /// <summary>GPU計測のサンプリング中かを返す。</summary>
    bool IsGpuSampling() const { return isGpuSampling_; }

    /// <summary>GPU計測を開始し、前回の集計値を破棄する。</summary>
    void StartGpuSampling() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        isGpuSampling_ = true;
        remainingSamplingFrames_ = kMaxSamplingFrames;
        gpuSampleAccum_.clear();
        gpuSampleCount_.clear();
    }

private:
    ProfilerManager() = default;
    ~ProfilerManager() = default;
    ProfilerManager(const ProfilerManager&) = delete;
    ProfilerManager& operator=(const ProfilerManager&) = delete;

    /// <summary>単一の読み込み処理名と所要時間。</summary>
    struct LoadData {
        std::string name;
        float timeMs;
    };

    // 直近フレームの計測値をリングバッファに保持する。
    static const int kHistorySize = 120;

    /// <summary>現在値、平滑化値、直近フレームの履歴を保持する。</summary>
    struct TimelineData {
        float current = 0.0f;        // 今フレームの生値
        float smoothed = 0.0f;       // スムージング後の値
        std::array<float, kHistorySize> history = {}; // 履歴リングバッファ
        int historyIndex = 0;
    };

    std::map<std::string, std::vector<LoadData>> loadDataMap_;
    std::map<std::string, TimelineData> gpuDataMap_;
    std::map<std::string, TimelineData> cpuDataMap_;
    std::map<std::string, float> latestGpuTimeMap_;
    mutable std::recursive_mutex mutex_;
    const std::vector<std::unique_ptr<engine::Object3d>>* currentObjects_ = nullptr;
    bool isOpen_ = false;
    int selectedIndex_ = 0;

    // 複数フレームのGPU計測値から平均を求める際の一時状態。
    bool isGpuSampling_ = false;
    int remainingSamplingFrames_ = 0;
    const int kMaxSamplingFrames = 60;
    std::map<std::string, float> gpuSampleAccum_;
    std::map<std::string, int> gpuSampleCount_;
    std::map<std::string, float> gpuSampleResult_;
};

/// <summary>スコープを抜けるまでのCPU所要時間を自動で記録する。</summary>
class ScopedCpuProfiler {
public:
    ScopedCpuProfiler(const char* name) : name_(name) {
        start_ = std::chrono::high_resolution_clock::now();
    }
    ~ScopedCpuProfiler() {
        auto end = std::chrono::high_resolution_clock::now();
        float ms = std::chrono::duration<float, std::milli>(end - start_).count();
        ProfilerManager::GetInstance()->RecordCpuTime(name_, ms);
    }
private:
    const char* name_;
    std::chrono::high_resolution_clock::time_point start_;
};

// 例: PROFILE_SCOPE("Scene Update"); __LINE__ を展開して呼び出し位置ごとに変数を作る。
#define PROFILE_SCOPE_CONCAT_IMPL(a, b) a##b
#define PROFILE_SCOPE_CONCAT(a, b) PROFILE_SCOPE_CONCAT_IMPL(a, b)
#define PROFILE_SCOPE(name) ScopedCpuProfiler PROFILE_SCOPE_CONCAT(_profiler_, __LINE__)(name)
