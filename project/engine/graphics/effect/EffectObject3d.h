#pragma once
#include "Object3d.h"
#include"Model.h"
#include <vector>
#include <wrl.h>

// ========================================================
// 斬撃・魔法陣などのメッシュエフェクト専用クラス
// ========================================================
// EffectObject3dは、メッシュベースのVFXをObject3dとして扱い、寿命、色、拡大、ディゾルブを制御します。
class EffectObject3d : public Object3d {
public:
    // シェーダーの cbuffer EffectMaterial と一致させる構造体
        // エフェクトシェーダーへ渡す色、時間、歪み、リビールなどのパラメータです。
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
        float alphaReference; // ★discardの閾値 (シェーダー側の padding1 と一致)
        Vector2 screenSize;
        int enableDistortion; // 0: 加算/通常, 1: 背景歪みモード
        int enableColorRamp;  //カラーランプ有効フラグ
        int enableNoiseTexture; //ノイズテクスチャ有効フラグ
        int enableReveal;
        int proceduralType;
        Vector3 padding2;
    };

    // 初期化・更新・描画
        // 通常Object3dの初期化に加えて、エフェクト専用マテリアルバッファを作成します。
void Initialize(Object3dCommon* common);
        // 再生時間に合わせてスケール、色、ディゾルブなどを補間します。
void Update(float deltaTime) override;

    // 通常のDrawをオーバーライドし、エフェクト専用のパイプラインで描画する
        // エフェクト用パイプラインでメッシュを描画します。
void Draw(ID3D12Resource* pointLightResource = nullptr, ID3D12Resource* spotLightResource = nullptr) override;

    // --- エディタから操作するためのアクセッサ ---
    void SetColor(const Vector4& color) { if (materialData_) materialData_->color = color; }
    void SetScrollSpeed(const Vector2& speed) { if (materialData_) materialData_->scrollSpeed = speed; }
    void SetIntensity(float intensity) { if (materialData_) materialData_->intensity = intensity; }
    void ResetTime() { if (materialData_) materialData_->time = 0.0f; }

    EffectMaterial* GetMaterialData() const { return materialData_; }
    // --- アニメーション用セッター ---
        // 寿命を指定してエフェクト再生を開始します。
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
    void SetDistortionStrength(float) { if (materialData_) materialData_->distortionStrength = 0.0f; }
    void SetDistortionSpeed(float s) { if (materialData_) materialData_->distortionSpeed = s; }
    void SetEdgeFadeStrength(float s) { if (materialData_) materialData_->edgeFadeStrength = s; }
    void SetEnableDistortion(bool) { if (materialData_) materialData_->enableDistortion = 0; }
    void SetAlphaReference(float ref) { if (materialData_) materialData_->alphaReference = ref; }
    void SetNoiseTexture(uint32_t handle) { noiseTextureHandle_ = handle; }
    void SetRampTexture(uint32_t handle) { rampTextureHandle_ = handle; }
    void SetEnableColorRamp(bool enable) { if (materialData_) materialData_->enableColorRamp = enable ? 1 : 0; }
    void SetEnableNoiseTexture(bool enable) { if (materialData_) materialData_->enableNoiseTexture = enable ? 1 : 0; }
    void SetBlendMode(BlendMode mode) { blendMode_ = mode; }
    BlendMode GetBlendMode() const { return blendMode_; }
    void SetEnableReveal(bool enable) { if (materialData_) materialData_->enableReveal = enable ? 1 : 0; }
    void SetEasingType(int type) { easingType_ = type; }
    int GetEasingType() const { return easingType_; }

    void SetAutoLoop(bool loop) { isAutoLoop_ = loop; }
    bool GetAutoLoop() const { return isAutoLoop_; }
        // JSONプリセットからエフェクト形状やマテリアル設定を読み込みます。
bool LoadFromJson(const std::string& jsonFilePath);

    void SetTargetObject(Object3d* target) { targetObject_ = target; }
    void SetOffsets(const Vector3& pos, const Vector3& rot) { offsetPos_ = pos; offsetRot_ = rot; }
    Object3d* GetTargetObject() const { return targetObject_; }
    void SetProceduralType(int type) { if (materialData_) materialData_->proceduralType = type; }
        // 編集パラメータから手続き生成メッシュを再構築します。
void UpdateProceduralMesh();
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
    Vector3 editBoxSize_ = {1.0f, 1.0f, 1.0f};
    Vector2 editPlaneSize_ = {2.0f, 2.0f};
    int   editPlaneSubdivisions_ = 1;
    float editTorusMajorRadius_ = 1.0f;
    float editTorusMinorRadius_ = 0.3f;
    float editConeRadius_ = 1.0f;
    float editConeHeight_ = 2.0f;
    float editRingOuterRadius_ = 1.0f;
    float editRingInnerRadius_ = 0.5f;
    float editTriangleSize_ = 1.0f;
    Vector2 editUvTiling_ = {1.0f, 1.0f};

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
        // エフェクト専用マテリアルをGPUへ渡すための定数バッファを作成します。
bool CreateMaterialBuffer(ID3D12Device* device);
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
    bool isAutoLoop_ = false;
    // 動的生成専用のモデル
    std::unique_ptr<Model> dynamicModel_;
    std::vector<Model::VertexData> proceduralVertices_;
    std::vector<uint32_t> proceduralIndices_;

    // 形状別の生成ロジック
        // 斬撃や三日月形エフェクト用の頂点とインデックスを生成します。
void GenerateSlashVertices(float angleDeg, float inRad, float outRad, float thickness, float spiralPitch, int segments, bool isCrescent = false);
    void GenerateThrustVertices(float length, float radius, int segments);
    void GenerateSphereVertices(float radius, int segments, int rings);
    void GenerateCylinderVertices(float radius, float height, int segments);
    void GenerateBoxVertices(const Vector3& size);
    void GeneratePlaneVertices(const Vector2& size, int subdivisions);
    void GenerateTorusVertices(float majorRad, float minorRad, int segments, int rings);
    void GenerateConeVertices(float radius, float height, int segments);
    void GenerateRingVertices(float outerRad, float innerRad, int segments);
    void GenerateTriangleVertices(float size);

};
