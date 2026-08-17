#pragma once

#include <cmath>

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
	Vector3& operator=(const Vector3& other) {
		x = other.x;
		y = other.y;
		z = other.z;
		return *this;
	}
	Vector3 operator/(const Vector3& v) const {
		return { x / v.x, y / v.y, z / v.z };
	}
};


struct Vector4
{
	float x;
	float y;
	float z;
	float w;
};

struct Quaternion {
	float x;
	float y;
	float z;
	float w;
	// クォータニオン同士の掛け算 (回転の合成用)
	Quaternion operator*(const Quaternion& other) const {
		return {
			w * other.x + x * other.w + y * other.z - z * other.y,
			w * other.y - x * other.z + y * other.w + z * other.x,
			w * other.z + x * other.y - y * other.x + z * other.w,
			w * other.w - x * other.x - y * other.y - z * other.z
		};
	}
};
struct Matrix3x3
{
	float m[3][3];
};


struct Matrix4x4
{
	float m[4][4];
};

// レイ（光線）
struct Ray {
	Vector3 origin; // 発射地点
	Vector3 diff;   // 方向ベクトル
};
struct RayResult {
	bool isHit;       // 当たったか？
	float distance;   // 距離
	Vector3 point;    // 当たった正確な座標
	Vector3 normal;   // 法線
};
Vector3 operator-(const Vector3& v1, const Vector3& v2);
Vector3 operator*(const Vector3& v, float scalar);
Vector3 operator+(const Vector3& v1, const Vector3& v2);
Vector3 operator/(const Vector3& v, float scalar);
Vector3 operator-(const Vector3& v);


Matrix4x4 operator*(const Matrix4x4& m1, const Matrix4x4& m2);
Vector3& operator+=(Vector3& v1, const Vector3& v2);
struct Plane {
	Vector3 normal;
	float distance;
};

struct Frustum {
	Plane planes[6]; // 左右上下・近遠の6面
};
class Math
{


public:

	static Matrix4x4 MakeIdentity4x4();
	static Matrix4x4 MakeScaleMatrix(const Vector3& scale);
	static Matrix4x4 MakeRotateXMatrix(float theta);
	static Matrix4x4 MakeRotateYMatrix(float theta);
	static Matrix4x4 MakeRotateZMatrix(float theta);
	static Matrix4x4 MakeTranslateMatrix(const Vector3& translate);
	static Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2);
	static Matrix4x4 Inverse(const Matrix4x4& m);
	//透視投影行列
	static Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);

	//正射影行列
	static Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);

	// ★追加: ビューポート行列 (3D -> 2D変換に必要)
	static Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth);

	static Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate);
	static Vector3 Normalize(const Vector3& v);
	static Matrix4x4 MakeRotateMatrix(const Vector3& rotate);
	static Vector3 TransformNormal(const Vector3& v, const Matrix4x4& m);
	static Matrix4x4 MakeLookAtMatrix(const Vector3& eye, const Vector3& target, const Vector3& up);
	static Vector3 Cross(const Vector3& v1, const Vector3& v2);
	static float Dot(const Vector3& v1, const Vector3& v2);
	static Matrix4x4 Transpose(const Matrix4x4& m);
	static float Clamp(float value, float min, float max);
	static float Length(const Vector3& v);
	/// <summary>
	/// 線形補間 (float)
	/// </summary>
	static float Lerp(float v1, float v2, float t);
	/// <summary>
	/// 線形補間 (Vector4)
	/// </summary>
	static Vector4 Lerp(const Vector4& v1, const Vector4& v2, float t);

	// ベクトルと行列の掛け算（w除算あり）
	// スクリーン座標をワールド座標に戻すために必須です
	static Vector3 Transform(const Vector3& v, const Matrix4x4& m);

	// minBox: 箱の最小座標 (center - scale)
	// maxBox: 箱の最大座標 (center + scale)
	static bool IntersectRayAABB(const Ray& ray, const Vector3& minBox, const Vector3& maxBox, RayResult* hit);
	// Vector3 の線形補間 (座標の移動用)
	static Vector3 Lerp(const Vector3& v1, const Vector3& v2, float t);

	// 最短経路での角度補間
	static float LerpShortAngle(float a, float b, float t);

	// クォータニオンの球面線形補間 (回転のアニメーション用)
	static Quaternion Slerp(const Quaternion& q0, const Quaternion& q1, float t);

	// クォータニオンから回転行列を作成
	static Matrix4x4 MakeRotateQuaternionMatrix(const Quaternion& q);
	static Matrix4x4 MakeOrthographicMatrix(float width, float height, float nearZ, float farZ);

	static Quaternion EulerToQuaternion(const Vector3& rot);
	static Vector3 MatrixToEuler(const Matrix4x4& m);
	static Quaternion MatrixToQuaternion(const Matrix4x4& m);
	//  ビュープロジェクション行列からフラスタムを抽出
	static Frustum ExtractFrustumPlanes(const Matrix4x4& vp);
	//  フラスタムとAABB（箱）の交差判定
	static bool IntersectFrustumAABB(const Frustum& frustum, const Vector3& minBox, const Vector3& maxBox);
	// 点からAABBまでの最短距離の二乗
	static float DistanceSquaredPointAABB(const Vector3& point, const Vector3& minBox, const Vector3& maxBox);
};
