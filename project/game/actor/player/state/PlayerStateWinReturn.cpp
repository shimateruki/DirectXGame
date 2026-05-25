#include "PlayerStateShared.h"

PlayerStateWinReturn::PlayerStateWinReturn(float groundY) : groundY_(groundY) {}
// ========================================================
// 勝利状態からの復帰 (WinReturn) 実装
// ========================================================
void PlayerStateWinReturn::Enter(Player *player) {
  if (!player)
    return;
  DebugConsole::GetInstance()->AddLog(
      "★ ENTER: Win Return State (着地＆ポーズ戻し)");

  player->SetIsControlActive(false); // まだ操作はさせない
  player->SetIsPhysicsActive(true);  // ★ 重力復活！自然に落下させる

  // アニメ的な少し速い落下にするための初速
  player->SetVelocity({0.0f, -15.0f, 0.0f});

  animTimer_ = 0.0f;
  bodyObj_ = player;
  TryFindHead(player, headObj_);
  TryFindArms(player, leftArmObj_, rightArmObj_);
  TryFindFeet(player, leftFootObj_, rightFootObj_);

  // 1. 現在の「ガッツポーズ」の角度を Start として記憶！
  if (bodyObj_)
    bodyStartRot_ = bodyObj_->GetRotation();
  if (headObj_)
    headStartRot_ = headObj_->GetRotation();
  if (rightArmObj_)
    rightArmStartRot_ = rightArmObj_->GetRotation();
  if (leftArmObj_)
    leftArmStartRot_ = leftArmObj_->GetRotation();
  if (rightFootObj_)
    rightFootStartRot_ = rightFootObj_->GetRotation();
  if (leftFootObj_)
    leftFootStartRot_ = leftFootObj_->GetRotation();

  // 2. 戻るべき「基本ポーズ」の角度を Default として設定
  if (bodyObj_)
    bodyDefaultRot_ = {0.0f, bodyStartRot_.y,
                       0.0f}; // 向き(Y)だけはそのまま維持！
  headDefaultRot_ = {0.0f, 0.0f, 0.0f};
  rightArmDefaultRot_ = {0.0f, 0.0f, 0.0f};
  leftArmDefaultRot_ = {0.0f, 0.0f, 0.0f};
  rightFootDefaultRot_ = {0.0f, 0.0f, 0.0f};
  leftFootDefaultRot_ = {0.0f, 0.0f, 0.0f};

  initializedParts_ = true;
}

void PlayerStateWinReturn::Update(Player *player) {
  if (!player)
    return;

  animTimer_ += 1.0f / 60.0f;

  // --- 姿勢の滑らかなブレンド (Lerp) ---
  if (initializedParts_) {
    float t = std::clamp(animTimer_ / blendDuration_, 0.0f, 1.0f);
    float easeT = -(std::cos(3.14159265f * t) - 1.0f) / 2.0f; // EaseInOutSine

    auto NormalizeAngle = [](float angle) {
      while (angle <= -3.14159265f)
        angle += 6.2831853f;
      while (angle > 3.14159265f)
        angle -= 6.2831853f;
      return angle;
    };
    auto SafeLerpRot = [&](const Vector3 &a, const Vector3 &b, float t) {
      return Vector3{a.x + NormalizeAngle(b.x - a.x) * t,
                     a.y + NormalizeAngle(b.y - a.y) * t,
                     a.z + NormalizeAngle(b.z - a.z) * t};
    };

    Vector3 curBody = SafeLerpRot(bodyStartRot_, bodyDefaultRot_, easeT);
    Vector3 curHead = SafeLerpRot(headStartRot_, headDefaultRot_, easeT);
    Vector3 curRtArm =
        SafeLerpRot(rightArmStartRot_, rightArmDefaultRot_, easeT);
    Vector3 curLtArm = SafeLerpRot(leftArmStartRot_, leftArmDefaultRot_, easeT);
    Vector3 curRtFoot =
        SafeLerpRot(rightFootStartRot_, rightFootDefaultRot_, easeT);
    Vector3 curLtFoot =
        SafeLerpRot(leftFootStartRot_, leftFootDefaultRot_, easeT);

    // 適用
    if (bodyObj_) {
      Transform *tf = bodyObj_->GetTransform();
      tf->rotate = curBody;
      tf->quaternion = Math::EulerToQuaternion(tf->rotate);
      tf->isQuaternionMaster = true;
      bodyObj_->UpdateWorldMatrix();
    }
    if (headObj_) {
      Transform *tf = headObj_->GetTransform();
      tf->rotate = curHead;
      tf->quaternion = Math::EulerToQuaternion(tf->rotate);
      tf->isQuaternionMaster = true;
      headObj_->UpdateWorldMatrix();
    }
    if (rightArmObj_) {
      Transform *tf = rightArmObj_->GetTransform();
      tf->rotate = curRtArm;
      tf->quaternion = Math::EulerToQuaternion(tf->rotate);
      tf->isQuaternionMaster = true;
      rightArmObj_->UpdateWorldMatrix();
    }
    if (leftArmObj_) {
      Transform *tf = leftArmObj_->GetTransform();
      tf->rotate = curLtArm;
      tf->quaternion = Math::EulerToQuaternion(tf->rotate);
      tf->isQuaternionMaster = true;
      leftArmObj_->UpdateWorldMatrix();
    }
    if (rightFootObj_) {
      Transform *tf = rightFootObj_->GetTransform();
      tf->rotate = curRtFoot;
      tf->quaternion = Math::EulerToQuaternion(tf->rotate);
      tf->isQuaternionMaster = true;
      rightFootObj_->UpdateWorldMatrix();
    }
    if (leftFootObj_) {
      Transform *tf = leftFootObj_->GetTransform();
      tf->rotate = curLtFoot;
      tf->quaternion = Math::EulerToQuaternion(tf->rotate);
      tf->isQuaternionMaster = true;
      leftFootObj_->UpdateWorldMatrix();
    }
  }

  // --- 着地判定と Idle への完全移行 ---
  Transform *tf = player->GetTransform();
  if (tf->translate.y <= groundY_) {
    tf->translate.y = groundY_; // 正しい高さでピタッと止める！
    player->SetVelocity({0.0f, 0.0f, 0.0f});

    // 着地しており、かつポーズ戻し(0.3秒)が終わっていれば、本当の待機状態へ！
    if (animTimer_ >= blendDuration_) {
      player->ChangeState(std::make_unique<PlayerStateIdle>());
    }
  }
}

void PlayerStateWinReturn::Exit(Player *player) {}
