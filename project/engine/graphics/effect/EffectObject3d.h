#pragma once
#include "Object3D.h"
#include"Model.h"
#include <algorithm>
#include <vector>
#include <wrl.h>

// 担当：Object3dを土台に、メッシュVFXの寿命・色・拡大・ディゾルブを制御する。
// 関連：Object3D（共通機能）、MeshEffectManager（生成・再利用）、MeshEffectEditor（編集）
// 主な入口：Initialize / Play / Update / Draw / ResetForReuse
class EffectObject3D : public engine::Object3D {
public:
    /// <summary>エフェクトシェーダーの cbuffer と一致する描画パラメータ。</summary>
    // 関連：EffectObject3D（この型を使う機能の本体）
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

    /// <summary>Object3D とエフェクト専用マテリアルバッファを初期化する。</summary>
    void Initialize(engine::Object3DCommon* common);

    /// <summary>GPUバッファを保持し、単発演出の状態を新規生成時の値へ戻す。</summary>
    void ResetForReuse();

    /// <summary>再生時間に応じてスケール、色、ディゾルブを更新する。</summary>
    void Update(float deltaTime) override;

    /// <summary>エフェクト用パイプラインでメッシュを描画する。</summary>
    void Draw(ID3D12Resource* pointLightResource = nullptr, ID3D12Resource* spotLightResource = nullptr) override;

    void DrawForCamera(Camera* camera, ID3D12Resource* pointLightResource,
                       ID3D12Resource* spotLightResource, int previewBufferIndex = 0) override;

    // エフェクト用マテリアルの描画値。バッファ未作成時の設定は反映されない。
    void SetColor(const engine::Vector4& color) { if (materialData_) materialData_->color = color; }

    void SetScrollSpeed(const engine::Vector2& speed) { if (materialData_) materialData_->scrollSpeed = speed; }

    void SetIntensity(float intensity) { if (materialData_) materialData_->intensity = intensity; }

    // 材質シェーダーの時間だけを0へ戻す。寿命の再生位置とは別。
    void ResetTime() { if (materialData_) materialData_->time = 0.0f; }

    EffectMaterial* GetMaterialData() const { return materialData_; }

    /// <summary>寿命を指定し、再生位置を先頭に戻して開始する。</summary>
    void Play(float lifetime) {
        currentTime_ = 0.0f; // 再生開始からの経過秒数。
        lifetime_ = lifetime; // 一回の再生の長さ（秒）。
        isPlaying_ = true;
        if (materialData_)
            materialData_->time = 0.0f;
    }

    /// <summary>設定済みの寿命を保って先頭から再生する。</summary>
    void Restart() {
        currentTime_ = 0.0f; // 再生開始からの経過秒数。
        isPlaying_ = true;
        if (materialData_)
            materialData_->time = 0.0f;
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

    void SetTargetObject(engine::Object3D* target) { targetObject_ = target; }

    void SetOffsets(const engine::Vector3& pos, const engine::Vector3& rot) { offsetPos_ = pos; offsetRot_ = rot; }

    engine::Object3D* GetTargetObject() const { return targetObject_; }

    void SetProceduralType(int type) { if (materialData_) materialData_->proceduralType = type; }

    /// <summary>編集パラメータから手続き生成メッシュを再構築する。</summary>
    void UpdateProceduralMesh();

    /// <summary>シーン終了時に球・リングの共有形状を解放する。表示中の参照は維持する。</summary>
    static void ClearProceduralMeshCache();

    /// <summary>現在の手続き生成メッシュを OBJ 形式で書き出す。</summary>
    void ExportToObj(const std::string& filePath) const;

    /// <summary>手続き形状・UV・衝突の編集用データ。保持状態への変更は適用関数を使う。</summary>
    // 関連：EffectObject3D（この型を使う機能の本体）
    struct MeshSettings {
        // --- プロシージャルメッシュ用パラメータ ---
        float slashAngle = 360.0f; // 斬撃の角度（360度以上も可能に）
        float innerRadius = 0.5f;  // 内径（剣の根元）
        float outerRadius = 4.0f;  // 外径（剣の先端）
        float thickness = 0.5f;    // 軌跡の厚み
        float spiralPitch = 0.0f;  // 螺旋の高さ（スパイラル）
        float thrustLength = 5.0f; // 突きの長さ
        float thrustRadius = 0.8f; // 突きの根本の太さ
        int   meshSegments = 16;   // ポリゴンの分割数（滑らかさ）
    
        // 基本プリミティブ用
        float sphereRadius = 1.0f;
        int   sphereRings = 16;

        float cylinderRadius = 1.0f;
        float cylinderHeight = 2.0f;

        engine::Vector3 boxSize = {1.0f, 1.0f, 1.0f};

        engine::Vector2 planeSize = {2.0f, 2.0f};
        int   planeSubdivisions = 1;

        float torusMajorRadius = 1.0f;
        float torusMinorRadius = 0.3f;

        float coneRadius = 1.0f;
        float coneHeight = 2.0f;

        float ringOuterRadius = 1.0f;
        float ringInnerRadius = 0.5f;

        float triangleSize = 1.0f;

        engine::Vector2 uvTiling = {1.0f, 1.0f};

        bool hasCollision = false;
        int collisionShape = 0; // 0:Sphere, 1:Aabb, 2:Obb, 3:Cylinder
        engine::Vector3 collisionSize = { 1.0f, 1.0f, 1.0f };
        engine::Vector3 collisionOffset = engine::Vector3::ZERO;

    };

    const MeshSettings& GetMeshSettings() const { return meshSettings_; }
    void SetMeshSettings(const MeshSettings& settings) { meshSettings_ = settings; }

    template<class Value>
    void SetMeshSetting(Value MeshSettings::* member, std::type_identity_t<Value> value) {
        meshSettings_.*member = std::move(value);
    }

    template<class Vector>
    void SetMeshComponent(Vector MeshSettings::* member, float Vector::* component, float value) {
        (meshSettings_.*member).*component = value;
    }

private:
    MeshSettings meshSettings_;

    // 呼出側が用意した変換バッファを使ってエフェクトを描画する。
    void DrawWithWvp(ID3D12Resource* wvpResource);
    // エフェクト専用のマテリアルバッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> materialBuffer_;
    EffectMaterial* materialData_ = nullptr;
    engine::Object3D* targetObject_ = nullptr; // 追従するObject。借用参照。
    engine::Vector3 offsetPos_ = engine::Vector3::ZERO; // 追従対象からの位置補正。
    engine::Vector3 offsetRot_ = engine::Vector3::ZERO; // 追従対象からの回転補正（ラジアン）。

    // エフェクト用マテリアルをGPUへ渡す定数バッファを作る。
    bool CreateMaterialBuffer(ID3D12Device* device);

    BlendMode blendMode_ = BlendMode::Add;

    float currentTime_ = 0.0f; // 再生開始からの経過秒数。
    float lifetime_ = 1.0f; // 一回の再生の長さ（秒）。
    bool isPlaying_ = false;

    uint32_t noiseTextureHandle_ = 0;
    uint32_t rampTextureHandle_ = 0;

    engine::Vector3 startScale_ = { 1.0f, 1.0f, 1.0f }; // 再生開始時の拡縮。
    engine::Vector3 endScale_ = { 1.0f, 1.0f, 1.0f }; // 再生終了時の拡縮。

    engine::Vector4 startColor_ = engine::Colors::White;
    engine::Vector4 endColor_ = engine::Colors::White;

    int easingType_ = 0; // デフォルトは 0 (Linear)
    bool isAutoLoop_ = false;

    // 動的生成専用のモデル
    // キャッシュした形状は読み取り専用で共有し、再編集時は別Modelへ切り替えます。
    std::shared_ptr<Model> dynamicModel_;
    std::vector<Model::VertexData> proceduralVertices_;
    std::vector<uint32_t> proceduralIndices_;

    // 以下の Generate* は形状別の頂点・インデックスを proceduralVertices_ / proceduralIndices_ に生成する。
    void GenerateSlashVertices(float angleDeg, float inRad, float outRad, float thickness, float spiralPitch, int segments, bool isCrescent = false);

    // 長さ・半径・分割数から突き形状の頂点を作る。
    void GenerateThrustVertices(float length, float radius, int segments);

    // 緯度ごとのリングを作り、UVの継ぎ目用に各リングの端点を重複させる。
    void GenerateSphereVertices(float radius, int segments, int rings);

    // 半径・高さ・周方向の分割数から円柱の頂点を作る。
    void GenerateCylinderVertices(float radius, float height, int segments);

    // 各面に独立した法線とUVを持たせ、箱の角が滑らかに補間されないようにする。
    void GenerateBoxVertices(const engine::Vector3& size);

    // 平面を等間隔の格子に分け、変形可能な頂点とUVを生成する。
    void GeneratePlaneVertices(const engine::Vector2& size, int subdivisions);

    // 大円に沿って小円を並べ、継ぎ目のUVを複製して輪状Meshを作る。
    void GenerateTorusVertices(float majorRad, float minorRad, int segments, int rings);

    // 半径・高さ・周方向の分割数から円錐の頂点を作る。
    void GenerateConeVertices(float radius, float height, int segments);

    // 円錐一枚では表現できない刃・鍔・柄の輪郭を持つ槍を生成します。
    void GenerateLanceVertices(float radius, float height, int segments);

    // 外半径・内半径・分割数から輪の頂点を作る。
    void GenerateRingVertices(float outerRad, float innerRad, int segments);

    // 重心が原点に来る正三角形をXZ平面へ作り、法線を上向きに揃える。
    void GenerateTriangleVertices(float size);

};
