
#pragma once
#include "Math.h"
#include <string>
#include <vector>
#include <cstdint>

// ���W�ϊ�
struct Transform
{
	Vector3 scale;
	Vector3 rotate;
	Vector3 translate;
};

// ���_�f�[�^
struct VertexData
{
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
};

// �}�e���A�� (3D�I�u�W�F�N�g�p)
struct Material
{
	Vector4 color;
	int32_t enableLighting;
	float padding1[3];
	Matrix4x4 uvTransform;
	int32_t selectedLighting;
	float padding2[3];
};

// ���W�ϊ��s��
struct TransformationMatrix
{
	Matrix4x4 WVP;
	Matrix4x4 world;
};

// ���s����
struct DirectionalLight
{
	Vector4 color;
	Vector3 direction;
	float padding; // �\���̂̃������z�u�𑵂��邽�߂̃p�f�B���O
	float intensity;
};

// �}�e���A���f�[�^ (�t�@�C������ǂݍ��ޏ��)
struct MaterialData // �� MateriaData ���� MaterialData �ɏC��
{
	std::string textureFilePath;
	uint32_t textureHandle = 0;
};

// ���f���f�[�^ (OBJ�t�@�C���S�̂̃f�[�^)
struct ModelData
{
	std::vector<VertexData> vertices;
	MaterialData material;
};