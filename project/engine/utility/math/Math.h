#pragma once

#include <cmath>

/// <summary>二次元の座標や方向を表す浮動小数ベクトル。</summary>
struct Vector2
{
	float x;
	float y;
};

/// <summary>三次元の座標や方向を表す浮動小数ベクトル。</summary>
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


/// <summary>四次元のベクトル。RGBA色や同次座標にも使う。</summary>
struct Vector4
{
	float x;
	float y;
	float z;
	float w;
};

/// <summary>四成分で三次元回転を表すクォータニオン。</summary>
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

/// <summary>三次元の線形変換を表す3×3行列。</summary>
struct Matrix3x3
{
	float m[3][3];
};


/// <summary>三次元のアフィン変換や投影を表す4×4行列。</summary>
struct Matrix4x4
{
	float m[4][4];
};

/// <summary>始点と方向ベクトルで表す半直線です。</summary>
struct Ray {
	Vector3 origin;
	Vector3 diff;
};

/// <summary>レイと形状の交差結果です。</summary>
struct RayResult {
	bool isHit;
	float distance;
	Vector3 point;
	Vector3 normal;
};

// ベクトル間の差、スカラー倍、和、除算、符号反転を定義する。
Vector3 operator-(const Vector3& v1, const Vector3& v2);
Vector3 operator*(const Vector3& v, float scalar);
Vector3 operator+(const Vector3& v1, const Vector3& v2);
Vector3 operator/(const Vector3& v, float scalar);
Vector3 operator-(const Vector3& v);


// 行列積とベクトルの加算代入。
Matrix4x4 operator*(const Matrix4x4& m1, const Matrix4x4& m2);
Vector3& operator+=(Vector3& v1, const Vector3& v2);

/// <summary>法線と原点からの距離で表す平面。</summary>
struct Plane {
	Vector3 normal;
	float distance;
};

/// <summary>左右・上下・近遠の六平面からなる視錐台。</summary>
struct Frustum {
	Plane planes[6]; // 左右上下・近遠の6面
};

/// <summary>ベクトル、行列、投影、交差判定の共通関数を提供します。</summary>
class Math
{


public:

	/// <summary>単位行列を作る。</summary>
	static Matrix4x4 MakeIdentity4x4();

	/// <summary>各軸の倍率から拡縮行列を作る。</summary>
	static Matrix4x4 MakeScaleMatrix(const Vector3& scale);

	// 指定ラジアン角で各軸を中心に回転する行列を作る。
	static Matrix4x4 MakeRotateXMatrix(float theta);
	static Matrix4x4 MakeRotateYMatrix(float theta);
	static Matrix4x4 MakeRotateZMatrix(float theta);

	/// <summary>移動量から平行移動行列を作る。</summary>
	static Matrix4x4 MakeTranslateMatrix(const Vector3& translate);

	/// <summary>二つの4×4行列を乗算する。</summary>
	static Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2);

	/// <summary>4x4 行列の逆行列を返します。逆行列がない場合はゼロ行列を返します。</summary>
	static Matrix4x4 Inverse(const Matrix4x4& m);

	/// <summary>垂直画角、アスペクト比、前後クリップ面から透視投影行列を作ります。</summary>
	static Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);

	/// <summary>左右上下と前後クリップ面から正射影行列を作ります。</summary>
	static Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);

	/// <summary>投影後の座標を画面座標へ写すビューポート行列を作ります。</summary>
	static Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth);

	/// <summary>拡縮・Euler角回転・移動を合成したアフィン行列を作る。</summary>
	static Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate);

	/// <summary>ベクトルを単位長に正規化する。</summary>
	static Vector3 Normalize(const Vector3& v);

	/// <summary>Euler角から回転行列を作る。</summary>
	static Matrix4x4 MakeRotateMatrix(const Vector3& rotate);

	/// <summary>行列の平行移動成分を除いて法線を変換する。</summary>
	static Vector3 TransformNormal(const Vector3& v, const Matrix4x4& m);

	/// <summary>視点・注視点・上方向からビュー行列を作る。</summary>
	static Matrix4x4 MakeLookAtMatrix(const Vector3& eye, const Vector3& target, const Vector3& up);

	/// <summary>二つの三次元ベクトルの外積を返す。</summary>
	static Vector3 Cross(const Vector3& v1, const Vector3& v2);

	/// <summary>二つの三次元ベクトルの内積を返す。</summary>
	static float Dot(const Vector3& v1, const Vector3& v2);

	/// <summary>行と列を入れ替えた行列を返す。</summary>
	static Matrix4x4 Transpose(const Matrix4x4& m);

	/// <summary>値を最小値と最大値の範囲に収める。</summary>
	static float Clamp(float value, float min, float max);

	/// <summary>三次元ベクトルの長さを返す。</summary>
	static float Length(const Vector3& v);

	/// <summary>
	/// 線形補間 (float)
	/// </summary>
	static float Lerp(float v1, float v2, float t);

	/// <summary>
	/// 線形補間 (Vector4)
	/// </summary>
	static Vector4 Lerp(const Vector4& v1, const Vector4& v2, float t);

	/// <summary>位置を行列で変換し、w が 0 でない場合は透視除算します。</summary>
	static Vector3 Transform(const Vector3& v, const Matrix4x4& m);

	/// <summary>レイと軸平行ボックスの最初の交点を調べます。</summary>
	/// <param name="ray">始点と方向ベクトル。</param>
	/// <param name="minBox">箱の各軸の最小座標。</param>
	/// <param name="maxBox">箱の各軸の最大座標。</param>
	/// <param name="hit">交差時に結果を書き込む先。</param>
	/// <returns>交差する場合は true。</returns>
	static bool IntersectRayAABB(const Ray& ray, const Vector3& minBox, const Vector3& maxBox, RayResult* hit);

	// Vector3 の線形補間 (座標の移動用)
	static Vector3 Lerp(const Vector3& v1, const Vector3& v2, float t);

	/// <summary>2 つの角度を最短経路で補間します。</summary>
	static float LerpShortAngle(float a, float b, float t);

	/// <summary>2 つの回転を球面線形補間します。</summary>
	static Quaternion Slerp(const Quaternion& q0, const Quaternion& q1, float t);

	/// <summary>Quaternion を回転行列へ変換します。</summary>
	static Matrix4x4 MakeRotateQuaternionMatrix(const Quaternion& q);

	/// <summary>幅と高さ、前後クリップ面から正射影行列を作る。</summary>
	static Matrix4x4 MakeOrthographicMatrix(float width, float height, float nearZ, float farZ);

	/// <summary>Euler角をクォータニオンへ変換する。</summary>
	static Quaternion EulerToQuaternion(const Vector3& rot);

	/// <summary>行列の回転成分をEuler角へ変換する。</summary>
	static Vector3 MatrixToEuler(const Matrix4x4& m);

	/// <summary>行列の回転成分をクォータニオンへ変換する。</summary>
	static Quaternion MatrixToQuaternion(const Matrix4x4& m);

	/// <summary>ビュー投影行列から 6 面の視錐台を取り出します。</summary>
	static Frustum ExtractFrustumPlanes(const Matrix4x4& vp);

	/// <summary>視錐台と軸平行ボックスが交差するか判定します。</summary>
	static bool IntersectFrustumAABB(const Frustum& frustum, const Vector3& minBox, const Vector3& maxBox);

	/// <summary>点から軸平行ボックスまでの最短距離の二乗を返します。</summary>
	static float DistanceSquaredPointAABB(const Vector3& point, const Vector3& minBox, const Vector3& maxBox);
};
