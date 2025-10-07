#include "engine/3d/debugCamera.h"
#include "engine/io/InputManager.h"

void DebugCamera::Initialize()
{
    float fovY = 0.45f * 3.141592f; // 約25度
    float aspectRatio = 16.0f / 9.0f;
    float nearZ = 0.1f;
    float farZ = 1000.0f;

    translation = { 0.0f, 0.0f, -10.0f };
    rotation = { 0.0f, 0.0f, 0.0f };
    Math math;
    projectionMatrix = math.MakePerspectiveFovMatrix(fovY, aspectRatio, nearZ, farZ);
}

void DebugCamera::Update()
{
    if (!inputManager) return;

    // --- マウス回転（右クリック中） ---
    if (inputManager->IsMouseButtonPressed(1)) {
        Vector2 mouseDelta = inputManager->GetMouseMoveDelta();
        const float rotateSpeed = 0.01f;
        rotation.x += mouseDelta.y * rotateSpeed;
        rotation.y += mouseDelta.x * rotateSpeed;
    }

    // --- ゲームパッド右スティックで回転 ---
    Vector2 rightStick = inputManager->GetGamepadRightStick();
    const float padRotateSpeed = 0.05f;
    rotation.x += -rightStick.y * padRotateSpeed;
    rotation.y += rightStick.x * padRotateSpeed;

    // --- 移動 ---
    Vector3 move = { 0, 0, 0 };
    const float moveSpeed = 0.1f;

    // キーボード移動
    if (inputManager->IsKeyPressed(DIK_W)) move.z += 1.0f;
    if (inputManager->IsKeyPressed(DIK_S)) move.z -= 1.0f;
    if (inputManager->IsKeyPressed(DIK_A)) move.x += 1.0f;
    if (inputManager->IsKeyPressed(DIK_D)) move.x -= 1.0f;
    if (inputManager->IsKeyPressed(DIK_Q)) move.y -= 1.0f;
    if (inputManager->IsKeyPressed(DIK_E)) move.y += 1.0f;

    // ゲームパッド左スティック移動
    Vector2 leftStick = inputManager->GetGamepadLeftStick();
    move.x += -leftStick.x;
    move.z += leftStick.y;
    Math math;
    // カメラ移動
    Matrix4x4 rotateMat = math. MakeRotateMatrix(rotation);
    Vector3 moveWorld = math.TransformNormal(move, rotateMat);
    translation = translation + moveWorld * moveSpeed;

    // ビュー行列の更新
    Matrix4x4 transMat = math.MakeTranslateMatrix(-translation);
    Matrix4x4 invRotateMat = math.MakeRotateMatrix({ -rotation.x, -rotation.y, -rotation.z });
    viewMatrix = invRotateMat * transMat;
}

void DebugCamera::SetInputManager(InputManager* manager) {
    inputManager = manager;
}