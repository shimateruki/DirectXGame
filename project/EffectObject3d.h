#pragma once
#include "Object3d.h"
#include"Model.h"
#include <vector>
#include <wrl.h>

// ========================================================
// 斬撃・魔法陣などのメッシュエフェクト専用クラス
// ========================================================
class EffectObject3d : public Object3d {
public:
    // シェーダーの cbuffer EffectMaterial と一致させる構造体
    struct EffectMaterial {
        Vector4 color;
        Vector2 scrollSpeed;
        float time;
        float intensity;
        float dissolveFade;   // 4バイト (0.0:完全な状態 ～ 1.0:完全に消滅)
        float revealProgress; // 4バイト
        float distortionStrength;
        float distortionSpeed;
        float edgeFadeStrength;
        float padding1;
        Vector2 screenSize;
        int enableDistortion; // 0: 加算/通常, 1: 背景歪みモード
        int enableColorRamp;  //カラーランプ有効フラグ
        int enableNoiseTexture; //ノイズテクスチャ有効フラグ
        int enableReveal;
        int proceduralType;
        Vector3 padding2;
    };

    // 初期化・更新・描画
    void Initialize(Object3dCommon* common);
    void Update(float deltaTime) override;

    // 通常のDrawをオーバーライドし、エフェクト専用のパイプラインで描画する
    void Draw(ID3D12Resource* pointLightResource = nullptr, ID3D12Resource* spotLightResource = nullptr) override;

    // --- エディタから操作するためのアクセッサ ---
    void SetColor(const Vector4& color) { materialData_->color = color; }
    void SetScrollSpeed(const Vector2& speed) { materialData_->scrollSpeed = speed; }
    void SetIntensity(float intensity) { materialData_->intensity = intensity; }
    void ResetTime() { materialData_->time = 0.0f; }

    EffectMaterial* GetMaterialData() const { return materialData_; }
    // --- アニメーション用セッター ---
    void Play(float lifetime) {
        currentTime_ = 0.0f;
        lifetime_ = lifetime;
        isPlaying_ = true;
        if (materialData_) materialData_->time = 0.0f;
    }
    bool IsPlaying() const { return isPlaying_; }

    void SetStartScale(const Vector3& s) { startScale_ = s; }
    void SetEndScale(const Vector3& s) { endScale_ = s; }
    void SetStartColor(const Vector4& c) { startColor_ = c; }
    void SetEndColor(const Vector4& c) { endColor_ = c; }
    void SetDistortionStrength(float s) { materialData_->distortionStrength = s; }
    void SetDistortionSpeed(float s) { materialData_->distortionSpeed = s; }
    void SetEdgeFadeStrength(float s) { materialData_->edgeFadeStrength = s; }
    void SetEnableDistortion(bool enable) { materialData_->enableDistortion = enable ? 1 : 0; }
    void SetNoiseTexture(uint32_t handle) { noiseTextureHandle_ = handle; }
    void SetRampTexture(uint32_t handle) { rampTextureHandle_ = handle; }
    void SetEnableColorRamp(bool enable) { materialData_->enableColorRamp = enable ? 1 : 0; }
    void SetEnableNoiseTexture(bool enable) { materialData_->enableNoiseTexture = enable ? 1 : 0; }
    void SetBlendMode(BlendMode mode) { blendMode_ = mode; }
    BlendMode GetBlendMode() const { return blendMode_; }
    void SetEnableReveal(bool enable) { materialData_->enableReveal = enable ? 1 : 0; }
    void SetEasingType(int type) { easingType_ = type; }
    int GetEasingType() const { return easingType_; }

    void SetTargetObject(Object3d* target) { targetObject_ = target; }
    void SetOffsets(const Vector3& pos, const Vector3& rot) { offsetPos_ = pos; offsetRot_ = rot; }
    Object3d* GetTargetObject() const { return targetObject_; }
    void SetProceduralType(int type) { materialData_->proceduralType = type; }
    void UpdateProceduralMesh();

    // --- プロシージャルメッシュ用パラメータ ---
    float editSlashAngle_ = 360.0f; // 斬撃の角度（360度以上も可能に）
    float editInnerRadius_ = 0.5f;  // 内径（剣の根元）
    float editOuterRadius_ = 4.0f;  // 外径（剣の先端）
    float editThickness_ = 0.5f;    // 軌跡の厚み
    float editSpiralPitch_ = 0.0f;  // 螺旋の高さ（スパイラル）
    float editThrustLength_ = 5.0f; // 突きの長さ
    float editThrustRadius_ = 0.8f; // 突きの根本の太さ
    int   editMeshSegments_ = 16;   // ポリゴンの分割数（滑らかさ）
    bool editHasCollision_ = false;
    int editCollisionShape_ = 0; // 0:Sphere, 1:AABB, 2:OBB, 3:Cylinder
    Vector3 editCollisionSize_ = { 1.0f, 1.0f, 1.0f };
    Vector3 editCollisionOffset_ = { 0.0f, 0.0f, 0.0f };
private:
    // エフェクト専用のマテリアルバッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> materialBuffer_;
    EffectMaterial* materialData_ = nullptr;
    Object3d* targetObject_ = nullptr;
    Vector3 offsetPos_ = { 0.0f, 0.0f, 0.0f };
    Vector3 offsetRot_ = { 0.0f, 0.0f, 0.0f };
    void CreateMaterialBuffer(ID3D12Device* device);
    BlendMode blendMode_ = BlendMode::kAdd;
    float currentTime_ = 0.0f;
    float lifetime_ = 1.0f;
    bool isPlaying_ = false;
    uint32_t noiseTextureHandle_ = 0;
    uint32_t rampTextureHandle_ = 0;
    Vector3 startScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 endScale_ = { 1.0f, 1.0f, 1.0f };
    Vector4 startColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 endColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    int easingType_ = 0; // デフォルトは 0 (Linear)
    // 動的生成専用のモデル
    std::unique_ptr<Model> dynamicModel_;
    std::vector<Model::VertexData> proceduralVertices_;
    std::vector<uint32_t> proceduralIndices_;

    // 形状別の生成ロジック
    void GenerateSlashVertices(float angleDeg, float inRad, float outRad, float thickness, float spiralPitch, int segments, bool isCrescent = false);
    void GenerateThrustVertices(float length, float radius, int segments);

};