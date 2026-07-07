#pragma once
#include "IEditable.h"
#include "LightManager.h"
#include "Object3d.h"
#include <memory>
#include <string>
#include <vector>

class Model;
class Object3dCommon;

/// <summary>
/// ライト設定ファイル、スカイボックス、ライトギズモを編集する。
/// </summary>
// LightEditorは、シーン全体のライト設定を確認、調整、保存するためのエディタです。
class LightEditor : public IEditable {
public:
    static LightEditor* GetInstance();

        // ライト編集に必要な参照を受け取り、UI操作できる状態にします。
void Initialize();
    void SetObject3dCommon(Object3dCommon* common);
    void Update();
    void Draw3D();
        // ライト種別ごとのパラメータ編集UIを描画します。
void DrawImGui() override;
    std::string GetName() override { return "Light Editor"; }

private:
    void SetStatusMessage(const std::string& message, bool success);
    void SyncCurrentFileNameFromManager();
    void DrawLightFileList();
    void DrawSkyboxTextureList();
    std::string BuildFullPathFromFileName() const;

    LightManager* lightManager_ = nullptr;
    char currentFileName_[128] = "light_layout.json";
    std::string syncedLightPath_;
    std::string statusMessage_;
    double statusVisibleUntil_ = 0.0;
    bool statusSuccess_ = true;
    int selectedPointLightIndex_ = -1;
    int selectedSpotLightIndex_ = -1;

    // ライト位置を視覚的に調整するためのギズモ。
    bool isVisibleGizmos_ = true;
    float gizmoScale_ = 0.75f;

    Object3dCommon* common_ = nullptr;
    std::vector<std::unique_ptr<Object3d>> pointLightGizmos_;
    std::vector<std::unique_ptr<Object3d>> spotLightGizmos_;
};
