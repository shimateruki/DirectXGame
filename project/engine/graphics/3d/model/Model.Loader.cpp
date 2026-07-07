#define NOMINMAX
#include <fstream>
#include "Model.h"
#include "DirectXCommon.h"
#include "engine/utility/math/Math.h"
#include <sstream>
#include <cassert>
#include <filesystem>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include "SRVManager.h"
#include <DebugConsole.h>
#include <LightManager.h>

namespace {
constexpr std::array<char, 8> kModelCacheMagic = { 'G', 'E', '3', 'M', 'C', 'A', 'C', 'H' };
constexpr uint32_t kModelCacheVersion = 2;
constexpr uint32_t kMaxCacheStringSize = 1024 * 1024;
constexpr uint32_t kMaxCacheVectorCount = 10'000'000;
constexpr const char* kDefaultWhiteTexture = "Resources/sprite/common/white.png";

template <typename T>
// POD値をバイナリへそのまま書き込み、モデルキャッシュ保存の基本単位にする。
bool WritePod(std::ostream& os, const T& value) {
    os.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return static_cast<bool>(os);
}

template <typename T>
// バイナリからPOD値を読み戻し、モデルキャッシュ復元の基本単位にする。
bool ReadPod(std::istream& is, T& value) {
    is.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(is);
}

// 文字列長と本文をセットで書き込み、可変長文字列を安全にキャッシュへ保存する。
bool WriteString(std::ostream& os, const std::string& text) {
    const uint32_t size = static_cast<uint32_t>(text.size());
    if (!WritePod(os, size)) {
        return false;
    }
    if (size > 0) {
        os.write(text.data(), size);
    }
    return static_cast<bool>(os);
}

// 文字列長を検証しながら本文を読み戻し、壊れたキャッシュで異常確保しないようにする。
bool ReadString(std::istream& is, std::string& text) {
    uint32_t size = 0;
    if (!ReadPod(is, size) || size > kMaxCacheStringSize) {
        return false;
    }
    text.resize(size);
    if (size > 0) {
        is.read(text.data(), size);
    }
    return static_cast<bool>(is);
}

template <typename T>
// POD配列の要素数と中身をまとめて書き込み、頂点やインデックス配列を保存する。
bool WritePodVector(std::ostream& os, const std::vector<T>& values) {
    const uint32_t count = static_cast<uint32_t>(values.size());
    if (!WritePod(os, count)) {
        return false;
    }
    if (count > 0) {
        os.write(reinterpret_cast<const char*>(values.data()), sizeof(T) * values.size());
    }
    return static_cast<bool>(os);
}

template <typename T>
// POD配列の要素数を検証してから読み戻し、不正なキャッシュサイズを弾く。
bool ReadPodVector(std::istream& is, std::vector<T>& values) {
    uint32_t count = 0;
    if (!ReadPod(is, count) || count > kMaxCacheVectorCount) {
        return false;
    }
    values.resize(count);
    if (count > 0) {
        is.read(reinterpret_cast<char*>(values.data()), sizeof(T) * values.size());
    }
    return static_cast<bool>(is);
}

// 0から1の範囲に値を丸め、色や係数の異常値を抑える。
float Clamp01(float value) {
    return (std::max)(0.0f, (std::min)(1.0f, value));
}

// 文字列を小文字化したコピーに変換し、拡張子や名前の比較を安定させる。
std::string ToLowerCopy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

// 対象文字列に候補キーワードのいずれかが含まれるかを判定する。
bool ContainsAny(std::string_view text, std::initializer_list<std::string_view> words) {
    for (std::string_view word : words) {
        if (text.find(word) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

// マテリアルに書かれたテクスチャパスを、モデルフォルダ基準の実在パスへ解決する。
std::string ResolveMaterialTexturePath(const std::string& directoryPath, const std::string& rawPath) {
    if (rawPath.empty()) {
        return "";
    }

    std::filesystem::path path(rawPath);
    if (path.is_absolute() && std::filesystem::exists(path)) {
        return path.generic_string();
    }

    const std::filesystem::path relativeCandidate = std::filesystem::path(directoryPath) / path;
    if (std::filesystem::exists(relativeCandidate)) {
        return relativeCandidate.generic_string();
    }

    const std::filesystem::path filenameCandidate = std::filesystem::path(directoryPath) / path.filename();
    return filenameCandidate.generic_string();
}

// Assimpマテリアルから指定種別のテクスチャを取得し、解決済みパスとして返す。
bool TryGetMaterialTexture(aiMaterial* material, const std::vector<aiTextureType>& types, aiString& outPath) {
    if (!material) {
        return false;
    }
    for (aiTextureType type : types) {
        if (material->GetTexture(type, 0, &outPath) == AI_SUCCESS) {
            return true;
        }
    }
    return false;
}

// 種別情報が曖昧なテクスチャから用途に合う候補を探し、補助的に採用する。
bool TryFindUnknownTexture(aiMaterial* material, std::initializer_list<std::string_view> keywords, aiString& outPath) {
    if (!material) {
        return false;
    }
    const unsigned int count = material->GetTextureCount(aiTextureType_UNKNOWN);
    for (unsigned int i = 0; i < count; ++i) {
        aiString candidate;
        if (material->GetTexture(aiTextureType_UNKNOWN, i, &candidate) != AI_SUCCESS) {
            continue;
        }
        const std::string lower = ToLowerCopy(candidate.C_Str());
        if (ContainsAny(lower, keywords)) {
            outPath = candidate;
            return true;
        }
    }
    return false;
}

// モデルファイル名から対応する独自キャッシュファイルの保存先パスを作成する。
std::filesystem::path MakeModelCachePath(const std::filesystem::path& sourcePath) {
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::current_path(ec).lexically_normal();
    std::filesystem::path absoluteSource = std::filesystem::absolute(sourcePath, ec).lexically_normal();
    std::filesystem::path relativeSource = ec ? sourcePath : absoluteSource.lexically_relative(root);
    if (relativeSource.empty() || relativeSource.native().find(L"..") == 0) {
        relativeSource = sourcePath.filename();
    }

    std::filesystem::path cachePath = std::filesystem::path("Resources/.cache/model") / relativeSource;
    cachePath += ".meshcache";
    return cachePath.lexically_normal();
}

// モデル本体と依存ファイルの更新時刻を調べ、キャッシュの新しさ判定に使う。
std::filesystem::file_time_type GetLatestModelDependencyTime(const std::filesystem::path& sourcePath) {
    std::error_code ec;
    auto latest = std::filesystem::last_write_time(sourcePath, ec);
    if (ec) {
        return std::filesystem::file_time_type::min();
    }

    const std::filesystem::path parent = sourcePath.parent_path();
    if (!std::filesystem::exists(parent, ec)) {
        return latest;
    }

    for (const auto& entry : std::filesystem::directory_iterator(parent, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (ext != ".bin" && ext != ".mtl" && ext != ".gltf" && ext != ".glb" && ext != ".obj") {
            continue;
        }

        auto time = std::filesystem::last_write_time(entry.path(), ec);
        if (!ec && time > latest) {
            latest = time;
        }
    }
    return latest;
}

// キャッシュファイルがモデルや依存ファイルより新しいかを確認し、再読み込みの要否を決める。
bool IsModelCacheUpToDate(const std::filesystem::path& sourcePath, const std::filesystem::path& cachePath) {
    std::error_code ec;
    if (!std::filesystem::exists(sourcePath, ec) || !std::filesystem::exists(cachePath, ec)) {
        return false;
    }

    const auto sourceTime = GetLatestModelDependencyTime(sourcePath);
    const auto cacheTime = std::filesystem::last_write_time(cachePath, ec);
    if (ec) {
        return false;
    }
    return cacheTime >= sourceTime;
}

// ノードの変換行列と子階層を再帰的に書き込み、階層構造をキャッシュへ保存する。
bool WriteNode(std::ostream& os, const Model::Node& node) {
    if (!WritePod(os, node.transform) ||
        !WritePod(os, node.localMatrix) ||
        !WriteString(os, node.name)) {
        return false;
    }

    const uint32_t childCount = static_cast<uint32_t>(node.children.size());
    if (!WritePod(os, childCount)) {
        return false;
    }
    for (const auto& child : node.children) {
        if (!WriteNode(os, child)) {
            return false;
        }
    }
    return true;
}

// キャッシュからノード階層を再帰的に読み戻し、モデルの親子構造を復元する。
bool ReadNode(std::istream& is, Model::Node& node) {
    if (!ReadPod(is, node.transform) ||
        !ReadPod(is, node.localMatrix) ||
        !ReadString(is, node.name)) {
        return false;
    }
    node.globalMatrix = node.localMatrix;

    uint32_t childCount = 0;
    if (!ReadPod(is, childCount) || childCount > kMaxCacheVectorCount) {
        return false;
    }
    node.children.resize(childCount);
    for (auto& child : node.children) {
        if (!ReadNode(is, child)) {
            return false;
        }
    }
    return true;
}

// ノード階層を走査して名前付きノードの参照表を作り、アニメーション適用先を探しやすくする。
void CollectNodes(const Model::Node& node, std::vector<Model::Node>& nodes) {
    nodes.push_back(node);
    for (const auto& child : node.children) {
        CollectNodes(child, nodes);
    }
}

// アニメーション名・時間・各ノードのキー情報をキャッシュへ保存する。
bool WriteAnimation(std::ostream& os, const Model::Animation& animation) {
    if (!WriteString(os, animation.name) ||
        !WritePod(os, animation.duration) ||
        !WritePod(os, animation.ticksPerSecond)) {
        return false;
    }

    const uint32_t nodeAnimCount = static_cast<uint32_t>(animation.nodeAnimations.size());
    if (!WritePod(os, nodeAnimCount)) {
        return false;
    }
    for (const auto& nodeAnim : animation.nodeAnimations) {
        if (!WriteString(os, nodeAnim.name) ||
            !WritePodVector(os, nodeAnim.scale) ||
            !WritePodVector(os, nodeAnim.rotate) ||
            !WritePodVector(os, nodeAnim.translate)) {
            return false;
        }
    }
    return true;
}

// キャッシュからアニメーション情報を読み戻し、再生に使える形へ復元する。
bool ReadAnimation(std::istream& is, Model::Animation& animation) {
    if (!ReadString(is, animation.name) ||
        !ReadPod(is, animation.duration) ||
        !ReadPod(is, animation.ticksPerSecond)) {
        return false;
    }

    uint32_t nodeAnimCount = 0;
    if (!ReadPod(is, nodeAnimCount) || nodeAnimCount > kMaxCacheVectorCount) {
        return false;
    }
    animation.nodeAnimations.resize(nodeAnimCount);
    for (auto& nodeAnim : animation.nodeAnimations) {
        if (!ReadString(is, nodeAnim.name) ||
            !ReadPodVector(is, nodeAnim.scale) ||
            !ReadPodVector(is, nodeAnim.rotate) ||
            !ReadPodVector(is, nodeAnim.translate)) {
            return false;
        }
    }
    return true;
}

// 読み込み済みModelData全体を独自キャッシュとして保存し、次回起動時の読み込みを軽くする。
bool WriteModelCache(const std::filesystem::path& sourcePath, const Model::ModelData& data) {
    const std::filesystem::path cachePath = MakeModelCachePath(sourcePath);
    std::error_code ec;
    std::filesystem::create_directories(cachePath.parent_path(), ec);
    if (ec) {
        return false;
    }

    std::ofstream os(cachePath, std::ios::binary);
    if (!os) {
        return false;
    }

    os.write(kModelCacheMagic.data(), kModelCacheMagic.size());
    if (!WritePod(os, kModelCacheVersion)) {
        return false;
    }

    const uint8_t hasSkinning = data.hasSkinning ? 1 : 0;
    const uint8_t usesNodeAnimationProxy = data.usesNodeAnimationProxy ? 1 : 0;
    if (!WritePod(os, hasSkinning) || !WritePod(os, usesNodeAnimationProxy)) {
        return false;
    }

    const uint32_t materialCount = static_cast<uint32_t>(data.materials.size());
    if (!WritePod(os, materialCount)) {
        return false;
    }
    for (const auto& material : data.materials) {
        const uint8_t hasNormalMap = material.hasNormalMap ? 1 : 0;
        const uint8_t hasOrmMap = material.hasOrmMap ? 1 : 0;
        const uint8_t doubleSided = material.doubleSided ? 1 : 0;
        if (!WriteString(os, material.textureFilePath) ||
            !WriteString(os, material.normalMapPath) ||
            !WriteString(os, material.ormMapPath) ||
            !WritePod(os, material.baseColorFactor) ||
            !WritePod(os, material.roughness) ||
            !WritePod(os, material.metallic) ||
            !WritePod(os, hasNormalMap) ||
            !WritePod(os, hasOrmMap) ||
            !WritePod(os, doubleSided)) {
            return false;
        }
    }

    const uint32_t meshCount = static_cast<uint32_t>(data.meshes.size());
    if (!WritePod(os, meshCount)) {
        return false;
    }
    for (const auto& mesh : data.meshes) {
        if (!WritePod(os, mesh.materialIndex) ||
            !WritePodVector(os, mesh.vertices) ||
            !WritePodVector(os, mesh.indices)) {
            return false;
        }
    }

    const uint32_t boneCount = static_cast<uint32_t>(data.bones.size());
    if (!WritePod(os, boneCount)) {
        return false;
    }
    for (const auto& bone : data.bones) {
        if (!WriteString(os, bone.name) || !WritePod(os, bone.inverseBindPoseMatrix)) {
            return false;
        }
    }

    if (!WriteNode(os, data.rootNode)) {
        return false;
    }

    const uint32_t animationCount = static_cast<uint32_t>(data.animations.size());
    if (!WritePod(os, animationCount)) {
        return false;
    }
    for (const auto& animation : data.animations) {
        if (!WriteAnimation(os, animation)) {
            return false;
        }
    }

    return static_cast<bool>(os);
}

// 独自キャッシュからModelData全体を復元し、通常読み込みを省略できるか確認する。
bool ReadModelCache(const std::filesystem::path& sourcePath, Model::ModelData& data) {
    const std::filesystem::path cachePath = MakeModelCachePath(sourcePath);
    if (!IsModelCacheUpToDate(sourcePath, cachePath)) {
        return false;
    }

    std::ifstream is(cachePath, std::ios::binary);
    if (!is) {
        return false;
    }

    std::array<char, 8> magic{};
    is.read(magic.data(), magic.size());
    uint32_t version = 0;
    if (!is || magic != kModelCacheMagic || !ReadPod(is, version) || version != kModelCacheVersion) {
        return false;
    }

    uint8_t hasSkinning = 0;
    uint8_t usesNodeAnimationProxy = 0;
    if (!ReadPod(is, hasSkinning) || !ReadPod(is, usesNodeAnimationProxy)) {
        return false;
    }
    data.hasSkinning = hasSkinning != 0;
    data.usesNodeAnimationProxy = usesNodeAnimationProxy != 0;

    uint32_t materialCount = 0;
    if (!ReadPod(is, materialCount) || materialCount > kMaxCacheVectorCount) {
        return false;
    }
    data.materials.resize(materialCount);
    for (auto& material : data.materials) {
        uint8_t hasNormalMap = 0;
        uint8_t hasOrmMap = 0;
        uint8_t doubleSided = 0;
        if (!ReadString(is, material.textureFilePath) ||
            !ReadString(is, material.normalMapPath) ||
            !ReadString(is, material.ormMapPath) ||
            !ReadPod(is, material.baseColorFactor) ||
            !ReadPod(is, material.roughness) ||
            !ReadPod(is, material.metallic) ||
            !ReadPod(is, hasNormalMap) ||
            !ReadPod(is, hasOrmMap) ||
            !ReadPod(is, doubleSided)) {
            return false;
        }
        material.hasNormalMap = hasNormalMap != 0;
        material.hasOrmMap = hasOrmMap != 0;
        material.doubleSided = doubleSided != 0;
        material.textureHandle = 0;
        material.normalMapHandle = 0;
        material.ormMapHandle = 0;
        material.materialResource.Reset();
        material.materialData = nullptr;
    }

    uint32_t meshCount = 0;
    if (!ReadPod(is, meshCount) || meshCount > kMaxCacheVectorCount) {
        return false;
    }
    data.meshes.resize(meshCount);
    for (auto& mesh : data.meshes) {
        if (!ReadPod(is, mesh.materialIndex) ||
            !ReadPodVector(is, mesh.vertices) ||
            !ReadPodVector(is, mesh.indices)) {
            return false;
        }
        mesh.vertexResource.Reset();
        mesh.indexResource.Reset();
        mesh.vertexBufferView = {};
        mesh.indexBufferView = {};
    }

    uint32_t boneCount = 0;
    if (!ReadPod(is, boneCount) || boneCount > kMaxCacheVectorCount) {
        return false;
    }
    data.bones.resize(boneCount);
    for (auto& bone : data.bones) {
        if (!ReadString(is, bone.name) || !ReadPod(is, bone.inverseBindPoseMatrix)) {
            return false;
        }
    }

    if (!ReadNode(is, data.rootNode)) {
        return false;
    }
    data.nodes.clear();
    CollectNodes(data.rootNode, data.nodes);

    uint32_t animationCount = 0;
    if (!ReadPod(is, animationCount) || animationCount > kMaxCacheVectorCount) {
        return false;
    }
    data.animations.resize(animationCount);
    for (auto& animation : data.animations) {
        if (!ReadAnimation(is, animation)) {
            return false;
        }
    }

    return static_cast<bool>(is);
}
}


// ==========================================
// 初期化: メッシュごとにバッファを作る
// ==========================================

// ========================================================================
// Model 読み込み処理
// ------------------------------------------------------------------------
// Assimpからモデル/マテリアル/ボーン/アニメーションを抽出し、
// 独自キャッシュを利用して読み込みコストを抑える処理を担当する。
// ========================================================================

// モデルファイルを読み込み、メッシュ・マテリアル・ボーン・アニメーション情報をModelDataへ変換する。
Model::ModelData Model::LoadFile(const std::string& directoryPath, const std::string& filename) {

    ModelData modelData;

    std::string sep = (directoryPath.back() == '/' || directoryPath.back() == '\\') ? "" : "/";
    std::string filePath = directoryPath + sep + filename;

    if (ReadModelCache(filePath, modelData)) {
        modelData.skeleton = CreateSkeleton(modelData.rootNode);
        return modelData;
    }

    Assimp::Importer importer;


    const aiScene* scene = importer.ReadFile(filePath,
        aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_ConvertToLeftHanded |
        aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices | aiProcess_OptimizeMeshes);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        return modelData;
    }

    // 1. ノード読み込み
    modelData.rootNode = ReadNode(scene->mRootNode, modelData.nodes);

    // 2. マテリアルの読み込み
    modelData.materials.resize(scene->mNumMaterials);
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        aiMaterial* aiMat = scene->mMaterials[i];
        aiString texPath;
        MaterialData& material = modelData.materials[i];

        if (TryGetMaterialTexture(aiMat, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE }, texPath)) {
            material.textureFilePath = ResolveMaterialTexturePath(directoryPath, texPath.C_Str());
        }
        else {
            material.textureFilePath = kDefaultWhiteTexture;
        }

        if (TryGetMaterialTexture(aiMat, { aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA, aiTextureType_HEIGHT }, texPath) ||
            TryFindUnknownTexture(aiMat, { "normal", "nrm" }, texPath)) {
            material.normalMapPath = ResolveMaterialTexturePath(directoryPath, texPath.C_Str());
            material.hasNormalMap = true;
        }

        if (TryGetMaterialTexture(aiMat, { aiTextureType_METALNESS, aiTextureType_DIFFUSE_ROUGHNESS }, texPath) ||
            TryFindUnknownTexture(aiMat, { "metallicroughness", "metallic_roughness", "metallic-roughness", "orm", "arm", "roughness", "metallic" }, texPath)) {
            material.ormMapPath = ResolveMaterialTexturePath(directoryPath, texPath.C_Str());
            material.hasOrmMap = true;
        }

        aiColor4D baseColor{};
        if (aiMat->Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS ||
            aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor) == AI_SUCCESS) {
            material.baseColorFactor = { baseColor.r, baseColor.g, baseColor.b, baseColor.a };
        }

        float roughness = material.roughness;
        if (aiMat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
            material.roughness = Clamp01(roughness);
        }
        else {
            float shininess = 0.0f;
            if (aiMat->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS && shininess > 0.0f) {
                material.roughness = Clamp01(1.0f - (shininess / 128.0f));
            }
        }

        float metallic = material.metallic;
        if (aiMat->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) {
            material.metallic = Clamp01(metallic);
        }

        int twoSided = 0;
        if (aiMat->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) {
            material.doubleSided = twoSided != 0;
        }
    }

    // 3. メッシュの解析
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* aiMesh = scene->mMeshes[i];
        Mesh mesh;
        mesh.materialIndex = aiMesh->mMaterialIndex;

        std::vector<VertexData> tempVertices;
        tempVertices.resize(aiMesh->mNumVertices);

        for (unsigned int v = 0; v < aiMesh->mNumVertices; ++v) {
            VertexData& vertex = tempVertices[v];
            vertex.position = { aiMesh->mVertices[v].x, aiMesh->mVertices[v].y, aiMesh->mVertices[v].z, 1.0f };

            // 法線の取得
            if (aiMesh->HasNormals()) {
                vertex.normal = { aiMesh->mNormals[v].x, aiMesh->mNormals[v].y, aiMesh->mNormals[v].z };
            }
            else {
                vertex.normal = { 0.0f, 1.0f, 0.0f };
            }


            if (aiMesh->HasTangentsAndBitangents()) {
                vertex.tangent = { aiMesh->mTangents[v].x, aiMesh->mTangents[v].y, aiMesh->mTangents[v].z };
            }
            else {
                vertex.tangent = { 1.0f, 0.0f, 0.0f }; // 計算できなかった場合の安全対策(仮のX軸)
            }
            // ==========================================

            // UVの取得
            if (aiMesh->HasTextureCoords(0)) {
                vertex.texcoord = { aiMesh->mTextureCoords[0][v].x, aiMesh->mTextureCoords[0][v].y };
            }
            else {
                vertex.texcoord = { 0.0f, 0.0f };
            }
            vertex.boneWeights = { 0.0f, 0.0f, 0.0f, 0.0f };
            vertex.boneIndices = { 0.0f, 0.0f, 0.0f, 0.0f };
        }

        // ボーン解析
        if (aiMesh->mNumBones > 0) {
            modelData.hasSkinning = true;
        }
        for (unsigned int b = 0; b < aiMesh->mNumBones; ++b) {
            aiBone* aiBone = aiMesh->mBones[b];
            std::string boneName = aiBone->mName.C_Str();
            int boneIndex = -1;
            for (int k = 0; k < modelData.bones.size(); ++k) {
                if (modelData.bones[k].name == boneName) {
                    boneIndex = k;
                    break;
                }
            }
            if (boneIndex == -1) {
                boneIndex = (int)modelData.bones.size();
                Bone newBone;
                newBone.name = boneName;
                aiMatrix4x4 offset = aiBone->mOffsetMatrix;
                newBone.inverseBindPoseMatrix.m[0][0] = offset.a1; newBone.inverseBindPoseMatrix.m[0][1] = offset.b1; newBone.inverseBindPoseMatrix.m[0][2] = offset.c1; newBone.inverseBindPoseMatrix.m[0][3] = offset.d1;
                newBone.inverseBindPoseMatrix.m[1][0] = offset.a2; newBone.inverseBindPoseMatrix.m[1][1] = offset.b2; newBone.inverseBindPoseMatrix.m[1][2] = offset.c2; newBone.inverseBindPoseMatrix.m[1][3] = offset.d2;
                newBone.inverseBindPoseMatrix.m[2][0] = offset.a3; newBone.inverseBindPoseMatrix.m[2][1] = offset.b3; newBone.inverseBindPoseMatrix.m[2][2] = offset.c3; newBone.inverseBindPoseMatrix.m[2][3] = offset.d3;
                newBone.inverseBindPoseMatrix.m[3][0] = offset.a4; newBone.inverseBindPoseMatrix.m[3][1] = offset.b4; newBone.inverseBindPoseMatrix.m[3][2] = offset.c4; newBone.inverseBindPoseMatrix.m[3][3] = offset.d4;
                modelData.bones.push_back(newBone);
            }

            for (unsigned int w = 0; w < aiBone->mNumWeights; ++w) {
                unsigned int vertexId = aiBone->mWeights[w].mVertexId;
                float weight = aiBone->mWeights[w].mWeight;
                if (vertexId < tempVertices.size()) {
                    auto& v = tempVertices[vertexId];
                    if (v.boneWeights.x == 0.0f) { v.boneWeights.x = weight; v.boneIndices.x = (float)boneIndex; }
                    else if (v.boneWeights.y == 0.0f) { v.boneWeights.y = weight; v.boneIndices.y = (float)boneIndex; }
                    else if (v.boneWeights.z == 0.0f) { v.boneWeights.z = weight; v.boneIndices.z = (float)boneIndex; }
                    else if (v.boneWeights.w == 0.0f) { v.boneWeights.w = weight; v.boneIndices.w = (float)boneIndex; }
                }
            }
        }

        mesh.vertices = tempVertices;

        for (unsigned int f = 0; f < aiMesh->mNumFaces; f++) {
            aiFace face = aiMesh->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                // 頂点をコピーするのではなく、頂点の「番号」だけを記録する！
                mesh.indices.push_back(face.mIndices[j]);
            }
        }
        modelData.meshes.push_back(mesh);
    }

    // =========================================================
    // : ボーンがない場合の対処 (ダミーボーン作戦)
    // =========================================================
    if (modelData.bones.empty()) {
        // 1. ダミーボーンを作る (単位行列)
        // アニメーションが適用されるNode名（多くの場合ルートの最初の子）と同じ名前にすることで、
        // ボーン無しの剛体アニメーションも正しく動作するようにする
        Bone dummyBone;
        if (scene->mNumAnimations > 0 && scene->mAnimations[0]->mNumChannels > 0) {
            dummyBone.name = scene->mAnimations[0]->mChannels[0]->mNodeName.C_Str();
            modelData.usesNodeAnimationProxy = true;
        } else if (scene->mRootNode && scene->mRootNode->mNumChildren > 0) {
            dummyBone.name = "__StaticMeshIdentityBone";
        } else {
            dummyBone.name = "__StaticMeshIdentityBone";
        }
        Math math;
        dummyBone.inverseBindPoseMatrix = math.MakeIdentity4x4();
        modelData.bones.push_back(dummyBone);

        // 2. すべての頂点にダミーボーンの影響(100%)を与える
        for (auto& mesh : modelData.meshes) {
            for (auto& v : mesh.vertices) {
                if (v.boneWeights.x == 0.0f && v.boneWeights.y == 0.0f &&
                    v.boneWeights.z == 0.0f && v.boneWeights.w == 0.0f) {

                    v.boneWeights = { 1.0f, 0.0f, 0.0f, 0.0f };
                    v.boneIndices = { 0.0f, 0.0f, 0.0f, 0.0f };
                }
            }
        }
    }

    // =========================================================
    // 4. アニメーションの読み込み 
    // =========================================================
    for (unsigned int i = 0; i < scene->mNumAnimations; ++i) {
        aiAnimation* aiAnim = scene->mAnimations[i];
        Animation animation;
        if (aiAnim->mName.length > 0) animation.name = aiAnim->mName.C_Str();
        else animation.name = "animation_" + std::to_string(i);
        float ticksPerSecond = (float)(aiAnim->mTicksPerSecond != 0 ? aiAnim->mTicksPerSecond : 25.0);

        animation.duration = (float)aiAnim->mDuration / ticksPerSecond;
        animation.ticksPerSecond = ticksPerSecond;

        for (unsigned int c = 0; c < aiAnim->mNumChannels; ++c) {
            aiNodeAnim* aiChannel = aiAnim->mChannels[c];
            NodeAnimation nodeAnim;
            nodeAnim.name = aiChannel->mNodeName.C_Str();

            // デバッグログ (必要に応じて残す)
            if (nodeAnim.name.find("Hips") != std::string::npos) {
                std::string log = "AnimNode: " + nodeAnim.name +
                    " | PosKeys: " + std::to_string(aiChannel->mNumPositionKeys) + "\n";
                DebugConsole::GetInstance()->AddLog(log.c_str());
            }



            // Position
            for (unsigned int k = 0; k < aiChannel->mNumPositionKeys; ++k) {
                aiVectorKey& key = aiChannel->mPositionKeys[k];
                KeyframeVector3 kf;
                kf.time = (float)key.mTime / ticksPerSecond;
                kf.value = { key.mValue.x, key.mValue.y, key.mValue.z };
                nodeAnim.translate.push_back(kf);
            }

            // Rotation
            for (unsigned int k = 0; k < aiChannel->mNumRotationKeys; ++k) {
                aiQuatKey& key = aiChannel->mRotationKeys[k];
                KeyframeQuaternion kf;
                kf.time = (float)key.mTime / ticksPerSecond;
                kf.value = { key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w };
                nodeAnim.rotate.push_back(kf);
            }

            // Scaling
            for (unsigned int k = 0; k < aiChannel->mNumScalingKeys; ++k) {
                aiVectorKey& key = aiChannel->mScalingKeys[k];
                KeyframeVector3 kf;
                kf.time = (float)key.mTime / ticksPerSecond;
                kf.value = { key.mValue.x, key.mValue.y, key.mValue.z };
                nodeAnim.scale.push_back(kf);
            }
            animation.nodeAnimations.push_back(nodeAnim);
        }
        modelData.animations.push_back(animation);
    }
    // ★ 追加: 読み込んだノードツリーからSkeleton(1次元配列)を作成する
    modelData.skeleton = CreateSkeleton(modelData.rootNode);

    WriteModelCache(filePath, modelData);

    return modelData;
}

// ノード読み込み
// Assimpのノード階層をゲーム側のNode構造へ再帰的に変換する。
Model::Node Model::ReadNode(aiNode* node, std::vector<Node>& nodes) {
    Node result;
    result.name = node->mName.C_Str();
    aiMatrix4x4 transform = node->mTransformation;
    result.localMatrix.m[0][0] = transform.a1; result.localMatrix.m[0][1] = transform.b1; result.localMatrix.m[0][2] = transform.c1; result.localMatrix.m[0][3] = transform.d1;
    result.localMatrix.m[1][0] = transform.a2; result.localMatrix.m[1][1] = transform.b2; result.localMatrix.m[1][2] = transform.c2; result.localMatrix.m[1][3] = transform.d2;
    result.localMatrix.m[2][0] = transform.a3; result.localMatrix.m[2][1] = transform.b3; result.localMatrix.m[2][2] = transform.c3; result.localMatrix.m[2][3] = transform.d3;
    result.localMatrix.m[3][0] = transform.a4; result.localMatrix.m[3][1] = transform.b4; result.localMatrix.m[3][2] = transform.c4; result.localMatrix.m[3][3] = transform.d4;
    
    aiVector3D scale, translate;
    aiQuaternion rotate;
    transform.Decompose(scale, rotate, translate);
    result.transform.scale = {scale.x, scale.y, scale.z};
    result.transform.rotate = {rotate.x, rotate.y, rotate.z, rotate.w};
    result.transform.translate = {translate.x, translate.y, translate.z};

    nodes.push_back(result);
    result.children.resize(node->mNumChildren);
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        result.children[i] = ReadNode(node->mChildren[i], nodes);
    }
    return result;
}



#include "DirectXCommon.h"

// 毎フレーム呼ぶ更新処理
