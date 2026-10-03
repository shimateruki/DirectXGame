#pragma once
#include "engine/utility/math/Math.h"

class InputManager; // 前方宣言

/// <summary>入力で移動・回転できる検証用カメラ。</summary>
class DebugCamera
{

public:

	/// <summary>初期位置、回転、行列を準備する。</summary>
	void Initialize();

	/// <summary>入力から位置と回転を更新する。</summary>
	void Update();
	const Matrix4x4& GetViewMatrix() const { return viewMatrix; }
	const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix; }

    /// <summary>デバッグカメラ操作に使う入力管理を設定する。</summary>
    void SetInputManager(InputManager* manager);
private:
	//x y z 周りのローカル回転角
	Vector3 rotation;

	//ローカル座標
	Vector3 translation;

	//ビュー行列
	Matrix4x4 viewMatrix;

	//射影行列
	Matrix4x4 projectionMatrix;
	InputManager* inputManager; // InputManagerのインスタンスへのポインタ
};

