#pragma once
#include "engine/utility/math/Math.h"

/// <summary>位置、回転、拡縮、親子関係とローカル/ワールド行列を保持する。</summary>
struct Transform {
    engine::Vector3 scale = { 1.0f, 1.0f, 1.0f };
    engine::Vector3 rotate = { 0.0f, 0.0f, 0.0f }; // インスペクター表示・操作用
    engine::Vector3 translate = { 0.0f, 0.0f, 0.0f };

    engine::Quaternion quaternion = { 0.0f, 0.0f, 0.0f, 1.0f };
    bool isQuaternionMaster = true; // falseの場合、次のUpdateMatrixでEuler角から再計算する。

    engine::Matrix4x4 matLocal = engine::Math::MakeIdentity4x4();
    engine::Matrix4x4 matWorld = engine::Math::MakeIdentity4x4();
    const Transform* parent = nullptr;

    /// <summary>拡縮、回転、移動と親行列からローカル/ワールド行列を更新する。</summary>
    void UpdateMatrix() {
        engine::Math math;
        engine::Matrix4x4 matScale = math.MakeScaleMatrix(scale);

        if (!isQuaternionMaster) {
            // スライダー等で手動変更された瞬間だけ、オイラー角から作り直す
            quaternion = math.EulerToQuaternion(rotate);
            isQuaternionMaster = true; // 一度作ったらクォータニオン優先に戻す
        }

        engine::Matrix4x4 matRot = math.MakeRotateQuaternionMatrix(quaternion);
        engine::Matrix4x4 matTrans = math.MakeTranslateMatrix(translate);

        matLocal = math.Multiply(math.Multiply(matScale, matRot), matTrans);

        if (parent) {
            matWorld = math.Multiply(matLocal, parent->matWorld);
        } else {
            matWorld = matLocal;
        }
    }
};
