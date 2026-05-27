#pragma once

#include "IEditable.h"
#include <string>
#include <vector>

class SceneManager;
class Object3d;

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

    struct Issue {
        Severity severity = Severity::Info;
        std::string objectName;
        std::string category;
        std::string message;
    };

private:
    void Refresh();
    void AddIssue(Severity severity, const Object3d* object, const std::string& category, const std::string& message);
    bool DoesModelExist(const std::string& modelName) const;
    bool DoesFileExist(const std::string& path) const;
    const char* GetSeverityLabel(Severity severity) const;
    unsigned int GetSeverityColor(Severity severity) const;

private:
    SceneManager* sceneManager_ = nullptr;
    std::vector<Issue> issues_;
    bool autoRefresh_ = false;
    int selectedSeverityFilter_ = 0;
    float autoRefreshTimer_ = 0.0f;
};
