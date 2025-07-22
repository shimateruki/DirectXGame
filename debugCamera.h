#pragma once
#include "Math.h"
class debugCamera
{

public:

	void Initialize();
	void Update();
private:
	//x y z 周りのローカル回転角
	Vector3 rotation = { 0.0f, 0.0f, 0.0f };
	//ローカル座標
	Vector3 translation = { 0.0f, 0.0f, -50.0f };
	//ビュー行列
	Matrix4x4 viewMatrix;
	//射影行列
	Matrix4x4 projectionMatrix;
};

