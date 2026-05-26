#include "PlayerStateShared.h"

// ========================================================
// 待機状態 (Idle)
// ========================================================
void PlayerStateIdle::Enter(Player *player) {
  if (!player)
    return;

  SetSwordActive(player, false);
  player->PlayAnimation("Idle", false);
  DebugConsole::GetInstance()->AddLog(
      "★ ENTER: Idle State (searching feet/arms/sword/head)");

  // 初期化
  leftFootObj_ = nullptr;
  rightFootObj_ = nullptr;
  leftFootSaved_ = false;
  rightFootSaved_ = false;
  leftFootStartRot_ = {0, 0, 0};
  rightFootStartRot_ = {0, 0, 0};
  leftArmObj_ = nullptr;
  rightArmObj_ = nullptr;
  leftArmSaved_ = false;
  rightArmSaved_ = false;
  leftArmStartRot_ = {0, 0, 0};
  rightArmStartRot_ = {0, 0, 0};
  swordObj_ = nullptr;
  swordSaved_ = false;
  headObj_ = nullptr;
  headSaved_ = false;
  headStartRot_ = {0, 0, 0};

  // ブレンド初期化
  blendTimer_ = 0.0f;

  // Idle では体の向きを強制しない（攻撃開始時に回すため）
  s_bodyStartY = player->GetRotation().y;
  s_bodyTargetY = s_bodyStartY;
  s_bodyBlendActive = false;

  TryFindFeet(player, leftFootObj_, rightFootObj_);
  if (leftFootObj_) {
    if (s_pendingIdleBlend.active && s_pendingIdleBlend.leftFoot) {
      // Attack1 が渡した start/target を使ってブレンドする
      leftFootDefaultRot_ = s_pendingIdleBlend.leftFootTarget;
      leftFootStartRot_ = s_pendingIdleBlend.leftFootStart;
    } else {
      // 通常の初期化（現在オブジェクトの回転をデフォルトに）
      leftFootDefaultRot_ = leftFootObj_->GetRotation();
      leftFootStartRot_ = leftFootDefaultRot_;
    }
    leftFootSaved_ = true;
  }

  if (rightFootObj_) {
    if (s_pendingIdleBlend.active && s_pendingIdleBlend.rightFoot) {
      rightFootDefaultRot_ = s_pendingIdleBlend.rightFootTarget;
      rightFootStartRot_ = s_pendingIdleBlend.rightFootStart;
    } else {
      rightFootDefaultRot_ = rightFootObj_->GetRotation();
      rightFootStartRot_ = rightFootDefaultRot_;
    }
    rightFootSaved_ = true;
  }

  // --- 腕 ---
  TryFindArms(player, leftArmObj_, rightArmObj_);
  if (leftArmObj_) {
    if (s_pendingIdleBlend.active && s_pendingIdleBlend.leftArm) {
      leftArmDefaultRot_ = s_pendingIdleBlend.leftArmTarget;
      leftArmStartRot_ = s_pendingIdleBlend.leftArmStart;
    } else {
      leftArmDefaultRot_ = leftArmObj_->GetRotation();
      leftArmStartRot_ = leftArmDefaultRot_;
    }
    leftArmSaved_ = true;
  }

  if (rightArmObj_) {
    if (s_pendingIdleBlend.active && s_pendingIdleBlend.rightArm) {
      rightArmDefaultRot_ = s_pendingIdleBlend.rightArmTarget;
      rightArmStartRot_ = s_pendingIdleBlend.rightArmStart;
    } else {
      rightArmDefaultRot_ = rightArmObj_->GetRotation();
      rightArmStartRot_ = rightArmDefaultRot_;
    }
    rightArmSaved_ = true;
  }

  // --- 頭 ---
  TryFindHead(player, headObj_);
  if (headObj_) {
    if (s_pendingIdleBlend.active && s_pendingIdleBlend.head) {
      headDefaultRot_ = s_pendingIdleBlend.headTarget;
      headStartRot_ = s_pendingIdleBlend.headStart;
    } else {
      Transform *htf = headObj_->GetTransform();
      headDefaultRot_ = headObj_->GetRotation();
      headStartRot_ = htf->rotate;
    }
    headSaved_ = true;
  }

  // --- 体 Y のブレンド有効化 ---
  if (s_pendingIdleBlend.active && s_pendingIdleBlend.body) {
    s_bodyStartRotVec = s_pendingIdleBlend.bodyStart;
    s_bodyTargetRotVec = s_pendingIdleBlend.bodyTarget;
    s_bodyBlendActive = true;
    blendTimer_ = 0.0f;
    blendDuration_ = s_pendingIdleBlend.blendDuration;
  }

  // 使用後は Pending をクリアしておく
  s_pendingIdleBlend.active = false;

  animTimer_ = 0.0f;
  footStage_ = 0;
}

void PlayerStateIdle::Update(Player *player) {
  if (!player)
    return;

  InputManager *im = player ? player->GetInputManager() : nullptr;
  bool attackTriggered = im && im->IsActionTriggered("Attack");
  if (player->GetIsControlActive()) {
    if (attackTriggered) {
      // --- 地上攻撃の処理  ---
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
  Vector3 vel = player->GetVelocity();
  vel.y = 0.0f;
  float speed = Math::Length(vel);
  if (speed > 0.1f) {
    player->ChangeState(std::make_unique<PlayerStateRun>());
    return;
  }

  // 再探索
  if (!leftFootObj_ || !rightFootObj_) {
    TryFindFeet(player, leftFootObj_, rightFootObj_);
    if (leftFootObj_ && !leftFootSaved_) {
      leftFootDefaultRot_ = leftFootObj_->GetRotation();
      leftFootStartRot_ = leftFootDefaultRot_;
      leftFootSaved_ = true;
    }
    if (rightFootObj_ && !rightFootSaved_) {
      rightFootDefaultRot_ = rightFootObj_->GetRotation();
      rightFootStartRot_ = rightFootDefaultRot_;
      rightFootSaved_ = true;
    }
  }

  if (!leftArmObj_ || !rightArmObj_) {
    TryFindArms(player, leftArmObj_, rightArmObj_);
    if (leftArmObj_ && !leftArmSaved_) {
      leftArmDefaultRot_ = leftArmObj_->GetRotation();
      leftArmStartRot_ = leftArmDefaultRot_;
      leftArmSaved_ = true;
    }
    if (rightArmObj_ && !rightArmSaved_) {
      rightArmDefaultRot_ = rightArmObj_->GetRotation();
      rightArmStartRot_ = rightArmDefaultRot_;
      rightArmSaved_ = true;
    }
  }

  if (!swordObj_) {
    TryFindSword(player, swordObj_);
    if (swordObj_ && !swordSaved_) {
      swordDefaultLocalPos_ = swordObj_->GetTransform()->translate;
      swordDefaultWorldPos_ = swordObj_->GetWorldPosition();
      swordSaved_ = true;
      // 剣がアタッチされた直後のこのタイミングで確実に初期判定（64）を無効化する
      SetSwordActive(player, false);
    }
  }

  if (!headObj_) {
    TryFindHead(player, headObj_);
    if (headObj_ && !headSaved_) {
      Transform *htf = headObj_->GetTransform();
      headDefaultRot_ = headObj_->GetRotation();
      headStartRot_ = htf->rotate;
      headSaved_ = true;
    }
  }
}

void PlayerStateIdle::Exit(Player *player) {
  // ブレンド中に Idle を途切れさせてしまうケース対策：
  // もし Idle 側の「体ブレンド」がアクティブなまま離脱するなら、
  // 目標回転を即時適用して攻撃ポーズが残らないようにする。
  if (s_bodyBlendActive && player) {
    player->SetRotation(s_bodyTargetRotVec);
    s_bodyBlendActive = false;
  }

  // 元に戻す
  if (leftFootObj_) {
    Transform *tf = leftFootObj_->GetTransform();
    tf->rotate = leftFootDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(leftFootDefaultRot_);
    tf->isQuaternionMaster = true;
    leftFootObj_->UpdateWorldMatrix();
  }

  if (rightFootObj_) {
    Transform *tf = rightFootObj_->GetTransform();
    tf->rotate = rightFootDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(rightFootDefaultRot_);
    tf->isQuaternionMaster = true;
    rightFootObj_->UpdateWorldMatrix();
  }

  if (leftArmObj_) {
    Transform *tf = leftArmObj_->GetTransform();
    tf->rotate = leftArmDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(leftArmDefaultRot_);
    tf->isQuaternionMaster = true;
    leftArmObj_->UpdateWorldMatrix();
  }

  if (rightArmObj_) {
    Transform *tf = rightArmObj_->GetTransform();
    tf->rotate = rightArmDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(rightArmDefaultRot_);
    tf->isQuaternionMaster = true;
    rightArmObj_->UpdateWorldMatrix();
  }

  if (headObj_ && headSaved_) {
    Transform *tf = headObj_->GetTransform();
    tf->rotate = headDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(headDefaultRot_);
    tf->isQuaternionMaster = true;
    headObj_->UpdateWorldMatrix();
  }

  if (swordObj_ && swordSaved_) {
    Transform *tf = swordObj_->GetTransform();
    tf->translate = swordDefaultLocalPos_;
    swordObj_->UpdateLocalMatrix();
    swordObj_->UpdateWorldMatrix();
  }

  // ブレンド解除（念のため）
  s_bodyBlendActive = false;
}

void PlayerStateIdle::ApplyPostUpdate(Player *player, float deltaTime) {
  if (!player)
    return;
  if (deltaTime <= 0.0f)
    return;

  // ブレンド更新
  blendTimer_ += deltaTime;
  float blendT = (blendDuration_ > 1e-6f)
                     ? std::clamp(blendTimer_ / blendDuration_, 0.0f, 1.0f)
                     : 1.0f;
  float blendEase = EaseInOutSine(blendT);

  // Idleの周期
  animTimer_ += deltaTime;
  float twoDur = animDuration_ * 2.0f;
  float local = (twoDur > 1e-6f) ? std::fmod(animTimer_, twoDur) : 0.0f;
  float t = (animDuration_ > 0.0f) ? (local / animDuration_) : 1.0f;
  if (t > 1.0f)
    t = 2.0f - t;
  float e = EaseInOutSine(t);

  auto DegToRad = [](float d) { return d * 3.14159265358979323846f / 180.0f; };

  float targetAngle = targetAngleRad_;
  float armZRightRad = DegToRad(5.0f);
  float armZLeftRad = DegToRad(-5.0f);
  const float pi = 3.14159265358979323846f;

  if (leftFootObj_ && leftFootSaved_) {
    Vector3 targetR = leftFootDefaultRot_;
    targetR.x = leftFootDefaultRot_.x + targetAngle * e;
    Vector3 final = LerpVec(leftFootStartRot_, targetR, blendEase);
    Transform *tf = leftFootObj_->GetTransform();
    tf->quaternion = Math::EulerToQuaternion(final);
    tf->isQuaternionMaster = true;
    leftFootObj_->UpdateWorldMatrix();
  }

  if (rightFootObj_ && rightFootSaved_) {
    Vector3 targetR = rightFootDefaultRot_;
    targetR.x = rightFootDefaultRot_.x + targetAngle * e;
    Vector3 final = LerpVec(rightFootStartRot_, targetR, blendEase);
    Transform *tf = rightFootObj_->GetTransform();
    tf->quaternion = Math::EulerToQuaternion(final);
    tf->isQuaternionMaster = true;
    rightFootObj_->UpdateWorldMatrix();
  }

  if (leftArmObj_ && leftArmSaved_) {
    Vector3 targetR = leftArmDefaultRot_;
    targetR.z = leftArmDefaultRot_.z + armZLeftRad * e;
    Vector3 final = LerpVec(leftArmStartRot_, targetR, blendEase);
    Transform *tf = leftArmObj_->GetTransform();
    tf->quaternion = Math::EulerToQuaternion(final);
    tf->isQuaternionMaster = true;
    leftArmObj_->UpdateWorldMatrix();
  }

  if (rightArmObj_ && rightArmSaved_) {
    Vector3 targetR = rightArmDefaultRot_;
    targetR.z = rightArmDefaultRot_.z + armZRightRad * e;
    Vector3 final = LerpVec(rightArmStartRot_, targetR, blendEase);
    Transform *tf = rightArmObj_->GetTransform();
    tf->quaternion = Math::EulerToQuaternion(final);
    tf->isQuaternionMaster = true;
    rightArmObj_->UpdateWorldMatrix();
  }

  if (swordObj_ && swordSaved_) {
    Transform *tf = swordObj_->GetTransform();
    tf->translate = swordDefaultLocalPos_;
    swordObj_->UpdateLocalMatrix();
    swordObj_->UpdateWorldMatrix();
  }

  if (headObj_ && headSaved_) {
    float phase = t * pi;
    float h = std::cos(phase);
    Vector3 targetEuler = headDefaultRot_;
    float headAmpRad = DegToRad(2.0f);
    targetEuler.x = headDefaultRot_.x + h * headAmpRad;
    Vector3 blendedEuler = LerpVec(headStartRot_, targetEuler, blendEase);

    Quaternion targetQ = Math::EulerToQuaternion(blendedEuler);
    Transform *tf = headObj_->GetTransform();
    Quaternion currentQ = tf->quaternion;
    float alpha = 1.0f - std::expf(-headSmoothSpeed_ * deltaTime);
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    Quaternion blendedQ = Math::Slerp(currentQ, targetQ, alpha);
    tf->quaternion = blendedQ;
    tf->isQuaternionMaster = true;
    Matrix4x4 rotMat = Math::MakeRotateQuaternionMatrix(blendedQ);
    tf->rotate = Math::MatrixToEuler(rotMat);
    headObj_->UpdateWorldMatrix();
  }

  if (s_bodyBlendActive) {
    float bodyBlendT =
        (blendDuration_ > 1e-6f)
            ? std::clamp(blendTimer_ / blendDuration_, 0.0f, 1.0f)
            : 1.0f;
    float bodyEase = EaseInOutSine(bodyBlendT);
    // Y成分は角度補間で正規化して最短回転を使う（420degのような大きな値で遠回りする問題を回避）
    Vector3 newRot;
    // X/Z は線形補間でよい
    newRot.x = s_bodyStartRotVec.x +
               (s_bodyTargetRotVec.x - s_bodyStartRotVec.x) * bodyEase;
    newRot.z = s_bodyStartRotVec.z +
               (s_bodyTargetRotVec.z - s_bodyStartRotVec.z) * bodyEase;
    // Y は LerpAngle を利用して角度差を正規化する
    newRot.y = LerpAngle(s_bodyStartRotVec.y, s_bodyTargetRotVec.y, bodyEase);
    player->SetRotation(newRot); // Quaternion も更新される
    if (bodyBlendT >= 1.0f)
      s_bodyBlendActive = false;
  }
}

// ========================================================
// 走り状態 (Run)
