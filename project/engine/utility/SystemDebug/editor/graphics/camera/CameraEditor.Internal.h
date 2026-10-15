#pragma once
// 分割実装だけが使う補助関数の宣言です。状態を重複して持たせません。
#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.h"
#include "CameraManager.h"
#include "InputManager.h" // 入力取得に必要
#include "PrimitiveDrawer.h"
#include "PostEffect.h"
#include "SRVManager.h"
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
    inline constexpr float kPi = 3.14159265f;
    inline constexpr const char* kCameraModelGizmoName = "Editor/camera_gizmo";
    inline constexpr bool kShowLegacyCameraEditor = false;

    // 再生中は通常の3人称カメラ表示だけを隠し、演出用カメラの確認を邪魔しないようにします。
    bool ShouldHideGameCameraGuideDuringPlay();

    BaseScene* GetCurrentScene();

    bool IsObjectInCurrentScene(const engine::Object3d* target);

    float DegToRad(float degrees);

    float RadToDeg(float radians);

    engine::Matrix4x4 MakeLineBoxMatrix(const engine::Vector3& start, const engine::Vector3& end, float thickness);

    // クォータニオンを使わず、オイラー角から前方ベクトルなどを求める簡易計算
    engine::Vector3 CalculateForward(const engine::Vector3& rotation);

    engine::Vector3 CalculateEditorForward(const engine::Vector3& rotation);

    engine::Vector3 Cross(const engine::Vector3& a, const engine::Vector3& b);

    float Length(const engine::Vector3& v);

    engine::Vector3 NormalizeSafe(const engine::Vector3& v, const engine::Vector3& fallback);

    engine::Vector3 MakeRotationFromForward(const engine::Vector3& forward);

    engine::Vector3 GetObjectWorldForward(const engine::Object3d* object);

    bool IsValidFocusBounds(const AABB& bounds);
}
