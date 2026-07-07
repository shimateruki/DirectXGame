#pragma once

#include "SceneSerializer.h"
#include "json.hpp"
#include <string>
#include <vector>

/// <summary>
/// 保存前にJSON差分を確認し、保存するかキャンセルするかを選ばせるプレビューUI。
/// </summary>
/// シーン保存前に差分を表示し、保存対象ファイルと変更内容を確認するためのプレビューUI。
class SceneSavePreview {
public:
    enum class Action {
        None,
        Confirm,
        Cancel
    };

    /// <summary>
    /// 保存対象の差分情報を構築する。
    /// </summary>
    /// 保存予定データと既存JSONを比較し、ファイルごとの差分情報を構築する。
    void Build(const std::vector<SceneSerializer::SaveTarget>& targets, const std::string& title);

    void Open();
    void Close();
    /// 差分確認ダイアログを描画し、保存/キャンセル/閉じるのユーザー操作を返す。
    Action Draw();

    bool IsOpen() const { return isOpen_; }
    const std::vector<SceneSerializer::SaveTarget>& GetTargets() const { return targets_; }
    const std::string& GetTitle() const { return title_; }
    const std::string& GetSavedFilesLabel() const { return savedFilesLabel_; }

private:
    enum class ChangeKind {
        Added,
        Removed,
        Modified,
        Unchanged,
        FileAdded,
        FileModified,
        FileUnchanged,
        FileInvalid
    };

    /// 追加・削除・変更されたObject単位の差分内容を保持する。
    struct ObjectChange {
        ChangeKind kind = ChangeKind::Unchanged;
        std::string name;
        std::string category;
        std::vector<std::string> details;
    };

    /// 1つの保存先ファイルについて、旧ファイルの有無とObject差分をまとめる。
    struct FileDiff {
        SceneSerializer::SaveTarget target;
        bool oldFileExists = false;
        bool oldJsonValid = true;
        bool hasObjectList = false;
        int addedCount = 0;
        int removedCount = 0;
        int modifiedCount = 0;
        int unchangedCount = 0;
        std::vector<std::string> fileDetails;
        std::vector<ObjectChange> objects;
    };

    struct Summary {
        int fileCount = 0;
        int addedFiles = 0;
        int modifiedFiles = 0;
        int invalidFiles = 0;
        int addedObjects = 0;
        int removedObjects = 0;
        int modifiedObjects = 0;
        int unchangedObjects = 0;

        bool HasChanges() const {
            return addedFiles > 0 || modifiedFiles > 0 || invalidFiles > 0 ||
                   addedObjects > 0 || removedObjects > 0 || modifiedObjects > 0;
        }
    };

    FileDiff BuildFileDiff(const SceneSerializer::SaveTarget& target) const;
    void BuildObjectDiff(FileDiff& diff, const nlohmann::json& oldJson, const nlohmann::json& newJson) const;
    void BuildFileLevelDiff(FileDiff& diff, const nlohmann::json& oldJson, const nlohmann::json& newJson) const;
    void UpdateSummary();

    static std::string GetObjectName(const nlohmann::json& obj, const std::string& fallback);
    static std::string GetObjectCategory(const nlohmann::json& obj);
    static std::string JsonValueToText(const nlohmann::json& value);
    /// JSONの差分を再帰的に集め、詳細表示用の短い説明リストへ変換する。
    static void CollectJsonDiffs(const nlohmann::json& before, const nlohmann::json& after, const std::string& path, std::vector<std::string>& outDetails, int maxDetails);

#ifdef USE_IMGUI
    void DrawSummaryCards() const;
    void DrawFileSummaryTable() const;
    void DrawFileDiff(FileDiff& diff);
    void DrawChangeBadge(ChangeKind kind) const;
    const char* GetKindLabel(ChangeKind kind) const;
#endif

    std::vector<SceneSerializer::SaveTarget> targets_;
    std::vector<FileDiff> fileDiffs_;
    Summary summary_;
    std::string title_;
    std::string savedFilesLabel_;
    bool isOpen_ = false;
    bool requestOpenPopup_ = false;
    bool showUnchanged_ = false;
};
