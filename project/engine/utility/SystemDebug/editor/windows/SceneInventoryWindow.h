#pragma once

#include "IEditable.h"

#include <string>
#include <vector>

class DebugEditor;
class Object3d;
class SceneManager;

/// <summary>
/// 現在のシーンに配置されているObjectを用途・種類・モデル・当たり判定別に集計するEditorウィンドウ。
/// </summary>
class SceneInventoryWindow : public IEditable {
public:
    /// <summary>現在シーンとエディターへの参照を設定します。</summary>
    /// <param name="sceneManager">集計する現在シーンを取得する管理者。</param>
    /// <param name="editor">選択したオブジェクトへ移動するためのエディター。</param>
    void Initialize(SceneManager* sceneManager, DebugEditor* editor);

    /// <summary>最新の配置を集計し、検索と分類結果を表示します。</summary>
    void DrawImGui() override;
    std::string GetName() override { return "Scene Inventory"; }

private:
    enum class GroupMode {
        Category,
        Type,
        Model,
        Collision,
    };

    /// 一つの配置オブジェクトから表示・分類に必要な情報を抜き出した行。
    struct Entry {
        Object3d* object = nullptr;
        std::string category;
        std::string type;
        std::string model;
        std::string collision;
        bool visible = true;
        bool editorOnly = false;
    };

    /// 選択された分類軸ごとの集計結果。
    struct Group {
        std::string label;
        std::vector<const Entry*> entries;
        int hiddenCount = 0;
    };

    // シーンの配置状態を読み取り、表示用の行と集計へ変換します。
    void RebuildEntries();
    void DrawSummary() const;
    void DrawGroupedList(GroupMode mode);
    void DrawObjectRows(const Group& group);
    void CopySummaryToClipboard() const;

    // 名前に基づく暫定分類を含むため、分類規則はここにまとめます。
    std::vector<Group> BuildGroups(GroupMode mode) const;
    bool MatchesSearch(const Entry& entry, const std::string& groupLabel = {}) const;
    std::string ClassifyCategory(const Object3d& object) const;
    std::string ClassifyType(const Object3d& object, const std::string& category) const;
    std::string ClassifyCollision(const Object3d& object) const;

private:
    // SceneManagerとDebugEditorは所有せず、現在シーンの走査とSceneカメラのフォーカスにのみ利用します。
    SceneManager* sceneManager_ = nullptr;
    DebugEditor* editor_ = nullptr;

    // 検索条件と表示対象の切り替えは現在のウィンドウ状態として保持します。
    std::vector<Entry> entries_;
    char searchBuffer_[128] = {};
    bool includeHidden_ = true;
    bool includeEditorObjects_ = false;
};
