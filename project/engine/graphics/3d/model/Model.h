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

    // --- 鬪ｨ ---
    struct Bone {
        std::string name;
        Matrix4x4 inverseBindPoseMatrix; // 蛻晄悄蟋ｿ蜍｢縺ｮ騾・｡悟・
    };

    // --- 繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ髢｢騾｣ ---
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
        std::string name;     // 繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ蜷・
        float duration;       // 蜈ｨ菴薙・髟ｷ縺・遘・
        float ticksPerSecond; // 1遘偵≠縺溘ｊ縺ｮ騾ｲ陦悟ｺｦ
        std::vector<NodeAnimation> nodeAnimations; // 蜷・ｪｨ縺ｮ蜍輔″繝ｪ繧ｹ繝・
    };

    // --- 繝槭ユ繝ｪ繧｢繝ｫ繝ｻ繝弱・繝・---
    struct Material {
        Vector4 color;
        int32_t enableLighting;
        float padding1[3];
        Matrix4x4 uvTransform;
        int32_t selectedLighting;
        float shininess;
        int32_t materialType;
        float roughness;           // 4 byte (邊励＆: 0.0=繝・Ν繝・Ν, 1.0=繧ｶ繝ｩ繧ｶ繝ｩ)
        float metallic;            // 4 byte (驥大ｱ槫ｺｦ: 0.0=髱樣≡螻・ 1.0=驥大ｱ・
        int32_t enableNormalMap;
        int32_t enableEnvMap;      // 4 byte (迺ｰ蠅・・繝・・譛牙柑蛹・
        float envIntensity;        // 4 byte (迺ｰ蠅・・繝・・蠑ｷ蠎ｦ)
        float emissive;            // 4 byte (閾ｪ蟾ｱ逋ｺ蜈峨・蠑ｷ縺輔・.0縺ｧ蜈峨ｉ縺ｪ縺・
        float time;                // 4 byte (譎る俣繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ逕ｨ)
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
        Matrix4x4 globalMatrix; // 笘・ｿｽ蜉: 繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ險育ｮ礼ｵ先棡(繝ｯ繝ｼ繝ｫ繝芽｡悟・)逕ｨ
        std::string name;
        std::vector<Node> children;
    };

        // モデルメッシュ1頂点分の位置、UV、法線などを表します。
struct VertexData {
        Vector4 position;
        Vector2 texcoord;
        Vector3 normal;
        Vector3 tangent;
        Vector4 boneWeights; // 驥阪∩
        Vector4 boneIndices; // 鬪ｨ逡ｪ蜿ｷ

    };

    // --- 繝・・繧ｿ縺ｾ縺ｨ繧√ｋ逕ｨ ---
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
        Microsoft::WRL::ComPtr<ID3D12Resource> indexResource; //  繧､繝ｳ繝・ャ繧ｯ繧ｹ逕ｨ繝舌ャ繝輔ぃ
        D3D12_INDEX_BUFFER_VIEW indexBufferView{};            //  繧､繝ｳ繝・ャ繧ｯ繧ｹ逕ｨ繝薙Η繝ｼ
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
        Skeleton skeleton; // 笘・ｿｽ蜉
    };

    struct BoneForGPU {
        Matrix4x4 finalMatrix;
    };


public: // 繝｡繝ｳ繝宣未謨ｰ
    /// <summary>
    /// 蛻晄悄蛹・
    /// </summary>
    void Initialize(ModelCommon* common, const std::string& directoryPath, const std::string& filename);
    void Update();
    void Update(bool force);
    /// <summary>
    /// 謠冗判
    /// </summary>
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
    /// 繝槭ユ繝ｪ繧｢繝ｫ諠・ｱ縺ｮ蜿門ｾ・(ImGui縺ｧ縺ｮ謫堺ｽ懃畑)
    /// </summary>
    Material* GetMaterial() { return materialData_; }

    /// <summary>
    /// 繝・け繧ｹ繝√Ε繝上Φ繝峨Ν繧貞叙蠕・
    /// </summary>
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
    
    // --- 諠・ｱ蜿門ｾ礼畑 ---
    uint32_t GetVertexCount() const;
    uint32_t GetPolygonCount() const;
    uint32_t GetMeshCount() const { return static_cast<uint32_t>(modelData_.meshes.size()); }
private: // 蜀・Κ蜃ｦ逅・未謨ｰ
    static ModelData LoadFile(const std::string& directoryPath, const std::string& filename);
    static Node ReadNode(aiNode* node, std::vector<Node>& nodes);

    static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);

    Node* FindNode(Node& node, const std::string& name);

    // 繧ｭ繝ｼ繝輔Ξ繝ｼ繝縺九ｉ蛟､繧定ｨ育ｮ励☆繧具ｼ郁｣憺俣・・
    static Vector3 CalculateValue(const std::vector<KeyframeVector3>& keyframes, float time);
    static Quaternion CalculateValue(const std::vector<KeyframeQuaternion>& keyframes, float time);

    // --- Skeleton 繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ ---
    static int32_t CreateJoint(const Node& node, const std::optional<int32_t>& parent, std::vector<Joint>& joints);
    static Skeleton CreateSkeleton(const Node& rootNode);
    void UpdateSkeleton(Skeleton& skeleton);
    void ApplyAnimationToSkeleton(Skeleton& skeleton, const Animation& animation, float time);

    // 繝懊・繝ｳ繝舌ャ繝輔ぃ髢｢騾｣
    void CreateBoneBuffer();
    void UpdateBoneBuffer();



private: // 繝｡繝ｳ繝仙､画焚
    ModelCommon* common_ = nullptr;
    ModelData modelData_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;

    Math math_;
    Vector3 size_ = { 1.0f, 1.0f, 1.0f };   // 繝・ヵ繧ｩ繝ｫ繝医・1x1x1
    Vector3 center_ = { 0.0f, 0.0f, 0.0f }; // 繝・ヵ繧ｩ繝ｫ繝医・蜴溽せ
    // --- 繝懊・繝ｳ繝舌ャ繝輔ぃ髢｢騾｣ ---
    Microsoft::WRL::ComPtr<ID3D12Resource> boneResource_;
    BoneForGPU* boneMappedData_ = nullptr;
    uint32_t boneSrvIndex_ = 0; //  繝懊・繝ｳ諠・ｱSRV縺ｮ繧､繝ｳ繝・ャ繧ｯ繧ｹ
    uint32_t lastUpdateFrame_ = 0xFFFFFFFF; // 笘・ｿｽ蜉・壽怙蠕後↓譖ｴ譁ｰ縺励◆繝輔Ξ繝ｼ繝逡ｪ蜿ｷ

    Vector3 localAabbMin_ = { 0.0f, 0.0f, 0.0f };
    Vector3 localAabbMax_ = { 0.0f, 0.0f, 0.0f };
};

