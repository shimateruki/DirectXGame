#pragma once

#include "json.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// シーン初期化より前に、ワーカースレッドで読み込めるCPU側の情報を列挙します。
struct SceneLoadManifest {
    struct TextureRequest {
        std::string path;
        bool linear = false;
    };

    std::vector<std::string> objectLayoutPaths;
    std::vector<std::string> spriteLayoutPaths;
    std::vector<std::string> additionalJsonPaths;
    std::vector<std::string> modelNames;
    std::vector<TextureRequest> textures;
    std::vector<std::string> audioPaths;

    void AddObjectLayout(const std::string& path);
    void AddSpriteLayout(const std::string& path);
    void AddJson(const std::string& path);
    void AddModel(const std::string& modelName);
    void AddTexture(const std::string& path, bool linear = false);
    void AddAudio(const std::string& path);
};
enum class SceneDependencyType {
    Json,
    Model,
    Texture,
    Audio,
    Other,
};

struct SceneDependencyRecord {
    SceneDependencyType type = SceneDependencyType::Other;
    std::string path;
    bool exists = true;
};

struct SceneDependencyReport {
    std::vector<SceneDependencyRecord> assets;
    std::vector<std::string> missingPaths;

    std::size_t Count(SceneDependencyType type) const;
};

// LoadingSceneから安全に参照できる、ワーカースレッド側の進行状況です。
class ScenePreloadProgress {
public:
    void Begin(std::size_t totalTasks);
    void AddTasks(std::size_t taskCount);
    void Advance(const std::string& currentTask);
    void Finish();

    float GetRatio() const;
    std::string GetCurrentTask() const;

private:
    std::atomic<std::size_t> completedTasks_ = 0;
    std::atomic<std::size_t> totalTasks_ = 1;
    mutable std::mutex taskMutex_;
    std::string currentTask_;
};

// ワーカースレッドで準備し、メインスレッドのScene初期化へ引き渡すデータです。
class ScenePreloadData {
public:
    bool TakeJson(const std::string& path, nlohmann::json& destination);

    std::unordered_map<std::string, nlohmann::json> jsonDocuments;
    std::vector<std::string> modelNames;
    std::vector<SceneLoadManifest::TextureRequest> textures;
    std::vector<std::string> audioPaths;
    SceneDependencyReport dependencyReport;
    std::vector<std::string> warnings;
};

// ファイルI/OとJSON解析だけをワーカースレッドで行います。
// DirectXリソース生成はメインスレッド側で少量ずつ処理します。
class ScenePreloader {
public:
    static std::shared_ptr<ScenePreloadData> Prepare(
        const SceneLoadManifest& manifest,
        const std::shared_ptr<ScenePreloadProgress>& progress);
    static SceneDependencyReport Inspect(const SceneLoadManifest& manifest);
};
