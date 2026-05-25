#include "PlayerStateShared.h"

void PlayerStateWin::Enter(Player *player) {
  if (!player)
    return;
  DebugConsole::GetInstance()->AddLog("★ ENTER: Win State (Victory Pose!)");

  player->SetIsControlActive(false);
  SetSwordActive(player, false);
  animTimer_ = 0.0f;
  isFrozen_ = false; // フリーズ状態リセット
  bodyObj_ = player;

  TryFindHead(player, headObj_);
  TryFindArms(player, leftArmObj_, rightArmObj_);
  TryFindFeet(player, leftFootObj_, rightFootObj_);

  initializedParts_ = false;
  if (bodyObj_) {
    bodyDefaultPos_ = bodyObj_->GetTransform()->translate;
    bodyDefaultRot_ = bodyObj_->GetRotation();
  }
  if (headObj_) {
    headDefaultRot_ = headObj_->GetRotation();
  }
  if (rightArmObj_) {
    rightArmDefaultRot_ = rightArmObj_->GetRotation();
  }
  if (leftArmObj_) {
    leftArmDefaultRot_ = leftArmObj_->GetRotation();
  }
  if (rightFootObj_) {
    rightFootDefaultRot_ = rightFootObj_->GetRotation();
  }
  if (leftFootObj_) {
    leftFootDefaultRot_ = leftFootObj_->GetRotation();
  }

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

  // =========================================================
  // カメラ計算を廃止単純に「今の向きから180度振り向く」だけ
  // =========================================================
  targetYAngle_ = bodyStartRot_.y + 3.14159265f; // +180度(π)

  initializedParts_ = true;
  ApplyPose(0.0f);
}
void PlayerStateWin::Update(Player *player) {
  if (!player)
    return;

  animTimer_ += 1.0f / 60.0f;
  Vector3 vel = player->GetVelocity();
  Transform *tf = player->GetTransform();

  // =========================================================
  // 前回の落下処理を全削除し、シンプルに空中で止まるだけに
  // =========================================================
  // 1. タメ期間 (0.0s ～ 0.2s)
  if (animTimer_ < 0.2f) {
    vel = {0.0f, 0.0f, 0.0f};
  }
  // 2. 上昇開始 (0.2s ～ 0.5s)
  else if (animTimer_ >= 0.2f && animTimer_ < 0.5f) {
    if (tf->translate.y < 3.0f) {
      vel.y = 15.0f;
    } else {
      vel.y = 0.0f;
    }
  }
  // 3. 空中停止（フリーズ）
  else {
    vel = {0.0f, 0.0f, 0.0f};
    if (!isFrozen_) {
      freezePosY_ = tf->translate.y;
      isFrozen_ = true;
      player->SetIsPhysicsActive(false); // 物理演算停止
    }
    tf->translate.y = freezePosY_; // 座標を固定
  }

  player->SetVelocity(vel);
  ApplyPose(0.0f); // ポーズ適用
}
void PlayerStateWin::Exit(Player *player) {
  if (player) {
    player->SetIsPhysicsActive(true);
    DebugConsole::GetInstance()->AddLog("★ EXIT: Win State (Physics Resumed)");
  }
}
void PlayerStateWin::ApplyPose(float t) {
  if (!initializedParts_)
    return;

  auto DegToRad = [](float d) { return d * 3.14159265358979323846f / 180.0f; };

  auto NormalizeAngle = [](float angle) {
    while (angle <= -3.14159265f)
      angle += 6.2831853f;
    while (angle > 3.14159265f)
      angle -= 6.2831853f;
    return angle;
  };
  auto SafeLerpRot = [&](const Vector3 &a, const Vector3 &b, float t) {
    float ax = NormalizeAngle(b.x - a.x);
    float ay = NormalizeAngle(b.y - a.y);
    float az = NormalizeAngle(b.z - a.z);
    return Vector3{a.x + ax * t, a.y + ay * t, a.z + az * t};
  };

  // --- 各フェーズのポーズ定義（Y軸は 180度反転した targetYAngle_） ---
  Vector3 squatRot = {DegToRad(15.0f), targetYAngle_, 0.0f};
  Vector3 jumpRot = {DegToRad(-10.0f), targetYAngle_, 0.0f};

  Vector3 rtArmWin =
      rightArmDefaultRot_ +
      Vector3{DegToRad(-120.0f), DegToRad(-45.0f), DegToRad(30.0f)};

  // 左腕は勝利ポーズの形を維持する。
  Vector3 ltArmWin =
      leftArmDefaultRot_ + Vector3{DegToRad(-40.0f), 0.0f, DegToRad(-15.0f)};

  Vector3 curBodyRot, curRtArmRot, curLtArmRot, curRtFootRot, curLtFootRot,
      curHeadRot;

  // --- タイムライン補間 ---
  if (animTimer_ < 0.2f) {
    // A. 走り/待機 → しゃがみタメ (0.0s ～ 0.2s)
    float nT = animTimer_ / 0.2f;
    curBodyRot = SafeLerpRot(bodyStartRot_, squatRot, nT);
    curRtArmRot = SafeLerpRot(rightArmStartRot_, rightArmDefaultRot_, nT);
    curLtArmRot = SafeLerpRot(leftArmStartRot_, leftArmDefaultRot_, nT);
    curRtFootRot =
        SafeLerpRot(rightFootStartRot_,
                    rightFootDefaultRot_ + Vector3{DegToRad(-40), 0, 0}, nT);
    curLtFootRot =
        SafeLerpRot(leftFootStartRot_,
                    leftFootDefaultRot_ + Vector3{DegToRad(-40), 0, 0}, nT);
    curHeadRot = SafeLerpRot(headStartRot_, headDefaultRot_, nT);
  } else {
    // B. ジャンプ中＆空中フリーズ (0.2s ～)
    float nT = std::clamp((animTimer_ - 0.2f) / 0.3f, 0.0f, 1.0f);

    curBodyRot = SafeLerpRot(squatRot, jumpRot, nT);

    // 左右非対称のガッツポーズへ
    curRtArmRot = SafeLerpRot(rightArmDefaultRot_, rtArmWin, nT);
    curLtArmRot = SafeLerpRot(leftArmDefaultRot_, ltArmWin, nT);

    // 足の非対称（右足曲げ、左足伸ばし）と非常に相性が良いです
    curRtFootRot = rightFootDefaultRot_ + Vector3{DegToRad(30), 0, 0};
    curLtFootRot = leftFootDefaultRot_ + Vector3{DegToRad(-20), 0, 0};
    curHeadRot = headDefaultRot_ + Vector3{DegToRad(-15), 0, 0};
  }

  // --- 適用 ---
  if (bodyObj_) {
    Transform *tf = bodyObj_->GetTransform();
    tf->rotate = curBodyRot;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    bodyObj_->UpdateWorldMatrix();
  }
  if (headObj_) {
    Transform *tf = headObj_->GetTransform();
    tf->rotate = curHeadRot;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    headObj_->UpdateWorldMatrix();
  }
  if (rightArmObj_) {
    Transform *tf = rightArmObj_->GetTransform();
    tf->rotate = curRtArmRot;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    rightArmObj_->UpdateWorldMatrix();
  }
  if (leftArmObj_) {
    Transform *tf = leftArmObj_->GetTransform();
    tf->rotate = curLtArmRot;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftArmObj_->UpdateWorldMatrix();
  }
  if (rightFootObj_) {
    Transform *tf = rightFootObj_->GetTransform();
    tf->rotate = curRtFootRot;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    rightFootObj_->UpdateWorldMatrix();
  }
  if (leftFootObj_) {
    Transform *tf = leftFootObj_->GetTransform();
    tf->rotate = curLtFootRot;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftFootObj_->UpdateWorldMatrix();
  }
}
