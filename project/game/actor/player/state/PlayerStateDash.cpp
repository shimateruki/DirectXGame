#include "PlayerStateShared.h"

void PlayerStateDash::Enter(Player *player) {
  if (!player)
    return;
  DebugConsole::GetInstance()->AddLog("★ ENTER: Dash State (Slide Step)");

  // PlayerMover を動かし続ける（入力制御は Mover 側）
  SetSwordActive(player, false);
  animTimer_ = 0.0f;
  bodyObj_ = player; // bodyObj_ は Root（プレイヤー自身）

  TryFindHead(player, headObj_);
  TryFindArms(player, leftArmObj_, rightArmObj_);
  TryFindFeet(player, leftFootObj_, rightFootObj_);

  initializedParts_ = false;
  if (bodyObj_) {
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

  initializedParts_ = true;
  ApplyPose(0.0f);

  // スピン初期化（開始角度を保存し、1回転分の目標角度を設定）
  if (spinEnabled_ && bodyObj_) {
    spinStartX_ = bodyObj_->GetRotation().x;
    spinTargetX_ = spinStartX_ + spinTotalRad_;
  }
}

void PlayerStateDash::Update(Player *player) {
  if (!player)
    return;
  animTimer_ += 1.0f / 60.0f;
  float t = std::clamp(animTimer_ / animDuration_, 0.0f, 1.0f);
  ApplyPose(t);

  if (animTimer_ >= animDuration_) {
    player->ChangeState(std::make_unique<PlayerStateIdle>());
    return;
  }
}

void PlayerStateDash::Exit(Player *player) {
  if (!initializedParts_)
    return;

  // 回転(Rotate)だけをデフォルトの姿勢に戻す（Mover が管理する Y は触らない）
  if (bodyObj_) {
    Transform *tf = bodyObj_->GetTransform();
    tf->rotate.x = 0.0f;
    tf->rotate.z = 0.0f; // Y は Mover が管理するため基本は触らない
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    bodyObj_->UpdateWorldMatrix();
  }
  if (headObj_) {
    Transform *tf = headObj_->GetTransform();
    tf->rotate = headDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    headObj_->UpdateWorldMatrix();
  }
  if (rightArmObj_) {
    Transform *tf = rightArmObj_->GetTransform();
    tf->rotate = rightArmDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    rightArmObj_->UpdateWorldMatrix();
  }
  if (leftArmObj_) {
    Transform *tf = leftArmObj_->GetTransform();
    tf->rotate = leftArmDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftArmObj_->UpdateWorldMatrix();
  }
  if (leftFootObj_) {
    Transform *tf = leftFootObj_->GetTransform();
    tf->rotate = leftFootDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftFootObj_->UpdateWorldMatrix();
  }
  if (rightFootObj_) {
    Transform *tf = rightFootObj_->GetTransform();
    tf->rotate = rightFootDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    rightFootObj_->UpdateWorldMatrix();
  }

  // スピンが有効なら、見た目の X
  // を確実に開始角度に戻す（360度回って元の向きに揃える）
  if (player && spinEnabled_) {
    Vector3 r = player->GetRotation();
    r.x = spinStartX_;
    player->SetRotation(r);
  }
}

void PlayerStateDash::ApplyPose(float t) {
  if (!initializedParts_)
    return;
  auto DegToRad = [](float d) { return d * 3.14159265358979323846f / 180.0f; };
  auto LerpVec3 = [](const Vector3 &a, const Vector3 &b, float t) {
    return Vector3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                   a.z + (b.z - a.z) * t};
  };
  auto EaseOutCubic = [](float x) { return 1.0f - std::pow(1.0f - x, 3.0f); };

  // 現在の移動向き（Mover が決定した Y）を取得
  float currentY = bodyObj_->GetRotation().y;

  // --- ポーズ定義 ---
  Vector3 bodyRot1 = {DegToRad(25.0f), currentY, 0.0f};
  Vector3 headRot1 = {DegToRad(-20.0f), 0.0f, 0.0f};
  Vector3 rtArmRot1 = {DegToRad(50.0f), DegToRad(10.0f), DegToRad(10.0f)};
  Vector3 ltArmRot1 = {DegToRad(50.0f), DegToRad(-10.0f), DegToRad(-10.0f)};
  Vector3 ltFootRot1 = {DegToRad(-20.0f), 0.0f, 0.0f};
  Vector3 rtFootRot1 = {DegToRad(20.0f), 0.0f, 0.0f};

  Vector3 bodyRot2 = {DegToRad(-5.0f), currentY, 0.0f};
  Vector3 headRot2 = {DegToRad(5.0f), 0.0f, 0.0f};
  Vector3 rtArmRot2 = {DegToRad(-10.0f), 0.0f, 0.0f};
  Vector3 ltArmRot2 = {DegToRad(-10.0f), 0.0f, 0.0f};
  Vector3 ltFootRot2 = {DegToRad(0.0f), 0.0f, 0.0f};
  Vector3 rtFootRot2 = {DegToRad(0.0f), 0.0f, 0.0f};

  Vector3 curBodyRot, curHeadRot, curRtArmRot, curLtArmRot, curLtFootRot,
      curRtFootRot;

  float t1 = 0.57f;
  if (t <= t1) {
    float localT = EaseOutCubic(t / t1);
    curBodyRot = LerpVec3(Vector3{0.0f, currentY, 0.0f}, bodyRot1, localT);
    curHeadRot = LerpVec3(headDefaultRot_, headRot1, localT);
    curRtArmRot = LerpVec3(rightArmDefaultRot_, rtArmRot1, localT);
    curLtArmRot = LerpVec3(leftArmDefaultRot_, ltArmRot1, localT);
    curLtFootRot = LerpVec3(leftFootDefaultRot_, ltFootRot1, localT);
    curRtFootRot = LerpVec3(rightFootDefaultRot_, rtFootRot1, localT);
  } else {
    float localT = EaseOutCubic((t - t1) / (1.0f - t1));
    curBodyRot = LerpVec3(bodyRot1, bodyRot2, localT);
    curHeadRot = LerpVec3(headRot1, headRot2, localT);
    curRtArmRot = LerpVec3(rtArmRot1, rtArmRot2, localT);
    curLtArmRot = LerpVec3(ltArmRot1, ltArmRot2, localT);
    curLtFootRot = LerpVec3(ltFootRot1, ltFootRot2, localT);
    curRtFootRot = LerpVec3(rtFootRot1, rtFootRot2, localT);
  }

  // 回転のみ適用（座標は上書きしない）
  if (bodyObj_) {
    Transform *tf = bodyObj_->GetTransform();

    // スピン処理（animTimer_ と animDuration_ に基づくイーズ）
    if (spinEnabled_) {
      float spinT = (animDuration_ > 1e-6f)
                        ? std::clamp(animTimer_ / animDuration_, 0.0f, 1.0f)
                        : 1.0f;
      float spinEase = EaseOutCubic(spinT);
      // 完全な 360° 回転を表現するため、正規化は使わず単純に加算
      float spinX = spinStartX_ + spinTotalRad_ * spinEase;
      curBodyRot.x = spinX;
    } else {
      // Mover が計算した X を上書きしない（通常は Mover は Y を管理）
      curBodyRot.x = bodyObj_->GetRotation().x;
    }

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
  if (leftFootObj_) {
    Transform *tf = leftFootObj_->GetTransform();
    tf->rotate = curLtFootRot;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftFootObj_->UpdateWorldMatrix();
  }
  if (rightFootObj_) {
    Transform *tf = rightFootObj_->GetTransform();
    tf->rotate = curRtFootRot;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    rightFootObj_->UpdateWorldMatrix();
  }
}

// ========================================================
// 死亡状態 (Dead - バタリ倒れバイオ4風) 実装
