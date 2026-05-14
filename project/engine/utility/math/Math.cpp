#include "Math.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <cmath> // C++ の数学ライブラリ
#include <algorithm> // std::max, std::min用


Vector3 operator-(const Vector3& v1, const Vector3& v2)
{
	return { v1.x - v2.x, v1.y - v2.y, v1.z - v2.z };
}

Vector3 operator*(const Vector3& v, float scalar)
{
	return { v.x * scalar, v.y * scalar, v.z * scalar };
}

Vector3 operator+(const Vector3& v1, const Vector3& v2)
{

	return { v1.x + v2.x, v1.y + v2.y, v1.z + v2.z };

}

Vector3 operator/(const Vector3& v, float scalar) {
	// 0除算を避けるための小さなチェック
	if (scalar != 0.0f) {
		return { v.x / scalar, v.y / scalar, v.z / scalar };
	}
	// 0で割ろうとした場合は、とりあえずゼロベクトルを返す
	return { 0.0f, 0.0f, 0.0f };
}

Vector3 operator-(const Vector3& v)
{
	return { -v.x, -v.y, -v.z };
}

Matrix4x4 operator*(const Matrix4x4& m1, const Matrix4x4& m2)
{
	Matrix4x4 result = {};
	for (int i = 0; i < 4; ++i)
	{
		for (int j = 0; j < 4; ++j)
		{
			result.m[i][j] = 0;
			for (int k = 0; k < 4; ++k)
			{
				result.m[i][j] += m1.m[i][k] * m2.m[k][j];
			}
		}
	}
	return result;
}

Vector3& operator+=(Vector3& v1, const Vector3& v2) {
	v1.x += v2.x;
	v1.y += v2.y;
	v1.z += v2.z;
	return v1;
}

Vector3& operator-=(Vector3& v1, const Vector3& v2) {
	v1.x -= v2.x;
	v1.y -= v2.y;
	v1.z -= v2.z;
	return v1;
}

Matrix4x4 Math::MakeIdentity4x4()
{

	Matrix4x4 result = {};
	result.m[0][0] = 1.0f;
	result.m[0][1] = 0.0f;
	result.m[0][2] = 0.0f;
	result.m[0][3] = 0.0f;

	result.m[1][0] = 0.0f;
	result.m[1][1] = 1.0f;
	result.m[1][2] = 0.0f;
	result.m[1][3] = 0.0f;

	result.m[2][0] = 0.0f;
	result.m[2][1] = 0.0f;
	result.m[2][2] = 1.0f;
	result.m[2][3] = 0.0f;

	result.m[3][0] = 0.0f;
	result.m[3][1] = 0.0f;
	result.m[3][2] = 0.0f;
	result.m[3][3] = 1.0f;
	return result;
}

Matrix4x4 Math::MakeScaleMatrix(const Vector3& scale)
{
	Matrix4x4 result{ scale.x, 0.0f, 0.0f, 0.0f, 0.0f, scale.y, 0.0f, 0.0f, 0.0f, 0.0f, scale.z, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

	return result;
}

Matrix4x4 Math::MakeRotateXMatrix(float theta)
{
	float sin = std::sin(theta);
	float cos = std::cos(theta);

	Matrix4x4 result{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, cos, sin, 0.0f, 0.0f, -sin, cos, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

	return result;
}

Matrix4x4 Math::MakeRotateYMatrix(float theta)
{
	float sin = std::sin(theta);
	float cos = std::cos(theta);

	Matrix4x4 result{ cos, 0.0f, -sin, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, sin, 0.0f, cos, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

	return result;
}

Matrix4x4 Math::MakeRotateZMatrix(float theta)
{
	float sin = std::sin(theta);
	float cos = std::cos(theta);

	Matrix4x4 result{ cos, sin, 0.0f, 0.0f, -sin, cos, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

	return result;
}

Matrix4x4 Math::MakeTranslateMatrix(const Vector3& translate)
{
	Matrix4x4 result{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, translate.x, translate.y, translate.z, 1.0f };

	return result;
}

Matrix4x4 Math::Multiply(const Matrix4x4& m1, const Matrix4x4& m2)
{
	Matrix4x4 result = {};
	for (int row = 0; row < 4; ++row) {
		for (int col = 0; col < 4; ++col) {
			result.m[row][col] =
				m1.m[row][0] * m2.m[0][col] +
				m1.m[row][1] * m2.m[1][col] +
				m1.m[row][2] * m2.m[2][col] +
				m1.m[row][3] * m2.m[3][col];
		}
	}
	return result;
}

Matrix4x4 Math::Inverse(const Matrix4x4& m)
{
	Matrix4x4 result = {};


	float a11 = m.m[0][0], a12 = m.m[0][1], a13 = m.m[0][2], a14 = m.m[0][3];
	float a21 = m.m[1][0], a22 = m.m[1][1], a23 = m.m[1][2], a24 = m.m[1][3];
	float a31 = m.m[2][0], a32 = m.m[2][1], a33 = m.m[2][2], a34 = m.m[2][3];
	float a41 = m.m[3][0], a42 = m.m[3][1], a43 = m.m[3][2], a44 = m.m[3][3];


	float det = a11 * a22 * a33 * a44 + a11 * a23 * a34 * a42 + a11 * a24 * a32 * a43
		- a11 * a24 * a33 * a42 - a11 * a23 * a32 * a44 - a11 * a22 * a34 * a43
		- a12 * a21 * a33 * a44 - a13 * a21 * a34 * a42 - a14 * a21 * a32 * a43
		+ a14 * a21 * a33 * a42 + a13 * a21 * a32 * a44 + a12 * a21 * a34 * a43
		+ a12 * a23 * a31 * a44 + a13 * a24 * a31 * a42 + a14 * a22 * a31 * a43
		- a14 * a23 * a31 * a42 - a13 * a22 * a31 * a44 - a12 * a24 * a31 * a43
		- a12 * a23 * a34 * a41 - a13 * a24 * a32 * a41 - a14 * a22 * a33 * a41
		+ a14 * a23 * a32 * a41 + a13 * a22 * a34 * a41 + a12 * a24 * a33 * a41;
	if (det == 0.0f) {
		// 逆行列が存在しない（行列式が0）
		return result;
	}

	float invDet = 1.0f / det;

	// 以下、各要素に対応する余因子を手動計算して代入（転置あり）

	// 1行目
	result.m[0][0] = (a22 * (a33 * a44 - a34 * a43) - a23 * (a32 * a44 - a34 * a42) + a24 * (a32 * a43 - a33 * a42)) * invDet;
	result.m[0][1] = -(a12 * (a33 * a44 - a34 * a43) - a13 * (a32 * a44 - a34 * a42) + a14 * (a32 * a43 - a33 * a42)) * invDet;
	result.m[0][2] = (a12 * (a23 * a44 - a24 * a43) - a13 * (a22 * a44 - a24 * a42) + a14 * (a22 * a43 - a23 * a42)) * invDet;
	result.m[0][3] = -(a12 * (a23 * a34 - a24 * a33) - a13 * (a22 * a34 - a24 * a32) + a14 * (a22 * a33 - a23 * a32)) * invDet;

	// 2行目
	result.m[1][0] = -(a21 * (a33 * a44 - a34 * a43) - a23 * (a31 * a44 - a34 * a41) + a24 * (a31 * a43 - a33 * a41)) * invDet;
	result.m[1][1] = (a11 * (a33 * a44 - a34 * a43) - a13 * (a31 * a44 - a34 * a41) + a14 * (a31 * a43 - a33 * a41)) * invDet;
	result.m[1][2] = -(a11 * (a23 * a44 - a24 * a43) - a13 * (a21 * a44 - a24 * a41) + a14 * (a21 * a43 - a23 * a41)) * invDet;
	result.m[1][3] = (a11 * (a23 * a34 - a24 * a33) - a13 * (a21 * a34 - a24 * a31) + a14 * (a21 * a33 - a23 * a31)) * invDet;

	// 3行目
	result.m[2][0] = (a21 * (a32 * a44 - a34 * a42) - a22 * (a31 * a44 - a34 * a41) + a24 * (a31 * a42 - a32 * a41)) * invDet;
	result.m[2][1] = -(a11 * (a32 * a44 - a34 * a42) - a12 * (a31 * a44 - a34 * a41) + a14 * (a31 * a42 - a32 * a41)) * invDet;
	result.m[2][2] = (a11 * (a22 * a44 - a24 * a42) - a12 * (a21 * a44 - a24 * a41) + a14 * (a21 * a42 - a22 * a41)) * invDet;
	result.m[2][3] = -(a11 * (a22 * a34 - a24 * a32) - a12 * (a21 * a34 - a24 * a31) + a14 * (a21 * a32 - a22 * a31)) * invDet;

	// 4行目
	result.m[3][0] = -(a21 * (a32 * a43 - a33 * a42) - a22 * (a31 * a43 - a33 * a41) + a23 * (a31 * a42 - a32 * a41)) * invDet;
	result.m[3][1] = (a11 * (a32 * a43 - a33 * a42) - a12 * (a31 * a43 - a33 * a41) + a13 * (a31 * a42 - a32 * a41)) * invDet;
	result.m[3][2] = -(a11 * (a22 * a43 - a23 * a42) - a12 * (a21 * a43 - a23 * a41) + a13 * (a21 * a42 - a22 * a41)) * invDet;
	result.m[3][3] = (a11 * (a22 * a33 - a23 * a32) - a12 * (a21 * a33 - a23 * a31) + a13 * (a21 * a32 - a22 * a31)) * invDet;

	return result;
}

Matrix4x4 Math::MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip)
{
	Matrix4x4 result = {};
	float f = 1.0f / tanf(fovY / 2.0f);
	result.m[0][0] = f / aspectRatio;
	result.m[0][1] = 0.0f;
	result.m[0][2] = 0.0f;
	result.m[0][3] = 0.0f;

	result.m[1][0] = 0.0f;
	result.m[1][1] = f;
	result.m[1][2] = 0.0f;
	result.m[1][3] = 0.0f;

	result.m[2][0] = 0.0f;
	result.m[2][1] = 0.0f;
	result.m[2][2] = farClip / (farClip - nearClip);
	result.m[2][3] = 1.0f;

	result.m[3][0] = 0.0f;
	result.m[3][1] = 0.0f;
	result.m[3][2] = -nearClip * farClip / (farClip - nearClip);
	result.m[3][3] = 0.0f;
	return result;
}

Matrix4x4 Math::MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip)
{
	Matrix4x4 result{};
	result.m[0][0] = 2.0f / (right - left);
	result.m[1][1] = 2.0f / (top - bottom);
	result.m[2][2] = 1.0f / (farClip - nearClip);
	result.m[3][0] = (left + right) / (left - right);
	result.m[3][1] = (top + bottom) / (bottom - top);
	result.m[3][2] = nearClip / (nearClip - farClip);
	result.m[3][3] = 1.0f;
	return result;
}

// ★追加: ビューポート行列
Matrix4x4 Math::MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth)
{
	Matrix4x4 result = {};
	result.m[0][0] = width / 2.0f;
	result.m[0][1] = 0.0f;
	result.m[0][2] = 0.0f;
	result.m[0][3] = 0.0f;

	result.m[1][0] = 0.0f;
	result.m[1][1] = -height / 2.0f; // Y軸反転（スクリーン座標系へ）
	result.m[1][2] = 0.0f;
	result.m[1][3] = 0.0f;

	result.m[2][0] = 0.0f;
	result.m[2][1] = 0.0f;
	result.m[2][2] = maxDepth - minDepth;
	result.m[2][3] = 0.0f;

	result.m[3][0] = left + width / 2.0f;
	result.m[3][1] = top + height / 2.0f;
	result.m[3][2] = minDepth;
	result.m[3][3] = 1.0f;

	return result;
}

Matrix4x4 Math::MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate)
{
	Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);
	Matrix4x4 rotateXMatrix = MakeRotateXMatrix(rotate.x);
	Matrix4x4 rotateYMatrix = MakeRotateYMatrix(rotate.y);
	Matrix4x4 rotateZMatrix = MakeRotateZMatrix(rotate.z);
	Matrix4x4 rotateMatrix = Multiply(Multiply(rotateXMatrix, rotateYMatrix), rotateZMatrix);
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);

	Matrix4x4 worldMatrix = Multiply(Multiply(scaleMatrix, rotateMatrix), translateMatrix);

	return worldMatrix;
}

Vector3 Math::Normalize(const Vector3& v)
{
	float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);

	if (length < 0.000001f) { // 適切な epsilon 値を設定してください
		return { 0.0f, 0.0f, 0.0f };
	}

	return { v.x / length, v.y / length, v.z / length };
}

Matrix4x4 Math::MakeRotateMatrix(const Vector3& rotate)
{
	Matrix4x4 rotX = MakeRotateXMatrix(rotate.x);
	Matrix4x4 rotY = MakeRotateYMatrix(rotate.y);
	Matrix4x4 rotZ = MakeRotateZMatrix(rotate.z);
	return Multiply(Multiply(rotZ, rotX), rotY); // Z→X→Y の順
}

Vector3 Math::TransformNormal(const Vector3& v, const Matrix4x4& m)
{
	Vector3 result;
	result.x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0];
	result.y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1];
	result.z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2];
	return result;
}

float Math::Dot(const Vector3& v1, const Vector3& v2)
{
	return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
}

float Math::Length(const Vector3& v)
{
	return std::sqrt(Dot(v, v));
}

Vector3 Math::Cross(const Vector3& v1, const Vector3& v2)
{
	return {
		v1.y * v2.z - v1.z * v2.y,
		v1.z * v2.x - v1.x * v2.z,
		v1.x * v2.y - v1.y * v2.x
	};
}

Matrix4x4 Math::MakeLookAtMatrix(const Vector3& eye, const Vector3& target, const Vector3& up)
{
	Vector3 zaxis = Normalize(target - eye);
	Vector3 xaxis = Normalize(Cross(up, zaxis));
	Vector3 yaxis = Cross(zaxis, xaxis);

	Matrix4x4 result = MakeIdentity4x4();
	result.m[0][0] = xaxis.x;
	result.m[0][1] = yaxis.x;
	result.m[0][2] = zaxis.x;
	result.m[1][0] = xaxis.y;
	result.m[1][1] = yaxis.y;
	result.m[1][2] = zaxis.y;
	result.m[2][0] = xaxis.z;
	result.m[2][1] = yaxis.z;
	result.m[2][2] = zaxis.z;
	result.m[3][0] = -Dot(eye, xaxis);
	result.m[3][1] = -Dot(eye, yaxis);
	result.m[3][2] = -Dot(eye, zaxis);

	return result;
}

float Math::Clamp(float value, float min, float max)
{
	if (value < min) {
		return min;
	}
	if (value > max) {
		return max;
	}
	return value;
}

float Math::Lerp(float v1, float v2, float t) {
	// t (0.0f～1.0f) の値に応じて、v1 から v2 への間の値を返す
	return v1 + (v2 - v1) * t;
}

Vector4 Math::Lerp(const Vector4& v1, const Vector4& v2, float t) {
	// Vector4 の各要素 (x, y, z, w) に対して Lerp を行う
	Vector4 result;
	result.x = Lerp(v1.x, v2.x, t);
	result.y = Lerp(v1.y, v2.y, t);
	result.z = Lerp(v1.z, v2.z, t);
	result.w = Lerp(v1.w, v2.w, t);
	return result;
}

Matrix4x4 Math::Transpose(const Matrix4x4& m)
{
	Matrix4x4 result;
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.m[i][j] = m.m[j][i]; // 行と列を入れ替える
		}
	}
	return result;
}

Vector3 Math::Transform(const Vector3& v, const Matrix4x4& m) {
	Vector3 result;
	// x, y, z に加えて w 成分も計算
	result.x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + m.m[3][0];
	result.y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + m.m[3][1];
	result.z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + m.m[3][2];
	float w = v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + m.m[3][3];

	// wで割って正規化する（これが重要！）
	if (w != 0.0f) {
		result.x /= w;
		result.y /= w;
		result.z /= w;
	}
	return result;
}


bool Math::IntersectRayAABB(const Ray& ray, const Vector3& minBox, const Vector3& maxBox, RayResult* hit) {
	float tMin = 0.0f;
	float tMax = 100000.0f; // 十分大きな値

	// --- X軸の判定 ---
	// レイのX方向がほぼ0（垂直）の場合
	if (std::abs(ray.diff.x) < 1e-6f) {
		// レイの始点が箱の外にあったら、絶対に当たらない
		if (ray.origin.x < minBox.x || ray.origin.x > maxBox.x) return false;
	} else {
		// スラブ法による判定
		float invD = 1.0f / ray.diff.x;
		float t1 = (minBox.x - ray.origin.x) * invD;
		float t2 = (maxBox.x - ray.origin.x) * invD;
		if (t1 > t2) std::swap(t1, t2);
		tMin = std::max(tMin, t1);
		tMax = std::min(tMax, t2);
		if (tMin > tMax) return false;
	}

	// --- Y軸の判定 ---
	if (std::abs(ray.diff.y) < 1e-6f) {
		if (ray.origin.y < minBox.y || ray.origin.y > maxBox.y) return false;
	} else {
		float invD = 1.0f / ray.diff.y;
		float t1 = (minBox.y - ray.origin.y) * invD;
		float t2 = (maxBox.y - ray.origin.y) * invD;
		if (t1 > t2) std::swap(t1, t2);
		tMin = std::max(tMin, t1);
		tMax = std::min(tMax, t2);
		if (tMin > tMax) return false;
	}

	// --- Z軸の判定 ---
	if (std::abs(ray.diff.z) < 1e-6f) {
		if (ray.origin.z < minBox.z || ray.origin.z > maxBox.z) return false;
	} else {
		float invD = 1.0f / ray.diff.z;
		float t1 = (minBox.z - ray.origin.z) * invD;
		float t2 = (maxBox.z - ray.origin.z) * invD;
		if (t1 > t2) std::swap(t1, t2);
		tMin = std::max(tMin, t1);
		tMax = std::min(tMax, t2);
		if (tMin > tMax) return false;
	}

	// 衝突確定
	if (hit) {
		hit->isHit = true;
		hit->distance = tMin;

		// 衝突点 = origin + diff * tMin
		hit->point.x = ray.origin.x + ray.diff.x * tMin;
		hit->point.y = ray.origin.y + ray.diff.y * tMin;
		hit->point.z = ray.origin.z + ray.diff.z * tMin;

		// 法線計算 (簡易版: 衝突点が箱のどの面に近いかで判定)
		Vector3 center = { (minBox.x + maxBox.x) * 0.5f, (minBox.y + maxBox.y) * 0.5f, (minBox.z + maxBox.z) * 0.5f };
		Vector3 size = { (maxBox.x - minBox.x) * 0.5f, (maxBox.y - minBox.y) * 0.5f, (maxBox.z - minBox.z) * 0.5f };
		Vector3 p = { hit->point.x - center.x, hit->point.y - center.y, hit->point.z - center.z };

		// 正規化して比較 (どの軸の端に近いか)
		float bias = 1.001f; // 誤差対策
		hit->normal = { 0, 0, 0 };
		if (p.x >= size.x / bias) hit->normal = { 1, 0, 0 };
		else if (p.x <= -size.x / bias) hit->normal = { -1, 0, 0 };
		else if (p.y >= size.y / bias) hit->normal = { 0, 1, 0 };
		else if (p.y <= -size.y / bias) hit->normal = { 0, -1, 0 };
		else if (p.z >= size.z / bias) hit->normal = { 0, 0, 1 };
		else if (p.z <= -size.z / bias) hit->normal = { 0, 0, -1 };
	}

	return true;
}

// Vector3のLerp
Vector3 Math::Lerp(const Vector3& v1, const Vector3& v2, float t) {
	return {
		v1.x + (v2.x - v1.x) * t,
		v1.y + (v2.y - v1.y) * t,
		v1.z + (v2.z - v1.z) * t
	};
}

// クォータニオンのSlerp
Quaternion Math::Slerp(const Quaternion& q0, const Quaternion& q1, float t) {
	float dot = q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w;

	// 内積が負＝反対側なら反転して最短経路を取る
	Quaternion trueQ1 = q1;
	if (dot < 0.0f) {
		trueQ1 = { -q1.x, -q1.y, -q1.z, -q1.w };
		dot = -dot;
	}

	// ほぼ同じ向きなら線形補間でOK (ゼロ除算防止)
	if (dot >= 1.0f - 0.0005f) {
		return {
			q0.x + (trueQ1.x - q0.x) * t,
			q0.y + (trueQ1.y - q0.y) * t,
			q0.z + (trueQ1.z - q0.z) * t,
			q0.w + (trueQ1.w - q0.w) * t
		};
	}

	float theta = std::acos(dot);
	float sinTheta = std::sin(theta);

	float scale0 = std::sin((1.0f - t) * theta) / sinTheta;
	float scale1 = std::sin(t * theta) / sinTheta;

	return {
		q0.x * scale0 + trueQ1.x * scale1,
		q0.y * scale0 + trueQ1.y * scale1,
		q0.z * scale0 + trueQ1.z * scale1,
		q0.w * scale0 + trueQ1.w * scale1
	};
}

// クォータニオン -> 行列 変換
Matrix4x4 Math::MakeRotateQuaternionMatrix(const Quaternion& q) {
	Matrix4x4 m;
	float x = q.x, y = q.y, z = q.z, w = q.w;

	m.m[0][0] = 1 - 2 * y * y - 2 * z * z;
	m.m[0][1] = 2 * x * y + 2 * w * z;
	m.m[0][2] = 2 * x * z - 2 * w * y;
	m.m[0][3] = 0;

	m.m[1][0] = 2 * x * y - 2 * w * z;
	m.m[1][1] = 1 - 2 * x * x - 2 * z * z;
	m.m[1][2] = 2 * y * z + 2 * w * x;
	m.m[1][3] = 0;

	m.m[2][0] = 2 * x * z + 2 * w * y;
	m.m[2][1] = 2 * y * z - 2 * w * x;
	m.m[2][2] = 1 - 2 * x * x - 2 * y * y;
	m.m[2][3] = 0;

	m.m[3][0] = 0; m.m[3][1] = 0; m.m[3][2] = 0; m.m[3][3] = 1;

	return m;
}

Matrix4x4 Math::MakeOrthographicMatrix(float width, float height, float nearZ, float farZ) {
	Matrix4x4 result{}; // ゼロ初期化

	result.m[0][0] = 2.0f / width;
	result.m[1][1] = 2.0f / height;
	result.m[2][2] = 1.0f / (farZ - nearZ);
	result.m[3][2] = -nearZ / (farZ - nearZ);
	result.m[3][3] = 1.0f;

	return result;
}
Quaternion Math::EulerToQuaternion(const Vector3& rot) {
	// 各軸の半角のサイン・コサインを計算
	float cx = std::cos(rot.x * 0.5f);
	float sx = std::sin(rot.x * 0.5f);
	float cy = std::cos(rot.y * 0.5f);
	float sy = std::sin(rot.y * 0.5f);
	float cz = std::cos(rot.z * 0.5f);
	float sz = std::sin(rot.z * 0.5f);

	// それぞれの軸単体のクォータニオン
	Quaternion qX = { sx, 0.0f, 0.0f, cx };
	Quaternion qY = { 0.0f, sy, 0.0f, cy };
	Quaternion qZ = { 0.0f, 0.0f, sz, cz };

	// Z -> X -> Y の順番で掛け算して合成
	return (qY * qX) * qZ;
}

Vector3 Math::MatrixToEuler(const Matrix4x4& m) {
	Vector3 euler;

	// 1. スケール成分を取り除いて純粋な回転行列にする
	Vector3 scale;
	scale.x = std::sqrt(m.m[0][0] * m.m[0][0] + m.m[0][1] * m.m[0][1] + m.m[0][2] * m.m[0][2]);
	scale.y = std::sqrt(m.m[1][0] * m.m[1][0] + m.m[1][1] * m.m[1][1] + m.m[1][2] * m.m[1][2]);
	scale.z = std::sqrt(m.m[2][0] * m.m[2][0] + m.m[2][1] * m.m[2][1] + m.m[2][2] * m.m[2][2]);

	Matrix4x4 rmat = m;
	if (scale.x > 0.0001f) { rmat.m[0][0] /= scale.x; rmat.m[0][1] /= scale.x; rmat.m[0][2] /= scale.x; }
	if (scale.y > 0.0001f) { rmat.m[1][0] /= scale.y; rmat.m[1][1] /= scale.y; rmat.m[1][2] /= scale.y; }
	if (scale.z > 0.0001f) { rmat.m[2][0] /= scale.z; rmat.m[2][1] /= scale.z; rmat.m[2][2] /= scale.z; }

	// 2. Z->X->Y の回転順序に基づいたオイラー角の逆算
	float sinX = -rmat.m[1][2];
	// 安全のため -1 ～ 1 の範囲にクランプ
	sinX = std::max(-1.0f, std::min(1.0f, sinX));
	euler.x = std::asin(sinX);

	// ジンバルロック（Xが±90度）の判定
	if (std::abs(rmat.m[1][2]) < 0.9999f) {
		euler.y = std::atan2(rmat.m[0][2], rmat.m[2][2]);
		euler.z = std::atan2(rmat.m[1][0], rmat.m[1][1]);
	} else {
		// ジンバルロック時は Zを0に固定し、Yだけで表現する
		euler.y = std::atan2(-rmat.m[2][0], rmat.m[0][0]);
		euler.z = 0.0f;
	}

	return euler;
}

Quaternion Math::MatrixToQuaternion(const Matrix4x4& m) {
	// 1. まず行列からスケール成分を取り除き、純粋な回転行列にする
	Vector3 scale;
	scale.x = std::sqrt(m.m[0][0] * m.m[0][0] + m.m[0][1] * m.m[0][1] + m.m[0][2] * m.m[0][2]);
	scale.y = std::sqrt(m.m[1][0] * m.m[1][0] + m.m[1][1] * m.m[1][1] + m.m[1][2] * m.m[1][2]);
	scale.z = std::sqrt(m.m[2][0] * m.m[2][0] + m.m[2][1] * m.m[2][1] + m.m[2][2] * m.m[2][2]);

	Matrix4x4 rmat = m;
	if (scale.x > 0.0001f) { rmat.m[0][0] /= scale.x; rmat.m[0][1] /= scale.x; rmat.m[0][2] /= scale.x; }
	if (scale.y > 0.0001f) { rmat.m[1][0] /= scale.y; rmat.m[1][1] /= scale.y; rmat.m[1][2] /= scale.y; }
	if (scale.z > 0.0001f) { rmat.m[2][0] /= scale.z; rmat.m[2][1] /= scale.z; rmat.m[2][2] /= scale.z; }

	// 2. 純粋な回転行列からクォータニオンを抽出する
	Quaternion q;
	float tr = rmat.m[0][0] + rmat.m[1][1] + rmat.m[2][2];

	if (tr > 0.0f) {
		float s = std::sqrt(tr + 1.0f) * 2.0f;
		q.w = 0.25f * s;
		q.x = (rmat.m[1][2] - rmat.m[2][1]) / s;
		q.y = (rmat.m[2][0] - rmat.m[0][2]) / s;
		q.z = (rmat.m[0][1] - rmat.m[1][0]) / s;
	} else if ((rmat.m[0][0] >= rmat.m[1][1]) && (rmat.m[0][0] >= rmat.m[2][2])) {
		float s = std::sqrt(1.0f + rmat.m[0][0] - rmat.m[1][1] - rmat.m[2][2]) * 2.0f;
		q.w = (rmat.m[1][2] - rmat.m[2][1]) / s;
		q.x = 0.25f * s;
		q.y = (rmat.m[0][1] + rmat.m[1][0]) / s;
		q.z = (rmat.m[2][0] + rmat.m[0][2]) / s;
	} else if (rmat.m[1][1] >= rmat.m[2][2]) {
		float s = std::sqrt(1.0f + rmat.m[1][1] - rmat.m[0][0] - rmat.m[2][2]) * 2.0f;
		q.w = (rmat.m[2][0] - rmat.m[0][2]) / s;
		q.x = (rmat.m[0][1] + rmat.m[1][0]) / s;
		q.y = 0.25f * s;
		q.z = (rmat.m[1][2] + rmat.m[2][1]) / s;
	} else {
		float s = std::sqrt(1.0f + rmat.m[2][2] - rmat.m[0][0] - rmat.m[1][1]) * 2.0f;
		q.w = (rmat.m[0][1] - rmat.m[1][0]) / s;
		q.x = (rmat.m[2][0] + rmat.m[0][2]) / s;
		q.y = (rmat.m[1][2] + rmat.m[2][1]) / s;
		q.z = 0.25f * s;
	}

	// 3. 念のため正規化（ゼロ除算や誤差の蓄積による爆発を防ぐ）
	float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	if (len > 0.0001f) {
		q.x /= len;
		q.y /= len;
		q.z /= len;
		q.w /= len;
	} else {
		q = { 0.0f, 0.0f, 0.0f, 1.0f }; // 安全な初期値
	}

	return q;
}


Frustum Math::ExtractFrustumPlanes(const Matrix4x4& vp) {
	Frustum f;
	// 左
	f.planes[0].normal.x = vp.m[0][3] + vp.m[0][0]; f.planes[0].normal.y = vp.m[1][3] + vp.m[1][0];
	f.planes[0].normal.z = vp.m[2][3] + vp.m[2][0]; f.planes[0].distance = vp.m[3][3] + vp.m[3][0];
	// 右
	f.planes[1].normal.x = vp.m[0][3] - vp.m[0][0]; f.planes[1].normal.y = vp.m[1][3] - vp.m[1][0];
	f.planes[1].normal.z = vp.m[2][3] - vp.m[2][0]; f.planes[1].distance = vp.m[3][3] - vp.m[3][0];
	// 下
	f.planes[2].normal.x = vp.m[0][3] + vp.m[0][1]; f.planes[2].normal.y = vp.m[1][3] + vp.m[1][1];
	f.planes[2].normal.z = vp.m[2][3] + vp.m[2][1]; f.planes[2].distance = vp.m[3][3] + vp.m[3][1];
	// 上
	f.planes[3].normal.x = vp.m[0][3] - vp.m[0][1]; f.planes[3].normal.y = vp.m[1][3] - vp.m[1][1];
	f.planes[3].normal.z = vp.m[2][3] - vp.m[2][1]; f.planes[3].distance = vp.m[3][3] - vp.m[3][1];
	// 近 (DirectX12は0〜w)
	f.planes[4].normal.x = vp.m[0][2]; f.planes[4].normal.y = vp.m[1][2];
	f.planes[4].normal.z = vp.m[2][2]; f.planes[4].distance = vp.m[3][2];
	// 遠
	f.planes[5].normal.x = vp.m[0][3] - vp.m[0][2]; f.planes[5].normal.y = vp.m[1][3] - vp.m[1][2];
	f.planes[5].normal.z = vp.m[2][3] - vp.m[2][2]; f.planes[5].distance = vp.m[3][3] - vp.m[3][2];

	// 正規化
	for (int i = 0; i < 6; ++i) {
		float length = std::sqrt(f.planes[i].normal.x * f.planes[i].normal.x +
			f.planes[i].normal.y * f.planes[i].normal.y +
			f.planes[i].normal.z * f.planes[i].normal.z);
		if (length > 0.0f) {
			f.planes[i].normal = f.planes[i].normal / length;
			f.planes[i].distance /= length;
		}
	}
	return f;
}

bool Math::IntersectFrustumAABB(const Frustum& f, const Vector3& minBox, const Vector3& maxBox) {
	for (int i = 0; i < 6; ++i) {
		// AABBの中で一番「平面の法線方向にある点(p)」を調べる
		Vector3 p = minBox;
		if (f.planes[i].normal.x >= 0) p.x = maxBox.x;
		if (f.planes[i].normal.y >= 0) p.y = maxBox.y;
		if (f.planes[i].normal.z >= 0) p.z = maxBox.z;

		// pが面よりも外側にあれば、箱全体が外側にあると確定！
		float dot = f.planes[i].normal.x * p.x + f.planes[i].normal.y * p.y + f.planes[i].normal.z * p.z;
		if (dot + f.planes[i].distance < 0) {
			return false;
		}
	}
	return true;
}