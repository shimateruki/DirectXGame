#pragma once
#include "engine/base/Math.h"
#include "engine/io/InputManager.h"

/// <summary>
/// 3Dシーンの視点を管理するカメラクラス
/// </summary>
class Camera {
public:
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 毎フレームの更新
    /// </summary>
    void Update();

    // --- セッター ---
    void SetInputManager(InputManager* inputManager) { inputManager_ = inputManager; }

    // --- ゲッター ---
    const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
    const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }
    void SetTarget(const Vector3* target);

private:
    // --- カメラの三要素 ---
    Vector3 eye_ = { 0.0f, 0.0f, -10.0f }; // 視点 (カメラの位置)
    Vector3 target_ = { 0.0f, 0.0f, 0.0f };   // 注視点 (カメラが見つめる先)
    Vector3 up_ = { 0.0f, 1.0f, 0.0f };       // 上方向ベクトル

    // --- プロジェクション行列のパラメータ ---
    float fovY_ = 0.45f;        // 縦画角 (ラジアン)
    float aspectRatio_ = 16.0f / 9.0f; // アスペクト比
    float nearClip_ = 0.1f;     // 前方クリップ距離
    float farClip_ = 1000.0f;   // 後方クリップ距離

    // --- 生成される行列 ---
    Matrix4x4 viewMatrix_;
    Matrix4x4 projectionMatrix_;
    // 追尾対象のワールド座標へのポインタ
    const Vector3* targetPosition_ = nullptr;
    // 追尾対象からのオフセット (カメラがどれだけ離れるか)
    Vector3 followOffset_ = { 0.0f, 5.0f, -15.0f };

    // --- デバッグカメラ用のメンバ ---
    InputManager* inputManager_ = nullptr; // 入力クラスへのポインタ
    Vector3 rotation_ = { 0.0f, 0.0f, 0.0f }; // デバッグ用の回転


};