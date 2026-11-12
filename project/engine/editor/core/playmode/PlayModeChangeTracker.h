#pragma once

#ifdef USE_IMGUI

#include "json.hpp"

#include <string>
#include <unordered_map>
#include <vector>

class DebugEditor;
namespace engine { class Object3D; }
class SceneManager;

// 担当：Play前後の編集可能な差分を抽出し、停止後に選択して適用する。
// 関連：PlayModeSnapshot（元の状態復元）、SceneManager / DebugEditor（対象と編集）
// 主な入口：CaptureBaseline / CaptureRuntimeChanges / OnSceneRestored / Draw
class PlayModeChangeTracker {
public:
    // SceneManager と DebugEditor を借用する。両者より先に Finalize する。
    void Initialize(SceneManager* sceneManager, DebugEditor* editor);

    // 追跡中の状態とシーン参照を解除する。
    void Finalize();

    // Play 開始時の状態を GUID ごとに保持し、終了時に実行中との差分を候補化する。
    void CaptureBaseline();

    // 停止前の実行状態から候補を作る。新規生成物ではなく、開始時に記録したGUIDだけ比較する。
    void CaptureRuntimeChanges();

    // シーン復元に失敗した場合は、持ち帰り候補も破棄する。
    void OnSceneRestored(bool restored);

    // 停止後に残った候補の取捨選択 UI を描く。選択したものだけ編集シーンへ適用する。
    void Draw();

    std::size_t GetPendingChangeCount() const { return candidates_.size(); }

    // 停止後に適用できる差分がある場合に、選択画面を開く。
    void ShowPendingChanges() { open_ = !candidates_.empty(); }

private:
    // Play 開始前の比較基準。終了後の差分候補を作るため、名前とクラスと保存可能な状態を控える。
    // 関連：PlayModeChangeTracker（この型を使う機能の本体）
    struct BaselineEntry {
        std::string name;
        std::string className;
        nlohmann::json state;
    };

    // Play 前後の差分と、停止後に持ち帰る項目の選択。検出しただけでは編集シーンへ適用しない。
    // 関連：PlayModeChangeTracker（この型を使う機能の本体）
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

    bool IsEligible(const engine::Object3D* object) const;

    bool IsRuntimeDrivenTransform(const engine::Object3D* object) const;

    // 保存状態から、差分比較用の姿勢項目だけを取り出す。
    static nlohmann::json ExtractTransform(const nlohmann::json& state);

    // 識別子とTransformを除き、持ち帰り対象のプロパティだけを比較用に抽出する。
    static nlohmann::json ExtractProperties(const nlohmann::json& state);

    static bool IsIdentityKey(const std::string& key);

    static bool IsTransformKey(const std::string& key);

    // 復元後の編集シーンへ選択項目を反映する。識別子は保持し、操作をUndo履歴へ登録する。
    void ApplySelectedChanges();

    // 比較基準・差分候補と選択画面の状態を初期化する。
    void Clear();

private:
    SceneManager* sceneManager_ = nullptr; // 現在シーンの取得元。借用参照。
    DebugEditor* editor_ = nullptr;
    std::unordered_map<std::string, BaselineEntry> baselineByGuid_;
    std::vector<ChangeCandidate> candidates_;
    bool open_ = false;
    std::string statusMessage_;
};

#endif
