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
#include <CollisionManager.h>

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

    //  デフォルトのカメラモードを kAimable に設定
    followMode_ = FollowMode::kAimable;

    //  kAimable のデフォルト距離を設定
    distance_ = 10.0f;

    isInputEnabled_ = true;

}

void Camera::Update() {
    static Math math;

    // -----------------------------------------------------------------
    //  ゲーム内カメラ挙動
    // -----------------------------------------------------------------
    if (followObject_) {
        Vector3 playerPos = followObject_->GetWorldPosition();

        // (A) 注視点 (Target) の計算
        // 全モード共通： プレイヤーの足元 + 高さ(Height)
        Vector3 targetPos = playerPos;
        targetPos.y += aimHeight_; // ★ Heightスライダーを全モードで有効化

        // ロックオン時は敵を見る
        if (followMode_ == FollowMode::kLockOn && targetObject_) {
            targetPos = targetObject_->GetWorldPosition();
        }
        // 一人称は目の高さ調整として Height を使う
        else if (followMode_ == FollowMode::kFirstPerson) {
            // 一人称用の特別オフセット(必要ならここも aimHeight_ にしてもOK)
            targetPos = playerPos + firstPersonOffset_;
        }

        target_ = targetPos; // Target決定！

        // (B) カメラ位置 (Eye) の計算
        Vector3 desiredEye = eye_;

        // 角度計算用（度数法 -> ラジアン）
        float angleRad = aimAngle_ * (3.1415f / 180.0f);

        switch (followMode_) {
        case FollowMode::kLockOn:
            // ... (ロックオン処理はそのまま) ...
            if (targetObject_) {
                Matrix4x4 rotateMat = math.MakeRotateYMatrix(followObject_->GetRotation().y);
                Vector3 rotatedOffset = math.TransformNormal(lockOnOffset_, rotateMat);
                desiredEye = playerPos + rotatedOffset;
            } else {
                followMode_ = FollowMode::kAimable;
            }
            break;

        case FollowMode::kAimable:
            // --- 3人称自由視点 ---
            // マウス操作(rotation_) を優先するが、初期距離などはスライダーを使う
        {
            // マウスの回転を使用
            Matrix4x4 rotateMat = math.MakeRotateXMatrix(rotation_.x) * math.MakeRotateYMatrix(rotation_.y);
            Vector3 offset = { 0.0f, 0.0f, -aimDistance_ }; // ★ Distance有効
            offset = math.TransformNormal(offset, rotateMat);
            desiredEye = target_ + offset;
        }
        break;

        case FollowMode::kFixed:
        
        {
            //  プレイヤーの rotation.y ではなく、現在のカメラの rotation_.y を使う
            float currentY = rotation_.y;

            // X軸回転: ImGuiのAngleを使う
            float pitch = angleRad;

            // 回転行列作成 (現在のカメラの向き + 指定した見下ろし角度)
            Matrix4x4 rotateMat = math.MakeRotateXMatrix(pitch) * math.MakeRotateYMatrix(currentY);

            // 後ろへ下がるオフセット
            Vector3 offset = { 0.0f, 0.0f, -aimDistance_ }; // Distance有効
            offset = math.TransformNormal(offset, rotateMat);

            // 配置
            desiredEye = target_ + offset;

            // カメラの回転情報を更新
            rotation_.x = pitch; // X軸(上下)はImGuiに従う
            // rotation_.y = currentY; // Y軸(左右)は勝手に変えない！
        }
        break;

        case FollowMode::kFirstPerson:
            desiredEye = targetPos;
            {
                Matrix4x4 rotateMatFP = math.MakeRotateXMatrix(rotation_.x) * math.MakeRotateYMatrix(rotation_.y);
                Vector3 forward = math.TransformNormal({ 0, 0, 1 }, rotateMatFP);
                // 一人称は Targetの方を動かす
                target_ = desiredEye + forward;
            }
            break;
        }

        // -----------------------------------------------------------------
        // (C) 壁めり込み防止 (Raycast)
        // -----------------------------------------------------------------
        if (followMode_ != FollowMode::kFirstPerson) {
            Vector3 toEye = desiredEye - target_;
            float dist = math.Length(toEye);
            Vector3 direction = (dist > 0.001f) ? math.Normalize(toEye) : Vector3{ 0,0,1 };

            // 0.1以上離れているならレイを飛ばす
            if (dist > 0.1f) {
                RaycastHit hit = CollisionManager::GetInstance()->Raycast(
                    target_, direction, dist, kGround
                );
                if (hit.isHit) {
                    const float kEpsilon = 0.2f;
                    eye_ = hit.hitPoint - (direction * kEpsilon);
                } else {
                    eye_ = desiredEye;
                }
            } else {
                eye_ = desiredEye;
            }
        } else {
            eye_ = desiredEye;
        }
    } else {
       
    }

    // 行列更新
    viewMatrix_ = math.MakeLookAtMatrix(eye_, target_, up_);
    projectionMatrix_ = math.MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
}




void Camera::SetFollowMode(FollowMode mode) {
    followMode_ = mode;
}

void Camera::ConfigFixed(const Vector3& offset) {
    fixedOffset_ = offset;
}

void Camera::ConfigAimable(float distance, float height, float angle) {
    aimDistance_ = distance;
    aimHeight_ = height;
    aimAngle_ = angle;
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