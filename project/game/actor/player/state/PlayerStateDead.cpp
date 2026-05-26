#include "PlayerStateShared.h"
#include "AudioPlayer.h"
#include "Player.h"

void PlayerStateDead::Enter(Player *player) {
  if (!player)
    return;
  DebugConsole::GetInstance()->AddLog("★ ENTER: Dead State (Biohazard4 Style)");

  player->SetIsControlActive(false); // 入力を完全封印
  SetSwordActive(player, false);     // 剣の当たり判定を消す
  animTimer_ = 0.0f;
  bodyObj_ = player;

  TryFindHead(player, headObj_);
  TryFindArms(player, leftArmObj_, rightArmObj_);
  TryFindFeet(player, leftFootObj_, rightFootObj_);
  TryFindSword(player, swordObj_);
  isSwordDropped_ = false;
  initializedParts_ = false;
  if (bodyObj_) {
    bodyDefaultPos_ = bodyObj_->GetTransform()->translate;
    bodyDefaultRot_ = bodyObj_->GetRotation();
    bodyStartRot_ = bodyObj_->GetTransform()->rotate;
  }
  if (headObj_) {
    Transform *tf = headObj_->GetTransform();
    headDefaultPos_ = tf->translate;
    headDefaultRot_ = headObj_->GetRotation();
    headStartRot_ = tf->rotate;
  }
  if (rightArmObj_) {
    Transform *tf = rightArmObj_->GetTransform();
    rightArmDefaultPos_ = tf->translate;
    rightArmDefaultRot_ = rightArmObj_->GetRotation();
    rtArmStartRot_ = tf->rotate;
  }
  if (leftArmObj_) {
    Transform *tf = leftArmObj_->GetTransform();
    leftArmDefaultPos_ = tf->translate;
    leftArmDefaultRot_ = leftArmObj_->GetRotation();
    ltArmStartRot_ = tf->rotate;
  }
  if (rightFootObj_) {
    Transform *tf = rightFootObj_->GetTransform();
    rightFootDefaultPos_ = tf->translate;
    rightFootDefaultRot_ = rightFootObj_->GetRotation();
    rtFootStartRot_ = tf->rotate;
  }
  if (leftFootObj_) {
    Transform *tf = leftFootObj_->GetTransform();
    leftFootDefaultPos_ = tf->translate;
    leftFootDefaultRot_ = leftFootObj_->GetRotation();
    ltFootStartRot_ = tf->rotate;
  }

  initializedParts_ = true;
  ApplyPose(0.0f);
}

void PlayerStateDead::Update(Player *player) {
  if (!player)
    return;
  animTimer_ += 1.0f / 60.0f;

  // 3.5秒(animDuration_)で1.0になるように計算
  float t = std::clamp(animTimer_ / animDuration_, 0.0f, 1.0f);
  ApplyPose(t);
}

void PlayerStateDead::Exit(Player *player) {
  // 復活処理がない限り呼ばれません
}

void PlayerStateDead::ApplyPose(float t) {
  if (!initializedParts_)
    return;

  // --- 1. ヘルパー関数群（ラムダ式） ---
  // 計算式のコロンをスラッシュに直しました
  auto DegToRad = [](float d) { return d * 3.14159265358979323846f / 180.0f; };
  auto LerpVec3 = [](const Vector3 &a, const Vector3 &b, float t) {
    return Vector3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                   a.z + (b.z - a.z) * t};
  };
  auto EaseOutCubic = [](float x) { return 1.0f - std::pow(1.0f - x, 3.0f); };
  auto EaseInCubic = [](float x) { return x * x * x; };
  auto EaseInOutSine = [](float x) {
    return -(std::cos(3.14159265f * x) - 1.0f) / 2.0f;
  };

  float currentY = bodyObj_->GetRotation().y;

  // =========================================================
  // 2. 各キーフレーム（ポーズ）の定義
  // =========================================================

  // --- [Pose 1] 被弾のけぞり ---
  Vector3 bodyPos1 = {0.0f, 0.0f, -0.2f};
  Vector3 bodyRot1 = {DegToRad(-15.0f), currentY, 0.0f};
  Vector3 headRot1 = headDefaultRot_ + Vector3{DegToRad(-20.0f), 0.0f, 0.0f};
  Vector3 rtArmRot1 =
      rightArmDefaultRot_ + Vector3{DegToRad(-20.0f), 0.0f, DegToRad(30.0f)};
  Vector3 ltArmRot1 =
      leftArmDefaultRot_ + Vector3{DegToRad(-20.0f), 0.0f, DegToRad(-10.0f)};
  Vector3 ltFootRot1 =
      leftFootDefaultRot_ + Vector3{DegToRad(10.0f), 0.0f, 0.0f};
  Vector3 rtFootRot1 =
      rightFootDefaultRot_ + Vector3{DegToRad(10.0f), 0.0f, 0.0f};

  // --- [Pose 2] 地面に激突 ---
  Vector3 bodyPos2 = {0.0f, -0.8f, 1.0f};
  Vector3 bodyRot2 = {DegToRad(75.0f), currentY, 0.0f};
  Vector3 headRot2 =
      headDefaultRot_ + Vector3{DegToRad(-20.0f), DegToRad(45.0f), 0.0f};
  Vector3 rtArmRot2 =
      rightArmDefaultRot_ + Vector3{DegToRad(10.0f), 0.0f, DegToRad(45.0f)};
  Vector3 ltArmRot2 =
      leftArmDefaultRot_ + Vector3{DegToRad(10.0f), 0.0f, DegToRad(-15.0f)};
  Vector3 ltFootRot2 =
      leftFootDefaultRot_ + Vector3{DegToRad(-10.0f), 0.0f, DegToRad(-15.0f)};
  Vector3 rtFootRot2 =
      rightFootDefaultRot_ + Vector3{DegToRad(-10.0f), 0.0f, DegToRad(15.0f)};

  // --- [Pose 3] 完全な沈黙 (地面ガード用ポーズ) ---
  Vector3 bodyPos3 = {0.0f, -1.0f, 1.2f};
  Vector3 bodyRot3 = {DegToRad(85.0f), currentY, 0.0f};
  Vector3 headRot3 =
      headDefaultRot_ + Vector3{DegToRad(-40.0f), DegToRad(70.0f), 0.0f};
  Vector3 rtArmRot3 =
      rightArmDefaultRot_ + Vector3{DegToRad(10.0f), 0.0f, DegToRad(40.0f)};
  Vector3 ltArmRot3 =
      leftArmDefaultRot_ + Vector3{DegToRad(10.0f), 0.0f, DegToRad(-10.0f)};

  // =========================================================
  // 3. 補間計算
  // =========================================================
  Vector3 curBodyPos, curBodyRot, curHeadRot, curRtArmRot, curLtArmRot,
      curLtFootRot, curRtFootRot;
  float t1 = 0.15f;
  float t2 = 0.40f;
  float t3 = 0.85f;

  if (t <= t1) {
    float localT = EaseOutCubic(t / t1);
    curBodyPos = LerpVec3(Vector3{0, 0, 0}, bodyPos1, localT);
    curBodyRot = LerpVec3(bodyStartRot_, bodyRot1, localT);
    curHeadRot = LerpVec3(headStartRot_, headRot1, localT);
    curRtArmRot = LerpVec3(rtArmStartRot_, rtArmRot1, localT);
    curLtArmRot = LerpVec3(ltArmStartRot_, ltArmRot1, localT);
    curLtFootRot = LerpVec3(ltFootStartRot_, ltFootRot1, localT);
    curRtFootRot = LerpVec3(rtFootStartRot_, rtFootRot1, localT);
  } else if (t <= t2) {
    float localT = EaseInCubic((t - t1) / (t2 - t1));
    curBodyPos = LerpVec3(bodyPos1, bodyPos2, localT);
    curBodyRot = LerpVec3(bodyRot1, bodyRot2, localT);
    curHeadRot = LerpVec3(headRot1, headRot2, localT);
    curRtArmRot = LerpVec3(rtArmRot1, rtArmRot2, localT);
    curLtArmRot = LerpVec3(ltArmRot1, ltArmRot2, localT);
    curLtFootRot = LerpVec3(ltFootRot1, ltFootRot2, localT);
    curRtFootRot = LerpVec3(rtFootRot1, rtFootRot2, localT);
  } else {
    float localT = EaseInOutSine(std::clamp((t - t2) / (t3 - t2), 0.0f, 1.0f));
    curBodyPos = LerpVec3(bodyPos2, bodyPos3, localT);
    curBodyRot = LerpVec3(bodyRot2, bodyRot3, localT);
    curHeadRot = LerpVec3(headRot2, headRot3, localT);
    curRtArmRot = LerpVec3(rtArmRot2, rtArmRot3, localT);
    curLtArmRot = LerpVec3(ltArmRot2, ltArmRot3, localT);
    curLtFootRot = ltFootRot2;
    curRtFootRot = rtFootRot2;
  }

  // =========================================================
  // 4. 行列適用（プレイヤー本体：地面ガード実装）
  // =========================================================
  if (bodyObj_) {
    Transform *tf = bodyObj_->GetTransform();

    float s = std::sin(currentY);
    float c = std::cos(currentY);
    Vector3 worldOffset;
    worldOffset.x = curBodyPos.x * c + curBodyPos.z * s;
    worldOffset.y = curBodyPos.y;
    worldOffset.z = -curBodyPos.x * s + curBodyPos.z * c;

    tf->translate.x = bodyDefaultPos_.x + worldOffset.x;
    tf->translate.z = bodyDefaultPos_.z + worldOffset.z;

    // 場所に関わらず「死んだ瞬間の足元の高さ」を地面として計算する
    // bodyDefaultPos_.y は立ち状態の中心座標。そこから -0.45f
    // した位置を「現在の地面」とする
    const float groundLevel = bodyDefaultPos_.y - 0.45f;

    // 元の高さ(bodyDefaultPos_.y)
    // からオフセットを足したものが、地面より下に行かないようにガード
    tf->translate.y =
        (std::max)(groundLevel, bodyDefaultPos_.y + worldOffset.y);

    tf->rotate = curBodyRot;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    bodyObj_->UpdateWorldMatrix();
  }

  // パーツの更新
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

  // =========================================================
  // 5. 剣の「スピン＆スティック」物理演出
  // =========================================================
  if (swordObj_) {
    Transform *stf = swordObj_->GetTransform();

    if (t >= 0.03f && !isSwordDropped_) {
      isSwordDropped_ = true;
      dropStartTime_ = animTimer_;

      // 死亡時の剣が飛ぶSE再生
      if (bodyObj_) {
        Player* player = static_cast<Player*>(bodyObj_);
        AudioPlayer::GetInstance()->PlaySE(player->GetSESwordHandle(), false, 1.0f);
      }

      Matrix4x4 wMat = swordObj_->GetWorldMatrix();
      swordDropPos_ = {wMat.m[3][0], wMat.m[3][1], wMat.m[3][2]};
      swordDropRot_ = Math::MatrixToEuler(wMat);
      swordDropScale_.x =
          Math::Length(Vector3{wMat.m[0][0], wMat.m[0][1], wMat.m[0][2]});
      swordDropScale_.y =
          Math::Length(Vector3{wMat.m[1][0], wMat.m[1][1], wMat.m[1][2]});
      swordDropScale_.z =
          Math::Length(Vector3{wMat.m[2][0], wMat.m[2][1], wMat.m[2][2]});

      float s = std::sin(currentY);
      float c = std::cos(currentY);
      Vector3 localVel = {3.0f, 6.5f, -2.5f}; // 少し勢いを強化
      swordVelocity_.x = localVel.x * c + localVel.z * s;
      swordVelocity_.y = localVel.y;
      swordVelocity_.z = -localVel.x * s + localVel.z * c;

      swordObj_->SetParent(nullptr);
      stf->scale = swordDropScale_;
      swordObj_->UpdateWorldMatrix();
    }

    if (isSwordDropped_ && !isSwordStuck_) {
      float elapsed = animTimer_ - dropStartTime_;
      const float gravity = 20.0f;

      Vector3 currentPos;
      currentPos.x = swordDropPos_.x + swordVelocity_.x * elapsed;
      currentPos.y = swordDropPos_.y + (swordVelocity_.y * elapsed) -
                     (0.5f * gravity * elapsed * elapsed);
      currentPos.z = swordDropPos_.z + swordVelocity_.z * elapsed;

      // 剣が刺さる高さも、プレイヤーの足元(groundLevel)に合わせる
      float swordGroundY = bodyDefaultPos_.y - 0.45f;
      if (currentPos.y <= swordGroundY) {
        currentPos.y = swordGroundY;
        isSwordStuck_ = true;
      }
      stf->translate = currentPos;

      if (!isSwordStuck_) {
        stf->rotate.x = swordDropRot_.x + swordSpinSpeed_ * elapsed * 2.5f;
        stf->rotate.z = swordDropRot_.z + swordSpinSpeed_ * elapsed;
      } else {
        // スピンの捻りが残らないようにY軸も必ず0にリセット
        stf->rotate.y = DegToRad(0.0f);
        stf->rotate.x = DegToRad(0.0f);
        stf->rotate.z = DegToRad(-90.0f); // または -90.0f
        stf->translate.y += 0.6f;
      }
      stf->quaternion = Math::EulerToQuaternion(stf->rotate);
      stf->isQuaternionMaster = true;
      swordObj_->UpdateWorldMatrix();
    }
  }
}

// ========================================================
// 落下攻撃状態 (Plunge Attack) 実装
