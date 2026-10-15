#pragma once
#include "Object3d.h"
#include"Model.h"
#include <algorithm>
#include <vector>
#include <wrl.h>

// ========================================================
// 斬撃・魔法陣などのメッシュエフェクト専用クラス
// ========================================================
// EffectObject3dは、メッシュベースのVFXをObject3dとして扱い、寿命、色、拡大、ディゾルブを制御します。
class EffectObject3d : public engine::Object3d {
public:
    /// <summary>エフェクトシェーダーの cbuffer と一致する描画パラメータ。</summary>
    struct EffectMaterial {
        engine::Vector4 color;
        engine::Vector2 scrollSpeed;
        float time;
        float intensity;
        float dissolveFade;   // 4バイト (0.0:完全な状態 ～ 1.0:完全に消滅)
        float revealProgress; // 4バイト
        float distortionStrength;
        float distortionSpeed;
        float edgeFadeStrength;
        float alphaReference; // ★discardの閾値 (シェーダー側の padding1 と一致)
        engine::Vector2 screenSize;
        int enableDistortion; // 0: 加算/通常, 1: 背景歪みモード
        int enableColorRamp;  //カラーランプ有効フラグ
        int enableNoiseTexture; //ノイズテクスチャ有効フラグ
        int enableReveal;
        int proceduralType;
        int useTextureColor;
        float chantFlowStrength;
        float chantRibbonStrength;
        float ringEnergyStrength;
        float ringSpinSpeed;
    };

    /// <summary>Object3d とエフェクト専用マテリアルバッファを初期化する。</summary>
    void Initialize(Object3dCommon* common);

    /// <summary>再生時間に応じてスケール、色、ディゾルブを更新する。</summary>
    void Update(float deltaTime) override;

    /// <summary>エフェクト用パイプラインでメッシュを描画する。</summary>
    void Draw(ID3D12Resource* pointLightResource = nullptr, ID3D12Resource* spotLightResource = nullptr) override;

    // エフェクト用マテリアルの描画値。バッファ未作成時の設定は反映されない。
    void SetColor(const engine::Vector4& color) { if (materialData_) materialData_->color = color; }
    void SetScrollSpeed(const engine::Vector2& speed) { if (materialData_) materialData_->scrollSpeed = speed; }
    void SetIntensity(float intensity) { if (materialData_) materialData_->intensity = intensity; }
    void ResetTime() { if (materialData_) materialData_->time = 0.0f; }

    EffectMaterial* GetMaterialData() const { return materialData_; }

    /// <summary>寿命を指定し、再生位置を先頭に戻して開始する。</summary>
    void Play(float lifetime) {
        currentTime_ = 0.0f;
        lifetime_ = lifetime;
        isPlaying_ = true;
        if (materialData_) materialData_->time = 0.0f;
    }

    /// <summary>設定済みの寿命を保って先頭から再生する。</summary>
    void Restart() {
        currentTime_ = 0.0f;
        isPlaying_ = true;
        if (materialData_) materialData_->time = 0.0f;
    }
    bool IsPlaying() const { return isPlaying_; }
    float GetLifetime() const { return lifetime_; }
    float GetCurrentTime() const { return currentTime_; }

    /// <summary>寿命に対する再生割合を 0～1 で返す。寿命が 0 以下なら 1。</summary>
    float GetPlaybackProgress() const {
        return lifetime_ > 0.0f ? std::clamp(currentTime_ / lifetime_, 0.0f, 1.0f) : 1.0f;
    }

    void SetStartScale(const engine::Vector3& s) { startScale_ = s; }
    void SetEndScale(const engine::Vector3& s) { endScale_ = s; }
    void SetStartColor(const engine::Vector4& c) { startColor_ = c; }
    void SetEndColor(const engine::Vector4& c) { endColor_ = c; }
    void SetDistortionStrength(float strength) { if (materialData_) materialData_->distortionStrength = (std::max)(0.0f, strength); }
    void SetDistortionSpeed(float s) { if (materialData_) materialData_->distortionSpeed = s; }
    void SetEdgeFadeStrength(float s) { if (materialData_) materialData_->edgeFadeStrength = s; }
    void SetEnableDistortion(bool enable) { if (materialData_) materialData_->enableDistortion = enable ? 1 : 0; }

    /// <summary>歪み描画が有効で、強度も正かを返す。</summary>
    bool IsDistortionEnabled() const {
        return materialData_ && materialData_->enableDistortion != 0 && materialData_->distortionStrength > 0.0f;
    }
    void SetAlphaReference(float ref) { if (materialData_) materialData_->alphaReference = ref; }
    void SetNoiseTexture(uint32_t handle) { noiseTextureHandle_ = handle; }
    void SetRampTexture(uint32_t handle) { rampTextureHandle_ = handle; }
    void SetEnableColorRamp(bool enable) { if (materialData_) materialData_->enableColorRamp = enable ? 1 : 0; }
    void SetEnableNoiseTexture(bool enable) { if (materialData_) materialData_->enableNoiseTexture = enable ? 1 : 0; }
    void SetUseTextureColor(bool enable) { if (materialData_) materialData_->useTextureColor = enable ? 1 : 0; }
    void SetChantFlowStrength(float strength) { if (materialData_) materialData_->chantFlowStrength = std::clamp(strength, 0.0f, 1.0f); }
    void SetChantRibbonStrength(float strength) { if (materialData_) materialData_->chantRibbonStrength = std::clamp(strength, 0.0f, 1.0f); }
    void SetRingEnergyStrength(float strength) { if (materialData_) materialData_->ringEnergyStrength = std::clamp(strength, 0.0f, 1.0f); }
    void SetRingSpinSpeed(float speed) { if (materialData_) materialData_->ringSpinSpeed = std::clamp(speed, -40.0f, 40.0f); }
    void SetBlendMode(BlendMode mode) { blendMode_ = mode; }
    BlendMode GetBlendMode() const { return blendMode_; }
    void SetEnableReveal(bool enable) { if (materialData_) materialData_->enableReveal = enable ? 1 : 0; }
    void SetRevealMode(int mode) { if (materialData_) materialData_->enableReveal = std::clamp(mode, 0, 2); }
    void SetRevealProgress(float progress) { if (materialData_) materialData_->revealProgress = std::clamp(progress, 0.0f, 1.0f); }
    void SetEasingType(int type) { easingType_ = type; }
    int GetEasingType() const { return easingType_; }

    void SetAutoLoop(bool loop) { isAutoLoop_ = loop; }
    bool GetAutoLoop() const { return isAutoLoop_; }

    /// <summary>JSONプリセットから形状とマテリアル設定を読み込む。</summary>
    bool LoadFromJson(const std::string& jsonFilePath);

    void SetTargetObject(engine::Object3d* target) { targetObject_ = target; }
    void SetOffsets(const engine::Vector3& pos, const engine::Vector3& rot) { offsetPos_ = pos; offsetRot_ = rot; }
    engine::Object3d* GetTargetObject() const { return targetObject_; }
    void SetProceduralType(int type) { if (materialData_) materialData_->proceduralType = type; }

    /// <summary>編集パラメータから手続き生成メッシュを再構築する。</summary>
    void UpdateProceduralMesh();

    /// <summary>現在の手続き生成メッシュを OBJ 形式で書き出す。</summary>
    void ExportToObj(const std::string& filePath) const;

    // --- プロシージャルメッシュ用パラメータ ---
    float editSlashAngle_ = 360.0f; // 斬撃の角度（360度以上も可能に）
    float editInnerRadius_ = 0.5f;  // 内径（剣の根元）
    float editOuterRadius_ = 4.0f;  // 外径（剣の先端）
    float editThickness_ = 0.5f;    // 軌跡の厚み
    float editSpiralPitch_ = 0.0f;  // 螺旋の高さ（スパイラル）
    float editThrustLength_ = 5.0f; // 突きの長さ
    float editThrustRadius_ = 0.8f; // 突きの根本の太さ
    int   editMeshSegments_ = 16;   // ポリゴンの分割数（滑らかさ）
    
    // 基本プリミティブ用
    float editSphereRadius_ = 1.0f;
    int   editSphereRings_ = 16;
    float editCylinderRadius_ = 1.0f;
    float editCylinderHeight_ = 2.0f;
    engine::Vector3 editBoxSize_ = {1.0f, 1.0f, 1.0f};
    engine::Vector2 editPlaneSize_ = {2.0f, 2.0f};
    int   editPlaneSubdivisions_ = 1;
    float editTorusMajorRadius_ = 1.0f;
    float editTorusMinorRadius_ = 0.3f;
    float editConeRadius_ = 1.0f;
    float editConeHeight_ = 2.0f;
    float editRingOuterRadius_ = 1.0f;
    float editRingInnerRadius_ = 0.5f;
    float editTriangleSize_ = 1.0f;
    engine::Vector2 editUvTiling_ = {1.0f, 1.0f};

    bool editHasCollision_ = false;
    int editCollisionShape_ = 0; // 0:Sphere, 1:AABB, 2:OBB, 3:Cylinder
    engine::Vector3 editCollisionSize_ = { 1.0f, 1.0f, 1.0f };
    engine::Vector3 editCollisionOffset_ = { 0.0f, 0.0f, 0.0f };
private:
    // エフェクト専用のマテリアルバッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> materialBuffer_;
    EffectMaterial* materialData_ = nullptr;
    engine::Object3d* targetObject_ = nullptr;
    engine::Vector3 offsetPos_ = { 0.0f, 0.0f, 0.0f };
    engine::Vector3 offsetRot_ = { 0.0f, 0.0f, 0.0f };

    // エフェクト用マテリアルをGPUへ渡す定数バッファを作る。
    bool CreateMaterialBuffer(ID3D12Device* device);
    BlendMode blendMode_ = BlendMode::kAdd;
    float currentTime_ = 0.0f;
    float lifetime_ = 1.0f;
    bool isPlaying_ = false;
    uint32_t noiseTextureHandle_ = 0;
    uint32_t rampTextureHandle_ = 0;
    engine::Vector3 startScale_ = { 1.0f, 1.0f, 1.0f };
    engine::Vector3 endScale_ = { 1.0f, 1.0f, 1.0f };
    engine::Vector4 startColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    engine::Vector4 endColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    int easingType_ = 0; // デフォルトは 0 (Linear)
    bool isAutoLoop_ = false;

    // 動的生成専用のモデル
    std::unique_ptr<Model> dynamicModel_;
    std::vector<Model::VertexData> proceduralVertices_;
    std::vector<uint32_t> proceduralIndices_;

    // 以下の Generate* は形状別の頂点・インデックスを proceduralVertices_ / proceduralIndices_ に生成する。
    void GenerateSlashVertices(float angleDeg, float inRad, float outRad, float thickness, float spiralPitch, int segments, bool isCrescent = false);
    void GenerateThrustVertices(float length, float radius, int segments);
    void GenerateSphereVertices(float radius, int segments, int rings);
    void GenerateCylinderVertices(float radius, float height, int segments);
    void GenerateBoxVertices(const engine::Vector3& size);
    void GeneratePlaneVertices(const engine::Vector2& size, int subdivisions);
    void GenerateTorusVertices(float majorRad, float minorRad, int segments, int rings);
    void GenerateConeVertices(float radius, float height, int segments);
    // 円錐一枚では表現できない刃・鍔・柄の輪郭を持つ槍を生成します。
    void GenerateLanceVertices(float radius, float height, int segments);
    void GenerateRingVertices(float outerRad, float innerRad, int segments);
    void GenerateTriangleVertices(float size);

};
