#include "PlayerStateShared.h"

void PlayerStateDamage::Enter(Player *player) {
  if (!player)
    return;
  DebugConsole::GetInstance()->AddLog(
      "★ ENTER: Damage State (Knockback & Spin!)");

  // 操作不能にし、剣の当たり判定を消す
  player->SetIsControlActive(false);
  SetSwordActive(player, false);
  isLanded_ = false;
  animTimer_ = 0.0f;
  bodyObj_ = player;

  TryFindHead(player, headObj_);
  TryFindArms(player, leftArmObj_, rightArmObj_);
  TryFindFeet(player, leftFootObj_, rightFootObj_);
  initializedParts_ = false;

  // =======================================================
  // ★修正ポイント1：ねじれたポーズを記憶しないように強制リセット！
  // 攻撃中に被弾しても、戻るべき「デフォルト姿勢(0,0,0)」を強制的に指定します。
  // =======================================================
  float currentY = bodyObj_ ? bodyObj_->GetRotation().y : 0.0f;
  if (bodyObj_)
    bodyDefaultRot_ = Vector3{0.0f, currentY, 0.0f}; // 体の向き(Y)だけは維持
  headDefaultRot_ = {0.0f, 0.0f, 0.0f};
  rightArmDefaultRot_ = {0.0f, 0.0f, 0.0f};
  leftArmDefaultRot_ = {0.0f, 0.0f, 0.0f};
  rightFootDefaultRot_ = {0.0f, 0.0f, 0.0f};
  leftFootDefaultRot_ = {0.0f, 0.0f, 0.0f};

  // =======================================================
  // ★修正ポイント2：攻撃の補間予約（バケツリレー）を完全に消去！
  // =======================================================
  s_pendingIdleBlend.active = false;

  initializedParts_ = true;

  // =======================================================
  // ★修正ポイント3：ノックバックの勢いを抑える
  // =======================================================
  float yaw = currentY;
  float knockbackSpeed = 6.0f; // 15.0f -> 6.0f (後ろに飛ぶ勢いを抑制)
  float knockupSpeed = 7.0f;   // 12.0f -> 7.0f (すぐ着地するように高さを抑制)

  Vector3 vel;
  vel.x = -std::sin(yaw) * knockbackSpeed;
  vel.z = -std::cos(yaw) * knockbackSpeed;
  vel.y = knockupSpeed;
  player->SetVelocity(vel);

  ApplyPose(player, 0.0f);
}

void PlayerStateDamage::Update(Player *player) {
  if (!player)
    return;
  const float dt = 1.0f / 60.0f;
  animTimer_ += dt;

  if (!isLanded_) {
    // 空中：落ちてきて地面に触れたら着地フェーズへ
    if (player->GetVelocity().y <= 0.0f && player->IsGrounded()) {
      isLanded_ = true;
      animTimer_ = 0.0f; // 着地からのタイマーにリセット
      DebugConsole::GetInstance()->AddLog("Damage: Landed! Recovering...");
    }
  } else {
    // 着地後：起き上がり時間が過ぎたら待機状態に戻る
    if (animTimer_ >= recoveryDuration_) {
      player->ChangeState(std::make_unique<PlayerStateIdle>());
      return;
    }
  }

  ApplyPose(player, animTimer_);
}

void PlayerStateDamage::Exit(Player *player) {
  if (player)
    player->SetIsControlActive(true);
  if (!initializedParts_)
    return;

  // 全パーツを確実にデフォルトに戻す
  if (bodyObj_) {
    Transform *tf = bodyObj_->GetTransform();
    tf->rotate = bodyDefaultRot_;
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
  if (rightFootObj_) {
    Transform *tf = rightFootObj_->GetTransform();
    tf->rotate = rightFootDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    rightFootObj_->UpdateWorldMatrix();
  }
  if (leftFootObj_) {
    Transform *tf = leftFootObj_->GetTransform();
    tf->rotate = leftFootDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftFootObj_->UpdateWorldMatrix();
  }
}

void PlayerStateDamage::ApplyPose(Player *player, float t) {
  if (!initializedParts_)
    return;
  auto DegToRad = [](float d) { return d * 3.1415926535f / 180.0f; };
  auto LerpVec = [](const Vector3 &a, const Vector3 &b, float t) {
    return Vector3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                   a.z + (b.z - a.z) * t};
  };
  // 起き上がり用の滑らかなイージング
  auto EaseOutCubic = [](float x) { return 1.0f - std::pow(1.0f - x, 3.0f); };

  Vector3 curBodyRot = bodyDefaultRot_;
  Vector3 curHeadRot = headDefaultRot_;
  Vector3 curRtArmRot = rightArmDefaultRot_;
  Vector3 curLtArmRot = leftArmDefaultRot_;
  Vector3 curRtFootRot = rightFootDefaultRot_;
  Vector3 curLtFootRot = leftFootDefaultRot_;

  if (!isLanded_) {
    // ===============================================
    // ① 空中フェーズ：バンザイしながら後ろにキリモミ回転！
    // ===============================================
    // 時間経過でマイナス方向（後ろ）に回し続ける。1秒間に約2回転。
    float spinX = -t * DegToRad(720.0f);
    curBodyRot.x = bodyDefaultRot_.x + spinX;

    // のけぞり＆バンザイポーズ
    curHeadRot.x = headDefaultRot_.x + DegToRad(-30.0f); // 上を向く
    curRtArmRot.x =
        rightArmDefaultRot_.x + DegToRad(-150.0f); // 腕を後ろに振り上げる
    curRtArmRot.z = rightArmDefaultRot_.z + DegToRad(45.0f); // 腕を開く
    curLtArmRot.x = leftArmDefaultRot_.x + DegToRad(-150.0f);
    curLtArmRot.z = leftArmDefaultRot_.z + DegToRad(-45.0f);

    // 足はバタバタさせる（少し前に投げ出す）
    curRtFootRot.x = rightFootDefaultRot_.x + DegToRad(-45.0f);
    curLtFootRot.x = leftFootDefaultRot_.x + DegToRad(-45.0f);
  } else {
    // ===============================================
    // ② 着地フェーズ：ダンッと膝をついてからスッと起き上がる
    // ===============================================
    float nT = std::clamp(t / recoveryDuration_, 0.0f, 1.0f);
    float easeT = EaseOutCubic(nT);

    // 着地した瞬間の「膝をついたような」ダメージポーズ
    Vector3 landBodyRot =
        bodyDefaultRot_ + Vector3{DegToRad(40.0f), 0.0f, 0.0f}; // 体を前に倒す
    Vector3 landHeadRot = headDefaultRot_ + Vector3{DegToRad(-20.0f), 0.0f,
                                                    0.0f}; // 顔は少し上げる
    Vector3 landRtArmRot =
        rightArmDefaultRot_ + Vector3{DegToRad(20.0f), 0.0f, 0.0f};
    Vector3 landLtArmRot =
        leftArmDefaultRot_ + Vector3{DegToRad(20.0f), 0.0f, 0.0f};
    Vector3 landRtFootRot = rightFootDefaultRot_ +
                            Vector3{DegToRad(-40.0f), 0.0f, 0.0f}; // 膝を曲げる
    Vector3 landLtFootRot =
        leftFootDefaultRot_ + Vector3{DegToRad(-40.0f), 0.0f, 0.0f};

    // ダメージポーズから、通常の直立（DefaultRot）へ滑らかに戻る
    curBodyRot = LerpVec(landBodyRot, bodyDefaultRot_, easeT);
    curHeadRot = LerpVec(landHeadRot, headDefaultRot_, easeT);
    curRtArmRot = LerpVec(landRtArmRot, rightArmDefaultRot_, easeT);
    curLtArmRot = LerpVec(landLtArmRot, leftArmDefaultRot_, easeT);
    curRtFootRot = LerpVec(landRtFootRot, rightFootDefaultRot_, easeT);
    curLtFootRot = LerpVec(landLtFootRot, leftFootDefaultRot_, easeT);
  }

  // 各パーツに適用
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
// ========================================================
// 勝利状態 (Win) 実装
