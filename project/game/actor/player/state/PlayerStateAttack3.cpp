#include "PlayerStateShared.h"

void PlayerStateAttack3::Enter(Player* player)
{
	if (!player) return;
	DebugConsole::GetInstance()->AddLog("★ ENTER: Attack3 State (Thrust!)");

	if (player) {
		player->SetIsControlActive(false);

		// 攻撃開始時の正面方向を保存する。
		const Matrix4x4& mat = player->GetWorldMatrix();
		Vector3 dir = { mat.m[2][0], 0.0f, mat.m[2][2] };
		if (Math::Length(dir) > 0.001f) dir = Math::Normalize(dir);
		else dir = { 0,0,1 };
		player->SetAttackDirection(dir);
	}
	SetSwordActive(player, true, player->GetAttackParams().damageCombo3);
	animTimer_ = 0.0f;

	// コンボ3用の突きエフェクトを発生させる。
	MeshEffectManager::GetInstance()->SpawnEffect("Resources/json/effect/effect_attak3.json", player->GetAttackParams().damageCombo3);

	// 剣に追従するパーティクルを再生する。
	Object3d* swordObj = nullptr;
	TryFindSword(player, swordObj);
	if (swordObj) {
		particleEmitter_ = std::make_unique<GPUParticleEmitter>();
		particleEmitter_->Initialize("playerattak", swordObj);
		particleEmitter_->SetOffset({ 1.3f, 0.0f, 0.0f });
		particleEmitter_->SetInterval(0.016f);
		particleEmitter_->Play();
	}

	bodyObj_ = player;

	TryFindHead(player, headObj_); TryFindArms(player, leftArmObj_, rightArmObj_); TryFindFeet(player, leftFootObj_, rightFootObj_);
	initializedParts_ = false;

	// 1. 位置情報と、戻るべき「本当の正面(待機)の角度」を取得
	if (bodyObj_) { bodyDefaultPos_ = bodyObj_->GetTransform()->translate; }
	if (headObj_) { headDefaultPos_ = headObj_->GetTransform()->translate; }
	if (rightArmObj_) { rightArmDefaultPos_ = rightArmObj_->GetTransform()->translate; }
	if (leftArmObj_) { leftArmDefaultPos_ = leftArmObj_->GetTransform()->translate; }
	if (rightFootObj_) { rightFootDefaultPos_ = rightFootObj_->GetTransform()->translate; }
	if (leftFootObj_) { leftFootDefaultPos_ = leftFootObj_->GetTransform()->translate; }

	float frontY = bodyObj_ ? bodyObj_->GetRotation().y : 0.0f;

	if (s_pendingIdleBlend.active) {
		if (bodyObj_) bodyDefaultRot_ = Vector3{ 0.0f, s_pendingIdleBlend.bodyTarget.y, 0.0f };
		if (headObj_) headDefaultRot_ = s_pendingIdleBlend.headTarget;
		if (rightArmObj_) rightArmDefaultRot_ = s_pendingIdleBlend.rightArmTarget;
		if (leftArmObj_) leftArmDefaultRot_ = s_pendingIdleBlend.leftArmTarget;
		if (rightFootObj_) rightFootDefaultRot_ = s_pendingIdleBlend.rightFootTarget;
		if (leftFootObj_) leftFootDefaultRot_ = s_pendingIdleBlend.leftFootTarget;
		frontY = bodyDefaultRot_.y;
	}
	else {
		if (bodyObj_) bodyDefaultRot_ = Vector3{ 0.0f, frontY, 0.0f };
		if (headObj_) headDefaultRot_ = headObj_->GetRotation();
		if (rightArmObj_) rightArmDefaultRot_ = rightArmObj_->GetRotation();
		if (leftArmObj_) leftArmDefaultRot_ = leftArmObj_->GetRotation();
		if (rightFootObj_) rightFootDefaultRot_ = rightFootObj_->GetRotation();
		if (leftFootObj_) leftFootDefaultRot_ = leftFootObj_->GetRotation();
	}

	// 2. 超重要：Attack2をいじらずに瞬間移動を直すため、Attack2の「最後のポーズ」を手動で開始ポーズにする
	auto DegToRad = [](float d) { return d * 3.1415926535f / 180.0f; };
	bodyStartRot_ = { 0.0f, frontY, DegToRad(40.0f) };
	headStartRot_ = { DegToRad(-20.0f), DegToRad(-57.0f), DegToRad(-11.0f) };
	rtArmStartRot_ = { DegToRad(0.0f), DegToRad(40.0f), DegToRad(57.0f) };
	ltArmStartRot_ = { DegToRad(0.0f), DegToRad(25.0f), DegToRad(-35.0f) };
	rtFootStartRot_ = { DegToRad(0.0f), DegToRad(0.0f), DegToRad(0.0f) };
	ltFootStartRot_ = { DegToRad(-20.0f), DegToRad(-52.0f), DegToRad(-1.0f) };

	s_pendingIdleBlend.active = false;

	initializedParts_ = true;
	ApplyPose(0.0f);
}

void PlayerStateAttack3::Update(Player* player)
{
	if (!player) return;
	animTimer_ += 1.0f / 60.0f;

	// チームメンバーの変更: パーティクルの更新
	if (particleEmitter_) {
		particleEmitter_->Update(1.0f / 60.0f);
	}

	float t = std::clamp(animTimer_ / animDuration_, 0.0f, 1.0f);
	ApplyPose(t);

	if (animTimer_ >= animDuration_)
	{
		// Idleに戻る際の滑らかな補間（ブレンド）を設定する
		if (initializedParts_)
		{
			if (!s_pendingIdleBlend.active)
			{
				s_pendingIdleBlend.active = true;
				s_pendingIdleBlend.blendDuration = 0.35f;

				if (bodyObj_) {
					s_pendingIdleBlend.body = true;
					s_pendingIdleBlend.bodyStart = player->GetRotation();
					s_pendingIdleBlend.bodyTarget = bodyDefaultRot_;
				}
				if (headObj_) {
					s_pendingIdleBlend.head = true;
					s_pendingIdleBlend.headStart = headObj_->GetTransform()->rotate;
					s_pendingIdleBlend.headTarget = headDefaultRot_;
				}
				if (rightArmObj_) {
					s_pendingIdleBlend.rightArm = true;
					s_pendingIdleBlend.rightArmStart = rightArmObj_->GetTransform()->rotate;
					s_pendingIdleBlend.rightArmTarget = rightArmDefaultRot_;
				}
				if (leftArmObj_) {
					s_pendingIdleBlend.leftArm = true;
					s_pendingIdleBlend.leftArmStart = leftArmObj_->GetTransform()->rotate;
					s_pendingIdleBlend.leftArmTarget = leftArmDefaultRot_;
				}
				if (rightFootObj_) {
					s_pendingIdleBlend.rightFoot = true;
					s_pendingIdleBlend.rightFootStart = rightFootObj_->GetTransform()->rotate;
					s_pendingIdleBlend.rightFootTarget = rightFootDefaultRot_;
				}
				if (leftFootObj_) {
					s_pendingIdleBlend.leftFoot = true;
					s_pendingIdleBlend.leftFootStart = leftFootObj_->GetTransform()->rotate;
					s_pendingIdleBlend.leftFootTarget = leftFootDefaultRot_;
				}
			}
		}

		player->ChangeState(std::make_unique<PlayerStateIdle>());
		return;
	}
}

void PlayerStateAttack3::Exit(Player* player)
{
	DebugConsole::GetInstance()->AddLog("★ EXIT: Attack3 State");
	if (player) player->SetIsControlActive(true);
	SetSwordActive(player, false);

	if (!initializedParts_) return;

	// チームメンバーの変更: パーティクルの停止と破棄
	if (particleEmitter_) {
		particleEmitter_->Stop();
		particleEmitter_.reset();
	}

	if (bodyObj_) {
		Transform* tf = bodyObj_->GetTransform();
		tf->translate = bodyDefaultPos_;
		bodyObj_->UpdateWorldMatrix();
	}
}
void PlayerStateAttack3::ApplyPose(float t) {
  if (!initializedParts_)
    return;

  auto DegToRad = [](float d) { return d * 3.1415926535f / 180.0f; };
  auto EaseOutExpo = [](float x) {
    return x == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * x);
  };
  auto EaseInOutSine = [](float x) {
    return -(std::cos(3.14159265f * x) - 1.0f) / 2.0f;
  };

  float baseYRad = bodyDefaultRot_.y;

  // =========================================================
  // 腕の回転
  // =========================================================
  Vector3 rtArmRot1 = {DegToRad(-10.0f), DegToRad(50.0f),
                       DegToRad(60.0f)}; // タメ
  Vector3 rtArmRot2 = {DegToRad(-10.0f), DegToRad(-10.0f),
                       DegToRad(30.0f)};   // 突き
  Vector3 rtArmRot3 = rightArmDefaultRot_; // 完全に待機ポーズに戻す

  Vector3 ltArmRot1 = ltArmStartRot_;     // 左腕はそのまま維持
  Vector3 ltArmRot2 = ltArmStartRot_;     // 維持
  Vector3 ltArmRot3 = leftArmDefaultRot_; // 左腕も一緒に待機ポーズに戻す

  // =========================================================
  // 体の動き
  // =========================================================
  Vector3 bodyPos1 = {0.0f, -0.4f, -0.5f}; // タメ
  Vector3 bodyRot1 = {DegToRad(15.0f), baseYRad, 0.0f};

  Vector3 bodyPos2 = {0.0f, -0.3f, 1.5f}; // 突き
  Vector3 bodyRot2 = {DegToRad(25.0f), baseYRad, 0.0f};

  Vector3 bodyPos3 = {0.0f, 0.0f, 0.0f}; // 戻り：元の位置へ
  Vector3 bodyRot3 = bodyDefaultRot_;    // 完全に待機ポーズに戻す

  // =========================================================
  // 足の動き
  // =========================================================
  Vector3 rtFootRot1 = {DegToRad(-10.0f), 0.0f, 0.0f};
  Vector3 ltFootRot1 = {DegToRad(-10.0f), 0.0f, 0.0f};

  Vector3 rtFootRot2 = {DegToRad(-35.0f), 0.0f, 0.0f}; // 踏ん張る
  Vector3 ltFootRot2 = {DegToRad(35.0f), 0.0f, 0.0f};

  Vector3 rtFootRot3 = rightFootDefaultRot_; // 完全に待機ポーズに戻す
  Vector3 ltFootRot3 = leftFootDefaultRot_;  // 完全に待機ポーズに戻す

  // =========================================================
  // 頭の動き
  // =========================================================
  Vector3 headRot1 = {DegToRad(0.0f), 0.0f, 0.0f};
  Vector3 headRot2 = {DegToRad(-10.0f), 0.0f, 0.0f};
  Vector3 headRot3 = headDefaultRot_; // 完全に待機ポーズに戻す

  // =========================================================
  // 補間計算（3段階）
  // =========================================================
  Vector3 curBodyPos, curBodyRot, curRtArmRot, curLtArmRot, curHeadRot;
  Vector3 curRtFootRot, curLtFootRot;

  if (t < 0.2f) {
    // ① タメ
    float nT = t / 0.2f;
    curBodyPos = LerpVec(Vector3{0, 0, 0}, bodyPos1, nT);
    curBodyRot = LerpVec(bodyStartRot_, bodyRot1, nT);
    curRtArmRot = LerpVec(rtArmStartRot_, rtArmRot1, nT);
    curLtArmRot = LerpVec(ltArmStartRot_, ltArmRot1, nT);
    curHeadRot = LerpVec(headStartRot_, headRot1, nT);

    curRtFootRot = LerpVec(Vector3{0, 0, 0}, rtFootRot1, nT);
    curLtFootRot = LerpVec(Vector3{0, 0, 0}, ltFootRot1, nT);
  } else if (t < 0.6f) {
    // ② 突き
    float nT = (t - 0.2f) / 0.4f;
    float easeT = EaseOutExpo(nT);
    curBodyPos = LerpVec(bodyPos1, bodyPos2, easeT);
    curBodyRot = LerpVec(bodyRot1, bodyRot2, easeT);
    curRtArmRot = LerpVec(rtArmRot1, rtArmRot2, easeT);
    curLtArmRot = LerpVec(ltArmRot1, ltArmRot2, easeT);
    curHeadRot = LerpVec(headRot1, headRot2, easeT);

    curRtFootRot = LerpVec(rtFootRot1, rtFootRot2, easeT);
    curLtFootRot = LerpVec(ltFootRot1, ltFootRot2, easeT);
  } else {
    // ③ 待機ポーズへ完全に戻る
    float nT = (t - 0.6f) / 0.4f;
    float easeT = EaseInOutSine(nT);
    curBodyPos = LerpVec(bodyPos2, bodyPos3, easeT);
    curBodyRot = LerpVec(bodyRot2, bodyRot3, easeT);
    curRtArmRot = LerpVec(rtArmRot2, rtArmRot3, easeT);
    curLtArmRot = LerpVec(ltArmRot2, ltArmRot3, easeT);
    curHeadRot = LerpVec(headRot2, headRot3, easeT);

    curRtFootRot = LerpVec(rtFootRot2, rtFootRot3, easeT);
    curLtFootRot = LerpVec(ltFootRot2, ltFootRot3, easeT);
  }

  // =========================================================
  // 反映
  // =========================================================
  if (bodyObj_) {
    Transform *tf = bodyObj_->GetTransform();
    float s = std::sin(baseYRad);
    float c = std::cos(baseYRad);
    tf->translate = bodyDefaultPos_ +
                    Vector3{curBodyPos.x * c + curBodyPos.z * s, curBodyPos.y,
                            -curBodyPos.x * s + curBodyPos.z * c};

    tf->rotate = curBodyRot;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    bodyObj_->UpdateWorldMatrix();
  }

  if (rightArmObj_) {
    Transform *tf = rightArmObj_->GetTransform();
    tf->rotate = curRtArmRot;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    rightArmObj_->UpdateWorldMatrix();
  }

  // 左腕の回転を反映する。
  if (leftArmObj_) {
    Transform *tf = leftArmObj_->GetTransform();
    tf->rotate = curLtArmRot;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    leftArmObj_->UpdateWorldMatrix();
  }

  if (headObj_) {
    Transform *tf = headObj_->GetTransform();
    tf->rotate = curHeadRot;
    tf->quaternion = Math::EulerToQuaternion(tf->rotate);
    tf->isQuaternionMaster = true;
    headObj_->UpdateWorldMatrix();
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