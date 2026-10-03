#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.Internal.h"

using namespace CameraEditorImplementation;

// カメラの軌道・フラスタムの可視化。
void CameraEditor::DrawOrbitCenterGizmo(const Vector2& gameViewOffset, const Vector2& gameViewSize, bool snapEnabled, float snapValue) {
#ifdef USE_IMGUI
    if (!EditorToolSettings::Get().IsVisible("orbitGizmo")) return;
    if (settings_.currentMode != Mode::Editor) return;
    if (settings_.gameFollowMode != Camera::FollowMode::kOrbit) return;
    if (!settings_.orbitGuideVisible || !settings_.orbitCenterGizmoVisible) return;
    if (gameViewSize.x <= 1.0f || gameViewSize.y <= 1.0f) return;
    if (!isGameViewHovered_ && !isDraggingOrbitCenterGizmo_) return;

    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) return;

    Math math;
    orbitEditGizmoTarget_ = (std::clamp)(orbitEditGizmoTarget_, 0, 1);
    const bool editStartEye = orbitEditGizmoTarget_ == 1;
    Vector3 gizmoPosition = editStartEye ? GetOrbitStartEye() : GetOrbitCenter();
    Matrix4x4 world = math.MakeTranslateMatrix(gizmoPosition);
    Matrix4x4 view = camera->GetViewMatrix();
    Matrix4x4 projection = camera->GetProjectionMatrix();
    float snap[3] = { snapValue, snapValue, snapValue };

    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(gameViewOffset.x, gameViewOffset.y, gameViewSize.x, gameViewSize.y);
    ImGuizmo::Manipulate(
        &view.m[0][0],
        &projection.m[0][0],
        ImGuizmo::TRANSLATE,
        ImGuizmo::WORLD,
        &world.m[0][0],
        nullptr,
        (snapEnabled && snapValue > 0.0f) ? snap : nullptr);

    if (ImGuizmo::IsUsing()) {
        isDraggingOrbitCenterGizmo_ = true;
        Vector3 editedCenter = { world.m[3][0], world.m[3][1], world.m[3][2] };
        if (editStartEye) {
            SetOrbitStartFromWorld(editedCenter);
        } else {
            SetOrbitCenterFromWorld(editedCenter);
        }
        camera->SetOrbitCenterOffset(settings_.orbitCenterOffset);
        camera->SetOrbitCenterHeight(settings_.orbitCenterHeight);
        camera->SetOrbitParams(settings_.orbitRadius, settings_.orbitHeight, settings_.orbitSpeed);
    }
    else if (isDraggingOrbitCenterGizmo_) {
        isDraggingOrbitCenterGizmo_ = false;
        SaveSettings();
        DebugConsole::GetInstance()->AddLog("Orbit camera center gizmo saved.");
    }
#else
    (void)gameViewOffset;
    (void)gameViewSize;
    (void)snapEnabled;
    (void)snapValue;
#endif
}

void CameraEditor::DrawCameraGuide(PrimitiveDrawer& primitiveDrawer, ID3D12GraphicsCommandList* commandList, int& instanceCount, int maxDrawLimit) {
#ifdef USE_IMGUI
    if (!EditorToolSettings::Get().IsVisible("cameraGuide")) return;
    if (!settings_.cameraGuideVisible) return;
    const bool isCameraEditorSelected =
        EditorManager::GetInstance()->GetSelectedObject() == this;
    if (!isCameraEditorSelected && GetSelectedCameraObject() == nullptr) return;
    if (instanceCount >= maxDrawLimit) return;

    Math math;
    const float markerSize = (std::max)(0.05f, settings_.cameraGuideSize);
    const float frustumLength = (std::max)(0.5f, settings_.cameraFrustumLength);

    auto drawSphere = [&](const Vector3& pos, float size, const Vector4& color) {
        if (instanceCount >= maxDrawLimit) return;
        Matrix4x4 world = math.Multiply(math.MakeScaleMatrix({ size, size, size }), math.MakeTranslateMatrix(pos));
        primitiveDrawer.DrawWireSphere(commandList, world, color, instanceCount++);
    };

    auto drawCubeLine = [&](const Vector3& start, const Vector3& end, float thickness, const Vector4& color) {
        if (instanceCount >= maxDrawLimit) return;
        primitiveDrawer.DrawWireCube(commandList, MakeLineBoxMatrix(start, end, thickness), color, instanceCount++);
    };

    auto drawWireBox = [&](const Vector3& center, const Vector3& right, const Vector3& up, const Vector3& forward,
        float halfRight, float halfUp, float halfForward, float thickness, const Vector4& color) {
        if (instanceCount >= maxDrawLimit) return;

        auto corner = [&](float sx, float sy, float sz) {
            return center + right * (halfRight * sx) + up * (halfUp * sy) + forward * (halfForward * sz);
        };

        const Vector3 lbf = corner(-1.0f, -1.0f, 1.0f);
        const Vector3 rbf = corner(1.0f, -1.0f, 1.0f);
        const Vector3 ltf = corner(-1.0f, 1.0f, 1.0f);
        const Vector3 rtf = corner(1.0f, 1.0f, 1.0f);
        const Vector3 lbb = corner(-1.0f, -1.0f, -1.0f);
        const Vector3 rbb = corner(1.0f, -1.0f, -1.0f);
        const Vector3 ltb = corner(-1.0f, 1.0f, -1.0f);
        const Vector3 rtb = corner(1.0f, 1.0f, -1.0f);

        drawCubeLine(lbf, rbf, thickness, color);
        drawCubeLine(rbf, rtf, thickness, color);
        drawCubeLine(rtf, ltf, thickness, color);
        drawCubeLine(ltf, lbf, thickness, color);

        drawCubeLine(lbb, rbb, thickness, color);
        drawCubeLine(rbb, rtb, thickness, color);
        drawCubeLine(rtb, ltb, thickness, color);
        drawCubeLine(ltb, lbb, thickness, color);

        drawCubeLine(lbf, lbb, thickness, color);
        drawCubeLine(rbf, rbb, thickness, color);
        drawCubeLine(ltf, ltb, thickness, color);
        drawCubeLine(rtf, rtb, thickness, color);
    };

    auto drawCameraBody = [&](const Vector3& eye, const Vector3& forward, float sizeScale, const Vector4& bodyColor, const Vector4& accentColor) {
        if (!settings_.cameraBodyVisible || instanceCount >= maxDrawLimit) return;
        if (object3dCommon_) return;

        const Vector3 safeForward = NormalizeSafe(forward, { 0.0f, 0.0f, 1.0f });
        const Vector3 right = GetConfiguredCameraRight(safeForward);
        const Vector3 up = GetConfiguredCameraUp(safeForward, right);
        const float size = markerSize * sizeScale;
        const float line = (std::max)(0.018f, size * 0.045f);

        // UnityのScene Viewに近い「カメラ筐体 + レンズ + 取っ手」を、Editor専用ワイヤーモデルとして生成します。
        const Vector3 bodyCenter = eye - safeForward * (size * 0.16f);
        drawWireBox(bodyCenter, right, up, safeForward, size * 0.82f, size * 0.48f, size * 0.42f, line, bodyColor);

        const Vector3 lensCenter = eye + safeForward * (size * 0.45f);
        drawWireBox(lensCenter, right, up, safeForward, size * 0.36f, size * 0.28f, size * 0.24f, line * 0.9f, accentColor);

        const Vector3 hoodCenter = eye + safeForward * (size * 0.78f);
        drawWireBox(hoodCenter, right, up, safeForward, size * 0.52f, size * 0.38f, size * 0.11f, line * 0.8f, accentColor);

        const Vector3 handleCenter = bodyCenter + up * (size * 0.66f) - safeForward * (size * 0.05f);
        drawWireBox(handleCenter, right, up, safeForward, size * 0.42f, size * 0.13f, size * 0.22f, line * 0.75f, bodyColor);

        const Vector3 tripodRoot = bodyCenter - up * (size * 0.52f);
        const Vector3 tripodCenter = tripodRoot - up * (size * 0.55f);
        drawCubeLine(tripodRoot, tripodCenter, line * 0.75f, bodyColor);
        drawCubeLine(tripodCenter, tripodCenter + right * (size * 0.58f) - up * (size * 0.22f), line * 0.65f, bodyColor);
        drawCubeLine(tripodCenter, tripodCenter - right * (size * 0.58f) - up * (size * 0.22f), line * 0.65f, bodyColor);
        drawCubeLine(tripodCenter, tripodCenter - safeForward * (size * 0.50f) - up * (size * 0.18f), line * 0.65f, bodyColor);
    };

    auto drawCameraBadge = [&](const Vector3& eye, const Vector3& forward, int badgeNumber, float sizeScale, const Vector4& color, bool selected) {
        if (instanceCount >= maxDrawLimit) return;

        const Vector3 safeForward = NormalizeSafe(forward, { 0.0f, 0.0f, 1.0f });
        const Vector3 right = GetConfiguredCameraRight(safeForward);
        const Vector3 up = GetConfiguredCameraUp(safeForward, right);
        const float size = markerSize * sizeScale;
        const float line = (std::max)(0.014f, size * 0.035f);
        const Vector3 tagCenter = eye + up * (selected ? size * 1.45f : size * 1.12f) - safeForward * (size * 0.28f);

        // リスト上の番号とScene上のカメラ位置を対応させるため、小さなタグを描きます。
        drawWireBox(tagCenter, right, up, safeForward, size * 0.44f, size * 0.20f, size * 0.06f, line, color);

        if (badgeNumber <= 0) {
            drawCubeLine(tagCenter - right * (size * 0.22f), tagCenter + right * (size * 0.22f), line * 1.2f, color);
            drawCubeLine(tagCenter, tagCenter + up * (size * 0.28f), line * 0.9f, color);
            return;
        }

        const int dotCount = (std::min)(badgeNumber, 5);
        const float start = -0.24f;
        const float step = dotCount > 1 ? 0.12f : 0.0f;
        for (int i = 0; i < dotCount; ++i) {
            const Vector3 dotPos = tagCenter + right * (size * (start + step * i));
            drawSphere(dotPos, size * (selected ? 0.075f : 0.060f), color);
        }
        if (badgeNumber > 5) {
            drawCubeLine(tagCenter + right * (size * 0.28f) - up * (size * 0.08f), tagCenter + right * (size * 0.34f) + up * (size * 0.08f), line * 0.75f, color);
        }
    };

    auto drawFrustum = [&](const Vector3& eye, const Vector3& forward, float fovY, float sizeScale, const Vector4& eyeColor, const Vector4& rayColor, const Vector4& frameColor) {
        if (instanceCount >= maxDrawLimit) return;

        const Vector3 safeForward = NormalizeSafe(forward, { 0.0f, 0.0f, 1.0f });
        const Vector3 right = GetConfiguredCameraRight(safeForward);
        const Vector3 up = GetConfiguredCameraUp(safeForward, right);
        const Vector3 target = eye + safeForward * frustumLength;
        const float safeFovY = std::clamp(fovY, DegToRad(1.0f), DegToRad(179.0f));
        const float halfHeight = frustumLength * std::tan(safeFovY * 0.5f);
        const float halfWidth = halfHeight * (16.0f / 9.0f);
        const float localMarkerSize = markerSize * sizeScale;
        const Vector3 topLeft = target + up * halfHeight - right * halfWidth;
        const Vector3 topRight = target + up * halfHeight + right * halfWidth;
        const Vector3 bottomLeft = target - up * halfHeight - right * halfWidth;
        const Vector3 bottomRight = target - up * halfHeight + right * halfWidth;

        drawCameraBody(eye, safeForward, sizeScale, eyeColor, frameColor);
        drawSphere(eye, settings_.cameraBodyVisible ? localMarkerSize * 0.24f : localMarkerSize, eyeColor);
        drawSphere(target, localMarkerSize * 0.45f, frameColor);
        drawCubeLine(eye, target, 0.035f * sizeScale, rayColor);
        drawCubeLine(eye, topLeft, 0.025f * sizeScale, frameColor);
        drawCubeLine(eye, topRight, 0.025f * sizeScale, frameColor);
        drawCubeLine(eye, bottomLeft, 0.025f * sizeScale, frameColor);
        drawCubeLine(eye, bottomRight, 0.025f * sizeScale, frameColor);
        drawCubeLine(topLeft, topRight, 0.03f * sizeScale, frameColor);
        drawCubeLine(topRight, bottomRight, 0.03f * sizeScale, frameColor);
        drawCubeLine(bottomRight, bottomLeft, 0.03f * sizeScale, frameColor);
        drawCubeLine(bottomLeft, topLeft, 0.03f * sizeScale, frameColor);
    };

    if (!ShouldHideGameCameraGuideDuringPlay()) {
        // ゲーム/3人称カメラの位置をScene上に表示します。
        const Vector3 gameEye = GetConfiguredCameraEye();
        const Vector3 gameForward = GetConfiguredCameraForward();
        const Vector4 gameEyeColor = { 1.0f, 0.82f, 0.18f, 1.0f };
        const Vector4 gameRayColor = { 1.0f, 0.72f, 0.18f, 0.95f };
        const Vector4 gameFrameColor = { 1.0f, 0.92f, 0.35f, 0.88f };
        drawFrustum(gameEye, gameForward, 0.45f, 0.85f, gameEyeColor, gameRayColor, gameFrameColor);
        drawCameraBadge(gameEye, gameForward, 0, 0.85f, gameFrameColor, true);
        if (targetPlayer_) {
            Vector3 lookTarget = targetPlayer_->GetWorldPosition();
            lookTarget.y += settings_.height;
            drawSphere(lookTarget, markerSize * 0.38f, gameFrameColor);
            drawCubeLine(gameEye, lookTarget, 0.024f, gameRayColor);
        }
    }

    // Camera Object選択中は、そのObject自身のFOV・位置・注視先でCamera Editorと同じガイドを描きます。
    if (Object3d* cameraObject = GetSelectedCameraObject()) {
        Vector3 eye{};
        Vector3 target{};
        if (ResolveSceneCameraPose(cameraObject, eye, target)) {
            const SceneCameraSettings& cameraSettings = cameraObject->GetSceneCameraSettings();
            const Vector3 forward = NormalizeSafe(target - eye, GetObjectWorldForward(cameraObject));
            const Vector4 eyeColor = { 0.2f, 1.0f, 0.35f, 1.0f };
            const Vector4 rayColor = { 0.2f, 1.0f, 0.35f, 0.95f };
            const Vector4 frameColor = { 0.75f, 1.0f, 0.35f, 0.95f };

            drawFrustum(eye, forward, cameraSettings.fovY, 0.95f, eyeColor, rayColor, frameColor);
            drawCameraBadge(eye, forward, 1, 0.95f, frameColor, true);
            drawSphere(target, markerSize * 0.32f, frameColor);
            drawCubeLine(eye, target, 0.032f, rayColor);
        }
    }

#else
    (void)primitiveDrawer;
    (void)commandList;
    (void)instanceCount;
    (void)maxDrawLimit;
#endif
}

void CameraEditor::DrawOrbitGuide(PrimitiveDrawer& primitiveDrawer, ID3D12GraphicsCommandList* commandList, int& instanceCount, int maxDrawLimit) {
#ifdef USE_IMGUI
    DrawCameraGuide(primitiveDrawer, commandList, instanceCount, maxDrawLimit);
    if (!EditorToolSettings::Get().IsVisible("orbitGuide")) return;
    if (settings_.currentMode != Mode::Editor) return;
    if (!settings_.orbitGuideVisible) return;
    if (settings_.gameFollowMode != Camera::FollowMode::kOrbit) return;
    if (instanceCount >= maxDrawLimit) return;

    Math math;
    const Vector3 center = GetOrbitCenter();
    const float radius = settings_.orbitRadius;
    const float markerSize = settings_.orbitGuideMarkerSize;
    const float startAngle = DegToRad(settings_.orbitStartAngleDeg);

    auto drawSphere = [&](const Vector3& pos, float size, const Vector4& color) {
        if (instanceCount >= maxDrawLimit) return;
        Matrix4x4 world = math.Multiply(math.MakeScaleMatrix({ size, size, size }), math.MakeTranslateMatrix(pos));
        primitiveDrawer.DrawWireSphere(commandList, world, color, instanceCount++);
        };

    auto drawCubeLine = [&](const Vector3& start, const Vector3& end, float thickness, const Vector4& color) {
        if (instanceCount >= maxDrawLimit) return;
        primitiveDrawer.DrawWireCube(commandList, MakeLineBoxMatrix(start, end, thickness), color, instanceCount++);
        };

    if (radius > 0.01f && instanceCount < maxDrawLimit) {
        Matrix4x4 ringWorld = math.Multiply(
            math.MakeScaleMatrix({ radius * 2.0f, 0.04f, radius * 2.0f }),
            math.MakeTranslateMatrix(center));
        primitiveDrawer.DrawWireCylinder(commandList, ringWorld, { 0.1f, 0.8f, 1.0f, 0.85f }, instanceCount++);
    }

    Vector3 startGround = {
        center.x + radius * std::cos(startAngle),
        center.y,
        center.z + radius * std::sin(startAngle)
    };
    Vector3 startEye = startGround;
    startEye.y += settings_.orbitHeight;

    drawSphere(center, markerSize, { 1.0f, 0.9f, 0.1f, 1.0f });
    drawSphere(startEye, markerSize, { 1.0f, 0.15f, 0.15f, 1.0f });
    drawCubeLine(center, startGround, 0.04f, { 0.1f, 0.9f, 1.0f, 0.9f });
    drawCubeLine(startGround, startEye, 0.04f, { 0.3f, 1.0f, 0.2f, 0.9f });

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera) {
        drawSphere(camera->GetEye(), markerSize * 0.75f, { 0.75f, 0.35f, 1.0f, 1.0f });
    }
#else
    (void)primitiveDrawer;
    (void)commandList;
    (void)instanceCount;
    (void)maxDrawLimit;
#endif
}
