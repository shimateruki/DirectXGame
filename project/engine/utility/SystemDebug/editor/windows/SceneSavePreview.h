#pragma once

#include "SceneSerializer.h"
#include "json.hpp"
#include <string>
#include <vector>

/// <summary>
/// 保存前に JSON 差分を確認し、保存するかキャンセルするかを選ばせるプレビュー UI。
/// </summary>
class SceneSavePreview {
public:
    enum class Action {
        None,
        Confirm,
        Cancel
    };

    /// <summary>
    /// 保存予定のデータと既存ファイルを比較し、表示用の差分を構築します。
    /// </summary>
    /// <param name="targets">保存先と新しい JSON データの一覧。</param>
    /// <param name="title">確認ダイアログに表示する題名。</param>
    void Build(const std::vector<SceneSerializer::SaveTarget>& targets, const std::string& title);

    /// 差分確認ダイアログを開く、または閉じます。
    void Open();
    void Close();

    /// 差分確認ダイアログを描画し、保存・取消・操作なしを返します。
    Action Draw();

    // 表示状態と、確認後に呼び出し元が保存する対象を公開します。
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

    /// ファイル単位とオブジェクト単位の件数を表示用に集計します。
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

    // 既存 JSON と保存予定 JSON を比較し、表示用データを一度だけ構築します。
    FileDiff BuildFileDiff(const SceneSerializer::SaveTarget& target) const;
    void BuildObjectDiff(FileDiff& diff, const nlohmann::json& oldJson, const nlohmann::json& newJson) const;
    void BuildFileLevelDiff(FileDiff& diff, const nlohmann::json& oldJson, const nlohmann::json& newJson) const;
    void UpdateSummary();

    // JSON の表示名と、詳細差分に使う短い文字列を生成します。
    static std::string GetObjectName(const nlohmann::json& obj, const std::string& fallback);
    static std::string GetObjectCategory(const nlohmann::json& obj);
    static std::string JsonValueToText(const nlohmann::json& value);
    /// JSONの差分を再帰的に集め、詳細表示用の短い説明リストへ変換する。
    static void CollectJsonDiffs(const nlohmann::json& before, const nlohmann::json& after, const std::string& path, std::vector<std::string>& outDetails, int maxDetails);

#ifdef USE_IMGUI
    // 差分データを描画する処理。比較ロジックは SceneSavePreview.cpp に置きます。
    void DrawSummaryCards() const;
    void DrawFileSummaryTable() const;
    void DrawFileDiff(FileDiff& diff);
    void DrawChangeBadge(ChangeKind kind) const;
    const char* GetKindLabel(ChangeKind kind) const;
#endif

    // Build() で確定したスナップショットを、利用者が判断するまで保持します。
    std::vector<SceneSerializer::SaveTarget> targets_;
    std::vector<FileDiff> fileDiffs_;
    Summary summary_;
    std::string title_;
    std::string savedFilesLabel_;
    bool isOpen_ = false;
    bool requestOpenPopup_ = false;
    bool showUnchanged_ = false;
};
