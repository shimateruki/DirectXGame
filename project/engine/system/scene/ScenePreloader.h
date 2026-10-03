#pragma once

#include "json.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/// <summary>シーン初期化前に必要なアセットを列挙します。</summary>
struct SceneLoadManifest {
    /// <summary>画像パスと、線形色空間で扱うかどうかの指定です。</summary>
    struct TextureRequest {
        std::string path;
        bool linear = false;
    };

    // レイアウト、明示的な依存アセットを種類ごとに保持します。Add 系関数は重複を除きます。
    std::vector<std::string> objectLayoutPaths;
    std::vector<std::string> spriteLayoutPaths;
    std::vector<std::string> additionalJsonPaths;
    std::vector<std::string> modelNames;
    std::vector<TextureRequest> textures;
    std::vector<std::string> audioPaths;

    /// <summary>オブジェクトレイアウトを追加します。分割 JSON も読み込み候補になります。</summary>
    void AddObjectLayout(const std::string& path);

    /// <summary>スプライトレイアウトを追加します。</summary>
    void AddSpriteLayout(const std::string& path);

    /// <summary>追加の JSON 依存ファイルを登録します。</summary>
    void AddJson(const std::string& path);

    /// <summary>事前準備するモデル名を登録します。</summary>
    void AddModel(const std::string& modelName);

    /// <summary>事前デコードする画像と色空間を登録します。</summary>
    void AddTexture(const std::string& path, bool linear = false);

    /// <summary>事前ロードする音声ファイルを登録します。</summary>
    void AddAudio(const std::string& path);
};

/// <summary>事前読み込みで調べる依存アセットの種類。</summary>
enum class SceneDependencyType {
    Json,
    Model,
    Texture,
    Audio,
    Other,
};

/// <summary>検出した依存アセットの種類、パス、存在状態です。</summary>
struct SceneDependencyRecord {
    SceneDependencyType type = SceneDependencyType::Other;
    std::string path;
    bool exists = true;
};

/// <summary>依存アセットと見つからなかったパスをまとめます。</summary>
struct SceneDependencyReport {
    std::vector<SceneDependencyRecord> assets;
    std::vector<std::string> missingPaths;

    /// <summary>指定種類の依存アセット数を返します。</summary>
    std::size_t Count(SceneDependencyType type) const;
};

/// <summary>ワーカースレッドと LoadingScene の間で共有する進行状況です。</summary>
class ScenePreloadProgress {
public:
    /// <summary>既知のタスク数で進捗を初期化します。</summary>
    /// <param name="totalTasks">開始時点で分かっているタスク数。</param>
    void Begin(std::size_t totalTasks);

    /// <summary>依存解析で後から見つかったタスクを総数へ加えます。</summary>
    void AddTasks(std::size_t taskCount);

    /// <summary>完了数を進め、現在処理中のパスを記録します。</summary>
    void Advance(const std::string& currentTask);

    /// <summary>すべてのタスクが完了した状態にします。</summary>
    void Finish();

    /// <summary>0～1 に収めた完了割合を返します。</summary>
    float GetRatio() const;

    /// <summary>最後に処理したタスク名のコピーを返します。</summary>
    std::string GetCurrentTask() const;

private:
    // 数値の更新は atomic、表示用の文字列は mutex で保護します。
    std::atomic<std::size_t> completedTasks_ = 0;
    std::atomic<std::size_t> totalTasks_ = 1;
    mutable std::mutex taskMutex_;
    std::string currentTask_;
};

/// <summary>ワーカー側で準備し、シーン初期化へ渡す CPU データです。</summary>
class ScenePreloadData {
public:
    /// <summary>読み込み済み JSON を取り出し、保持領域から削除します。</summary>
    /// <param name="path">読み込み時と同じ正規化対象のパス。</param>
    /// <param name="destination">見つかった JSON の移動先。</param>
    /// <returns>該当 JSON があれば true。</returns>
    bool TakeJson(const std::string& path, nlohmann::json& destination);

    // JSON は TakeJson で消費されます。その他の一覧はシーン側の段階的な初期化に渡します。
    std::unordered_map<std::string, nlohmann::json> jsonDocuments;
    std::vector<std::string> modelNames;
    std::vector<SceneLoadManifest::TextureRequest> textures;
    std::vector<std::string> audioPaths;
    SceneDependencyReport dependencyReport;
    std::vector<std::string> warnings;
};

/// <summary>シーン依存ファイルを調べ、ワーカーで可能な CPU 側の準備を行います。</summary>
/// <remarks>DirectX リソースの最終生成はメインスレッド側で行います。</remarks>
class ScenePreloader {
public:
    /// <summary>JSON の解析から参照アセットを辿り、画像・モデル・音声を事前準備します。</summary>
    /// <param name="manifest">シーンが明示した依存ファイル。</param>
    /// <param name="progress">進行状況の共有先。不要なら nullptr。</param>
    /// <returns>シーン初期化へ引き渡すデータと不足アセットの報告。</returns>
    static std::shared_ptr<ScenePreloadData> Prepare(
        const SceneLoadManifest& manifest,
        const std::shared_ptr<ScenePreloadProgress>& progress);

    /// <summary>依存ファイルを調べてCPU側の事前準備を行い、存在状態を報告します。</summary>
    /// <param name="manifest">調査対象の依存ファイル。</param>
    /// <returns>検出した依存アセットの一覧。</returns>
    static SceneDependencyReport Inspect(const SceneLoadManifest& manifest);
};
