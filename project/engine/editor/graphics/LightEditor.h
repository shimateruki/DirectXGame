#pragma once
#include "IEditable.h"
#include "LightManager.h"
#include "Object3D.h"
#include <memory>
#include <string>
#include <vector>

class Model;
namespace engine { class Object3DCommon; }

// 担当：ライト・空・環境プロファイルを編集し、設定ファイルへ保存する。
// 関連：LightManager（保持・適用）、Object3DCommon（ギズモ描画）
// 主な入口：Initialize / Update / Draw3D / DrawImGui
class LightEditor : public IEditable {
public:
    static LightEditor* GetInstance();
#ifdef USE_IMGUI

    bool* GetGizmoVisiblePtr() { return &isVisibleGizmos_; }
#endif

        // ライト編集に必要な参照を受け取り、UI操作できる状態にします。
void Initialize();

    void SetObject3DCommon(engine::Object3DCommon* common);

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

    // 保存済みのライト設定ファイルを選択する一覧を描画する。
    void DrawLightFileList();

    // 背景用キューブマップの選択一覧を描画する。
    void DrawSkyboxTextureList();

    // ライト・空・画面効果をまとめた環境プロファイルを編集する。
    void DrawEnvironmentProfilePanel();

    // 保存名を照明設定用JSONディレクトリに固定します。
    std::string BuildFullPathFromFileName() const;

    LightManager* lightManager_ = nullptr;
    char environmentProfileFileName_[128] = "environment_profile.json";
    float environmentProfileBlendDuration_ = 0.75f; // 環境設定を補間する秒数。
    char currentFileName_[128] = "light_layout.json";
    std::string syncedLightPath_;
    std::string statusMessage_;
    double statusVisibleUntil_ = 0.0;
    bool statusSuccess_ = true;
    int selectedPointLightIndex_ = -1; // 選択中の点光源。-1は未選択。
    int selectedSpotLightIndex_ = -1; // 選択中のスポットライト。-1は未選択。

    // ライト位置を視覚的に調整するためのギズモ。
    bool isVisibleGizmos_ = true;
    float gizmoScale_ = 0.75f;

    engine::Object3DCommon* common_ = nullptr;
    std::vector<std::unique_ptr<engine::Object3D>> pointLightGizmos_;
    std::vector<std::unique_ptr<engine::Object3D>> spotLightGizmos_;
};
