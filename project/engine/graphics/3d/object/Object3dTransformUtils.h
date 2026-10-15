#pragma once

#include "Transform.h"
#include "engine/utility/math/Math.h"

#include <algorithm>

namespace Object3dTransformUtils {

// 行列から移動・倍率・回転を復元し、親変更と実行時コピーで同じ姿勢を使います。
inline void ApplyMatrixToTransform(Transform& transform, const engine::Matrix4x4& matrix) {
    // 行ベクトルの長さを倍率として取り出し、回転行列から倍率を除いて姿勢を復元する。
    const float epsilon = 0.0001f;

    engine::Vector3 rowX = { matrix.m[0][0], matrix.m[0][1], matrix.m[0][2] };
    engine::Vector3 rowY = { matrix.m[1][0], matrix.m[1][1], matrix.m[1][2] };
    engine::Vector3 rowZ = { matrix.m[2][0], matrix.m[2][1], matrix.m[2][2] };

    transform.scale = {
        std::max(engine::Math::Length(rowX), epsilon),
        std::max(engine::Math::Length(rowY), epsilon),
        std::max(engine::Math::Length(rowZ), epsilon),
    };
    transform.translate = { matrix.m[3][0], matrix.m[3][1], matrix.m[3][2] };

    engine::Matrix4x4 rotateMatrix = engine::Math::MakeIdentity4x4();
    rotateMatrix.m[0][0] = matrix.m[0][0] / transform.scale.x;
    rotateMatrix.m[0][1] = matrix.m[0][1] / transform.scale.x;
    rotateMatrix.m[0][2] = matrix.m[0][2] / transform.scale.x;
    rotateMatrix.m[1][0] = matrix.m[1][0] / transform.scale.y;
    rotateMatrix.m[1][1] = matrix.m[1][1] / transform.scale.y;
    rotateMatrix.m[1][2] = matrix.m[1][2] / transform.scale.y;
    rotateMatrix.m[2][0] = matrix.m[2][0] / transform.scale.z;
    rotateMatrix.m[2][1] = matrix.m[2][1] / transform.scale.z;
    rotateMatrix.m[2][2] = matrix.m[2][2] / transform.scale.z;

    transform.quaternion = engine::Math::MatrixToQuaternion(rotateMatrix);
    transform.rotate = engine::Math::MatrixToEuler(rotateMatrix);
    transform.isQuaternionMaster = true;
}

} // namespace Object3dTransformUtils
