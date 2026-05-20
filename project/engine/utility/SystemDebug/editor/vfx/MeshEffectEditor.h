#pragma once
#include "IEditable.h"
#include "EffectObject3d.h"
#include "Object3d.h"
#include <memory>
#include <string>
#include <vector>  

class SceneManager;
class BaseScene;

class MeshEffectEditor : public IEditable {
public:
    MeshEffectEditor() = default;
    ~MeshEffectEditor() override = default;

    void Initialize(SceneManager* sceneManager);

    void Update(float deltaTime);
    void Draw();
    void DrawImGui() override;

    std::string GetName() override { return "Mesh Effect Editor"; }
    EffectObject3d* GetPreviewEffect() const { return previewEffect_.get(); }

private:
    void RefreshTextureList();
    void SyncTextureIndices();
    void RefreshJsonFileList();
    void SaveToJson();
    void LoadFromJson();
private:
    // SceneManager を保持
    SceneManager* sceneManager_ = nullptr;
    BaseScene* lastScene_ = nullptr;
    std::unique_ptr<EffectObject3d> previewEffect_;
    std::vector<std::string> textureFileList_;
    Object3d* targetObject_ = nullptr;
    int currentTextureIndex_ = 0; // 現在選択されているテクスチャのインデックス

    Vector3 editPosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 editRotation_ = { 0.0f, 0.0f, 0.0f };
    Vector3 editScale_ = { 1.0f, 1.0f, 1.0f }; // スケールは最初から 1.0 にしておく

    char editModelName_[128] = "Primitives/plane";
    char editTexturePath_[256] = "";
    Vector4 editColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector2 editScrollSpeed_ = { 0.0f, -1.0f };
    float editIntensity_ = 2.0f;

    // --- アニメーションパラメータ ---
    bool isAutoLoop_ = true;
    float editLifetime_ = 1.0f;
    // --- Start / End パラメータ ---
    Vector3 editStartScale_ = { 0.1f, 0.1f, 0.1f };
    Vector3 editEndScale_ = { 3.0f, 3.0f, 3.0f };

    Vector4 editStartColor_ = { 2.0f, 2.0f, 2.0f, 1.0f }; // 最初は白く発光
    Vector4 editEndColor_ = { 0.5f, 0.0f, 1.0f, 0.0f }; // 紫で消滅
    bool editEnableDistortion_ = true;
    // --- 歪みと透明化のパラメータ (エディタUI用) ---
    float editDistortionStrength_ = 0.05f;
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
    int currentJsonIndex_ = -1; // -1は「新規作成」扱い
    int editEasingType_ = 0;
    int editProceduralType_ = 0; // 0:Tex, 1:Slash, 2:Aura, 3:Noise
    int editVolumeMode_ = 0; // 0: なし, 1: 十字クロス, 2: 3枚重ね
    std::vector<std::unique_ptr<EffectObject3d>> extraPreviewEffects_; // 立体化プレビュー用
    bool forcePlayRequest_ = false;

};