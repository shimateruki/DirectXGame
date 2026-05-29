#include "PlayerStateShared.h"
#include "AudioPlayer.h"

void PlayerStatePlungeAttack::Enter(Player *player) {
  if (!player)
    return;
  DebugConsole::GetInstance()->AddLog(
      "★ ENTER: Plunge Attack (Genshin Greatsword Style)");
  SetSwordActive(player, true, player->GetAttackParams().damagePlunge);
  
  // 空中からの落下攻撃開始時
  AudioPlayer::GetInstance()->PlaySE(player->GetSEDownAttack1Handle(), false, 1.0f);
  isPlunging_ = false;
  isLanded_ = false;
  recoveryTimer_ = 0.0f;
  bodyObj_ = player;

  TryFindHead(player, headObj_);
  TryFindArms(player, leftArmObj_, rightArmObj_);
  TryFindFeet(player, leftFootObj_, rightFootObj_);

  // 剣を探してデフォルト角度と位置を保存
  TryFindSword(player, swordObj_);
  if (swordObj_) {
    swordDefaultRot_ = swordObj_->GetRotation();
    // ローカル位置も保存しておく（復帰用）
    swordDefaultLocalPos_ = swordObj_->GetTransform()->translate;
    swordDefaultWorldPos_ = swordObj_->GetWorldPosition();
    Matrix4x4 wMat = swordObj_->GetWorldMatrix();
    swordDefaultWorldRot_ = Math::MatrixToEuler(wMat);
  }

  initializedParts_ = false;
  // 体のデフォルトを保存
  if (bodyObj_)
    bodyDefaultPos_ = bodyObj_->GetTransform()->translate;
  if (bodyObj_)
    bodyDefaultRot_ = bodyObj_->GetRotation();
  if (headObj_)
    headDefaultRot_ = headObj_->GetRotation();
  if (rightArmObj_)
    rightArmDefaultRot_ = rightArmObj_->GetRotation();
  if (leftArmObj_)
    leftArmDefaultRot_ = leftArmObj_->GetRotation();
  if (rightFootObj_)
    rightFootDefaultRot_ = rightFootObj_->GetRotation();
  if (leftFootObj_)
    leftFootDefaultRot_ = leftFootObj_->GetRotation();

  // --- 重要: 足の当たり属性を確実に有効化（安全側） ---
  if (leftFootObj_)
    leftFootObj_->SetCollisionAttribute(kPlayer);
  if (rightFootObj_)
    rightFootObj_->SetCollisionAttribute(kPlayer);

  initializedParts_ = true;

  Vector3 vel = player->GetVelocity();
  vel.y = 5.0f;
  vel.x = 0.0f;
  vel.z = 0.0f;
  player->SetVelocity(vel);

  ApplyPose(player);
}

void PlayerStatePlungeAttack::Update(Player *player) {
  if (!player)
    return;

  if (!isLanded_) {
    Vector3 vel = player->GetVelocity();

    if (!isPlunging_) {
      // ホップが終わり、落ち始めたら猛スピードで落下
      if (vel.y <= 0.0f) {
        vel.y = -40.0f; // 爆速落下
        player->SetVelocity(vel);
        isPlunging_ = true;
      }
    } else {
      // 着地判定：猛スピード(-40)だったのが、床にぶつかって速度が0に近づいたら着地
      if (vel.y > -5.0f) {
        isLanded_ = true;
        DebugConsole::GetInstance()->AddLog("Plunge Attack: LANDED! (DOOOM!)");
        
        // 地面に激突した時
        AudioPlayer::GetInstance()->PlaySE(player->GetSEDownAttack2Handle(), false, 1.0f);

        Vector3 landPos = player->GetWorldPosition();
        MeshEffectManager::GetInstance()->SpawnEffectAt(
            "Resources/json/effect/effect_bakuhatu.json",
            landPos,
            { 0.0f, 0.0f, 0.0f }, // 地面に合わせた回転
            { 1.5f, 1.5f, 1.5f }, // 衝撃波のスケール
            player->GetAttackParams().damagePlunge
        );

        // パーティクルの発生
        particleEmitter_ = std::make_unique<GPUParticleEmitter>();
        particleEmitter_->Initialize("fallAttak", bodyObj_);
        particleEmitter_->SetOffset({ 0.0f, 0.0f, 0.0f });
        particleEmitter_->SetInterval(0.016f);
        particleEmitter_->Play();
      }
    }
  } else {
    // 着地後の立ち上がり硬直
    recoveryTimer_ += 1.0f / 60.0f;
    if (recoveryTimer_ >= recoveryDuration_) {
      player->ChangeState(std::make_unique<PlayerStateIdle>());
      return;
    }
  }

  if (particleEmitter_) {
    particleEmitter_->Update(1.0f / 60.0f);
  }

  ApplyPose(player);
}

void PlayerStatePlungeAttack::Exit(Player *player) {
  if (!player)
    return;
  SetSwordActive(player, false);

  if (particleEmitter_) {
    particleEmitter_->Stop();
    particleEmitter_.reset();
  }

  // 初期化されていなければ復帰処理を行わない
  if (!initializedParts_)
    return;

  // 体（プレイヤー本体）の位置: Y
  // は物理(エンジン)管理を維持するため上書きしない
  if (bodyObj_) {
    Transform *tf = bodyObj_->GetTransform();
    // preserve current engine-driven Y
    float currentY = tf->translate.y;

    // Restore X/Z to saved defaults, keep Y as-is
    tf->translate.x = bodyDefaultPos_.x;
    tf->translate.z = bodyDefaultPos_.z;
    tf->translate.y = currentY;

    // Restore rotation
    tf->rotate = bodyDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    bodyObj_->UpdateWorldMatrix();
  }

  // 頭・腕・脚の復帰
  if (headObj_) {
    Transform *tf = headObj_->GetTransform();
    tf->translate = headDefaultPos_;
    tf->rotate = headDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    headObj_->UpdateWorldMatrix();
  }

  if (rightArmObj_) {
    Transform *tf = rightArmObj_->GetTransform();
    tf->translate = rightArmDefaultPos_;
    tf->rotate = rightArmDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    rightArmObj_->UpdateWorldMatrix();
  }

  if (leftArmObj_) {
    Transform *tf = leftArmObj_->GetTransform();
    tf->translate = leftArmDefaultPos_;
    tf->rotate = leftArmDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftArmObj_->UpdateLocalMatrix();
    leftArmObj_->UpdateWorldMatrix();
  }

  if (rightFootObj_) {
    Transform *tf = rightFootObj_->GetTransform();
    tf->translate = rightFootDefaultPos_;
    tf->rotate = rightFootDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    rightFootObj_->UpdateLocalMatrix();
    rightFootObj_->UpdateWorldMatrix();
  }

  if (leftFootObj_) {
    Transform *tf = leftFootObj_->GetTransform();
    tf->translate = leftFootDefaultPos_;
    tf->rotate = leftFootDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftFootObj_->UpdateLocalMatrix();
    leftFootObj_->UpdateWorldMatrix();
  }

  // 剣は親状態に応じて正しく復帰させる（Unparentedだったらプレイヤーに再アタッチしてローカル座標を戻す、
  // 親が存在する／再アタッチ不可な場合はワールド座標で復帰）
  if (swordObj_) {
    Transform *stf = swordObj_->GetTransform();

    // 保存してあるデフォルトは Enter 時に取ったローカルとワールドの両方
    // drop により親が外れている (GetParent() == nullptr) 場合は
    // - 可能なら bodyObj_ に再アタッチしてローカル位置を復元する（手元に戻す）
    // - bodyObj_ が無ければワールド座標を復元する
    if (swordObj_->GetParent() == nullptr) {
      Object3d* targetParent = rightArmObj_ ? rightArmObj_ : bodyObj_;
      if (targetParent) {
        // 再アタッチする場合は、"保存したワールド座標/回転"
        // を親の逆行列でローカル行列に変換してから適用する。
        // これにより、親が変化してもワールド空間で期待される位置回転を保ちつつローカルに正しく戻せる。
        // 1)
        // 期待するワールド行列を作る（保存したワールド位置/回転、現在のスケールを使用）
        Matrix4x4 desiredWorld = Math::MakeAffineMatrix(
            stf->scale, swordDefaultWorldRot_, swordDefaultWorldPos_);
        // 2) 親のワールド行列の逆を掛けてローカル行列を得る
        Matrix4x4 parentWorld = targetParent->GetWorldMatrix();
        Matrix4x4 invParent = Math::Inverse(parentWorld);
        Matrix4x4 localMat = Math::Multiply(desiredWorld, invParent);
        // 3) localMat から位置・回転・スケールを抜き出して Transform に設定
        Vector3 localPos = {localMat.m[3][0], localMat.m[3][1],
                            localMat.m[3][2]};
        Vector3 localRot = Math::MatrixToEuler(localMat);
        // スケールは列長で復元（必要なら）
        Vector3 localScale = {
            Math::Length(
                Vector3{localMat.m[0][0], localMat.m[0][1], localMat.m[0][2]}),
            Math::Length(
                Vector3{localMat.m[1][0], localMat.m[1][1], localMat.m[1][2]}),
            Math::Length(
                Vector3{localMat.m[2][0], localMat.m[2][1], localMat.m[2][2]})};

        // 先に Transform のローカル値をセット（SetParent の中で
        // UpdateWorldMatrix が走るため）
        stf->translate = localPos;
        stf->rotate = localRot;
        stf->scale = localScale;
        stf->quaternion = Math::EulerToQuaternion(stf->rotate);
        stf->isQuaternionMaster = true;

        // 再アタッチ -> 内部で UpdateWorldMatrix
        // が呼ばれるはずだが、確実に更新する
        swordObj_->SetParent(targetParent);
        swordObj_->UpdateLocalMatrix();
        swordObj_->UpdateWorldMatrix();
      } else {
        // 親がなく、再アタッチ先も無い場合はワールド位置で復帰
        stf->translate = swordDefaultWorldPos_;
        stf->rotate = swordDefaultWorldRot_;
        // スケールは現状維持（または保存してあれば使う）
        stf->quaternion = Math::EulerToQuaternion(stf->rotate);
        stf->isQuaternionMaster = true;
        // UpdateWorldMatrix は親が nullptr
        // のときローカル＝ワールドとして扱われる
        swordObj_->UpdateWorldMatrix();
      }
    } else {
      // 既に親付き（通常はプレイヤーの子）の場合はローカルを復元
      stf->translate = swordDefaultLocalPos_;
      stf->rotate = swordDefaultRot_;
      stf->quaternion = Math::EulerToQuaternion(stf->rotate);
      stf->isQuaternionMaster = true;
      swordObj_->UpdateLocalMatrix();
      swordObj_->UpdateWorldMatrix();
    }
  }
}

void PlayerStatePlungeAttack::ApplyPose(Player *player) {
  if (!initializedParts_)
    return;
  auto DegToRad = [](float d) { return d * 3.14159265358979323846f / 180.0f; };
  auto LerpVec3 = [](const Vector3 &a, const Vector3 &b, float t) {
    return Vector3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                   a.z + (b.z - a.z) * t};
  };
  auto EaseOutCubic = [](float x) { return 1.0f - std::pow(1.0f - x, 3.0f); };

  float currentY = bodyObj_->GetRotation().y;
  Vector3 curBodyPos = {0, 0, 0}, curBodyRot, curHeadPos{0, 0, 0}, curHeadRot,
          curRtArmPos{0, 0, 0}, curRtArmRot, curLtArmPos{0, 0, 0}, curLtArmRot,
          curLtFootPos{0, 0, 0}, curLtFootRot, curRtFootPos{0, 0, 0},
          curRtFootRot;

  // --- [Pose] 定義（省略せず既存の値をそのまま使う） ---
  Vector3 fallBodyPos = {0.0f, 0.0f, 0.0f};
  Vector3 fallBodyRot = {DegToRad(0.0f), currentY, DegToRad(0.0f)};

  Vector3 fallHeadPos = {0.0f, 0.0f, 0.0f};
  Vector3 fallHeadRot = {DegToRad(15.0f), DegToRad(0.0f), DegToRad(0.0f)};

  Vector3 fallRtArmPos = {0.0f, 0.0f, 0.0f};
  Vector3 fallRtArmRot = {DegToRad(-90.0f), DegToRad(-65.0f), DegToRad(0.0f)};

  Vector3 fallLtArmPos = {0.0f, 0.0f, 0.0f};
  Vector3 fallLtArmRot = {DegToRad(-90.0f), DegToRad(65.0f), DegToRad(0.0f)};

  Vector3 fallRtFootPos = {0.0f, 0.0f, 0.0f};
  Vector3 fallRtFootRot = {DegToRad(0.0f), DegToRad(0.0f), DegToRad(0.0f)};

  Vector3 fallLtFootPos = {0.0f, 0.0f, 0.0f};
  Vector3 fallLtFootRot = {DegToRad(0.0f), DegToRad(0.0f), DegToRad(0.0f)};

  Vector3 landBodyPos = {0.0f, -0.65f, 0.3f};
  Vector3 landBodyRot = {DegToRad(0.0f), currentY, 0.0f};
  Vector3 landHeadRot = headDefaultRot_ + Vector3{DegToRad(-20.0f), 0.0f, 0.0f};

  Vector3 landRtArmRot =
      rightArmDefaultRot_ + Vector3{DegToRad(20.0f), 0.0f, 0.0f};
  Vector3 landLtArmRot =
      leftArmDefaultRot_ + Vector3{DegToRad(20.0f), 0.0f, 0.0f};

  Vector3 landRtFootRot =
      rightFootDefaultRot_ + Vector3{DegToRad(-60.0f), 0.0f, DegToRad(20.0f)};
  Vector3 landLtFootRot =
      leftFootDefaultRot_ + Vector3{DegToRad(-60.0f), 0.0f, DegToRad(-20.0f)};

  // --- 状態に応じた選択 ---
  if (!isLanded_) {
    curBodyPos = fallBodyPos;
    curBodyRot = fallBodyRot;
    curHeadPos = fallHeadPos;
    curHeadRot = fallHeadRot;
    curRtArmPos = fallRtArmPos;
    curRtArmRot = fallRtArmRot;
    curLtArmPos = fallLtArmPos;
    curLtArmRot = fallLtArmRot;
    curLtFootPos = fallLtFootPos;
    curLtFootRot = fallLtFootRot;
    curRtFootPos = fallRtFootPos;
    curRtFootRot = fallRtFootRot;
  } else {
    curBodyPos = landBodyPos;
    curBodyRot = landBodyRot;
    curHeadPos = fallHeadPos;
    curHeadRot = fallHeadRot;
    curRtArmPos = fallRtArmPos;
    curRtArmRot = fallRtArmRot;
    curLtArmPos = fallLtArmPos;
    curLtArmRot = fallLtArmRot;
    curLtFootPos = leftFootDefaultPos_;
    curLtFootRot = leftFootDefaultRot_;
    curRtFootPos = rightFootDefaultPos_;
    curRtFootRot = rightFootDefaultRot_;
    curLtFootRot.x = 0.0f;
    curRtFootRot.x = 0.0f;
  }

  // --- 本体適用（Yは物理優先で最低高さを守る） ---
  if (bodyObj_) {
    Transform *tf = bodyObj_->GetTransform();
    tf->translate.x = bodyDefaultPos_.x + curBodyPos.x;
    tf->translate.z = bodyDefaultPos_.z + curBodyPos.z;
    float engineY = tf->translate.y;
    const float groundLevel = 0.55f;
    tf->translate.y = (std::max)(groundLevel, engineY + curBodyPos.y);

    tf->rotate = curBodyRot;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    bodyObj_->UpdateWorldMatrix();
  }

  // --- 頭・腕 ---
  if (headObj_) {
    Transform *tf = headObj_->GetTransform();
    tf->translate = headDefaultPos_ + curHeadPos;
    tf->rotate = curHeadRot;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    headObj_->UpdateWorldMatrix();
  }
  if (rightArmObj_) {
    Transform *tf = rightArmObj_->GetTransform();
    tf->translate = curRtArmPos;
    tf->rotate = curRtArmRot;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    rightArmObj_->UpdateWorldMatrix();
  }
  if (leftArmObj_) {
    Transform *tf = leftArmObj_->GetTransform();
    tf->translate = curLtArmPos;
    tf->rotate = curLtArmRot;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftArmObj_->UpdateLocalMatrix();
    leftArmObj_->UpdateWorldMatrix();
  }

  // --- 足: ローカル更新→ワールド更新（コライダーに反映） ---
  if (leftFootObj_) {
    Transform *tf = leftFootObj_->GetTransform();
    tf->translate = curLtFootPos;
    tf->rotate = curLtFootRot;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftFootObj_->UpdateLocalMatrix();
    leftFootObj_->UpdateWorldMatrix();
    // 着地時は衝突属性を確実に戻す（対策）
    if (isLanded_)
      leftFootObj_->SetCollisionAttribute(kPlayer);
  }
  if (rightFootObj_) {
    Transform *tf = rightFootObj_->GetTransform();
    tf->translate = curRtFootPos;
    tf->rotate = curRtFootRot;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    rightFootObj_->UpdateLocalMatrix();
    rightFootObj_->UpdateWorldMatrix();
    if (isLanded_)
      rightFootObj_->SetCollisionAttribute(kPlayer);
  }

  // 補正: 着地時、足のワールドAABBを基準に body の Y
  // を微調整して足が地面に埋まらないようにする ---
  if (isLanded_ && bodyObj_) {
    const float groundLevel = 0.55f;
    float minFootY = 1e9f;
    if (leftFootObj_) {
      AABB a = leftFootObj_->GetAABB();
      minFootY = std::min(minFootY, a.min.y);
    }
    if (rightFootObj_) {
      AABB b = rightFootObj_->GetAABB();
      minFootY = std::min(minFootY, b.min.y);
    }
    if (minFootY < 1e8f) {
      if (minFootY < groundLevel) {
        Transform *btf = bodyObj_->GetTransform();
        // 足が groundLevel より下にある分だけ本体を持ち上げる
        btf->translate.y += (groundLevel - minFootY);
        bodyObj_->UpdateWorldMatrix();
      }
    }
  }

  // --- 剣処理（既存） ---
  if (swordObj_) {
    Transform *stf = swordObj_->GetTransform();
    Vector3 plungeSwordLocalPos = {0.45f, -0.14f, 0.0f};
    Vector3 plungeSwordLocalRot = {DegToRad(30.0f), DegToRad(-90.0f),
                                   DegToRad(-180.0f)};

    if (!isLanded_) {
      stf->translate = plungeSwordLocalPos;
      stf->rotate = plungeSwordLocalRot;
    } else {
      if (recoveryTimer_ < recoveryDuration_) {
        stf->translate = plungeSwordLocalPos;
        stf->rotate = plungeSwordLocalRot;
      } else {
        stf->translate = swordDefaultLocalPos_;
        stf->rotate = swordDefaultRot_;
      }
    }

    stf->quaternion = Math::EulerToQuaternion(stf->rotate);
    stf->isQuaternionMaster = true;
    swordObj_->UpdateLocalMatrix();
    swordObj_->UpdateWorldMatrix();
  }
}
