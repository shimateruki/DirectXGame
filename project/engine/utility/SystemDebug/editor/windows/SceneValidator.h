#pragma once

#include "IEditable.h"
#include <string>
#include <vector>

class SceneManager;
class Object3d;

/// <summary>
/// シーン内オブジェクトの参照切れや設定ミスを検査するEditorウィンドウ。
/// </summary>
/// 現在シーン内のモデル欠落、参照ミス、設定不備を検出して一覧表示する検証ウィンドウ。
class SceneValidator : public IEditable {
public:
    void Initialize(SceneManager* sceneManager);
    void DrawImGui() override;
    std::string GetName() override { return "Scene Validator"; }

private:
    enum class Severity {
        Info,
        Warning,
        Error
    };

    /// 1件の検証結果として、重要度・対象Object名・カテゴリ・説明文を保持する。
    struct Issue {
        Severity severity = Severity::Info;
        std::string objectName;
        std::string category;
        std::string message;
    };

private:
    /// 現在シーンを再走査し、検証結果一覧を作り直す。
    void Refresh();
    /// 検出した問題を重要度付きで一覧へ追加する。
    void AddIssue(Severity severity, const Object3d* object, const std::string& category, const std::string& message);
    /// Objectが参照しているモデル名がResources内に存在するか確認する。
    bool DoesModelExist(const std::string& modelName) const;
    bool DoesFileExist(const std::string& path) const;
    const char* GetSeverityLabel(Severity severity) const;
    unsigned int GetSeverityColor(Severity severity) const;

private:
    // SceneManagerへの参照。SceneValidatorは所有しない。
    SceneManager* sceneManager_ = nullptr;

    std::vector<Issue> issues_;
    bool autoRefresh_ = false;
    int selectedSeverityFilter_ = 0;
    float autoRefreshTimer_ = 0.0f;
};
