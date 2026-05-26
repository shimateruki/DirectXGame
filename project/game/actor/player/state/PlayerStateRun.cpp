#include "PlayerStateShared.h"
#include "AudioPlayer.h"

void PlayerStateRun::Enter(Player *player) {
  if (!player)
    return;

  SetSwordActive(player, false);
  DebugConsole::GetInstance()->AddLog(
      "★ ENTER: Run State (custom procedural pose)");

  bodyObj_ = player;
  bodySaved_ = false;
  headObj_ = nullptr;
  rightArmObj_ = nullptr;
  leftArmObj_ = nullptr;
  rightFootObj_ = nullptr;
  leftFootObj_ = nullptr;
  rightArmSaved_ = leftArmSaved_ = rightFootSaved_ = leftFootSaved_ =
      headSaved_ = false;

  animTimer_ = 0.0f;
  footstepTimer_ = 0.0f;

  TryFindArms(player, leftArmObj_, rightArmObj_);
  TryFindFeet(player, leftFootObj_, rightFootObj_);
  TryFindHead(player, headObj_);

  if (bodyObj_) {
    Transform *tf = bodyObj_->GetTransform();
    bodyDefaultPos_ = tf->translate;
    bodyDefaultRot_ = tf->rotate;
    bodySaved_ = true;
  }

  if (headObj_) {
    Transform *htf = headObj_->GetTransform();
    headDefaultPos_ = htf->translate;
    headDefaultRot_ = headObj_->GetRotation();
    headStartRot_ = htf->rotate;
    headSaved_ = true;
  }

  if (rightArmObj_) {
    rightArmDefaultPos_ = rightArmObj_->GetTransform()->translate;
    rightArmDefaultRot_ = rightArmObj_->GetRotation();
    rightArmStartRot_ = rightArmDefaultRot_;
    rightArmSaved_ = true;
  }

  if (leftArmObj_) {
    leftArmDefaultPos_ = leftArmObj_->GetTransform()->translate;
    leftArmDefaultRot_ = leftArmObj_->GetRotation();
    leftArmStartRot_ = leftArmDefaultRot_;
    leftArmSaved_ = true;
  }

  if (rightFootObj_) {
    rightFootDefaultPos_ = rightFootObj_->GetTransform()->translate;
    rightFootDefaultRot_ = rightFootObj_->GetRotation();
    rightFootStartRot_ = rightFootDefaultRot_;
    rightFootSaved_ = true;
  }

  if (leftFootObj_) {
    leftFootDefaultPos_ = leftFootObj_->GetTransform()->translate;
    leftFootDefaultRot_ = leftFootObj_->GetRotation();
    leftFootStartRot_ = leftFootDefaultRot_;
    leftFootSaved_ = true;
  }

  // ブレンド初期化
  blendTimer_ = 0.0f;

  auto DegToRad = [](float d) { return d * 3.14159265358979323846f / 180.0f; };

  // 体の前傾と腕脚の初期姿勢は即時適用してもよい（Enter 時の姿勢）
  if (bodyObj_) {
    Transform *tf = bodyObj_->GetTransform();
    tf->rotate.x = DegToRad(10.0f);
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    bodyObj_->UpdateWorldMatrix();
  }
  // 頭は走り中は常に -10deg にする（待機の頭振りを使わない）
  if (headObj_) {
    Transform *tf = headObj_->GetTransform();
    tf->rotate.x = DegToRad(-10.0f);
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    headObj_->UpdateWorldMatrix();
  }

  if (rightArmObj_) {
    Transform *tf = rightArmObj_->GetTransform();
    tf->rotate.x = DegToRad(-10.0f);
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    rightArmObj_->UpdateWorldMatrix();
  }

  if (leftArmObj_) {
    Transform *tf = leftArmObj_->GetTransform();
    tf->rotate.x = DegToRad(-10.0f);
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftArmObj_->UpdateWorldMatrix();
  }

  if (rightFootObj_) {
    Transform *tf = rightFootObj_->GetTransform();
    tf->rotate.x = DegToRad(-10.0f);
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    rightFootObj_->UpdateWorldMatrix();
  }

  if (leftFootObj_) {
    Transform *tf = leftFootObj_->GetTransform();
    tf->rotate.x = DegToRad(-10.0f);
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftFootObj_->UpdateWorldMatrix();
  }
}

void PlayerStateRun::Update(Player *player) {
  if (!player)
    return;

  InputManager *im = player ? player->GetInputManager() : nullptr;
  bool attackTriggered = im && im->IsActionTriggered("Attack");

  if (player->GetIsControlActive()) {
    if (attackTriggered) {
      // --- 地上攻撃の処理 ---
      if ((player && player->ConsumePendingAttack2()) ||
          (player && player->IsComboWindowActive())) {
        player->ChangeState(std::make_unique<PlayerStateAttack2>());
      } else {
        if (player) {
          player->RecordAttackInput(0.15f);
          player->MarkAttackBufferUsedForStateStart();
          player->ChangeState(std::make_unique<PlayerStateAttack1>());
        }
      }
      return;
    }
  }

  Vector3 rawVel = player->GetVelocity();
  Vector3 flatVel = rawVel;
  flatVel.y = 0.0f;
  float speed = Math::Length(flatVel);
  if (speed <= 0.1f) {
    // 直接 Idle に切り替えず、Run 側でブレンドしてから遷移する
    if (!exitBlendActive_) {
      exitBlendActive_ = true;
      exitBlendTimer_ = 0.0f;
      // 現在の回転を開始値として保存（ブレンド開始時点）
      if (rightArmObj_ && rightArmSaved_)
        rightArmExitStartRot_ = rightArmObj_->GetRotation();

      if (leftArmObj_ && leftArmSaved_)
        leftArmExitStartRot_ = leftArmObj_->GetRotation();

      if (rightFootObj_ && rightFootSaved_)
        rightFootExitStartRot_ = rightFootObj_->GetRotation();

      if (leftFootObj_ && leftFootSaved_)
        leftFootExitStartRot_ = leftFootObj_->GetRotation();

      if (headObj_ && headSaved_) {
        headExitStartRot_ = headObj_->GetRotation();
        // クォータニオンも保存（Slerp 用）
        Transform *htf = headObj_->GetTransform();
        headExitStartQuat_ = htf->quaternion;
      }

      if (bodyObj_ && bodySaved_)
        bodyExitStartRot_ = bodyObj_->GetTransform()->rotate;
    }
    return;
  }
}

void PlayerStateRun::Exit(Player *player) {
  DebugConsole::GetInstance()->AddLog("EXIT: Run State - restore defaults");

  // 体: x軸（前後の傾き）だけデフォルトに戻す。Y（向き）は維持する。
  if (bodyObj_ && bodySaved_) {
    Transform *tf = bodyObj_->GetTransform();
    // preserve current Y/Z, restore X from saved default
    Vector3 cur = tf->rotate;
    cur.x = bodyDefaultRot_.x;
    tf->rotate = cur;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    bodyObj_->UpdateWorldMatrix();
  }

  // 頭: Exit では即時復帰しない（ApplyPostUpdate のブレンドに任せる）
  // --- ただし、他状態へ即遷移（例: 攻撃）する場合は Run
  // 側で保存しているデフォルトへ即時復帰しておく ---
  if (headObj_ && headSaved_) {
    Transform *tf = headObj_->GetTransform();
    tf->rotate = headDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    headObj_->UpdateWorldMatrix();
  }

  // 頭: Exit では即時復帰しない（ApplyPostUpdate のブレンドに任せる）
  // 右腕
  if (rightArmObj_ && rightArmSaved_) {
    Transform *tf = rightArmObj_->GetTransform();
    tf->rotate = rightArmDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    rightArmObj_->UpdateWorldMatrix();
  }

  // 左腕: ローカル位置と回転をデフォルトに戻す
  if (leftArmObj_ && leftArmSaved_) {
    Transform *tf = leftArmObj_->GetTransform();
    tf->translate = leftArmDefaultPos_;
    tf->rotate = leftArmDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftArmObj_->UpdateLocalMatrix();
    leftArmObj_->UpdateWorldMatrix();
  }

  // 右足
  if (rightFootObj_ && rightFootSaved_) {
    Transform *tf = rightFootObj_->GetTransform();
    tf->rotate = rightFootDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    rightFootObj_->UpdateWorldMatrix();
  }

  // 左足
  if (leftFootObj_ && leftFootSaved_) {
    Transform *tf = leftFootObj_->GetTransform();
    tf->rotate = leftFootDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftFootObj_->UpdateWorldMatrix();
  }
}

void PlayerStateRun::ApplyPostUpdate(Player *player, float deltaTime) {
  if (!player)
    return;
  if (deltaTime <= 0.0f)
    return;

  animTimer_ += deltaTime;
  blendTimer_ += deltaTime;
  float blendT = (blendDuration_ > 1e-6f)
                     ? std::clamp(blendTimer_ / blendDuration_, 0.0f, 1.0f)
                     : 1.0f;
  float blendEase = EaseInOutSine(blendT);

  // 足音SEの再生制御
  if (!exitBlendActive_) {
    footstepTimer_ += deltaTime;
    float stepInterval = stepPeriod_ / 2.0f;
    if (footstepTimer_ >= stepInterval) {
      footstepTimer_ -= stepInterval;
      AudioPlayer::GetInstance()->PlaySE(player->GetSEMoveHandle(), false, 0.4f);
    }
  }

  float phase = 0.0f;
  if (stepPeriod_ > 1e-6f)
    phase = std::fmod(animTimer_, stepPeriod_) / stepPeriod_;

  const float PI = 3.14159265358979323846f;
  float s = std::sin(phase * 2.0f * PI);
  float easedS = EaseSinToSmooth(s);

  // --- 終了ブレンドがアクティブな場合は、Run の動きを徐々にデフォルト（Run
  // が保存しているデフォルト）へ戻す ---
  if (exitBlendActive_) {
    exitBlendTimer_ += deltaTime;
    float et =
        (exitBlendDuration_ > 1e-6f)
            ? std::clamp(exitBlendTimer_ / exitBlendDuration_, 0.0f, 1.0f)
            : 1.0f;
    float eease = EaseInOutSine(et);

    // 右腕 -> デフォルト
    if (rightArmObj_ && rightArmSaved_) {
      Vector3 targetR = rightArmDefaultRot_;
      Vector3 final = LerpVec(rightArmExitStartRot_, targetR, eease);
      Transform *tf = rightArmObj_->GetTransform();
      tf->quaternion = Math::EulerToQuaternion(final);
      tf->isQuaternionMaster = true;
      rightArmObj_->UpdateWorldMatrix();
    }

    // 左腕 -> デフォルト
    if (leftArmObj_ && leftArmSaved_) {
      Vector3 targetR = leftArmDefaultRot_;
      Vector3 final = LerpVec(leftArmExitStartRot_, targetR, eease);
      Transform *tf = leftArmObj_->GetTransform();
      tf->quaternion = Math::EulerToQuaternion(final);
      tf->isQuaternionMaster = true;
      leftArmObj_->UpdateWorldMatrix();
    }

    // 右足 -> デフォルト
    if (rightFootObj_ && rightFootSaved_) {
      Vector3 targetR = rightFootDefaultRot_;
      Vector3 final = LerpVec(rightFootExitStartRot_, targetR, eease);
      Transform *tf = rightFootObj_->GetTransform();
      tf->quaternion = Math::EulerToQuaternion(final);
      tf->isQuaternionMaster = true;
      rightFootObj_->UpdateWorldMatrix();
    }

    // 左足 -> デフォルト
    if (leftFootObj_ && leftFootSaved_) {
      Vector3 targetR = leftFootDefaultRot_;
      Vector3 final = LerpVec(leftFootExitStartRot_, targetR, eease);
      Transform *tf = leftFootObj_->GetTransform();
      tf->quaternion = Math::EulerToQuaternion(final);
      tf->isQuaternionMaster = true;
      leftFootObj_->UpdateWorldMatrix();
    }

    // 頭 -> デフォルト（クォータニオンで滑らかに戻す）
    if (headObj_ && headSaved_) {
      Transform *tf = headObj_->GetTransform();
      Quaternion targetQ = Math::EulerToQuaternion(headDefaultRot_);
      Quaternion blendedQ = Math::Slerp(headExitStartQuat_, targetQ, eease);
      tf->quaternion = blendedQ;
      tf->isQuaternionMaster = true;
      Matrix4x4 rotMat = Math::MakeRotateQuaternionMatrix(blendedQ);
      tf->rotate = Math::MatrixToEuler(rotMat);
      headObj_->UpdateWorldMatrix();
    }

    // 体の傾き X -> デフォルト（角度差を正規化して滑らかに）
    if (bodyObj_ && bodySaved_) {
      Transform *tf = bodyObj_->GetTransform();
      float sx = bodyExitStartRot_.x;
      float tx = bodyDefaultRot_.x;
      float nx = LerpAngle(sx, tx, eease);
      Vector3 cur = tf->rotate;
      cur.x = nx; // X のみ戻す（Yはそのまま）
      tf->rotate = cur;
      tf->quaternion = Math::EulerToQuaternion(tf->rotate);
      tf->isQuaternionMaster = true;
      bodyObj_->UpdateWorldMatrix();
    }

    // ブレンド完了で Idle に遷移
    if (et >= 1.0f) {
      // exitBlendActive_ をリセットしてから状態遷移する
      exitBlendActive_ = false;
      player->ChangeState(std::make_unique<PlayerStateIdle>());
      return;
    }

    // 終了ブレンド中はここで終了
    return;
  }

  // --- 通常の走りアニメーション ---
  if (rightArmObj_ && rightArmSaved_) {
    Vector3 targetR = rightArmDefaultRot_;
    targetR.x = rightArmDefaultRot_.x - rightArmAmpRad_ * easedS;
    Vector3 final = LerpVec(rightArmStartRot_, targetR, blendEase);
    Transform *tf = rightArmObj_->GetTransform();
    tf->quaternion = Math::EulerToQuaternion(final);
    tf->isQuaternionMaster = true;
    rightArmObj_->UpdateWorldMatrix();
  }

  if (leftArmObj_ && leftArmSaved_) {
    Vector3 targetR = leftArmDefaultRot_;
    targetR.x = leftArmDefaultRot_.x + leftArmAmpRad_ * easedS;
    Vector3 final = LerpVec(leftArmStartRot_, targetR, blendEase);
    Transform *tf = leftArmObj_->GetTransform();
    tf->quaternion = Math::EulerToQuaternion(final);
    tf->isQuaternionMaster = true;
    leftArmObj_->UpdateWorldMatrix();
  }

  if (rightFootObj_ && rightFootSaved_) {
    Vector3 targetR = rightFootDefaultRot_;
    targetR.x = rightFootDefaultRot_.x + footAmpRad_ * easedS;
    Vector3 final = LerpVec(rightFootStartRot_, targetR, blendEase);
    Transform *tf = rightFootObj_->GetTransform();
    tf->quaternion = Math::EulerToQuaternion(final);
    tf->isQuaternionMaster = true;
    rightFootObj_->UpdateWorldMatrix();
  }

  if (leftFootObj_ && leftFootSaved_) {
    Vector3 targetR = leftFootDefaultRot_;
    targetR.x = leftFootDefaultRot_.x - footAmpRad_ * easedS;
    Vector3 final = LerpVec(leftFootStartRot_, targetR, blendEase);
    Transform *tf = leftFootObj_->GetTransform();
    tf->quaternion = Math::EulerToQuaternion(final);
    tf->isQuaternionMaster = true;
    leftFootObj_->UpdateWorldMatrix();
  }

  // 頭: 待機の頭振りを使わず、走りでは常に -10deg を基準にする
  if (headObj_ && headSaved_) {
    auto DegToRad = [](float d) {
      return d * 3.14159265358979323846f / 180.0f;
    };
    Vector3 targetEuler = headDefaultRot_;
    targetEuler.x = headDefaultRot_.x + DegToRad(-10.0f);
    Vector3 final = LerpVec(headStartRot_, targetEuler, blendEase);
    Transform *tf = headObj_->GetTransform();
    tf->quaternion = Math::EulerToQuaternion(final);
    tf->isQuaternionMaster = true;
    headObj_->UpdateWorldMatrix();
  }
}

// ========================================================
// 攻撃1段目状態 (Attack1)
