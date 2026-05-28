#include "PlayerStateShared.h"
#include "AudioPlayer.h"

void PlayerStateAttack2::Enter(Player* player)
{
	if (!player) return;

	// 攻撃2段目SEの再生 (1回目)
	AudioPlayer::GetInstance()->PlaySE(player->GetSESwingMiss1Handle(), false, 2.0f);
	hasPlayedSecondSE_ = false;

	DebugConsole::GetInstance()->AddLog("★ ENTER: Attack2 State");

	if (player) {
		player->SetIsControlActive(false);
		
		// 攻撃開始時の正面方向を保存する。
		const Matrix4x4& mat = player->GetWorldMatrix();
		Vector3 dir = { mat.m[2][0], 0.0f, mat.m[2][2] };
		if (Math::Length(dir) > 0.001f) dir = Math::Normalize(dir);
		else dir = { 0,0,1 };
		player->SetAttackDirection(dir);
	}
	SetSwordActive(player, true, player->GetAttackParams().damageCombo2);
	animTimer_ = 0.0f;
	
	// コンボ2用の斬撃エフェクトを発生させる。
	MeshEffectManager::GetInstance()->SpawnEffect("Resources/json/effect/effect_attak2.json", player->GetAttackParams().damageCombo2);

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

	// パーツ探索
	TryFindHead(player, headObj_);
	TryFindArms(player, leftArmObj_, rightArmObj_);
	TryFindFeet(player, leftFootObj_, rightFootObj_);

	initializedParts_ = false;

	// 退避（現在のデフォルトを保存）
	if (bodyObj_) { bodyDefaultPos_ = bodyObj_->GetTransform()->translate; bodyDefaultRot_ = bodyObj_->GetRotation(); }

	if (headObj_) {
		Transform* htf = headObj_->GetTransform();
		headDefaultPos_ = htf->translate;
		headDefaultRot_ = headObj_->GetRotation();
		headStartRot_ = htf->rotate;
	}

	if (rightArmObj_) { rightArmDefaultPos_ = rightArmObj_->GetTransform()->translate; rightArmDefaultRot_ = rightArmObj_->GetRotation(); }

	if (leftArmObj_) { leftArmDefaultPos_ = leftArmObj_->GetTransform()->translate; leftArmDefaultRot_ = leftArmObj_->GetRotation(); }

	if (rightFootObj_) { rightFootDefaultPos_ = rightFootObj_->GetTransform()->translate; rightFootDefaultRot_ = rightFootObj_->GetRotation(); }

	if (leftFootObj_) { leftFootDefaultPos_ = leftFootObj_->GetTransform()->translate; leftFootDefaultRot_ = leftFootObj_->GetRotation(); }

	initializedParts_ = true;

	// 指定された「開始ポーズ」を適用する。
	auto DegToRad = [](float d) { return d * 3.14159265358979323846f / 180.0f; };

	// body start: Attack1 終点と同等（相対値）
	if (bodyObj_)
	{
		Transform* tf = bodyObj_->GetTransform();
		// bodyDefaultRot_ は Enter 時点で保存したデフォルト向き。
		Vector3 startRot = bodyDefaultRot_;
		startRot.y = bodyDefaultRot_.y + DegToRad(-100.0f);
		startRot.z = DegToRad(-36.0f);
		tf->rotate = startRot;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		bodyObj_->UpdateWorldMatrix();
	}

	// head start: Attack1 終点
	if (headObj_)
	{
		Transform* tf = headObj_->GetTransform();
		Vector3 startRot = Vector3{ DegToRad(-22.0f), DegToRad(61.0f), 0.0f };
		tf->rotate = startRot;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		headObj_->UpdateWorldMatrix();
	}

	// rightArm: 指定された開始位置/回転。ただし要求により回転.x を 0.0 にしてからアニメ開始する
	if (rightArmObj_)
	{
		Transform* tf = rightArmObj_->GetTransform();
		// 指定開始位置: x=0.2, y=-0.3, z=0.0
		tf->translate = Vector3{ 0.2f, -0.3f, 0.0f };
		// 指定開始回転: (-15, -55, 57) deg -> x を 0 にして適用
		Vector3 rotDeg = Vector3{ -15.0f, -55.0f, 57.0f };
		Vector3 rotRad = Vector3{ 0.0f, DegToRad(rotDeg.y), DegToRad(rotDeg.z) }; // x = 0 as requested
		tf->rotate = rotRad;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		rightArmObj_->UpdateLocalMatrix();
		rightArmObj_->UpdateWorldMatrix();
	}

	// leftArm start: Attack1 終点（位置は少し移動させるが Attack1 終点を基準に）
	if (leftArmObj_)
	{
		Transform* tf = leftArmObj_->GetTransform();
		// 位置アニメーションを無効化: 常に保存しているデフォルトのローカル位置を使う
		tf->translate = leftArmDefaultPos_;
		// 回転も一旦アニメーションしない（デフォルト回転を維持）
		tf->rotate = leftArmDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		leftArmObj_->UpdateLocalMatrix();
		leftArmObj_->UpdateWorldMatrix();
	}

	// rightFoot start: Attack1 終点 (rtFootEndRot: 43,3,-10)
	if (rightFootObj_)
	{
		Transform* tf = rightFootObj_->GetTransform();
		tf->translate = rightFootDefaultPos_;
		tf->rotate = Vector3{ DegToRad(43.0f), DegToRad(3.0f), DegToRad(-10.0f) };
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		rightFootObj_->UpdateWorldMatrix();
	}

	// leftFoot start: Attack1 終点
	if (leftFootObj_)
	{
		Transform* tf = leftFootObj_->GetTransform();
		tf->translate = leftFootDefaultPos_;
		tf->rotate = Vector3{ DegToRad(57.0f), DegToRad(81.0f), DegToRad(-6.0f) };
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		leftFootObj_->UpdateLocalMatrix();
		leftFootObj_->UpdateWorldMatrix();
	}

	// ApplyPose の t=0 で開始ポーズを確実に設定
	ApplyPose(0.0f);
}

void PlayerStateAttack2::Update(Player* player)
{
	if (!player) return;

	// 入力で3段目予約
	InputManager* im = player->GetInputManager();
	if ((im && im->IsActionTriggered("Attack"))) {
		player->SetPendingAttack2(true);
		DebugConsole::GetInstance()->AddLog("Attack2: input detected -> pending Attack3 set");
	}

	// 固定フレームでタイマー更新（既存スタイル）
	animTimer_ += 1.0f / 60.0f;

	// 2回目のSE再生（回転しているように聞こえるよう時間差で）
	if (animTimer_ > 0.15f && !hasPlayedSecondSE_) {
		AudioPlayer::GetInstance()->PlaySE(player->GetSESwingMiss1Handle(), false, 2.0f);
		hasPlayedSecondSE_ = true;
	}

	// チームメンバーの変更: パーティクルの更新
	if (particleEmitter_) {
		particleEmitter_->Update(1.0f / 60.0f);
	}

	float t = std::clamp(animTimer_ / animDuration_, 0.0f, 1.0f);
	float et = EaseInOutSine(t);
	ApplyPose(et);

	// 到達判定
	if (animTimer_ >= animDuration_)
	{
		DebugConsole::GetInstance()->AddLog("Attack2: anim finished (animTimer_=" + std::to_string(animTimer_) + ", animDuration_=" + std::to_string(animDuration_) + ")");

		// 予約があればAttack3へ
		if (player->ConsumePendingAttack2()) {
			DebugConsole::GetInstance()->AddLog("Attack2: ConsumePendingAttack2() == true -> Change to Attack3");
			player->ChangeState(std::make_unique<PlayerStateAttack3>());
		}
		else {
			DebugConsole::GetInstance()->AddLog("Attack2: no pending -> prepare s_pendingIdleBlend and Change to Idle");
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
				else
				{
					// 既に pending が存在する（Attack1 が設定済み）場合は
					// target を上書きせず start 値だけ最新の表示状態に更新する
					if (bodyObj_) s_pendingIdleBlend.bodyStart = player->GetRotation();
					if (headObj_) s_pendingIdleBlend.headStart = headObj_->GetTransform()->rotate;
					if (rightArmObj_) s_pendingIdleBlend.rightArmStart = rightArmObj_->GetTransform()->rotate;
					if (leftArmObj_) s_pendingIdleBlend.leftArmStart = leftArmObj_->GetTransform()->rotate;
					if (rightFootObj_) s_pendingIdleBlend.rightFootStart = rightFootObj_->GetTransform()->rotate;
					if (leftFootObj_) s_pendingIdleBlend.leftFootStart = leftFootObj_->GetTransform()->rotate;
				}
			}
			player->ChangeState(std::make_unique<PlayerStateIdle>());
		}
		return;
	}
}

void PlayerStateAttack2::Exit(Player* player)
{
	DebugConsole::GetInstance()->AddLog("★ EXIT: Attack2 State");

	if (player) player->SetIsControlActive(true);
	SetSwordActive(player, false);
	if (!initializedParts_) return;

	// チームメンバーの変更: パーティクルの停止と破棄
	if (particleEmitter_) {
		particleEmitter_->Stop();
		particleEmitter_.reset();
	}

	// 補間は Exit では作らない（Idle へ遷移する直前の Update で作成する）。
	// 戻す（保存したデフォルトに復帰）
	if (bodyObj_) {
		Transform* tf = bodyObj_->GetTransform();
		tf->translate = bodyDefaultPos_;
		tf->rotate = bodyDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true; bodyObj_->UpdateWorldMatrix();
	}

	if (headObj_) {
		Transform* tf = headObj_->GetTransform();
		tf->translate = headDefaultPos_;
		tf->rotate = headDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		headObj_->UpdateWorldMatrix();
	}

	if (rightArmObj_) {
		Transform* tf = rightArmObj_->GetTransform();
		tf->translate = rightArmDefaultPos_;
		tf->rotate = rightArmDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		rightArmObj_->UpdateLocalMatrix();
		rightArmObj_->UpdateWorldMatrix();
	}

	if (leftArmObj_) {
		Transform* tf = leftArmObj_->GetTransform();
		tf->translate = leftArmDefaultPos_;
		tf->rotate = leftArmDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		leftArmObj_->UpdateLocalMatrix();
		leftArmObj_->UpdateWorldMatrix();
	}


	if (rightFootObj_) {
		Transform* tf = rightFootObj_->GetTransform();
		tf->translate = rightFootDefaultPos_;
		tf->rotate = rightFootDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		rightFootObj_->UpdateWorldMatrix();
	}

	if (leftFootObj_) {
		Transform* tf = leftFootObj_->GetTransform();
		tf->translate = leftFootDefaultPos_;
		tf->rotate = leftFootDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		leftFootObj_->UpdateLocalMatrix();
		leftFootObj_->UpdateWorldMatrix();
	}
}

void PlayerStateAttack2::ApplyPose(float t)
{
	if (!initializedParts_) return;

	auto DegToRad = [](float d) { return d * 3.14159265358979323846f / 180.0f; };
	auto LerpVec3 = [](const Vector3& a, const Vector3& b, float t) {
		return Vector3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
		};

	// --- 開始ポーズ ---
	// Body start: bodyDefaultRot_.y + (-100deg), z = -36deg (Enterで設定)
	Vector3 bodyStartPos{ 0.0f, 0.0f, 0.0f };
	Vector3 bodyStartRot = bodyDefaultRot_;
	// Y 回転をプレイヤーの向き（bodyDefaultRot_.y）に対する相対値で指定して、向いている方向で攻撃が出るようにする
	float baseY = bodyDefaultRot_.y;
	bodyStartRot.y = baseY + DegToRad(60.0f);

	// Head start: Enterで設定済み
	Vector3 headStartPos{ 0.0f, 0.0f, 0.0f };
	Vector3 headStartRot = headObj_ ? headObj_->GetTransform()->rotate : Vector3{ 0,0,0 };

	// Right arm start: Enterで設定済み
	Vector3 rtArmStartPos = rightArmObj_ ? rightArmObj_->GetTransform()->translate : Vector3{ 0,0,0 };
	Vector3 rtArmStartRot = rightArmObj_ ? rightArmObj_->GetTransform()->rotate : Vector3{ 0,0,0 };

	// Left arm start: Enter で設定した Attack1 終点相当
	Vector3 ltArmStartPos = leftArmObj_ ? leftArmObj_->GetTransform()->translate : Vector3{ 0,0,0 };
	Vector3 ltArmStartRot = leftArmObj_ ? leftArmObj_->GetTransform()->rotate : Vector3{ 0,0,0 };

	// Right foot start
	Vector3 rtFootStartPos = rightFootObj_ ? rightFootObj_->GetTransform()->translate : Vector3{ 0,0,0 };
	Vector3 rtFootStartRot = rightFootObj_ ? rightFootObj_->GetTransform()->rotate : Vector3{ 0,0,0 };

	// Left foot start
	Vector3 ltFootStartPos = leftFootObj_ ? leftFootObj_->GetTransform()->translate : Vector3{ 0,0,0 };
	Vector3 ltFootStartRot = leftFootObj_ ? leftFootObj_->GetTransform()->rotate : Vector3{ 0,0,0 };

	// --- 終了ポーズ ---
	// Body end
	// 変更: Y 回転をプレイヤーの向き（bodyDefaultRot_.y）に対する相対値で指定して、向いている方向で攻撃が出るようにする
	Vector3 bodyEndPos{ 0.0f, 0.0f, 0.0f };
	Vector3 bodyEndRot{ DegToRad(0.0f), DegToRad(0.0f), DegToRad(40.0f) };
	if (bodyObj_)
	{
		// bodyDefaultRot_.y を基準に 420deg 回す（相対指定）
		bodyEndRot.y = bodyDefaultRot_.y + DegToRad(540.0f);
	}
	else
	{
		bodyEndRot.y = DegToRad(540.0f);
	}

	// Head end
	Vector3 headEndPos{ 0.0f, 0.0f, 0.0f };
	Vector3 headEndRot{ DegToRad(-20.0f), DegToRad(-57.0f), DegToRad(-11.0f) };

	// Right arm end
	Vector3 rtArmEndPos{ 0.2f, -0.3f, 0.0f };
	Vector3 rtArmEndRot{ DegToRad(0.0f), DegToRad(40.0f), DegToRad(57.0f) };

	// Left arm end
	Vector3 ltArmEndPos{ DegToRad(-0.1f), DegToRad(-0.2f), 0.0f };
	ltArmEndPos = Vector3{ -0.1f, -0.2f, 0.0f };
	Vector3 ltArmEndRot{ DegToRad(0.0f), DegToRad(25.0f), DegToRad(-35.0f) };

	// Left foot end
	Vector3 ltFootEndPos{ 0.0f, 0.2f, -0.1f };
	Vector3 ltFootEndRot{ DegToRad(-20.0f), DegToRad(-52.0f), DegToRad(-1.0f) };

	// Right foot end
	Vector3 rtFootEndPos{ 0.0f, 0.0f, 0.0f };
	Vector3 rtFootEndRot{ DegToRad(0.0f), DegToRad(0.0f), DegToRad(0.0f) };

	// --- 補間適用 ---
	// Body
	if (bodyObj_)
	{
		Transform* tf = bodyObj_->GetTransform();
		tf->translate = bodyDefaultPos_ + LerpVec3(bodyStartPos, bodyEndPos, t);
		tf->rotate = LerpVec3(bodyStartRot, bodyEndRot, t);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		bodyObj_->UpdateWorldMatrix();
	}

	// Head
	if (headObj_)
	{
		Transform* tf = headObj_->GetTransform();
		tf->translate = headDefaultPos_ + LerpVec3(headStartPos, headEndPos, t);
		tf->rotate = LerpVec3(headStartRot, headEndRot, t);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		headObj_->UpdateWorldMatrix();
	}

	// Right arm
	if (rightArmObj_)
	{
		Transform* tf = rightArmObj_->GetTransform();
		tf->translate = LerpVec3(rtArmStartPos, rtArmEndPos, t);
		Vector3 rot = LerpVec3(rtArmStartRot, rtArmEndRot, t);
		tf->rotate = rot;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		rightArmObj_->UpdateLocalMatrix();
		rightArmObj_->UpdateWorldMatrix();
	}

	// Left arm
	if (leftArmObj_)
	{
		Transform* tf = leftArmObj_->GetTransform();
		tf->translate = LerpVec3(ltArmStartPos, ltArmEndPos, t);
		tf->rotate = LerpVec3(ltArmStartRot, ltArmEndRot, t);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		leftArmObj_->UpdateLocalMatrix();
		leftArmObj_->UpdateWorldMatrix();
	}

	// Right foot
	if (rightFootObj_)
	{
		Transform* tf = rightFootObj_->GetTransform();
		tf->translate = LerpVec3(rtFootStartPos, rtFootEndPos, t);
		tf->rotate = LerpVec3(rtFootStartRot, rtFootEndRot, t);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		rightFootObj_->UpdateWorldMatrix();
	}

	// Left foot
	if (leftFootObj_)
	{
		Transform* tf = leftFootObj_->GetTransform();
		tf->translate = LerpVec3(ltFootStartPos, ltFootEndPos, t);
		tf->rotate = LerpVec3(ltFootStartRot, ltFootEndRot, t);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		leftFootObj_->UpdateLocalMatrix();
		leftFootObj_->UpdateWorldMatrix();
	}
}

// ========================================================
// 攻撃3段目状態 (Attack3 - 突き攻撃) 実装
