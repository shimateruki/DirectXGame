#pragma once

// 担当：編集カメラの方向・角度・ギズモ変換用の内部関数。
// 関連：CameraEditorの分割.cpp、Camera / Object3D（姿勢）
// 分割実装だけが使う補助関数の宣言です。状態を重複して持たせません。
#include "engine/editor/graphics/camera/CameraEditor.h"
#include <numbers>
#include "CameraManager.h"
#include "InputManager.h" // 入力取得に必要
#include "PrimitiveDrawer.h"
#include "PostEffect.h"
#include "SrvManager.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "imgui.h"
#include "ImGuizmo.h"
#include <algorithm>
#include <cmath>
#include <DebugConsole.h>
#include "EditorManager.h"
#include "EditorToolSettings.h"
#include "IconsFontAwesome5.h"

namespace CameraEditorImplementation {
    inline constexpr float kPi = std::numbers::pi_v<float>;
    inline constexpr const char* kCameraModelGizmoName = "Editor/camera_gizmo";
    inline constexpr bool kShowLegacyCameraEditor = false;

    // 再生中は通常の3人称カメラ表示だけを隠し、演出用カメラの確認を邪魔しないようにします。
    bool ShouldHideGameCameraGuideDuringPlay();

    BaseScene* GetCurrentScene();

    bool IsObjectInCurrentScene(const engine::Object3D* target);

    // 度をラジアンへ変換する。
    float DegToRad(float degrees);

    // ラジアンを度へ変換する。
    float RadToDeg(float radians);

    // 始点から終点を結ぶ、指定の太さの箱の変換行列を作る。
    engine::Matrix4x4 MakeLineBoxMatrix(const engine::Vector3& start, const engine::Vector3& end, float thickness);

    // クォータニオンを使わず、オイラー角から前方ベクトルなどを求める簡易計算
    engine::Vector3 CalculateForward(const engine::Vector3& rotation);

    // 編集カメラの回転から前方向を求める。
    engine::Vector3 CalculateEditorForward(const engine::Vector3& rotation);

    // 二つのベクトルに垂直な外積ベクトルを返す。
    engine::Vector3 Cross(const engine::Vector3& a, const engine::Vector3& b);

    // ベクトルの長さを返す。
    float Length(const engine::Vector3& v);

    // 長さがほぼゼロなら fallback を返す。ゼロ除算でカメラの向きを壊さないための補助。
    engine::Vector3 NormalizeSafe(const engine::Vector3& v, const engine::Vector3& fallback);

    // 前方向から Euler 回転をラジアンで求める。カメラの姿勢計算に使用する。
    engine::Vector3 MakeRotationFromForward(const engine::Vector3& forward);

    engine::Vector3 GetObjectWorldForward(const engine::Object3D* object);

    bool IsValidFocusBounds(const Aabb& bounds);
}
