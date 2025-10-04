
#pragma once
#include "Math.h"
#include <string>
#include <vector>
#include <cstdint>

// 座標変換
struct Transform
{
	Vector3 scale;
	Vector3 rotate;
	Vector3 translate;
};

// 頂点データ
struct VertexData
{
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
};

// マテリアル (3Dオブジェクト用)
struct Material
{
	Vector4 color;
	int32_t enableLighting;
	float padding1[3];
	Matrix4x4 uvTransform;
	int32_t selectedLighting;
	float padding2[3];
};

// 座標変換行列
struct TransformationMatrix
{
	Matrix4x4 WVP;
	Matrix4x4 world;
};

// 平行光源
struct DirectionalLight
{
	Vector4 color;
	Vector3 direction;
	float padding; // 構造体のメモリ配置を揃えるためのパディング
	float intensity;
};

// マテリアルデータ (ファイルから読み込む情報)
struct MaterialData // ★ MateriaData から MaterialData に修正
{
	std::string textureFilePath;
	uint32_t textureHandle = 0;
};

// モデルデータ (OBJファイル全体のデータ)
struct ModelData
{
	std::vector<VertexData> vertices;
	MaterialData material;
};