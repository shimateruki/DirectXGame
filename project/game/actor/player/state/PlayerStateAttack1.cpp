#include "PlayerStateShared.h"
#include "AudioPlayer.h"

void PlayerStateAttack1::Enter(Player* player)
{
	DebugConsole::GetInstance()->AddLog("★ ENTER: Attack1 State");

	if (player) {
		// 攻撃1段目SEの再生
		AudioPlayer::GetInstance()->PlaySE(player->GetSESwingMiss1Handle(), false, 1.0f);

		player->SetIsControlActive(false);
		
		// タイクラーさんの変更: 攻撃方向の設定
		const Matrix4x4& mat = player->GetWorldMatrix();
		Vector3 dir = { mat.m[2][0], 0.0f, mat.m[2][2] };
		if (Math::Length(dir) > 0.001f) dir = Math::Normalize(dir);
		else dir = { 0,0,1 };
		player->SetAttackDirection(dir);
	}
	SetSwordActive(player, true, player->GetAttackParams().damageCombo1);
	animTimer_ = 0.0f;
	
	// チームメンバーの変更: エフェクト名の変更とパーティクル生成
	MeshEffectManager::GetInstance()->SpawnEffect("Resources/json/effect/effect_attak1.json", player->GetAttackParams().damageCombo1);

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

	TryFindHead(player, headObj_);
	TryFindArms(player, leftArmObj_, rightArmObj_);
	TryFindFeet(player, leftFootObj_, rightFootObj_);

	initializedParts_ = false;

	if (bodyObj_) {
		bodyDefaultPos_ = bodyObj_->GetTransform()->translate;
		bodyDefaultRot_ = bodyObj_->GetRotation();
	}

	if (headObj_) {
		Transform* htf = headObj_->GetTransform();
		headDefaultPos_ = htf->translate;
		headDefaultRot_ = headObj_->GetRotation();
		headStartRot_ = htf->rotate;
	}

	if (rightArmObj_) {
		rightArmDefaultPos_ = rightArmObj_->GetTransform()->translate;
		rightArmDefaultRot_ = rightArmObj_->GetRotation();
	}

	if (leftArmObj_) {
		leftArmDefaultPos_ = leftArmObj_->GetTransform()->translate;
		leftArmDefaultRot_ = leftArmObj_->GetRotation();
	}

	if (rightFootObj_) {
		rightFootDefaultPos_ = rightFootObj_->GetTransform()->translate;
		rightFootDefaultRot_ = rightFootObj_->GetRotation();
	}

	if (leftFootObj_) {
		leftFootDefaultPos_ = leftFootObj_->GetTransform()->translate;
		leftFootDefaultRot_ = leftFootObj_->GetRotation();
	}

	initializedParts_ = true;

	if (bodyObj_)
	{
		auto DegToRad = [](float d) { return d * 3.14159265358979323846f / 180.0f; };
		Transform* btf = bodyObj_->GetTransform();

		// 攻撃ポーズの Y 回転を「現在の向き（bodyDefaultRot_.y）を基準にした相対角度」にする
		// これにより、どの方向を向いていても向いた方向で攻撃ポーズが出る
		float baseY = bodyDefaultRot_.y;
		float startOffset = DegToRad(60.0f);     // 開始時に体を向けるオフセット
		float targetOffset = DegToRad(-100.0f);  // 最終的な回転オフセット

		btf->rotate.y = baseY + startOffset;
		btf->quaternion = Math::EulerToQuaternion(btf->rotate);
		btf->isQuaternionMaster = true;
		bodyObj_->UpdateWorldMatrix();

		s_bodyStartY = btf->rotate.y;
		s_bodyTargetY = baseY + targetOffset;
		s_bodyBlendActive = false;
	}

	// 最初のポーズ適用
	ApplyPose(0.0f);
}

void PlayerStateAttack1::Update(Player* player)
{
	if (!player) return;

	// フレーム固定で更新している既存スタイルに合わせる（1/60）
	animTimer_ += 1.0f / 60.0f;

	// チームメンバーの変更: パーティクルの更新
	if (particleEmitter_) {
		particleEmitter_->Update(1.0f / 60.0f);
	}

	// バッファ消費チェック
	if (player && player->ConsumeBufferedAttackInput())
	{
		player->SetPendingAttack2(true);
	}

	// 攻撃中にクリックまたは K で 2 段目を予約できるようにする
	if (player)
	{
		InputManager* im = player->GetInputManager();
		if (im && (im && im->IsActionTriggered("Attack")))
		{
			player->SetPendingAttack2(true);
		}
	}

	// 通常アニメーション部分
	float t = std::clamp(animTimer_ / animDuration_, 0.0f, 1.0f);

	// イーズインアウト
	float et = EaseInOutSine(t);
	ApplyPose(et);

	if (animTimer_ >= animDuration_)
	{
		// Attack1 のアニメが終わったとき、Pending フラグ（またはコンボ時間窓）があれば
		// 一度 Idle に戻さず直接 Attack2 に遷移するようにする。
		bool goAttack2 = false;
		if (player)
		{
			// まずコンボ時間窓が有効ならそのまま 2 段目へ
			if (player->IsComboWindowActive())
			{
				goAttack2 = true;
			}
			else
			{
				// pending フラグが立っていれば消費して true (直接遷移)
				if (player->ConsumePendingAttack2())
				{
					goAttack2 = true;
				}
			}
		}

		if (goAttack2)
		{
			// Attack1 から直接 Attack2 に遷移する場合でも
			// 「元の待機デフォルト」を保持するために Pending を作成しておく
			if (initializedParts_)
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

			player->ChangeState(std::make_unique<PlayerStateAttack2>());
		}
		else
		{
			// 予約が無ければ従来通り Idle に戻す
			// Idle に戻す直前に Pending 補間データを作成する（Exit ではなく遷移決定局所で作る）
			if (initializedParts_)
			{
				s_pendingIdleBlend.active = true;
				s_pendingIdleBlend.blendDuration = 0.35f;

				if (bodyObj_) {
					s_pendingIdleBlend.body = true;
					// 表示中のワールド回転を開始値として使う
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

			player->ChangeState(std::make_unique<PlayerStateIdle>());
		}
		return;
	}
}

void PlayerStateAttack1::Exit(Player* player)
{
	DebugConsole::GetInstance()->AddLog("★ EXIT: Attack1 State");

	if (player) player->SetIsControlActive(true);
	SetSwordActive(player, false);

	// 既に初期化されていなければ何もしない
	if (!initializedParts_) return;

	// チームメンバーの変更: パーティクルの停止と破棄
	if (particleEmitter_) {
		particleEmitter_->Stop();
		particleEmitter_.reset();
	}

	// ここでは補間の Pending を作らない。Idle に遷移する直前（Update 内）で限定的に作るようにした。
}

void PlayerStateAttack1::ApplyPose(float t)
{
	if (!initializedParts_) return;

	auto DegToRad = [](float d) { return d * 3.14159265358979323846f / 180.0f; };
	auto LerpVec3 = [](const Vector3& a, const Vector3& b, float t) {
		return Vector3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
	};

	// 開始ポーズ
	Vector3 bodyStartPos{ 0.0f, 0.0f, 0.0f };
	Vector3 bodyStartRot = bodyDefaultRot_;
	// Y 回転をデフォルト向きに対する相対値で設定
	bodyStartRot.y = bodyDefaultRot_.y + DegToRad(60.0f);

	// 頭:  開始はデフォルトの位置
	Vector3 headStartPos{ 0.0f, 0.0f, 0.0f };
	Vector3 headStartRot{ 0.0f, 0.0f, 0.0f };

	// 右手: Y を前に出して体から離す
	Vector3 rtArmStartPos{ 0.0f, 0.0f, 0.0f };
	Vector3 rtArmStartRot{ DegToRad(-70.0f), DegToRad(32.0f), DegToRad(-23.0f) };

	// 左手: Z を大きくして体に埋まらないように調整
	Vector3 ltArmStartPos{ 0.0f, 0.0f, 1.0f };
	Vector3 ltArmStartRot{ DegToRad(-190.0f), DegToRad(45.0f), DegToRad(-2.0f) };

	// 右足: 開始はデフォルトの位置
	Vector3 rtFootStartPos{ 0.0f, 0.0f, 0.0f };
	Vector3 rtFootStartRot{ 0.0f, 0.0f, 0.0f };

	// 左足: Y を持ち上げて埋まりを防止
	Vector3 ltFootStartPos{ 0.0f, 1.0f, 0.4f };
	Vector3 ltFootStartRot{ DegToRad(-72.0f), 0.0f, 0.0f };

	// 終了ポーズ
	Vector3 bodyEndPos{ 0.0f, 0.0f, 0.0f };
	// 体の Y を -100deg にする（要求どおり）
	Vector3 bodyEndRot = bodyStartRot;
	// End もデフォルト向きを基準にした相対角度で設定
	bodyEndRot.y = bodyDefaultRot_.y + DegToRad(-100.0f);
	bodyEndRot.z = DegToRad(-36.0f);

	Vector3 headEndPos{ 0.0f, 0.0f, 0.0f };
	Vector3 headEndRot{ DegToRad(-22.0f), DegToRad(61.0f), 0.0f };

	Vector3 rtArmEndPos{ 0.0f, 0.0f, 0.0f };
	Vector3 rtArmEndRot{ DegToRad(-151.0f), DegToRad(-70.0f), DegToRad(57.0f) };

	// 左手終了位置もZを高めに
	Vector3 ltArmEndPos{ 0.0f, 0.0f, 1.2f };
	Vector3 ltArmEndRot{ DegToRad(43.0f), DegToRad(3.0f), DegToRad(-10.0f) };

	Vector3 rtFootEndPos{ 0.0f, 0.0f, 0.0f };
	Vector3 rtFootEndRot{ DegToRad(43.0f), DegToRad(3.0f), DegToRad(-10.0f) };

	// 左足終了位置
	Vector3 ltFootEndPos{ 0.1f, 0.9f, -0.1f };
	Vector3 ltFootEndRot{ DegToRad(57.0f), DegToRad(81.0f), DegToRad(-6.0f) };

	// 体は位置と回転を両方アニメーションさせる
	if (bodyObj_) {
		Transform* tf = bodyObj_->GetTransform();
		tf->translate = bodyDefaultPos_ + LerpVec3(bodyStartPos, bodyEndPos, t);
		tf->rotate = LerpVec3(bodyStartRot, bodyEndRot, t);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		bodyObj_->UpdateWorldMatrix();
	}

	if (headObj_) {
		Transform* tf = headObj_->GetTransform();
		tf->translate = headDefaultPos_ + LerpVec3(Vector3{ 0,0,0 }, headEndPos, t);
		tf->rotate = LerpVec3(headStartRot_, headEndRot, t);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		headObj_->UpdateWorldMatrix();
	}

	if (rightArmObj_) {
		Transform* tf = rightArmObj_->GetTransform();
		tf->translate = rightArmDefaultPos_ + LerpVec3(rtArmStartPos, rtArmEndPos, t);
		tf->rotate = LerpVec3(rtArmStartRot, rtArmEndRot, t);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		rightArmObj_->UpdateWorldMatrix();
	}

	if (leftArmObj_) {
		Transform* tf = leftArmObj_->GetTransform();
		// 位置アニメーションを無効化: 常にデフォルトのローカル位置を使う
		tf->translate = leftArmDefaultPos_;
		tf->rotate = LerpVec3(ltArmStartRot, ltArmEndRot, t);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		leftArmObj_->UpdateLocalMatrix();
		leftArmObj_->UpdateWorldMatrix();
	}

	if (rightFootObj_) {
		Transform* tf = rightFootObj_->GetTransform();
		tf->translate = rightFootDefaultPos_ + LerpVec3(rtFootStartPos, rtFootEndPos, t);
		tf->rotate = LerpVec3(rtFootStartRot, rtFootEndRot, t);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		rightFootObj_->UpdateWorldMatrix();
	}

	if (leftFootObj_) {
		Transform* tf = leftFootObj_->GetTransform();
		// 位置アニメーションを無効化: 常にデフォルトのローカル位置を使う
		tf->translate = leftFootDefaultPos_;
		tf->rotate = LerpVec3(ltFootStartRot, ltFootEndRot, t);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		leftFootObj_->UpdateLocalMatrix();
		leftFootObj_->UpdateWorldMatrix();
	}
}