#pragma once

#include "IEditable.h"
#include "json.hpp"

#include <string>
#include <vector>

class DebugEditor;
class Object3d;
struct EditorPropertyDescriptor;

/// 複数Objectの共通プロパティを表形式で比較・編集するウィンドウです。
class PropertyMatrixWindow : public IEditable {
public:
    void Initialize(DebugEditor* editor);
    void DrawImGui() override;
    std::string GetName() override { return "プロパティマトリクス"; }

    /// 横長の編集ウィンドウを開きます。
    void Open();

    /// 独立したProperty Matrixウィンドウを描画します。
    void DrawWindow();

private:
    struct EditSnapshot {
        Object3d* object = nullptr;
        nlohmann::json state;
    };

    void DrawMatrixContents();
    void DrawFilters();
    void DrawBulkEditor(const std::vector<Object3d*>& targets,
        const std::vector<const EditorPropertyDescriptor*>& properties);
    void DrawPropertyTable(const std::vector<Object3d*>& targets,
        const std::vector<const EditorPropertyDescriptor*>& properties);

    bool DrawValueEditor(const EditorPropertyDescriptor& property,
        nlohmann::json& value,
        const char* id,
        bool compact) const;
    bool IsPropertyVisible(const EditorPropertyDescriptor& property) const;
    bool CanEditProperty(const Object3d* object, const EditorPropertyDescriptor& property) const;
    std::vector<Object3d*> CollectTargets() const;
    std::vector<const EditorPropertyDescriptor*> CollectProperties(const std::vector<Object3d*>& targets) const;

    void BeginActiveEdit(const std::string& editId, const std::vector<Object3d*>& targets);
    void CommitActiveEdit(const std::string& label);
    void RefreshBulkDraft(const std::vector<Object3d*>& targets,
        const EditorPropertyDescriptor* property,
        bool force);
    std::string BuildSelectionSignature(const std::vector<Object3d*>& targets) const;

private:
    DebugEditor* editor_ = nullptr;
    bool isOpen_ = false;

    char objectFilter_[96] = {};
    char propertyFilter_[96] = {};
    bool showIdentity_ = true;
    bool showTransform_ = true;
    bool showRendering_ = true;
    bool showEditor_ = false;
    bool showCollision_ = false;
    bool showComponent_ = false;
    bool showCamera_ = true;
    bool showGameplay_ = true;

    std::string activeEditId_;
    std::vector<EditSnapshot> activeEditSnapshots_;

    std::string bulkPropertyPath_;
    nlohmann::json bulkValue_;
    std::string bulkSelectionSignature_;
};
