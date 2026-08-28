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
    void Initialize(SceneManager* sceneManager, DebugEditor* editor);
    void DrawImGui() override;
    std::string GetName() override { return "Scene Inventory"; }

private:
    enum class GroupMode {
        Category,
        Type,
        Model,
        Collision,
    };

    struct Entry {
        Object3d* object = nullptr;
        std::string category;
        std::string type;
        std::string model;
        std::string collision;
        bool visible = true;
        bool editorOnly = false;
    };

    struct Group {
        std::string label;
        std::vector<const Entry*> entries;
        int hiddenCount = 0;
    };

    void RebuildEntries();
    void DrawSummary() const;
    void DrawGroupedList(GroupMode mode);
    void DrawObjectRows(const Group& group);
    void CopySummaryToClipboard() const;

    std::vector<Group> BuildGroups(GroupMode mode) const;
    bool MatchesSearch(const Entry& entry, const std::string& groupLabel = {}) const;
    std::string ClassifyCategory(const Object3d& object) const;
    std::string ClassifyType(const Object3d& object, const std::string& category) const;
    std::string ClassifyCollision(const Object3d& object) const;

private:
    // SceneManagerとDebugEditorは所有せず、現在シーンの走査とSceneカメラのフォーカスにのみ利用します。
    SceneManager* sceneManager_ = nullptr;
    DebugEditor* editor_ = nullptr;

    std::vector<Entry> entries_;
    char searchBuffer_[128] = {};
    bool includeHidden_ = true;
    bool includeEditorObjects_ = false;
};
