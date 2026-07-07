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
// Model スケルトン管理
// ------------------------------------------------------------------------
// ジョイントの生成、姿勢リセット、行列更新、エディタ用の姿勢操作を
// 担当する。
// ========================================================================

// モデルがスケルトンを持つかを返し、スキニング処理の有無を判断する。
bool Model::HasSkeleton() const {
    return !modelData_.skeleton.joints.empty();
}

// 読み取り専用でジョイント一覧を返し、エディタ表示などから姿勢情報を参照できるようにする。
const std::vector<Model::Joint>& Model::GetJoints() const {
    return modelData_.skeleton.joints;
}

// ジョイント名からインデックスを探し、存在しない場合は-1を返す。
int Model::FindJointIndex(const std::string& name) const {
    auto it = modelData_.skeleton.jointMap.find(name);
    if (it == modelData_.skeleton.jointMap.end()) {
        return -1;
    }
    return it->second;
}

// 指定ジョイントの現在のローカルトランスフォームを取得する。
Model::QuaternionTransform Model::GetJointTransform(int jointIndex) const {
    if (jointIndex < 0 || jointIndex >= static_cast<int>(modelData_.skeleton.joints.size())) {
        return {};
    }
    return modelData_.skeleton.joints[jointIndex].transform;
}

// 指定ジョイントのローカルトランスフォームを更新し、エディタで調整した姿勢を反映する。
bool Model::SetJointTransform(int jointIndex, const QuaternionTransform& transform) {
    if (jointIndex < 0 || jointIndex >= static_cast<int>(modelData_.skeleton.joints.size())) {
        return false;
    }

    Joint& joint = modelData_.skeleton.joints[jointIndex];
    joint.transform = transform;
    Matrix4x4 mS = math_.MakeScaleMatrix(joint.transform.scale);
    Matrix4x4 mR = math_.MakeRotateQuaternionMatrix(joint.transform.rotate);
    Matrix4x4 mT = math_.MakeTranslateMatrix(joint.transform.translate);
    joint.localMatrix = math_.Multiply(mS, math_.Multiply(mR, mT));
    RebuildSkeletonForEditor();
    return true;
}

// 読み込み時のルートノードからスケルトンを作り直し、編集された姿勢を初期状態へ戻す。
void Model::ResetSkeletonPose() {
    modelData_.skeleton = CreateSkeleton(modelData_.rootNode);
    RebuildSkeletonForEditor();
}

// エディタでジョイントを直接編集した後、スケルトン行列とGPUバッファを再構築する。
void Model::RebuildSkeletonForEditor() {
    UpdateSkeleton(modelData_.skeleton);
    UpdateBoneBuffer();
}



// ノード情報からジョイントを作成し、親子関係と名前インデックスを登録する。
int32_t Model::CreateJoint(const Node& node, const std::optional<int32_t>& parent, std::vector<Joint>& joints) {
    Joint joint;
    joint.name = node.name;
    joint.localMatrix = node.localMatrix;
    
    Math math;
    joint.skeletonSpaceMatrix = math.MakeIdentity4x4();
    joint.transform = node.transform;
    joint.index = int32_t(joints.size()); // 現在登録されている数をIndexに
    joint.parent = parent;

    joints.push_back(joint); // SkeletonのJoint列に追加

    for (const Node& child : node.children) {
        // 子Jointを作成し、そのIndexを登録
        int32_t childIndex = CreateJoint(child, joint.index, joints);
        joints[joint.index].children.push_back(childIndex);
    }
    
    // 自身のIndexを返す
    return joint.index;
}

// ノード階層全体からスケルトン構造を生成し、各ジョイントの親子関係を整える。
Model::Skeleton Model::CreateSkeleton(const Node& rootNode) {
    Skeleton skeleton;
    skeleton.root = CreateJoint(rootNode, std::nullopt, skeleton.joints);
    
    // Joint名をキーにしてIndexを引けるMapを作る
    for (const Joint& joint : skeleton.joints) {
        skeleton.jointMap[joint.name] = joint.index;
    }
    
    return skeleton;
}

// 全ジョイントのローカル行列とスケルトン空間行列を親子順に更新する。
void Model::UpdateSkeleton(Skeleton& skeleton) {
    Math math;
    // すべてのJointを更新。親が若いので通常ループで処理可能になっている
    for (Joint& joint : skeleton.joints) {
        if (joint.parent) {
            // 親がいれば親の行列を掛ける
            joint.skeletonSpaceMatrix = math.Multiply(joint.localMatrix, skeleton.joints[*joint.parent].skeletonSpaceMatrix);
        } else {
            // 親がいないのでlocalMatrixとskeletonSpaceMatrixは一致する
            joint.skeletonSpaceMatrix = joint.localMatrix;
        }
    }
}

// アニメーションカーブをジョイントへ適用し、現在時刻の姿勢を作成する。
void Model::ApplyAnimationToSkeleton(Skeleton& skeleton, const Animation& animation, float time) {
    Math math;
    for (Joint& joint : skeleton.joints) {
        // 対象のJointのAnimationがあれば、値の適用を行う。
        auto it = std::find_if(animation.nodeAnimations.begin(), animation.nodeAnimations.end(),
            [&joint](const NodeAnimation& na) { return na.name == joint.name; });
            
        if (it != animation.nodeAnimations.end()) {
            const NodeAnimation& rootNodeAnimation = *it;
            joint.transform.translate = CalculateValue(rootNodeAnimation.translate, time);
            joint.transform.rotate = CalculateValue(rootNodeAnimation.rotate, time);
            joint.transform.scale = CalculateValue(rootNodeAnimation.scale, time);
            
            // アニメーションがある場合のみlocalMatrixを更新する
            Matrix4x4 mS = math.MakeScaleMatrix(joint.transform.scale);
            Matrix4x4 mR = math.MakeRotateQuaternionMatrix(joint.transform.rotate);
            Matrix4x4 mT = math.MakeTranslateMatrix(joint.transform.translate);
            joint.localMatrix = math.Multiply(mS, math.Multiply(mR, mT));
        }
    }
}
