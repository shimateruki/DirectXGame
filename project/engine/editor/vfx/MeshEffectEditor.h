#pragma once
#include "EffectObject3D.h"
#include "IEditable.h"
#include "Object3D.h"
#include <memory>
#include <string>
#include <vector>

class SceneManager;
class BaseScene;

// 担当：メッシュVFXの形状・画像・色・歪み・再生条件を編集する。
// 関連：EffectObject3D（プレビュー）、MeshEffectManager（管理）、EffectPreviewStage
// 主な入口：Initialize / Update / Draw / DrawImGui
class MeshEffectEditor : public IEditable {
public:
    MeshEffectEditor() = default;

    ~MeshEffectEditor() override = default;

    // シーンへの接続と、メッシュVFXの編集・プレビューを準備する。
    void Initialize(SceneManager* sceneManager);

        // プレビュー中のメッシュエフェクトを時間経過で更新します。
void Update(float deltaTime);

    // 編集中のメッシュVFXプレビューを描画する。
    void Draw();

        // 色、サイズ、寿命、テクスチャなどの編集UIを描画します。
void DrawImGui() override;

    std::string GetName() override { return "Mesh Effect Editor"; }

    EffectObject3D* GetPreviewEffect() const { return previewEffect_.get(); }

private:
        // 選択可能なテクスチャ一覧を最新のアセット状態に更新します。
void RefreshTextureList();

    // 保存済みパスの表記ゆれを吸収し、画像一覧の選択位置へ戻す。
    void SyncTextureIndices();

    // 保存済みメッシュVFX設定の一覧を更新する。
    void RefreshJsonFileList();

    // 編集中のメッシュVFX設定をJSONへ保存する。
    void SaveToJson();

    // 選択したJSONからメッシュVFX設定を読み込む。
    void LoadFromJson();

private:
    SceneManager* sceneManager_ = nullptr; // 現在シーンの取得元。借用参照。
    BaseScene* lastScene_ = nullptr; // 前回プレビューを更新したシーン。借用参照。
    std::unique_ptr<EffectObject3D> previewEffect_;
    std::vector<std::string> textureFileList_;
    engine::Object3D* targetObject_ = nullptr; // エフェクトの追従対象。借用参照。
    int currentTextureIndex_ = 0;
    char textureSearch_[3][128] = {};
    char modelSearch_[128] = {};
    char jsonSearch_[128] = {};

    // 基本Transform。
    engine::Vector3 editPosition_ = engine::Vector3::ZERO;
    engine::Vector3 editRotation_ = engine::Vector3::ZERO;
    engine::Vector3 editScale_ = { 1.0f, 1.0f, 1.0f };

    // 表示素材と見た目。
    char editModelName_[128] = "Primitives/plane";
    char editTexturePath_[256] = "";
    engine::Vector4 editColor_ = engine::Colors::White;
    engine::Vector2 editScrollSpeed_ = { 0.0f, -1.0f };
    float editIntensity_ = 2.0f;

    // 寿命と補間。
    bool isAutoLoop_ = true;
    float editLifetime_ = 1.0f;
    engine::Vector3 editStartScale_ = { 0.1f, 0.1f, 0.1f };
    engine::Vector3 editEndScale_ = { 3.0f, 3.0f, 3.0f };

    engine::Vector4 editStartColor_ = { 2.0f, 2.0f, 2.0f, 1.0f };
    engine::Vector4 editEndColor_ = { 0.5f, 0.0f, 1.0f, 0.0f };
    bool editEnableDistortion_ = false;
    float editAlphaReference_ = 0.0f;

    // 歪みと透明化の調整値。
    float editDistortionStrength_ = 0.0f;
    float editDistortionSpeed_ = 15.0f;
    float editEdgeFadeStrength_ = 1.5f;
    char saveFileName_[128] = "effect_slash.json";

    char editNoiseTexturePath_[256] = "";
    int currentNoiseTextureIndex_ = 0;

    char editRampTexturePath_[256] = "";
    int currentRampTextureIndex_ = -1; // 選択ランプ画像。-1は未選択。

    int currentBlendModeIndex_ = 2;

    bool editEnableReveal_ = true;
    bool editUseTextureColor_ = false;

    float editChantFlowStrength_ = 0.0f;
    float editChantRibbonStrength_ = 0.0f;

    float editRingEnergyStrength_ = 0.0f;
    float editRingSpinSpeed_ = 0.0f;

    int editRevealMode_ = 1;

    std::vector<std::string> jsonFileList_;
    int currentJsonIndex_ = -1; // 選択設定ファイル。-1は未選択。

    int editEasingType_ = 0;
    int editProceduralType_ = 0;
    int editVolumeMode_ = 0;

    std::vector<std::unique_ptr<EffectObject3D>> extraPreviewEffects_;
    bool forcePlayRequest_ = false; // 次の更新でプレビューを再開始する予約。

    int lastStagePlayRequestSerial_ = 0; // 前回処理したプレビュー再生要求の番号。
    int lastStageStopRequestSerial_ = 0; // 前回処理したプレビュー停止要求の番号。
    int lastStageSeekRequestSerial_ = 0; // 前回処理したプレビューシーク要求の番号。
};
