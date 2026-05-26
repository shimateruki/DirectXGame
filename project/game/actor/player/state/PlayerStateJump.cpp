#include "PlayerStateShared.h"
#include "AudioPlayer.h"

void PlayerStateJump::Enter(Player *player) {
  if (!player)
    return;

  // ジャンプSEの再生
  AudioPlayer::GetInstance()->PlaySE(player->GetSEJumpHandle(), false, 1.0f);

  // 剣は不要（地上攻撃状態ではないため）
  SetSwordActive(player, false);

  // 初期化
  bodyObj_ = player;
  headObj_ = nullptr;
  rightArmObj_ = nullptr;
  leftArmObj_ = nullptr;
  rightFootObj_ = nullptr;
  leftFootObj_ = nullptr;
  swordObj_ = nullptr;
  initializedParts_ = false;

  // 退避（回転のみ）
  if (bodyObj_)
    bodyDefaultRot_ = bodyObj_->GetRotation();

  TryFindHead(player, headObj_);
  TryFindArms(player, leftArmObj_, rightArmObj_);
  TryFindFeet(player, leftFootObj_, rightFootObj_);
  TryFindSword(player, swordObj_);

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
  if (swordObj_)
    swordDefaultLocalPos_ = swordObj_->GetTransform()->translate;

  initializedParts_ = true;
  apexReached_ = false;
  blendTimer_ = 0.0f;

  // 頭
  if (bodyObj_) {
    Transform *tf = bodyObj_->GetTransform();
    // Y軸（向き）はジャンプ中に変化した現在のプレイヤー回転を優先して使う
    Vector3 restore = bodyDefaultRot_;
    restore.y = NormalizeAngle(player->GetRotation().y);
    tf->rotate = restore;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    bodyObj_->UpdateWorldMatrix();
  }

  // NOTE: 要求により「頭の角度を回転させない」ため、head の rotate
  // はここで上書きしない。
  // 右腕・左腕・足は指定回転を適用する（視覚的にジャンプポーズ）
  if (rightArmObj_) {
    Transform *tf = rightArmObj_->GetTransform();
    tf->rotate = rightArmJumpRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    rightArmObj_->UpdateLocalMatrix();
    rightArmObj_->UpdateWorldMatrix();
  }
  if (leftArmObj_) {
    Transform *tf = leftArmObj_->GetTransform();
    tf->rotate = leftArmJumpRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftArmObj_->UpdateLocalMatrix();
    leftArmObj_->UpdateWorldMatrix();
  }
  if (rightFootObj_) {
    Transform *tf = rightFootObj_->GetTransform();
    tf->rotate = rightFootJumpRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    rightFootObj_->UpdateLocalMatrix();
    rightFootObj_->UpdateWorldMatrix();
  }
  if (leftFootObj_) {
    Transform *tf = leftFootObj_->GetTransform();
    tf->rotate = leftFootJumpRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftFootObj_->UpdateLocalMatrix();
    leftFootObj_->UpdateWorldMatrix();
  }

  // 剣はローカル位置を保ったまま更新
  if (swordObj_) {
    Transform *tf = swordObj_->GetTransform();
    tf->translate = swordDefaultLocalPos_;
    swordObj_->UpdateLocalMatrix();
    swordObj_->UpdateWorldMatrix();
  }
}

void PlayerStateJump::Update(Player *player) {
  if (!player)
    return;

  // 先に攻撃入力チェック：空中で攻撃されたら落下攻撃へ遷移
  InputManager *im = player->GetInputManager();
  bool attackTriggered = im && (im->IsActionTriggered("Attack") ||
                                im->IsKeyTriggered(DIK_K) ||
                                im->IsMouseButtonTriggered(0));

  if (attackTriggered && !player->IsGrounded()) {
    if (apexReached_) {
      // 頂点に到達している場合のみ落下攻撃に遷移
      player->ChangeState(std::make_unique<PlayerStatePlungeAttack>());
      return;
    }
    // 頂点に達していない場合は入力を無視（または将来的にバッファ化の追加可）
  }

  // フレーム固定ステップ（既存の他の Update と同様の扱い）
  const float dt = 1.0f / 60.0f;

  // 頂点検出：上昇中から下降に転じた瞬間（velocity.y <= 0）
  if (!apexReached_) {
    Vector3 vel = player->GetVelocity();
    if (vel.y <= 0.0f) {
      // 頂点到達: ブレンド開始
      apexReached_ = true;
      blendTimer_ = 0.0f;

      // 変更: 開始角度は Transform 側ではなく Player が管理する現在の向きを使う
      //     これにより入力で回転が変わっていた場合でも瞬時に反映される
      bodyJumpRot_.y = NormalizeAngle(player->GetRotation().y);
    } else {
      // 上昇中はジャンプポーズを維持しつつ、Yだけはプレイヤーの現在向きに追従させる。
      // これにより空中で向きを変えたときに見た目がスナップしない。
      if (bodyObj_) {
        Transform *tf = bodyObj_->GetTransform();
        tf->rotate.y = NormalizeAngle(player->GetRotation().y);
        tf->quaternion = Math::EulerToQuaternion(tf->rotate);
        tf->isQuaternionMaster = true;
        bodyObj_->UpdateWorldMatrix();
      }
      // まだ上昇中なのでブレンドは行わない
      return;
    }
  }

  // 頂点後：ブレンド中
  blendTimer_ += dt;
  float t = (blendDuration_ > 1e-6f)
                ? std::clamp(blendTimer_ / blendDuration_, 0.0f, 1.0f)
                : 1.0f;
  // 既存のイージングを利用
  float eased = EaseInOutSine(t);

  // ブレンド: jumpPose -> 現在のプレイヤー向き（X/Z は既存補間）
  if (bodyObj_) {
    Vector3 target = bodyDefaultRot_;

    Vector3 cur;
    cur.x = bodyJumpRot_.x + (target.x - bodyJumpRot_.x) * eased;
    cur.z = bodyJumpRot_.z + (target.z - bodyJumpRot_.z) * eased;

    Transform *tf = bodyObj_->GetTransform();
    float startY = NormalizeAngle(tf->rotate.y); // 表示中の角度を開始値に
    float targetY =
        NormalizeAngle(player->GetRotation().y); // プレイヤーが管理する目標角度
    cur.y = LerpAngle(startY, targetY, eased);

    tf->rotate = cur;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    bodyObj_->UpdateWorldMatrix();
  }

  // NOTE: 要求により、頂点後の落下中も「頭は回転させない」。したがって head
  // の補間は行わない。

  if (rightArmObj_) {
    Vector3 target = rightArmDefaultRot_;
    Vector3 cur = LerpVec(rightArmJumpRot_, target, eased);
    Transform *tf = rightArmObj_->GetTransform();
    tf->rotate = cur;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    rightArmObj_->UpdateLocalMatrix();
    rightArmObj_->UpdateWorldMatrix();
  }
  if (leftArmObj_) {
    Vector3 target = leftArmDefaultRot_;
    Vector3 cur = LerpVec(leftArmJumpRot_, target, eased);
    Transform *tf = leftArmObj_->GetTransform();
    tf->rotate = cur;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftArmObj_->UpdateLocalMatrix();
    leftArmObj_->UpdateWorldMatrix();
  }
  if (rightFootObj_) {
    Vector3 target = rightFootDefaultRot_;
    Vector3 cur = LerpVec(rightFootJumpRot_, target, eased);
    Transform *tf = rightFootObj_->GetTransform();
    tf->rotate = cur;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    rightFootObj_->UpdateLocalMatrix();
    rightFootObj_->UpdateWorldMatrix();
  }
  if (leftFootObj_) {
    Vector3 target = leftFootDefaultRot_;
    Vector3 cur = LerpVec(leftFootJumpRot_, target, eased);
    Transform *tf = leftFootObj_->GetTransform();
    tf->rotate = cur;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftFootObj_->UpdateLocalMatrix();
    leftFootObj_->UpdateWorldMatrix();
  }

  // 剣はローカル位置を維持（必要ならアニメで戻す）
  if (swordObj_) {
    Transform *tf = swordObj_->GetTransform();
    tf->translate = swordDefaultLocalPos_;
    swordObj_->UpdateLocalMatrix();
    swordObj_->UpdateWorldMatrix();
  }

  // 着地判定：地面に接地したら Idle に戻す（Exit でデフォルトを復元）
  if (player->IsGrounded()) {
    player->ChangeState(std::make_unique<PlayerStateIdle>());
    return;
  }
}

void PlayerStateJump::Exit(Player *player) {
  // 保存してあるデフォルトへ復帰
  if (!initializedParts_)
    return;

  if (bodyObj_) {
    Transform *tf = bodyObj_->GetTransform();
    // X/Z は保存値に戻すが、Y は現在のプレイヤー向きを優先する
    Vector3 restore = bodyDefaultRot_;
    restore.y = NormalizeAngle(player ? player->GetRotation().y : restore.y);
    tf->rotate = restore;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    bodyObj_->UpdateWorldMatrix();
  }
  if (headObj_) {
    // head は Enter/Update で変更していないが、念のためデフォルトを復帰
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
    rightArmObj_->UpdateLocalMatrix();
    rightArmObj_->UpdateWorldMatrix();
  }
  if (leftArmObj_) {
    Transform *tf = leftArmObj_->GetTransform();
    tf->rotate = leftArmDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftArmObj_->UpdateLocalMatrix();
    leftArmObj_->UpdateWorldMatrix();
  }
  if (rightFootObj_) {
    Transform *tf = rightFootObj_->GetTransform();
    tf->rotate = rightFootDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    rightFootObj_->UpdateLocalMatrix();
    rightFootObj_->UpdateWorldMatrix();
  }
  if (leftFootObj_) {
    Transform *tf = leftFootObj_->GetTransform();
    tf->rotate = leftFootDefaultRot_;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftFootObj_->UpdateLocalMatrix();
    leftFootObj_->UpdateWorldMatrix();
  }
  if (swordObj_) {
    Transform *tf = swordObj_->GetTransform();
    tf->translate = swordDefaultLocalPos_;
    swordObj_->UpdateLocalMatrix();
    swordObj_->UpdateWorldMatrix();
  }
}

// ========================================================
// 被弾・吹っ飛び状態 (Damage / Knockback) 実装
