#pragma once
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <array>
#include <memory>
#include <mutex>

class Object3d;

#ifdef USE_IMGUI
#include "imgui.h"
#endif

/// <summary>
/// システム全体のパフォーマンスやロード時間を計測・可視化するマネージャ
/// </summary>
class ProfilerManager {
public:
    static ProfilerManager* GetInstance();

    void Initialize();

    /// <summary>
    /// ロード時間の記録
    /// </summary>
    void RecordLoadTime(const std::string& category, const std::string& name, float timeMs);
    void RecordGpuTime(const std::string& name, float timeMs);
    void RecordCpuTime(const std::string& name, float timeMs);

    /// <summary>
    /// ImGui描画
    /// </summary>
    void DrawImGui();

    // メニューバーからの操作用
    void Open() { isOpen_ = true; }
    void Close() { isOpen_ = false; }
    bool IsOpen() const { return isOpen_; }

private:
    ProfilerManager() = default;
    ~ProfilerManager() = default;
    ProfilerManager(const ProfilerManager&) = delete;
    ProfilerManager& operator=(const ProfilerManager&) = delete;

    struct LoadData {
        std::string name;
        float timeMs;
    };

    // フレーム履歴付きの計測データ
    static const int kHistorySize = 120;
    struct TimelineData {
        float current = 0.0f;        // 今フレームの生値
        float smoothed = 0.0f;       // スムージング後の値
        std::array<float, kHistorySize> history = {}; // 履歴リングバッファ
        int historyIndex = 0;
    };

    std::map<std::string, std::vector<LoadData>> loadDataMap_;
    std::map<std::string, TimelineData> gpuDataMap_;
    std::map<std::string, TimelineData> cpuDataMap_;
    mutable std::recursive_mutex mutex_;
    const std::vector<std::unique_ptr<Object3d>>* currentObjects_ = nullptr;
    bool isOpen_ = false;
    int selectedIndex_ = 0;

    // --- GPUサンプリング用 ---
    bool isGpuSampling_ = false;
    int remainingSamplingFrames_ = 0;
    const int kMaxSamplingFrames = 60;
    std::map<std::string, float> gpuSampleAccum_; // 累積時間
    std::map<std::string, int> gpuSampleCount_;   // サンプル数
    std::map<std::string, float> gpuSampleResult_; // 最終平均結果

public:
    void SetObjectList(const std::vector<std::unique_ptr<Object3d>>* objects) { currentObjects_ = objects; }
    bool IsGpuSampling() const { return isGpuSampling_; }
    void StartGpuSampling() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        isGpuSampling_ = true;
        remainingSamplingFrames_ = kMaxSamplingFrames;
        gpuSampleAccum_.clear();
        gpuSampleCount_.clear();
    }
};

// ============================================================
// スコープ計測ヘルパー
// 使い方: 関数やブロックの先頭に PROFILE_SCOPE("名前"); を書くだけ
// スコープを抜けるとき自動でProfilerManagerに記録される
// ============================================================
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

// マクロ：1行で計測を仕込める
// 例: PROFILE_SCOPE("Scene Update");
#define PROFILE_SCOPE(name) ScopedCpuProfiler _profiler_##__LINE__(name)
