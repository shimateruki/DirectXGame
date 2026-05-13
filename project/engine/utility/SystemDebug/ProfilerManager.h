#pragma once
#include <string>
#include <vector>
#include <map>
#include <chrono>

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
    /// <param name="category">カテゴリ（"Sprite", "Model" など）</param>
    /// <param name="name">ファイル名など</param>
    /// <param name="timeMs">かかった時間(ms)</param>
    void RecordLoadTime(const std::string& category, const std::string& name, float timeMs);

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

    std::map<std::string, std::vector<LoadData>> loadDataMap_;
    bool isOpen_ = false;
    int selectedIndex_ = 0; // 現在選択されているタブのインデックス
};
