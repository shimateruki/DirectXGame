#pragma once

#include "engine/utility/math/Math.h"
#include "TextureManager.h"
#include "ModelCommon.h"
#include <string>
#include <vector>
#include <d3d12.h>
#include <wrl.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <map>
#include <optional>

namespace engine { class Object3d; }

/// <summary>モデルのCPU側データと描画用GPUリソースを保持する。</summary>
class Model {
public:
    /// <summary>スキニングに使うボーンと逆バインド姿勢。</summary>
    struct Bone {
        std::string name;
        engine::Matrix4x4 inverseBindPoseMatrix; // メッシュの初期姿勢へ戻す行列。
    };

    /// <summary>アニメーションの時刻（秒）と、その時点の値を保持する。</summary>
    template <typename T>
    struct Keyframe {
        float time;
        T value;
    };
    using KeyframeVector3 = Keyframe<engine::Vector3>;
    using KeyframeQuaternion = Keyframe<engine::Quaternion>;

    /// <summary>一つのノードに適用する拡縮・回転・移動のキーフレーム群。</summary>
    struct NodeAnimation {
        std::vector<KeyframeVector3> scale;
        std::vector<KeyframeQuaternion> rotate;
        std::vector<KeyframeVector3> translate;
        std::string name;
    };

    /// <summary>モデルに含まれる名前付きアニメーション。長さと各キーの時刻は秒単位。</summary>
    struct Animation {
        std::string name;
        float duration; // 長さ（秒）。Assimpのtick値は読み込み時に変換する。
        float ticksPerSecond; // 元データの1秒あたりのtick数。
        std::vector<NodeAnimation> nodeAnimations;
    };

    /// <summary>モデル用シェーダーへ渡すマテリアル定数。GPU側のレイアウトと一致させる。</summary>
    struct Material {
        engine::Vector4 color;
        int32_t enableLighting;
        float padding1[3];
        engine::Matrix4x4 uvTransform;
        int32_t selectedLighting;
        float shininess;
        int32_t materialType;
        float roughness; // 粗さ。0で滑らか、1で粗い。
        float metallic; // 金属度。0で非金属、1で金属。
        int32_t enableNormalMap;
        int32_t enableEnvMap; // 環境マップの有効フラグ。
        float envIntensity; // 環境マップの強度。
        float emissive; // 自己発光の強度。
        float time; // シェーダー内の時間演出に使う値。
        float portalClipEnabled;
        float portalClipProgress;
        engine::Vector3 portalClipCenter;
        float portalClipEdgeWidth;
        engine::Vector3 portalClipNormal;
        float portalClipDissolve;
        engine::Vector4 portalClipColor;

    };

    /// <summary>ノードやジョイントのローカル姿勢を拡縮・クォータニオン・移動で表す。</summary>
    struct QuaternionTransform {
        engine::Vector3 scale = {1.0f, 1.0f, 1.0f};
        engine::Quaternion rotate = {0.0f, 0.0f, 0.0f, 1.0f};
        engine::Vector3 translate = {0.0f, 0.0f, 0.0f};
    };

    /// <summary>スケルトン内の一関節と、親子関係および計算済み行列を保持する。</summary>
    struct Joint {
        QuaternionTransform transform;
        engine::Matrix4x4 localMatrix;
        engine::Matrix4x4 skeletonSpaceMatrix;
        std::string name;
        std::vector<int32_t> children;
        int32_t index;
        std::optional<int32_t> parent;
    };

    /// <summary>ルート関節、名前からの索引、全関節の姿勢をまとめる。</summary>
    struct Skeleton {
        int32_t root;
        std::map<std::string, int32_t> jointMap;
        std::vector<Joint> joints;
    };

    /// <summary>読み込んだモデルの階層ノード。親から累積した変換も保持する。</summary>
    struct Node {
        QuaternionTransform transform;
        engine::Matrix4x4 localMatrix;
        engine::Matrix4x4 globalMatrix; // 親から累積したノード変換。
        std::string name;
        std::vector<Node> children;
    };

    /// <summary>モデルメッシュ1頂点分の属性。</summary>
    struct VertexData {
        engine::Vector4 position;
        engine::Vector2 texcoord;
        engine::Vector3 normal;
        engine::Vector3 tangent;
        engine::Vector4 boneWeights; // スキニングの重み。
        engine::Vector4 boneIndices; // 参照するボーン番号。

    };


    /// <summary>モデルファイルから読んだマテリアルとテクスチャ参照。</summary>
    struct MaterialData {
        std::string textureFilePath;
        std::string normalMapPath;
        std::string ormMapPath;
        uint32_t textureHandle = 0;
        uint32_t normalMapHandle = 0;
        uint32_t ormMapHandle = 0;
        engine::Vector4 baseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
        float roughness = 0.5f;
        float metallic = 0.0f;
        bool hasNormalMap = false;
        bool hasOrmMap = false;
        bool doubleSided = false;
        Microsoft::WRL::ComPtr<ID3D12Resource> materialResource;
        Material* materialData = nullptr;
    };

    /// <summary>一メッシュの頂点・インデックスと、通常描画／Compute Skinning用GPUバッファ。</summary>
    struct Mesh {
        std::vector<VertexData> vertices;
        std::vector<uint32_t> indices;
        uint32_t materialIndex;
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
        Microsoft::WRL::ComPtr<ID3D12Resource> computeSkinnedVertexResource;
        D3D12_VERTEX_BUFFER_VIEW computeSkinnedVertexBufferView{};
        D3D12_RESOURCE_STATES computeSkinnedVertexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        Microsoft::WRL::ComPtr<ID3D12Resource> indexResource;
        D3D12_INDEX_BUFFER_VIEW indexBufferView{};
    };

    /// <summary>ファイル読込で得るCPU側のメッシュ、マテリアル、階層、アニメーション一式。</summary>
    struct ModelData {
        std::vector<Mesh> meshes;
        std::vector<MaterialData> materials;
        Node rootNode;
        std::vector<Node> nodes;
        std::vector<Bone> bones;
        std::vector<Animation> animations;
        bool hasSkinning = false;
        bool usesNodeAnimationProxy = false;
        Skeleton skeleton;
    };

    /// <summary>スキニング用にGPUへ転送するボーンの最終変換行列。</summary>
    struct BoneForGPU {
        engine::Matrix4x4 finalMatrix;
    };

public:
    /// <summary>ファイルを読み、モデルと描画用リソースを初期化する。</summary>
    /// <param name="common">GPUリソース生成に使うモデル共通機能。</param>
    /// <param name="directoryPath">モデルファイルのディレクトリ。</param>
    /// <param name="filename">読み込むファイル名。</param>
    void Initialize(ModelCommon* common, const std::string& directoryPath, const std::string& filename);

    /// <summary>AssimpまたはキャッシュからCPU側データだけを読み込む。</summary>
    /// <remarks>DirectXリソースを生成しないため、ワーカースレッドでの準備に使用できる。</remarks>
    /// <returns>GPUリソース未作成のモデルデータ。</returns>
    static ModelData LoadCpuData(const std::string& directoryPath, const std::string& filename);

    /// <summary>準備済みCPUデータから描画用GPUリソースを作成する。</summary>
    /// <param name="common">GPUリソース生成に使うモデル共通機能。</param>
    /// <param name="modelData">所有権を移すCPU側データ。</param>
    /// <param name="sourceName">キャッシュや診断に使う元モデル名。</param>
    void InitializeFromCpuData(
        ModelCommon* common,
        ModelData modelData,
        const std::string& sourceName);

    /// <summary>同じフレーム内の重複更新を避けながら、現在姿勢の関節行列とボーンバッファを更新する。</summary>
    void Update();

    /// <summary>forceが真ならフレーム内で更新済みでもボーンを再計算する。</summary>
    void Update(bool force);

    /// <summary>指定の描画リソースを使ってモデルを描画する。</summary>
    /// <param name="wvpResource">描画対象の座標変換定数。</param>
    /// <param name="directionalLightResource">平行光源の定数。</param>
    /// <param name="cameraResource">カメラ位置などの定数。</param>
    /// <param name="pointLightResource">点光源の定数。</param>
    /// <param name="spotLightResource">スポットライトの定数。</param>
    /// <param name="overrideMaterialResource">指定時は通常のマテリアル定数に優先する。</param>
    /// <param name="normalMapHandle">上書き用法線マップのSRV番号。</param>
    /// <param name="ormMapHandle">上書き用ORMマップのSRV番号。</param>
    /// <param name="overrideTextureHandle">上書き用ベーステクスチャのSRV番号。</param>
    /// <param name="instanceCount">描画するインスタンス数。</param>
    /// <param name="startInstanceLocation">描画開始インスタンス番号。</param>
    /// <param name="meshDrawIndex">描画するメッシュ番号。-1は全メッシュ。</param>
    void Draw(ID3D12Resource* wvpResource,
        ID3D12Resource* directionalLightResource,
        ID3D12Resource* cameraResource,
        ID3D12Resource* pointLightResource,
        ID3D12Resource* spotLightResource,
        ID3D12Resource* overrideMaterialResource = nullptr, uint32_t normalMapHandle = 0, uint32_t ormMapHandle = 0, uint32_t overrideTextureHandle = 0,
        uint32_t instanceCount = 1,
        uint32_t startInstanceLocation = 0,
        int meshDrawIndex = -1
    );

    /// <summary>影マップ用に深度のみを描画する。-1なら全メッシュを対象にする。</summary>
    void DrawShadow(ID3D12Resource* wvpResource, int meshDrawIndex = -1);

    /// <summary>
    /// 現在のボーン行列からスキニング済み頂点をCompute Shaderで生成します。
    /// 利用できない場合はfalseを返し、従来のVertex Shader版へフォールバックします。
    /// </summary>
    bool PrepareComputeSkinning();

    /// <summary>Compute Skinningを利用できるか返す。</summary>
    bool UsesComputeSkinning() const { return computeSkinningAvailable_; }

    /// <summary>先頭マテリアルの編集用ポインタを返す。</summary>
    Material* GetMaterial() { return materialData_; }

    /// <summary>先頭マテリアルのテクスチャハンドルを返す。未設定時は0。</summary>
    uint32_t GetTextureHandle() const {
        if (modelData_.materials.empty()) return 0;
        return modelData_.materials[0].textureHandle;
    }

    /// <summary>読み込み済みのモデルデータを参照する。</summary>
    const ModelData& GetModelData() const { return modelData_; }

    /// <summary>先頭のマテリアルを返す。存在しなければnullptr。</summary>
    const MaterialData* GetPrimaryMaterialData() const {
        return modelData_.materials.empty() ? nullptr : &modelData_.materials[0];
    }

    /// <summary>モデルのローカル空間での大きさを返す。</summary>
    engine::Vector3 GetSize() const { return size_; }

    /// <summary>モデルのローカル空間での中心を返す。</summary>
    engine::Vector3 GetCenter() const { return center_; }

    /// <summary>指定秒のアニメーション姿勢をモデルへ適用する。</summary>
    void ApplyAnimation(const Animation& animation, float time);

    /// <summary>二つのアニメーション姿勢を重みで補間して適用する。</summary>
    /// <param name="blendWeight">0はfrom、1はto側の姿勢。</param>
    void ApplyBlendedAnimation(
        const Animation& fromAnimation,
        float fromTime,
        const Animation& toAnimation,
        float toTime,
        float blendWeight);

    /// <summary>名前に一致するアニメーションを返す。未登録ならnullptr。</summary>
    const Animation* GetAnimation(const std::string& name) const;

    /// <summary>操作可能なスケルトンがあるか返す。</summary>
    bool HasSkeleton() const;

    /// <summary>現在の関節一覧を参照する。</summary>
    const std::vector<Joint>& GetJoints() const;

    /// <summary>名前に一致する関節の索引を返す。未登録なら-1。</summary>
    int FindJointIndex(const std::string& name) const;

    /// <summary>指定関節の現在のローカル姿勢を返す。</summary>
    QuaternionTransform GetJointTransform(int jointIndex) const;

    /// <summary>指定関節のローカル姿勢を変更する。索引が無効ならfalse。</summary>
    bool SetJointTransform(int jointIndex, const QuaternionTransform& transform);

    /// <summary>関節をモデルの初期姿勢に戻す。</summary>
    void ResetSkeletonPose();

    /// <summary>エディター操作後の関節姿勢からスケルトン行列とGPUデータを再計算する。</summary>
    void RebuildSkeletonForEditor();

    /// <summary>ボーン行列のSRV番号を返す。</summary>
    uint32_t GetBoneSrvIndex() const { return boneSrvIndex_; }

    /// <summary>モデルのローカル空間AABBの最小点を返す。</summary>
    engine::Vector3 GetLocalAabbMin() const { return localAabbMin_; }

    /// <summary>モデルのローカル空間AABBの最大点を返す。</summary>
    engine::Vector3 GetLocalAabbMax() const { return localAabbMax_; }

    /// <summary>マテリアル設定を行わず、指定メッシュの頂点・インデックスを描画する。</summary>
    void DrawMeshOnly(int meshDrawIndex = -1);

    /// <summary>外部から渡された頂点・インデックスで描画モデルを作る。</summary>
    void CreateFromVertices(ModelCommon* common, const std::vector<VertexData>& vertices, const std::vector<uint32_t>& indices);
    
    /// <summary>全メッシュの頂点数を返す。</summary>
    uint32_t GetVertexCount() const;

    /// <summary>全メッシュの三角形数を返す。</summary>
    uint32_t GetPolygonCount() const;

    /// <summary>メッシュ数を返す。</summary>
    uint32_t GetMeshCount() const { return static_cast<uint32_t>(modelData_.meshes.size()); }
private:
    // ファイルを解析し、モデルのCPU側表現へ変換する。
    static ModelData LoadFile(const std::string& directoryPath, const std::string& filename);

    // Assimpのノード階層を再帰的に読み、平坦な索引にも記録する。
    static Node ReadNode(aiNode* node, std::vector<Node>& nodes);

    // テンプレートのマテリアル定義を読み込む。
    static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);

    // ノード階層から名前に一致する要素を探す。
    Node* FindNode(Node& node, const std::string& name);

    // 指定秒を挟むキーフレームから値を補間する。
    static engine::Vector3 CalculateValue(const std::vector<KeyframeVector3>& keyframes, float time);
    static engine::Quaternion CalculateValue(const std::vector<KeyframeQuaternion>& keyframes, float time);

    // 読み込んだノード階層から関節一覧と名前索引を組み立てる。
    static int32_t CreateJoint(const Node& node, const std::optional<int32_t>& parent, std::vector<Joint>& joints);
    static Skeleton CreateSkeleton(const Node& rootNode);

    // 現在のローカル姿勢を親から積み上げ、スケルトン空間の行列を更新する。
    void UpdateSkeleton(Skeleton& skeleton);

    // アニメーションの各トラックを対応する関節へ適用する。
    void ApplyAnimationToSkeleton(Skeleton& skeleton, const Animation& animation, float time);

    // ボーン行列を格納するGPUバッファの生成・転送。
    void CreateBoneBuffer();
    void UpdateBoneBuffer();

    // Compute Skinning用の入出力バッファとディスクリプタを準備する。
    void CreateComputeSkinningResources();

    // Compute出力が使える場合はスキニング済み頂点、それ以外は元の頂点を選ぶ。
    const D3D12_VERTEX_BUFFER_VIEW& GetActiveVertexBufferView(const Mesh& mesh) const;

private:
    ModelCommon* common_ = nullptr;
    ModelData modelData_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;

    engine::Math math_;
    engine::Vector3 size_ = { 1.0f, 1.0f, 1.0f };
    engine::Vector3 center_ = { 0.0f, 0.0f, 0.0f };

    // ボーン行列のGPU転送状態。
    Microsoft::WRL::ComPtr<ID3D12Resource> boneResource_;
    BoneForGPU* boneMappedData_ = nullptr;
    uint32_t boneSrvIndex_ = 0; // ボーン行列を参照するSRV番号。
    uint32_t lastUpdateFrame_ = 0xFFFFFFFF; // 最後に更新したフレーム番号。
    bool computeSkinningAvailable_ = false;
    bool computeSkinningDirty_ = true;
    bool computeSkinningOutputReady_ = false;

    engine::Vector3 localAabbMin_ = { 0.0f, 0.0f, 0.0f };
    engine::Vector3 localAabbMax_ = { 0.0f, 0.0f, 0.0f };
};

