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
    void Initialize(SceneManager* sceneManager, DebugEditor* editor);
    void Finalize();
    void CaptureBaseline();
    void CaptureRuntimeChanges();
    void OnSceneRestored(bool restored);
    void Draw();

private:
    struct BaselineEntry {
        std::string name;
        std::string className;
        nlohmann::json state;
    };

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
