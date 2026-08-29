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

class Object3d;

// Modelは、OBJ/GLTFなどから読み込んだメッシュ、マテリアル、テクスチャ情報をGPU描画用に保持します。
class Model {
public:

    // スキニングで使用するボーン情報。
    struct Bone {
        std::string name;
        Matrix4x4 inverseBindPoseMatrix; // 初期姿勢からボーン空間へ変換する逆バインド行列。
    };

    // アニメーションのキーフレームとノード単位のトラック。
    template <typename T>
    struct Keyframe {
        float time;
        T value;
    };
    using KeyframeVector3 = Keyframe<Vector3>;
    using KeyframeQuaternion = Keyframe<Quaternion>;

    struct NodeAnimation {
        std::vector<KeyframeVector3> scale;
        std::vector<KeyframeQuaternion> rotate;
        std::vector<KeyframeVector3> translate;
        std::string name;
    };

    struct Animation {
        std::string name;     // アニメーション名。
        float duration;       // 全体の長さ（秒）。
        float ticksPerSecond; // 読み込み元データの1秒当たりのTick数。
        std::vector<NodeAnimation> nodeAnimations; // ノードごとのアニメーショントラック。
    };

    // シェーダーへ渡すマテリアル定数。HLSL側の配置と順序を一致させます。
    struct Material {
        Vector4 color;
        int32_t enableLighting;
        float padding1[3];
        Matrix4x4 uvTransform;
        int32_t selectedLighting;
        float shininess;
        int32_t materialType;
        float roughness;           // 粗さ。0.0が平滑、1.0が粗い表面。
        float metallic;            // 金属度。0.0が非金属、1.0が金属。
        int32_t enableNormalMap;
        int32_t enableEnvMap;      // 環境マップを使用するか。
        float envIntensity;        // 環境マップの反射強度。
        float emissive;            // 自己発光強度。0.0で無効。
        float time;                // 時間依存マテリアルのアニメーション用。
        float portalClipEnabled;
        float portalClipProgress;
        Vector3 portalClipCenter;
        float portalClipEdgeWidth;
        Vector3 portalClipNormal;
        float portalClipDissolve;
        Vector4 portalClipColor;

    };
    struct QuaternionTransform {
        Vector3 scale = {1.0f, 1.0f, 1.0f};
        Quaternion rotate = {0.0f, 0.0f, 0.0f, 1.0f};
        Vector3 translate = {0.0f, 0.0f, 0.0f};
    };

    struct Joint {
        QuaternionTransform transform;
        Matrix4x4 localMatrix;
        Matrix4x4 skeletonSpaceMatrix;
        std::string name;
        std::vector<int32_t> children;
        int32_t index;
        std::optional<int32_t> parent;
    };

    struct Skeleton {
        int32_t root;
        std::map<std::string, int32_t> jointMap;
        std::vector<Joint> joints;
    };

    struct Node {
        QuaternionTransform transform;
        Matrix4x4 localMatrix;
        Matrix4x4 globalMatrix; // アニメーション計算後のモデル内グローバル行列。
        std::string name;
        std::vector<Node> children;
    };

    // モデルメッシュ1頂点分の位置、UV、法線、スキニング情報です。
    struct VertexData {
        Vector4 position;
        Vector2 texcoord;
        Vector3 normal;
        Vector3 tangent;
        Vector4 boneWeights; // 各ボーンの影響度。
        Vector4 boneIndices; // 参照するボーン番号。

    };

    // モデルファイルから読み取ったマテリアルとテクスチャ参照情報です。
    struct MaterialData {
        std::string textureFilePath;
        std::string normalMapPath;
        std::string ormMapPath;
        uint32_t textureHandle = 0;
        uint32_t normalMapHandle = 0;
        uint32_t ormMapHandle = 0;
        Vector4 baseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
        float roughness = 0.5f;
        float metallic = 0.0f;
        bool hasNormalMap = false;
        bool hasOrmMap = false;
        bool doubleSided = false;
        Microsoft::WRL::ComPtr<ID3D12Resource> materialResource;
        Material* materialData = nullptr;
    };

    struct Mesh {
        std::vector<VertexData> vertices;
        std::vector<uint32_t> indices;
        uint32_t materialIndex;
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
        Microsoft::WRL::ComPtr<ID3D12Resource> computeSkinnedVertexResource;
        D3D12_VERTEX_BUFFER_VIEW computeSkinnedVertexBufferView{};
        D3D12_RESOURCE_STATES computeSkinnedVertexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        Microsoft::WRL::ComPtr<ID3D12Resource> indexResource; // インデックス用GPUバッファ。
        D3D12_INDEX_BUFFER_VIEW indexBufferView{};            // インデックスバッファビュー。
    };

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

    struct BoneForGPU {
        Matrix4x4 finalMatrix;
    };


public:
    /// モデルファイルを読み込み、描画に必要なGPUリソースまで初期化します。
    void Initialize(ModelCommon* common, const std::string& directoryPath, const std::string& filename);
    // Assimp/キャッシュ解析だけを行い、DirectXリソースを生成しないCPU側ロードです。
    static ModelData LoadCpuData(const std::string& directoryPath, const std::string& filename);
    // ワーカースレッドで準備したCPUデータから、メインスレッドでGPUリソースを生成します。
    void InitializeFromCpuData(
        ModelCommon* common,
        ModelData modelData,
        const std::string& sourceName);
    void Update();
    void Update(bool force);
    /// モデルを描画します。meshDrawIndexが負数の場合は全Meshが対象です。
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
    void DrawShadow(ID3D12Resource* wvpResource, int meshDrawIndex = -1);
    /// <summary>
    /// 現在のボーン行列からスキニング済み頂点をCompute Shaderで生成します。
    /// 利用できない場合はfalseを返し、従来のVertex Shader版へフォールバックします。
    /// </summary>
    bool PrepareComputeSkinning();
    bool UsesComputeSkinning() const { return computeSkinningAvailable_; }
    /// 先頭マテリアルの編集用ポインタを返します。
    Material* GetMaterial() { return materialData_; }

    /// 先頭マテリアルのテクスチャハンドルを返します。未設定なら0です。
    uint32_t GetTextureHandle() const {
        if (modelData_.materials.empty()) return 0;
        return modelData_.materials[0].textureHandle;
    }
    const ModelData& GetModelData() const { return modelData_; }
    const MaterialData* GetPrimaryMaterialData() const {
        return modelData_.materials.empty() ? nullptr : &modelData_.materials[0];
    }
    Vector3 GetSize() const { return size_; }
    Vector3 GetCenter() const { return center_; }
    void ApplyAnimation(const Animation& animation, float time);
    void ApplyBlendedAnimation(
        const Animation& fromAnimation,
        float fromTime,
        const Animation& toAnimation,
        float toTime,
        float blendWeight);
    const Animation* GetAnimation(const std::string& name) const;
    bool HasSkeleton() const;
    const std::vector<Joint>& GetJoints() const;
    int FindJointIndex(const std::string& name) const;
    QuaternionTransform GetJointTransform(int jointIndex) const;
    bool SetJointTransform(int jointIndex, const QuaternionTransform& transform);
    void ResetSkeletonPose();
    void RebuildSkeletonForEditor();
    uint32_t GetBoneSrvIndex() const { return boneSrvIndex_; }
    Vector3 GetLocalAabbMin() const { return localAabbMin_; }
    Vector3 GetLocalAabbMax() const { return localAabbMax_; }

    void DrawMeshOnly(int meshDrawIndex = -1);
    void CreateFromVertices(ModelCommon* common, const std::vector<VertexData>& vertices, const std::vector<uint32_t>& indices);
    
    // エディターや統計表示で使用する読取専用情報。
    uint32_t GetVertexCount() const;
    uint32_t GetPolygonCount() const;
    uint32_t GetMeshCount() const { return static_cast<uint32_t>(modelData_.meshes.size()); }
private:
    static ModelData LoadFile(const std::string& directoryPath, const std::string& filename);
    static Node ReadNode(aiNode* node, std::vector<Node>& nodes);

    static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);

    Node* FindNode(Node& node, const std::string& name);

    // キーフレーム間を補間して指定時刻の値を求めます。
    static Vector3 CalculateValue(const std::vector<KeyframeVector3>& keyframes, float time);
    static Quaternion CalculateValue(const std::vector<KeyframeQuaternion>& keyframes, float time);

    // Skeletonの構築とアニメーション適用。
    static int32_t CreateJoint(const Node& node, const std::optional<int32_t>& parent, std::vector<Joint>& joints);
    static Skeleton CreateSkeleton(const Node& rootNode);
    void UpdateSkeleton(Skeleton& skeleton);
    void ApplyAnimationToSkeleton(Skeleton& skeleton, const Animation& animation, float time);

    // ボーン行列をGPUへ渡すバッファとCompute Skinning用リソース。
    void CreateBoneBuffer();
    void UpdateBoneBuffer();
    void CreateComputeSkinningResources();
    const D3D12_VERTEX_BUFFER_VIEW& GetActiveVertexBufferView(const Mesh& mesh) const;



private:
    ModelCommon* common_ = nullptr;
    ModelData modelData_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;

    Math math_;
    Vector3 size_ = { 1.0f, 1.0f, 1.0f };   // 読み込み前の既定サイズ。
    Vector3 center_ = { 0.0f, 0.0f, 0.0f }; // 読み込み前の既定中心。
    // ボーンバッファはModelが所有し、SRV番号だけを描画側へ公開します。
    Microsoft::WRL::ComPtr<ID3D12Resource> boneResource_;
    BoneForGPU* boneMappedData_ = nullptr;
    uint32_t boneSrvIndex_ = 0; // ボーン情報SRVのインデックス。
    uint32_t lastUpdateFrame_ = 0xFFFFFFFF; // 同一フレームでの重複更新を防ぐ更新番号。
    bool computeSkinningAvailable_ = false;
    bool computeSkinningDirty_ = true;
    bool computeSkinningOutputReady_ = false;

    Vector3 localAabbMin_ = { 0.0f, 0.0f, 0.0f };
    Vector3 localAabbMax_ = { 0.0f, 0.0f, 0.0f };
};

