#pragma once
#include "engine/utility/math/Math.h"

// Transformは、位置、回転、拡縮、親子関係、ローカル/ワールド行列を保持します。
struct Transform {
    Vector3 scale = { 1.0f, 1.0f, 1.0f };
    Vector3 rotate = { 0.0f, 0.0f, 0.0f }; // インスペクター表示・操作用
    Vector3 translate = { 0.0f, 0.0f, 0.0f };

    Quaternion quaternion = { 0.0f, 0.0f, 0.0f, 1.0f };
    bool isQuaternionMaster = true; // ★追加：クォータニオンを優先するかどうか

    Matrix4x4 matLocal = Math::MakeIdentity4x4();
    Matrix4x4 matWorld = Math::MakeIdentity4x4();
    const Transform* parent = nullptr;

        // scale、quaternion、translateと親行列からローカル行列とワールド行列を更新します。
void UpdateMatrix() {
        Math math;
        Matrix4x4 matScale = math.MakeScaleMatrix(scale);

        if (!isQuaternionMaster) {
            // スライダー等で手動変更された瞬間だけ、オイラー角から作り直す
            quaternion = math.EulerToQuaternion(rotate);
            isQuaternionMaster = true; // 一度作ったらクォータニオン優先に戻す
        }

        Matrix4x4 matRot = math.MakeRotateQuaternionMatrix(quaternion);
        Matrix4x4 matTrans = math.MakeTranslateMatrix(translate);

        matLocal = math.Multiply(math.Multiply(matScale, matRot), matTrans);

        if (parent) {
            matWorld = math.Multiply(matLocal, parent->matWorld);
        } else {
            matWorld = matLocal;
        }
    }
};