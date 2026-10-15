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
    // 編集した関節のローカル行列を更新してから、子関節へ姿勢を伝播させる。
    if (jointIndex < 0 || jointIndex >= static_cast<int>(modelData_.skeleton.joints.size())) {
        return false;
    }

    Joint& joint = modelData_.skeleton.joints[jointIndex];
    joint.transform = transform;
    engine::Matrix4x4 mS = math_.MakeScaleMatrix(joint.transform.scale);
    engine::Matrix4x4 mR = math_.MakeRotateQuaternionMatrix(joint.transform.rotate);
    engine::Matrix4x4 mT = math_.MakeTranslateMatrix(joint.transform.translate);
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
    
    engine::Math math;
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
    engine::Math math;
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
    engine::Math math;
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
            engine::Matrix4x4 mS = math.MakeScaleMatrix(joint.transform.scale);
            engine::Matrix4x4 mR = math.MakeRotateQuaternionMatrix(joint.transform.rotate);
            engine::Matrix4x4 mT = math.MakeTranslateMatrix(joint.transform.translate);
            joint.localMatrix = math.Multiply(mS, math.Multiply(mR, mT));
        }
    }
}

// 2つのAnimationをJointごとに補間し、状態遷移中の急な姿勢変化を防ぎます。
void Model::ApplyBlendedAnimation(
    const Animation& fromAnimation,
    float fromTime,
    const Animation& toAnimation,
    float toTime,
    float blendWeight) {
    engine::Math math;
    const float weight = std::clamp(blendWeight, 0.0f, 1.0f);
    for (Joint& joint : modelData_.skeleton.joints) {
        const auto FindNodeAnimation = [&joint](const Animation& animation) -> const NodeAnimation* {
            const auto it = std::find_if(
                animation.nodeAnimations.begin(), animation.nodeAnimations.end(),
                [&joint](const NodeAnimation& node) { return node.name == joint.name; });
            return it == animation.nodeAnimations.end() ? nullptr : &(*it);
        };
        const NodeAnimation* fromNode = FindNodeAnimation(fromAnimation);
        const NodeAnimation* toNode = FindNodeAnimation(toAnimation);
        if (!fromNode && !toNode) {
            continue;
        }

        auto SampleScale = [](const NodeAnimation* node, float time, const engine::Vector3& fallback) {
            return node && !node->scale.empty() ? CalculateValue(node->scale, time) : fallback;
        };
        auto SampleTranslate = [](const NodeAnimation* node, float time, const engine::Vector3& fallback) {
            return node && !node->translate.empty() ? CalculateValue(node->translate, time) : fallback;
        };
        auto SampleRotate = [](const NodeAnimation* node, float time, const engine::Quaternion& fallback) {
            return node && !node->rotate.empty() ? CalculateValue(node->rotate, time) : fallback;
        };

        const engine::Vector3 fromScale = SampleScale(fromNode, fromTime, joint.transform.scale);
        const engine::Vector3 fromTranslate = SampleTranslate(fromNode, fromTime, joint.transform.translate);
        const engine::Quaternion fromRotate = SampleRotate(fromNode, fromTime, joint.transform.rotate);
        const engine::Vector3 toScale = SampleScale(toNode, toTime, fromScale);
        const engine::Vector3 toTranslate = SampleTranslate(toNode, toTime, fromTranslate);
        const engine::Quaternion toRotate = SampleRotate(toNode, toTime, fromRotate);

        joint.transform.scale = {
            std::lerp(fromScale.x, toScale.x, weight),
            std::lerp(fromScale.y, toScale.y, weight),
            std::lerp(fromScale.z, toScale.z, weight)
        };
        joint.transform.translate = {
            std::lerp(fromTranslate.x, toTranslate.x, weight),
            std::lerp(fromTranslate.y, toTranslate.y, weight),
            std::lerp(fromTranslate.z, toTranslate.z, weight)
        };
        joint.transform.rotate = engine::Math::Slerp(fromRotate, toRotate, weight);

        const engine::Matrix4x4 scaleMatrix = math.MakeScaleMatrix(joint.transform.scale);
        const engine::Matrix4x4 rotateMatrix = math.MakeRotateQuaternionMatrix(joint.transform.rotate);
        const engine::Matrix4x4 translateMatrix = math.MakeTranslateMatrix(joint.transform.translate);
        joint.localMatrix = math.Multiply(scaleMatrix, math.Multiply(rotateMatrix, translateMatrix));
    }
}
