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

class Object3d;

class Model {
public:

    // --- 骨 ---
    struct Bone {
        std::string name;
        Matrix4x4 inverseBindPoseMatrix; // 初期姿勢の逆行列
    };

    // --- アニメーション関連 ---
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
        std::string name;     // アニメーション名
        float duration;       // 全体の長さ(秒)
        float ticksPerSecond; // 1秒あたりの進行度
        std::vector<NodeAnimation> nodeAnimations; // 各骨の動きリスト
    };

    // --- マテリアル・ノード ---
    struct Material {
        Vector4 color;
        int32_t enableLighting;
        float padding1[3];
        Matrix4x4 uvTransform;
        int32_t selectedLighting;
        float shininess;
        int32_t materialType;
        float roughness;           // 4 byte (粗さ: 0.0=ツルツル, 1.0=ザラザラ)
        float metallic;            // 4 byte (金属度: 0.0=非金属, 1.0=金属)
        float padding2[2];         // 8 byte (アライメント調整)

    };

    struct Node {
        Matrix4x4 localMatrix;
        Matrix4x4 globalMatrix; // ★追加: アニメーション計算結果(ワールド行列)用
        std::string name;
        std::vector<Node> children;
    };

    struct VertexData {
        Vector4 position;
        Vector2 texcoord;
        Vector3 normal;
        Vector4 boneWeights; // 重み
        Vector4 boneIndices; // 骨番号
    };

    // --- データまとめる用 ---
    struct MaterialData {
        std::string textureFilePath;
        uint32_t textureHandle = 0;
        Microsoft::WRL::ComPtr<ID3D12Resource> materialResource;
        Material* materialData = nullptr;
    };

    struct Mesh {
        std::vector<VertexData> vertices;
        uint32_t materialIndex;
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
    };

    struct ModelData {
        std::vector<Mesh> meshes;
        std::vector<MaterialData> materials;
        Node rootNode;
        std::vector<Node> nodes;
        std::vector<Bone> bones;
        std::vector<Animation> animations;
    };

    struct BoneForGPU {
        Matrix4x4 finalMatrix;
    };


public: // メンバ関数
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(ModelCommon* common, const std::string& directoryPath, const std::string& filename);
    void Update();
    /// <summary>
    /// 描画
    /// </summary>
    void Draw(ID3D12Resource* wvpResource,
        ID3D12Resource* directionalLightResource,
        ID3D12Resource* cameraResource,
        ID3D12Resource* pointLightResource,
        ID3D12Resource* spotLightResource,
        ID3D12Resource* overrideMaterialResource = nullptr);

    /// <summary>
    /// マテリアル情報の取得 (ImGuiでの操作用)
    /// </summary>
    Material* GetMaterial() { return materialData_; }

    /// <summary>
    /// テクスチャハンドルを取得
    /// </summary>
    uint32_t GetTextureHandle() const {
        if (modelData_.materials.empty()) return 0;
        return modelData_.materials[0].textureHandle;
    }
    const ModelData& GetModelData() const { return modelData_; }
    Vector3 GetSize() const { return size_; }
    Vector3 GetCenter() const { return center_; }
    void ApplyAnimation(const Animation& animation, float time);
    const Animation* GetAnimation(const std::string& name) const;
private: // 内部処理関数
    static ModelData LoadFile(const std::string& directoryPath, const std::string& filename);
    static Node ReadNode(aiNode* node, std::vector<Node>& nodes);
    void UpdateNodeMatrix(Node& node, const Matrix4x4& parentMatrix);

    static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);

    Node* FindNode(Node& node, const std::string& name);

    // キーフレームから値を計算する（補間）
    static Vector3 CalculateValue(const std::vector<KeyframeVector3>& keyframes, float time);
    static Quaternion CalculateValue(const std::vector<KeyframeQuaternion>& keyframes, float time);

    // ボーンバッファ関連
    void CreateBoneBuffer();
    void UpdateBoneBuffer();

  

private: // メンバ変数
    ModelCommon* common_ = nullptr;
    ModelData modelData_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;

    Math math_;
    Vector3 size_ = { 1.0f, 1.0f, 1.0f };   // デフォルトは1x1x1
    Vector3 center_ = { 0.0f, 0.0f, 0.0f }; // デフォルトは原点

    // --- ボーンバッファ関連 ---
    Microsoft::WRL::ComPtr<ID3D12Resource> boneResource_;
    BoneForGPU* boneMappedData_ = nullptr;
    uint32_t boneSrvIndex_ = 0; // ★追加: ボーン情報SRVのインデックス
};