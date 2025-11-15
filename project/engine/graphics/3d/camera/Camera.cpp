#define NOMINMAX
#include "Camera.h"
#include "WinApp.h" 
#include <algorithm> // std::min, std::max のために追加
#include "imgui.h"
#include "ImGuizmo.h"
#include "Object3d.h"
// --- 角度変換ヘルパー ---
#include <cmath> // M_PI が定義されていない場合のため
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const float PI = (float)M_PI;
static Math math;


void Camera::Initialize() {
    // デフォルトの視点、注視点、上方向を設定
    eye_ = { 0.0f, 5.0f, -20.0f };
    target_ = { 0.0f, 0.0f, 0.0f };
    up_ = { 0.0f, 1.0f, 0.0f };
    rotation_ = { 0.0f, 0.0f, 0.0f }; // 回転も初期化

    // Releaseモードのデフォルト設定を eye_ から反映
    fixedOffset_ = eye_;

    // アスペクト比をウィンドウサイズから計算
    aspectRatio_ = (float)WinApp::kClientWidth / WinApp::kClientHeight;

    // ★ デフォルトのカメラモードを kAimable に設定
    followMode_ = FollowMode::kAimable;

    // ★ kAimable のデフォルト距離を設定
    distance_ = 10.0f;

    isInputEnabled_ = true;

}

void Camera::Update() {
	static Math math; // ★ Math ユーティリティ

	// (1) 追従対象 (Player) が設定されているか？
	if (followObject_) {
		// --- ゲーム内カメラ (Player 追従) ---

		Vector3 playerPos = followObject_->GetWorldPosition();

		// (B) カメラモードに応じて Eye と Target を決定
		switch (followMode_) {

		case FollowMode::kLockOn:
			if (targetObject_) { // 注視対象(Enemy)がいる
				this->target_ = targetObject_->GetWorldPosition();
				Matrix4x4 rotateMat = math.MakeRotateYMatrix(followObject_->GetRotation().y);
				Vector3 rotatedOffset = math.TransformNormal(lockOnOffset_, rotateMat);
				this->eye_ = playerPos + rotatedOffset;
			} else {
				followMode_ = FollowMode::kAimable;

			}
			break;

		case FollowMode::kAimable:
			this->target_ = playerPos;
			Matrix4x4 rotateMat = math.MakeRotateXMatrix(rotation_.x) * math.MakeRotateYMatrix(rotation_.y);
			Vector3 offset = math.TransformNormal({ 0, 0, -distance_ }, rotateMat);
			this->eye_ = playerPos + offset;
			break;

		case FollowMode::kFirstPerson:
			this->eye_ = playerPos + firstPersonOffset_;
			Matrix4x4 rotateMatFP = math.MakeRotateXMatrix(rotation_.x) * math.MakeRotateYMatrix(rotation_.y);
			Vector3 forward = math.TransformNormal({ 0, 0, 1 }, rotateMatFP);
			this->target_ = this->eye_ + forward;
			break;

		case FollowMode::kFixed:
		default:
			this->eye_ = playerPos + fixedOffset_;
			this->target_ = playerPos;
			break;
		}

	} else {
		

#ifdef _DEBUG


        //ImGuiの入力キャプチャをチェック
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse || io.WantCaptureKeyboard || !isInputEnabled_) {
            // ImGuiがマウスかキーボードを使っている
        } else {

            // 左クリック + マウス移動での回転
            if (inputManager_->IsMouseButtonPressed(0)) {
                Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
                const float rotateSpeed = 0.001f;
                rotation_.x += mouseDelta.y * rotateSpeed;
                rotation_.y += mouseDelta.x * rotateSpeed;
            }
            // A/D/Q/E/W/S/Wheel での移動
            Vector3 move = { 0, 0, 0 };
            const float moveSpeed = 0.3f;
            if (inputManager_->IsKeyPressed(DIK_LEFT)) { move.x -= moveSpeed; }
            if (inputManager_->IsKeyPressed(DIK_RIGHT)) { move.x += moveSpeed; }
            if (inputManager_->IsKeyPressed(DIK_UP)) { move.y += moveSpeed; }
            if (inputManager_->IsKeyPressed(DIK_DOWN)) { move.y -= moveSpeed; }

            float wheelDelta = inputManager_->GetMouseWheelDelta();
            const float wheelSpeed = 0.005f;


            //ホイール操作も io.WantCaptureMouse でガードされる
            move.z += wheelDelta * wheelSpeed;

            Matrix4x4 rotateMatrix = math.Multiply(math.MakeRotateXMatrix(rotation_.x), math.MakeRotateYMatrix(rotation_.y));
            move = math.TransformNormal(move, rotateMatrix);
            eye_ = eye_ + move;

            // 注視点も一緒に動かす
            Vector3 targetForward = { 0.0f, 0.0f, 1.0f };
            targetForward = math.TransformNormal(targetForward, rotateMatrix);
            target_ = eye_ + targetForward;
        }
    

#endif // _DEBUG
	
	}

	// (2) 最終的なビュー・プロジェクション行列の計算 (共通)
	viewMatrix_ = math.MakeLookAtMatrix(eye_, target_, up_);
	projectionMatrix_ = math.MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
}




void Camera::SetFollowMode(FollowMode mode) {
    followMode_ = mode;
}

void Camera::ConfigFixed(const Vector3& offset) {
    fixedOffset_ = offset;
}

void Camera::ConfigAimable(float distance, float minDistance, float maxDistance) {
    distance_ = distance;
    minDistance_ = minDistance;
    maxDistance_ = maxDistance;
}

void Camera::ConfigFirstPerson(const Vector3& eyeOffset) {
    firstPersonOffset_ = eyeOffset;
}

void Camera::AddRotation(const Vector2& mouseDelta) {
    // kAimable と kFirstPerson で共用の回転処理
    const float rotateSpeed = 0.005f;
    rotation_.x += mouseDelta.y * rotateSpeed;
    rotation_.y += mouseDelta.x * rotateSpeed;

    // X軸の回転（ピッチ）に制限をかける
    const float pitchLimit = PI / 2.0f - 0.01f; // 90度手前
    rotation_.x = std::max(-pitchLimit, std::min(pitchLimit, rotation_.x));

    // Y軸の回転（ヨー）は 2PI でラップアラウンド
    if (rotation_.y > PI) { rotation_.y -= 2.0f * PI; }
    if (rotation_.y < -PI) { rotation_.y += 2.0f * PI; }
}

void Camera::SyncRotationToCurrentView() {
    static Math math;

    // (A) 注視点 (Target) を決定する
    Vector3 targetPos;
    if (targetObject_) {
        // ロックオン対象 (Enemy) がいれば、そこを基準にする
        targetPos = targetObject_->GetWorldPosition();
    } else if (followObject_) {
        // 追従対象 (Player) がいれば、そこを基準にする
        targetPos = followObject_->GetWorldPosition();
    } else {
        // どちらもいなければ、現在の target_ (デバッグ用) を使う
        targetPos = target_;
    }

    // (B) Eye から Target への「前方ベクトル」を計算
    Vector3 forward = targetPos - eye_;
    if (math.Length(forward) < 0.001f) {
        // ゼロベクトルの場合はデフォルト (Z+) を向く
        forward = { 0.0f, 0.0f, 1.0f };
    } else {
        forward = math.Normalize(forward);
    }

    // (C) 前方ベクトルからヨー(Y軸回転)とピッチ(X軸回転)を逆算
    
    // ヨー (Y軸回転)
    rotation_.y = std::atan2(forward.x, forward.z);

    // ピッチ (X軸回転)
    // (forward.y は sin(-rotation_.x) に相当)
    rotation_.x = std::asin(-forward.y);

    // X軸の回転（ピッチ）に制限をかける (AddRotation と同じ処理)
    const float pitchLimit = PI / 2.0f - 0.01f; // 90度手前
    rotation_.x = std::max(-pitchLimit, std::min(pitchLimit, rotation_.x));
}