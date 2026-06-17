#pragma once
#include <string>
#include <vector>
#include <memory>
#include "LightManager.h"
#include "Object3d.h"
#include "IEditable.h" 

class Model;
class Object3dCommon; 

class LightEditor : public IEditable { 
public:
    static LightEditor* GetInstance();

    void Initialize();
    void SetObject3dCommon(Object3dCommon* common);
    void Update();
    void Draw3D();

    // Inspectorに表示するUI描画処理
    void DrawImGui() override;

    // Inspector上部に表示される名前
    std::string GetName() override { return "Light Editor"; }

private:
    void SetStatusMessage(const std::string& message, bool success);
    void SyncCurrentFileNameFromManager();
    void DrawLightFileList();
    std::string BuildFullPathFromFileName() const;

    LightManager* lightManager_ = nullptr;
    char currentFileName_[128] = "light_layout.json";
    std::string syncedLightPath_;
    std::string statusMessage_;
    double statusVisibleUntil_ = 0.0;
    bool statusSuccess_ = true;
    int selectedPointLightIndex_ = -1;
    int selectedSpotLightIndex_ = -1;

    // ギズモの可視化フラグ
    bool isVisibleGizmos_ = true;
    float gizmoScale_ = 0.75f;

    Object3dCommon* common_ = nullptr;
    std::vector<std::unique_ptr<Object3d>> pointLightGizmos_;
    std::vector<std::unique_ptr<Object3d>> spotLightGizmos_;
};
