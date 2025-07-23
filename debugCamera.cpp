#include "debugCamera.h"
#include "InputManager.h"
#include "Math.h"
void DebugCamera::Initialize()
{

    float fovY = 0.45f * 3.141592f; // 約25度
    float aspectRatio = 16.0f / 9.0f;
    float nearZ = 0.1f;
    float farZ = 1000.0f;


    // 初期位置と回転を設定
    translation = { 0.0f, 0.0f, -10.0f };  // 適切な初期位置を設定
    rotation = { 0.0f, 0.0f, 0.0f };


    projectionMatrix = math->MakePerspectiveFovMatrix(0.45f, aspectRatio, nearZ, farZ);
  
}

void DebugCamera::Update()
{
    // --- マウス回転（右クリック中） ---
    if (inputManager->IsMouseButtonPressed(1)) {
        Vector2 mouseDelta = inputManager->GetMouseMoveDelta();
        const float rotateSpeed = 0.01f;
        rotation.x += mouseDelta.y * rotateSpeed;
        rotation.y += mouseDelta.x * rotateSpeed;
    }
    // 移動（WASDなど）
    Vector3 move = { 0, 0, 0 };
    const float moveSpeed = 0.1f;
    if (!inputManager) return; // 安全確認

    if (inputManager->IsKeyPressed(DIK_W)) move.z += 1.0f;
    if (inputManager->IsKeyPressed(DIK_S)) move.z -= 1.0f;
    if (inputManager->IsKeyPressed(DIK_A)) move.x += 1.0f;
    if (inputManager->IsKeyPressed(DIK_D)) move.x -= 1.0f;
    if (inputManager->IsKeyPressed(DIK_Q)) move.y -= 1.0f;
    if (inputManager->IsKeyPressed(DIK_E)) move.y += 1.0f;

    // 回転からカメラの向きベクトルを生成
    Matrix4x4 rotateMat = math-> MakeRotateMatrix(rotation);
    Vector3 moveWorld = math-> TransformNormal(move, rotateMat);
    translation = translation + moveWorld * moveSpeed;

    // ビュー行列の更新
    Matrix4x4 transMat = math-> MakeTranslateMatrix(-translation);
    Matrix4x4 invRotateMat =  math-> MakeRotateMatrix({ -rotation.x, -rotation.y, -rotation.z });
    viewMatrix = invRotateMat * transMat;


}
void DebugCamera::SetInputManager(InputManager* manager) {
    inputManager = manager;
}