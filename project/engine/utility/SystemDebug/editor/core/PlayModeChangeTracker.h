#pragma once

#ifdef USE_IMGUI

#include "json.hpp"

#include <string>
#include <unordered_map>
#include <vector>

class DebugEditor;
class Object3d;
class SceneManager;

// PlayModeChangeTrackerは、Play中の編集可能な変更だけを停止後に選択して持ち帰ります。
class PlayModeChangeTracker {
public:
    // SceneManager と DebugEditor を借用する。両者より先に Finalize する。
    void Initialize(SceneManager* sceneManager, DebugEditor* editor);
    void Finalize();
    // Play 開始時の状態を GUID ごとに保持し、終了時に実行中との差分を候補化する。
    void CaptureBaseline();
    void CaptureRuntimeChanges();
    // シーン復元に失敗した場合は、持ち帰り候補も破棄する。
    void OnSceneRestored(bool restored);
    // 停止後に残った候補の取捨選択 UI を描く。選択したものだけ編集シーンへ適用する。
    void Draw();
    std::size_t GetPendingChangeCount() const { return candidates_.size(); }
    void ShowPendingChanges() { open_ = !candidates_.empty(); }

private:
    // Play 開始前の比較基準。終了後の差分候補を作るため、名前とクラスと保存可能な状態を控える。
    struct BaselineEntry {
        std::string name;
        std::string className;
        nlohmann::json state;
    };

    // Play 前後の差分と、停止後に持ち帰る項目の選択。検出しただけでは編集シーンへ適用しない。
    struct ChangeCandidate {
        std::string guid;
        std::string name;
        std::string className;
        nlohmann::json beforeState;
        nlohmann::json runtimeState;
        bool transformChanged = false;
        bool propertiesChanged = false;
        bool applyTransform = true;
        bool applyProperties = true;
    };

    bool IsEligible(const Object3d* object) const;
    bool IsRuntimeDrivenTransform(const Object3d* object) const;
    static nlohmann::json ExtractTransform(const nlohmann::json& state);
    static nlohmann::json ExtractProperties(const nlohmann::json& state);
    static bool IsIdentityKey(const std::string& key);
    static bool IsTransformKey(const std::string& key);
    void ApplySelectedChanges();
    void Clear();

private:
    SceneManager* sceneManager_ = nullptr;
    DebugEditor* editor_ = nullptr;
    std::unordered_map<std::string, BaselineEntry> baselineByGuid_;
    std::vector<ChangeCandidate> candidates_;
    bool open_ = false;
    std::string statusMessage_;
};

#endif
