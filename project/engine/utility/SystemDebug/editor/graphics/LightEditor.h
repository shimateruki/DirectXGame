#pragma once
#include "IEditable.h"
#include "LightManager.h"
#include "Object3d.h"
#include <memory>
#include <string>
#include <vector>

class Model;
namespace engine { class Object3dCommon; }

/// <summary>
/// ライト設定ファイル、スカイボックス、ライトギズモを編集する。
/// </summary>
// LightEditorは、シーン全体のライト設定を確認、調整、保存するためのエディタです。
class LightEditor : public IEditable {
public:
    static LightEditor* GetInstance();
#ifdef USE_IMGUI

    bool* GetGizmoVisiblePtr() { return &isVisibleGizmos_; }
#endif

        // ライト編集に必要な参照を受け取り、UI操作できる状態にします。
void Initialize();

    void SetObject3dCommon(engine::Object3dCommon* common);

    // 現在の点光源・スポットライトの個数と位置へ、編集用ギズモを同期する。
    void Update();

    // 可視設定が有効な場合だけギズモを描く。ゲームのライト本体の描画ではない。
    void Draw3D();

        // ライト種別ごとのパラメータ編集UIを描画します。
void DrawImGui() override;

    std::string GetName() override { return "Light Editor"; }

private:
    void SetStatusMessage(const std::string& message, bool success);

    // 実行側で切り替わった照明ファイル名をUIへ取り込みます。
    void SyncCurrentFileNameFromManager();

    void DrawLightFileList();

    void DrawSkyboxTextureList();

    void DrawEnvironmentProfilePanel();

    // 保存名を照明設定用JSONディレクトリに固定します。
    std::string BuildFullPathFromFileName() const;

    LightManager* lightManager_ = nullptr;
    char environmentProfileFileName_[128] = "environment_profile.json";
    float environmentProfileBlendDuration_ = 0.75f;
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

    engine::Object3dCommon* common_ = nullptr;
    std::vector<std::unique_ptr<engine::Object3d>> pointLightGizmos_;
    std::vector<std::unique_ptr<engine::Object3d>> spotLightGizmos_;
};
