#pragma once
#include "EffectObject3d.h"
#include "IEditable.h"
#include "Object3d.h"
#include <memory>
#include <string>
#include <vector>

class SceneManager;
class BaseScene;

/// <summary>
/// メッシュエフェクトのモデル、テクスチャ、色、歪み、再生パラメータを編集する。
/// </summary>
// MeshEffectEditorは、メッシュを使った斬撃や衝撃波などのエフェクトを編集、プレビューします。
class MeshEffectEditor : public IEditable {
public:
    MeshEffectEditor() = default;
    ~MeshEffectEditor() override = default;

    void Initialize(SceneManager* sceneManager);

        // プレビュー中のメッシュエフェクトを時間経過で更新します。
void Update(float deltaTime);
    void Draw();
        // 色、サイズ、寿命、テクスチャなどの編集UIを描画します。
void DrawImGui() override;

    std::string GetName() override { return "Mesh Effect Editor"; }
    EffectObject3d* GetPreviewEffect() const { return previewEffect_.get(); }

private:
        // 選択可能なテクスチャ一覧を最新のアセット状態に更新します。
void RefreshTextureList();
    void SyncTextureIndices();
    void RefreshJsonFileList();
    void SaveToJson();
    void LoadFromJson();

private:
    SceneManager* sceneManager_ = nullptr;
    BaseScene* lastScene_ = nullptr;
    std::unique_ptr<EffectObject3d> previewEffect_;
    std::vector<std::string> textureFileList_;
    Object3d* targetObject_ = nullptr;
    int currentTextureIndex_ = 0;

    // 基本Transform。
    Vector3 editPosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 editRotation_ = { 0.0f, 0.0f, 0.0f };
    Vector3 editScale_ = { 1.0f, 1.0f, 1.0f };

    // 表示素材と見た目。
    char editModelName_[128] = "Primitives/plane";
    char editTexturePath_[256] = "";
    Vector4 editColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector2 editScrollSpeed_ = { 0.0f, -1.0f };
    float editIntensity_ = 2.0f;

    // 寿命と補間。
    bool isAutoLoop_ = true;
    float editLifetime_ = 1.0f;
    Vector3 editStartScale_ = { 0.1f, 0.1f, 0.1f };
    Vector3 editEndScale_ = { 3.0f, 3.0f, 3.0f };

    Vector4 editStartColor_ = { 2.0f, 2.0f, 2.0f, 1.0f };
    Vector4 editEndColor_ = { 0.5f, 0.0f, 1.0f, 0.0f };
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
    int currentRampTextureIndex_ = -1;
    int currentBlendModeIndex_ = 2;
    bool editEnableReveal_ = true;
    std::vector<std::string> jsonFileList_;
    int currentJsonIndex_ = -1;
    int editEasingType_ = 0;
    int editProceduralType_ = 0;
    int editVolumeMode_ = 0;
    std::vector<std::unique_ptr<EffectObject3d>> extraPreviewEffects_;
    bool forcePlayRequest_ = false;
    int lastStagePlayRequestSerial_ = 0;
    int lastStageStopRequestSerial_ = 0;
    int lastStageSeekRequestSerial_ = 0;
};
