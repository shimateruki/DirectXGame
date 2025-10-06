#pragma once
#include "engine/base/Math.h"

class InputManager; // 前方宣言

class DebugCamera
{

public:

	void Initialize();
	void Update();
	const Matrix4x4& GetViewMatrix() const { return viewMatrix; }
	const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix; }

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
	Math* math;
	InputManager* inputManager; // InputManagerのインスタンスへのポインタ
};

