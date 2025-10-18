#pragma once



struct Vector2
{
	float x;
	float y;
};

struct Vector3
{
	float x;
	float y;
	float z;
};


struct Vector4
{
	float x;
	float y;
	float z;
	float w;
};

struct Matrix3x3
{
	float m[3][3];
};


struct Matrix4x4
{
	float m[4][4];
};
Vector3 operator-(const Vector3& v1, const Vector3& v2);
Vector3 operator*(const Vector3& v, float scalar);
Vector3 operator+(const Vector3& v1, const Vector3& v2);
Vector3 operator/(const Vector3& v, float scalar);
Vector3 operator-(const Vector3& v);

Matrix4x4 operator*(const Matrix4x4& m1, const Matrix4x4& m2);
Vector3& operator+=(Vector3& v1, const Vector3& v2);

class Math
{


public:

	Matrix4x4 makeIdentity4x4();
	Matrix4x4 MakeScaleMatrix(const Vector3& scale);
	Matrix4x4 MakeRotateXMatrix(float theta);
	Matrix4x4 MakeRotateYMatrix(float theta);
	Matrix4x4 MakeRotateZMatrix(float theta);
	Matrix4x4 MakeTranslateMatrix(const Vector3& translate);
	Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2);
	Matrix4x4 Inverse(const Matrix4x4& m);
	//透視投影行列
	Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);

	//正射影行列
	Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);

	Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate);
	Vector3 Normalize(const Vector3& v);
	Matrix4x4 MakeRotateMatrix(const Vector3& rotate);
	Vector3 TransformNormal(const Vector3& v, const Matrix4x4& m);
	Matrix4x4 MakeLookAtMatrix(const Vector3& eye, const Vector3& target, const Vector3& up);
	Vector3 Cross(const Vector3& v1, const Vector3& v2);
	float Dot(const Vector3& v1, const Vector3& v2);
	float Length(const Vector3& v);

	float Clamp(float value, float min, float max); 
};