#pragma once

#include "engine/utility/math/Math.h"

struct Transform {
    // --- ローカル情報 ---
    Vector3 scale = { 1.0f, 1.0f, 1.0f };
    Vector3 rotate = { 0.0f, 0.0f, 0.0f };
    Vector3 translate = { 0.0f, 0.0f, 0.0f };

    // --- 計算結果バッファ (Object3dから引っ越してきました) ---
    Matrix4x4 matLocal = Math::MakeIdentity4x4();
    Matrix4x4 matWorld = Math::MakeIdentity4x4();

    // --- 親子関係 ---
    const Transform* parent = nullptr;

    // --- 行列更新メソッド ---
    // これを呼ぶだけで Local も World も一発で計算するようにします
    void UpdateMatrix() {
        Math math;
        // 1. ローカル行列の計算 (Scale * Rotate * Translate)
        matLocal = math.MakeAffineMatrix(scale, rotate, translate);

        // 2. 親子関係の解決
        if (parent) {
            // 親がいる場合: 親のワールド行列 * 自分のローカル行列
            matWorld = math.Multiply(matLocal, parent->matWorld);
        } else {
            // 親がいない場合: ローカル行列がそのままワールド行列
            matWorld = matLocal;
        }
    }
};