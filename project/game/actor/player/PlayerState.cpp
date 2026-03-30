#include "PlayerState.h"
#include "Player.h"
#include "InputManager.h"
#include "engine/utility/math/Math.h"
#include "DebugConsole.h"
#include "SceneManager.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>

// ========================================================
// ヘルパ: 小文字化
// ========================================================
static std::string ToLower(const std::string& s)
{
	std::string out; out.reserve(s.size());
	for (unsigned char c : s) out.push_back(static_cast<char>(std::tolower(c)));
	return out;
}

// ========================================================
// 再帰検索: 指定ルート配下から左右の足パーツを探す
// ========================================================
static void FindFeetRecursive(Object3d* node, Object3d*& leftOut, Object3d*& rightOut)
{
	if (!node) return;
	const auto& children = node->GetChildren();
	for (Object3d* child : children)
	{
		if (!child) continue;
		std::string name = ToLower(child->GetName());

		bool hasFoot = (name.find("foot") != std::string::npos);
		bool hasRight = (name.find("right") != std::string::npos) || (name.find("_r") != std::string::npos);
		bool hasLeft = (name.find("left") != std::string::npos) || (name.find("_l") != std::string::npos);

		if (hasFoot)
		{
			if (hasRight && !rightOut) rightOut = child;
			else if (hasLeft && !leftOut) leftOut = child;
			else
			{
				if (!leftOut) leftOut = child;
				else if (!rightOut) rightOut = child;
			}
		}
		if (leftOut && rightOut) return;
		FindFeetRecursive(child, leftOut, rightOut);
		if (leftOut && rightOut) return;
	}
}



// ========================================================
// シーン検索（名前ベース）
// ========================================================
static void FindFeetInSceneByName(Player* player, Object3d*& leftOut, Object3d*& rightOut)
{
	if (!SceneManager::GetInstance()) return;
	auto scene = SceneManager::GetInstance()->GetCurrentScene();
	if (!scene) return;

	for (auto& obj : scene->GetObjects())
	{
		if (!obj) continue;
		std::string n = ToLower(obj->GetName());
		if (!leftOut)
		{
			if (n.find("player_leftfoot") != std::string::npos || n.find("leftfoot") != std::string::npos ||
				(n.find("left") != std::string::npos && n.find("foot") != std::string::npos))
			{
				leftOut = obj.get();
			}
		}
		if (!rightOut)
		{
			if (n.find("player_rightfoot") != std::string::npos || n.find("rightfoot") != std::string::npos ||
				(n.find("right") != std::string::npos && n.find("foot") != std::string::npos))
			{
				if (obj.get() != leftOut) rightOut = obj.get();
			}
		}
		if (leftOut && rightOut) return;
	}

	// プレイヤー名プレフィックスを使って探す補助
	if (!player) return;
	std::string playerName = ToLower(player->GetName());
	if (!playerName.empty())
	{
		for (auto& obj : scene->GetObjects())
		{
			if (!obj) continue;
			std::string n = ToLower(obj->GetName());
			if (!leftOut && n.find(playerName) != std::string::npos && n.find("left") != std::string::npos) leftOut = obj.get();
			if (!rightOut && n.find(playerName) != std::string::npos && n.find("right") != std::string::npos) rightOut = obj.get();
			if (leftOut && rightOut) return;
		}
	}
}

static void TryFindFeet(Player* player, Object3d*& leftOut, Object3d*& rightOut)
{
	if (!player) return;
	FindFeetRecursive(player, leftOut, rightOut);
	if (leftOut && rightOut) return;
	FindFeetInSceneByName(player, leftOut, rightOut);
}

// ========================================================
// 腕探索ヘルパー
// ========================================================
static void FindArmsRecursive(Object3d* node, Object3d*& leftArmOut, Object3d*& rightArmOut)
{
	if (!node) return;
	const auto& children = node->GetChildren();
	for (Object3d* child : children)
	{
		if (!child) continue;
		std::string name = ToLower(child->GetName());
		bool hasArm = (name.find("arm") != std::string::npos) || (name.find("upperarm") != std::string::npos) || (name.find("shoulder") != std::string::npos);
		bool isRight = (name.find("right") != std::string::npos) || (name.find("_r") != std::string::npos);
		bool isLeft = (name.find("left") != std::string::npos) || (name.find("_l") != std::string::npos);

		if (hasArm)
		{
			if (isRight && !rightArmOut) rightArmOut = child;
			else if (isLeft && !leftArmOut) leftArmOut = child;
			else
			{
				if (!leftArmOut) leftArmOut = child;
				else if (!rightArmOut) rightArmOut = child;
			}
		}
		if (leftArmOut && rightArmOut) return;
		FindArmsRecursive(child, leftArmOut, rightArmOut);
		if (leftArmOut && rightArmOut) return;
	}
}

static void FindArmsInSceneByName(Player* player, Object3d*& leftArmOut, Object3d*& rightArmOut)
{
	if (!SceneManager::GetInstance()) return;
	auto scene = SceneManager::GetInstance()->GetCurrentScene();
	if (!scene) return;
	for (auto& obj : scene->GetObjects())
	{
		if (!obj) continue;
		std::string n = ToLower(obj->GetName());
		if (!leftArmOut)
		{
			if (n.find("player_leftarm") != std::string::npos || (n.find("left") != std::string::npos && n.find("arm") != std::string::npos)) leftArmOut = obj.get();
		}
		if (!rightArmOut)
		{
			if (n.find("player_rightarm") != std::string::npos || (n.find("right") != std::string::npos && n.find("arm") != std::string::npos))
			{
				if (obj.get() != leftArmOut) rightArmOut = obj.get();
			}
		}
		if (leftArmOut && rightArmOut) return;
	}

	if (!player) return;
	std::string playerName = ToLower(player->GetName());
	if (!playerName.empty())
	{
		for (auto& obj : scene->GetObjects())
		{
			if (!obj) continue;
			std::string n = ToLower(obj->GetName());
			if (!leftArmOut && n.find(playerName) != std::string::npos && n.find("left") != std::string::npos && n.find("arm") != std::string::npos) leftArmOut = obj.get();
			if (!rightArmOut && n.find(playerName) != std::string::npos && n.find("right") != std::string::npos && n.find("arm") != std::string::npos) rightArmOut = obj.get();
			if (leftArmOut && rightArmOut) return;
		}
	}
}

static void TryFindArms(Player* player, Object3d*& leftArmOut, Object3d*& rightArmOut)
{
	if (!player) return;
	FindArmsRecursive(player, leftArmOut, rightArmOut);
	if (leftArmOut && rightArmOut) return;
	FindArmsInSceneByName(player, leftArmOut, rightArmOut);
}

// ========================================================
// 剣・頭探索ヘルパー
// ========================================================
static void FindSwordRecursive(Object3d* node, Object3d*& swordOut)
{
	if (!node) return;
	const auto& children = node->GetChildren();
	for (Object3d* child : children)
	{
		if (!child) continue;
		std::string name = ToLower(child->GetName());
		if (name.find("sword") != std::string::npos || name.find("katana") != std::string::npos || name.find("blade") != std::string::npos)
		{
			swordOut = child;
			return;
		}
		FindSwordRecursive(child, swordOut);
		if (swordOut) return;
	}
}

static void FindSwordInSceneByName(Player* player, Object3d*& swordOut)
{
	if (!SceneManager::GetInstance()) return;
	auto scene = SceneManager::GetInstance()->GetCurrentScene();
	if (!scene) return;
	for (auto& obj : scene->GetObjects())
	{
		if (!obj) continue;
		std::string n = ToLower(obj->GetName());
		if (n.find("sword") != std::string::npos || n.find("katana") != std::string::npos || n.find("blade") != std::string::npos)
		{
			swordOut = obj.get();
			return;
		}
	}
}

static void TryFindSword(Player* player, Object3d*& swordOut)
{
	if (!player) return;
	FindSwordRecursive(player, swordOut);
	if (swordOut) return;
	FindSwordInSceneByName(player, swordOut);
}

static void SetSwordActive(Player* player, bool isActive)
{
	Object3d* swordObj = nullptr;
	// ★最強の探索関数を使って、確実に剣を見つけ出す！
	TryFindSword(player, swordObj);

	if (swordObj) {
		// 見つけたら、ONなら「kPlayerAttack」、OFFなら「0 (無害)」にする
		swordObj->SetCollisionAttribute(isActive ? kPlayerAttack : 0);
	}
}

static void FindHeadRecursive(Object3d* node, Object3d*& headOut)
{
	if (!node) return;
	const auto& children = node->GetChildren();
	for (Object3d* child : children)
	{
		if (!child) continue;
		std::string name = ToLower(child->GetName());
		if (name.find("head") != std::string::npos || name.find("neck") != std::string::npos)
		{
			headOut = child;
			return;
		}
		FindHeadRecursive(child, headOut);
		if (headOut) return;
	}
}

static void FindHeadInSceneByName(Player* player, Object3d*& headOut)
{
	if (!SceneManager::GetInstance()) return;
	auto scene = SceneManager::GetInstance()->GetCurrentScene();
	if (!scene) return;
	for (auto& obj : scene->GetObjects())
	{
		if (!obj) continue;
		std::string n = ToLower(obj->GetName());
		if (n.find("head") != std::string::npos || n.find("neck") != std::string::npos)
		{
			headOut = obj.get();
			return;
		}
	}
}

static void TryFindHead(Player* player, Object3d*& headOut)
{
	if (!player) return;
	FindHeadRecursive(player, headOut);
	if (headOut) return;
	FindHeadInSceneByName(player, headOut);
}

static void SetSwordCollisionActive(Player* player, bool isActive) {
	Object3d* swordObj = nullptr;
	TryFindSword(player, swordObj);
	if (swordObj) {
		// trueなら敵(kEnemy)と当たる、falseなら誰とも当たらない(0)
		swordObj->SetCollisionMask(isActive ? kEnemy : 0);
	}
}

// ========================================================
// ヘルパ: Lerp / Easing / Angle
// ========================================================
static Vector3 LerpVec(const Vector3& a, const Vector3& b, float t)
{
	return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}
static float EaseInOutSine(float t)
{
	const float pi = 3.14159265358979323846f;
	return 0.5f * (1.0f - std::cos(pi * t));
}
// map [-1..1] -> eased [-1..1]
static float EaseSinToSmooth(float s)
{
	float u = (s + 1.0f) * 0.5f;
	float e = EaseInOutSine(u);
	return e * 2.0f - 1.0f;
}
static float NormalizeAngle(float a)
{
	const float PI = 3.14159265358979323846f;
	while (a > PI) a -= 2.0f * PI;
	while (a < -PI) a += 2.0f * PI;
	return a;
}
static float LerpAngle(float a, float b, float t)
{
	float diff = NormalizeAngle(b - a);
	return a + diff * t;
}

// ========================================================
// グローバル: 待機状態での体の向きブレンド管理
// ========================================================
static bool s_bodyBlendActive = false;
static float s_bodyStartY = 0.0f;
static float s_bodyTargetY = 0.0f;

// ========================================================
// 待機状態 (Idle)
// ========================================================
void PlayerStateIdle::Enter(Player* player)
{
	if (!player) return;

	SetSwordActive(player, false);
	player->PlayAnimation("Idle", false);
	DebugConsole::GetInstance()->AddLog("★ ENTER: Idle State (searching feet/arms/sword/head)");

	// 初期化
	leftFootObj_ = nullptr; rightFootObj_ = nullptr;
	leftFootSaved_ = false; rightFootSaved_ = false;
	leftFootStartRot_ = { 0,0,0 }; rightFootStartRot_ = { 0,0,0 };
	leftArmObj_ = nullptr; rightArmObj_ = nullptr;
	leftArmSaved_ = false; rightArmSaved_ = false;
	leftArmStartRot_ = { 0,0,0 }; rightArmStartRot_ = { 0,0,0 };
	swordObj_ = nullptr; swordSaved_ = false;
	headObj_ = nullptr; headSaved_ = false; headStartRot_ = { 0,0,0 };

	// ブレンド初期化
	blendTimer_ = 0.0f;

	// Idle では体の向きを強制しない（攻撃開始時に回すため）
	s_bodyStartY = player->GetRotation().y;
	s_bodyTargetY = s_bodyStartY;
	s_bodyBlendActive = false;

	TryFindFeet(player, leftFootObj_, rightFootObj_);
	if (leftFootObj_) {
		leftFootDefaultRot_ = leftFootObj_->GetRotation();
		leftFootStartRot_ = leftFootDefaultRot_;
		leftFootSaved_ = true;
	}

	if (rightFootObj_) {
		rightFootDefaultRot_ = rightFootObj_->GetRotation();
		rightFootStartRot_ = rightFootDefaultRot_;
		rightFootSaved_ = true;
	}

	TryFindArms(player, leftArmObj_, rightArmObj_);
	if (leftArmObj_) {
		leftArmDefaultRot_ = leftArmObj_->GetRotation();
		leftArmStartRot_ = leftArmDefaultRot_;
		leftArmSaved_ = true;
	}

	if (rightArmObj_) {
		rightArmDefaultRot_ = rightArmObj_->GetRotation();
		rightArmStartRot_ = rightArmDefaultRot_;
		rightArmSaved_ = true;
	}

	TryFindSword(player, swordObj_);
	if (swordObj_) {
		swordDefaultLocalPos_ = swordObj_->GetTransform()->translate;
		swordDefaultWorldPos_ = swordObj_->GetWorldPosition();
		swordSaved_ = true;
	}

	TryFindHead(player, headObj_);
	if (headObj_) {
		// ここで headStartRot_ は「実際に現在使われている Transform->rotate（クォータニオンに基づく）」を使う
		Transform* htf = headObj_->GetTransform();
		headDefaultRot_ = headObj_->GetRotation();
		headStartRot_ = htf->rotate;
		headSaved_ = true;
	}

	animTimer_ = 0.0f;
	footStage_ = 0;
}

void PlayerStateIdle::Update(Player* player)
{
	if (!player) return;

	InputManager* im = player ? player->GetInputManager() : nullptr;
	bool attackTriggered = im && (im->IsKeyTriggered(DIK_K) || im->IsMouseButtonTriggered(0));

	if (attackTriggered)
	{
	
		bool isAirborne = !player->IsGrounded() || std::abs(player->GetVelocity().y) > 0.5f;
		if (isAirborne)
		{
			player->ChangeState(std::make_unique<PlayerStatePlungeAttack>());
			return;
		}

		// --- 地上攻撃の処理  ---
		if ((player && player->ConsumePendingAttack2()) || (player && player->IsComboWindowActive()))
		{
			player->ChangeState(std::make_unique<PlayerStateAttack2>());
		}
		else
		{
			if (player)
			{
				player->RecordAttackInput(0.15f);
				player->MarkAttackBufferUsedForStateStart();
				player->ChangeState(std::make_unique<PlayerStateAttack1>());
			}
		}
		return;
	}

	Vector3 vel = player->GetVelocity(); vel.y = 0.0f;
	float speed = Math::Length(vel);
	if (speed > 0.1f)
	{
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
		}
	}

	if (!headObj_) {
		TryFindHead(player, headObj_);
		if (headObj_ && !headSaved_) {
			Transform* htf = headObj_->GetTransform();
			headDefaultRot_ = headObj_->GetRotation();
			headStartRot_ = htf->rotate;
			headSaved_ = true;
		}
	}
}
void PlayerStateIdle::Exit(Player* player)
{
	// 元に戻す
	if (leftFootObj_) {
		Transform* tf = leftFootObj_->GetTransform();
		tf->rotate = leftFootDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(leftFootDefaultRot_);
		tf->isQuaternionMaster = true;
		leftFootObj_->UpdateWorldMatrix();
	}

	if (rightFootObj_) {
		Transform* tf = rightFootObj_->GetTransform();
		tf->rotate = rightFootDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(rightFootDefaultRot_);
		tf->isQuaternionMaster = true;
		rightFootObj_->UpdateWorldMatrix();
	}

	if (leftArmObj_) {
		Transform* tf = leftArmObj_->GetTransform();
		tf->rotate = leftArmDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(leftArmDefaultRot_);
		tf->isQuaternionMaster = true;
		leftArmObj_->UpdateWorldMatrix();
	}

	if (rightArmObj_) {
		Transform* tf = rightArmObj_->GetTransform();
		tf->rotate = rightArmDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(rightArmDefaultRot_);
		tf->isQuaternionMaster = true;
		rightArmObj_->UpdateWorldMatrix();
	}

	if (headObj_ && headSaved_) {
		Transform* tf = headObj_->GetTransform();
		tf->rotate = headDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(headDefaultRot_);
		tf->isQuaternionMaster = true;
		headObj_->UpdateWorldMatrix();
	}

	if (swordObj_ && swordSaved_) {
		Transform* tf = swordObj_->GetTransform();
		tf->translate = swordDefaultLocalPos_;
		swordObj_->UpdateLocalMatrix();
		swordObj_->UpdateWorldMatrix();
	}

	// ブレンド解除（念のため）
	s_bodyBlendActive = false;
}

void PlayerStateIdle::ApplyPostUpdate(Player* player, float deltaTime)
{
	if (!player) return;
	if (deltaTime <= 0.0f) return;

	// ブレンド更新
	blendTimer_ += deltaTime;
	float blendT = (blendDuration_ > 1e-6f) ? std::clamp(blendTimer_ / blendDuration_, 0.0f, 1.0f) : 1.0f;
	float blendEase = EaseInOutSine(blendT);

	// Idleの周期
	animTimer_ += deltaTime;
	float twoDur = animDuration_ * 2.0f;
	float local = (twoDur > 1e-6f) ? std::fmod(animTimer_, twoDur) : 0.0f;
	float t = (animDuration_ > 0.0f) ? (local / animDuration_) : 1.0f;
	if (t > 1.0f) t = 2.0f - t;
	float e = EaseInOutSine(t);

	auto DegToRad = [](float d) { return d * 3.14159265358979323846f / 180.0f; };

	float targetAngle = targetAngleRad_;
	float armZRightRad = DegToRad(5.0f);
	float armZLeftRad = DegToRad(-5.0f);
	const float pi = 3.14159265358979323846f;

	if (leftFootObj_ && leftFootSaved_)
	{
		Vector3 targetR = leftFootDefaultRot_; targetR.x = leftFootDefaultRot_.x + targetAngle * e;
		Vector3 final = LerpVec(leftFootStartRot_, targetR, blendEase);
		Transform* tf = leftFootObj_->GetTransform();
		tf->quaternion = Math::EulerToQuaternion(final);
		tf->isQuaternionMaster = true;
		leftFootObj_->UpdateWorldMatrix();
	}

	if (rightFootObj_ && rightFootSaved_)
	{
		Vector3 targetR = rightFootDefaultRot_; targetR.x = rightFootDefaultRot_.x + targetAngle * e;
		Vector3 final = LerpVec(rightFootStartRot_, targetR, blendEase);
		Transform* tf = rightFootObj_->GetTransform();
		tf->quaternion = Math::EulerToQuaternion(final);
		tf->isQuaternionMaster = true;
		rightFootObj_->UpdateWorldMatrix();
	}

	if (leftArmObj_ && leftArmSaved_)
	{
		Vector3 targetR = leftArmDefaultRot_; targetR.z = leftArmDefaultRot_.z + armZLeftRad * e;
		Vector3 final = LerpVec(leftArmStartRot_, targetR, blendEase);
		Transform* tf = leftArmObj_->GetTransform();
		tf->quaternion = Math::EulerToQuaternion(final);
		tf->isQuaternionMaster = true;
		leftArmObj_->UpdateWorldMatrix();
	}

	if (rightArmObj_ && rightArmSaved_)
	{
		Vector3 targetR = rightArmDefaultRot_; targetR.z = rightArmDefaultRot_.z + armZRightRad * e;
		Vector3 final = LerpVec(rightArmStartRot_, targetR, blendEase);
		Transform* tf = rightArmObj_->GetTransform();
		tf->quaternion = Math::EulerToQuaternion(final);
		tf->isQuaternionMaster = true;
		rightArmObj_->UpdateWorldMatrix();
	}

	if (swordObj_ && swordSaved_)
	{
		Transform* tf = swordObj_->GetTransform();
		tf->translate = swordDefaultLocalPos_;
		swordObj_->UpdateLocalMatrix();
		swordObj_->UpdateWorldMatrix();
	}

	if (headObj_ && headSaved_)
	{
		float phase = t * pi;
		float h = std::cos(phase);
		Vector3 targetEuler = headDefaultRot_; float headAmpRad = DegToRad(2.0f);
		targetEuler.x = headDefaultRot_.x + h * headAmpRad;
		Vector3 blendedEuler = LerpVec(headStartRot_, targetEuler, blendEase);

		Quaternion targetQ = Math::EulerToQuaternion(blendedEuler);
		Transform* tf = headObj_->GetTransform();
		Quaternion currentQ = tf->quaternion;
		float alpha = 1.0f - std::expf(-headSmoothSpeed_ * deltaTime);
		alpha = std::clamp(alpha, 0.0f, 1.0f);
		Quaternion blendedQ = Math::Slerp(currentQ, targetQ, alpha);
		tf->quaternion = blendedQ; tf->isQuaternionMaster = true;
		Matrix4x4 rotMat = Math::MakeRotateQuaternionMatrix(blendedQ);
		tf->rotate = Math::MatrixToEuler(rotMat);
		headObj_->UpdateWorldMatrix();
	}

	if (s_bodyBlendActive)
	{
		float bodyBlendT = (blendDuration_ > 1e-6f) ? std::clamp(blendTimer_ / blendDuration_, 0.0f, 1.0f) : 1.0f;
		float bodyEase = EaseInOutSine(bodyBlendT);
		float newY = LerpAngle(s_bodyStartY, s_bodyTargetY, bodyEase);
		player->SetRotationY(newY); // SetRotationY は quaternion 更新する
		if (bodyBlendT >= 1.0f) s_bodyBlendActive = false;
	}
}

// ========================================================
// 走り状態 (Run)
// ========================================================
void PlayerStateRun::Enter(Player* player)
{
	if (!player) return;

	SetSwordActive(player, false);
	DebugConsole::GetInstance()->AddLog("★ ENTER: Run State (custom procedural pose)");

	bodyObj_ = player; bodySaved_ = false;
	headObj_ = nullptr; rightArmObj_ = nullptr; leftArmObj_ = nullptr; rightFootObj_ = nullptr; leftFootObj_ = nullptr;
	rightArmSaved_ = leftArmSaved_ = rightFootSaved_ = leftFootSaved_ = headSaved_ = false;

	animTimer_ = 0.0f;

	TryFindArms(player, leftArmObj_, rightArmObj_);
	TryFindFeet(player, leftFootObj_, rightFootObj_);
	TryFindHead(player, headObj_);

	if (bodyObj_) {
		Transform* tf = bodyObj_->GetTransform();
		bodyDefaultPos_ = tf->translate;
		bodyDefaultRot_ = tf->rotate;
		bodySaved_ = true;
	}

	if (headObj_) {
		Transform* htf = headObj_->GetTransform();
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
	if (bodyObj_) { Transform* tf = bodyObj_->GetTransform(); tf->rotate.x = DegToRad(10.0f); tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; bodyObj_->UpdateWorldMatrix(); }
	// 頭は走り中は常に -10deg にする（待機の頭振りを使わない）
	if (headObj_) {
		Transform* tf = headObj_->GetTransform();
		tf->rotate.x = DegToRad(-10.0f);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		headObj_->UpdateWorldMatrix();
	}

	if (rightArmObj_) {
		Transform* tf = rightArmObj_->GetTransform();
		tf->rotate.x = DegToRad(-10.0f);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		rightArmObj_->UpdateWorldMatrix();
	}

	if (leftArmObj_) {
		Transform* tf = leftArmObj_->GetTransform();
		tf->rotate.x = DegToRad(-10.0f);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		leftArmObj_->UpdateWorldMatrix();
	}

	if (rightFootObj_) {
		Transform* tf = rightFootObj_->GetTransform();
		tf->rotate.x = DegToRad(-10.0f);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		rightFootObj_->UpdateWorldMatrix();
	}

	if (leftFootObj_) {
		Transform* tf = leftFootObj_->GetTransform();
		tf->rotate.x = DegToRad(-10.0f);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		leftFootObj_->UpdateWorldMatrix();
	}
}

void PlayerStateRun::Update(Player* player)
{
	if (!player) return;

	InputManager* im = player ? player->GetInputManager() : nullptr;
	bool attackTriggered = im && (im->IsKeyTriggered(DIK_K) || im->IsMouseButtonTriggered(0));

	if (attackTriggered)
	{
		// ★最優先: 空中判定（移動ジャンプ中なら絶対に落下攻撃を出す）
		bool isAirborne = !player->IsGrounded() || std::abs(player->GetVelocity().y) > 0.5f;
		if (isAirborne)
		{
			player->ChangeState(std::make_unique<PlayerStatePlungeAttack>());
			return;
		}

		// --- 地上攻撃の処理 ---
		if ((player && player->ConsumePendingAttack2()) || (player && player->IsComboWindowActive()))
		{
			player->ChangeState(std::make_unique<PlayerStateAttack2>());
		}
		else
		{
			if (player)
			{
				player->RecordAttackInput(0.15f);
				player->MarkAttackBufferUsedForStateStart();
				player->ChangeState(std::make_unique<PlayerStateAttack1>());
			}
		}
		return;
	}

	Vector3 rawVel = player->GetVelocity(); Vector3 flatVel = rawVel; flatVel.y = 0.0f;
	float speed = Math::Length(flatVel);
	if (speed <= 0.1f)
	{
		// 直接 Idle に切り替えず、Run 側でブレンドしてから遷移する
		if (!exitBlendActive_)
		{
			exitBlendActive_ = true;
			exitBlendTimer_ = 0.0f;
			// 現在の回転を開始値として保存（ブレンド開始時点）
			if (rightArmObj_ && rightArmSaved_) rightArmExitStartRot_ = rightArmObj_->GetRotation();

			if (leftArmObj_ && leftArmSaved_) leftArmExitStartRot_ = leftArmObj_->GetRotation();

			if (rightFootObj_ && rightFootSaved_) rightFootExitStartRot_ = rightFootObj_->GetRotation();

			if (leftFootObj_ && leftFootSaved_) leftFootExitStartRot_ = leftFootObj_->GetRotation();

			if (headObj_ && headSaved_)
			{
				headExitStartRot_ = headObj_->GetRotation();
				// クォータニオンも保存（Slerp 用）
				Transform* htf = headObj_->GetTransform();
				headExitStartQuat_ = htf->quaternion;
			}

			if (bodyObj_ && bodySaved_) bodyExitStartRot_ = bodyObj_->GetTransform()->rotate;
		}
		return;
	}
}
void PlayerStateRun::Exit(Player* player)
{
	DebugConsole::GetInstance()->AddLog("EXIT: Run State - restore defaults");

	// 体: x軸（前後の傾き）だけデフォルトに戻す。Y（向き）は維持する。
	if (bodyObj_ && bodySaved_)
	{
		Transform* tf = bodyObj_->GetTransform();
		// preserve current Y/Z, restore X from saved default
		Vector3 cur = tf->rotate;
		cur.x = bodyDefaultRot_.x;
		tf->rotate = cur;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		bodyObj_->UpdateWorldMatrix();
	}

	// 頭: Exit では即時復帰しない（ApplyPostUpdate のブレンドに任せる）
	// --- ただし、他状態へ即遷移（例: 攻撃）する場合は Run 側で保存しているデフォルトへ即時復帰しておく ---
	if (headObj_ && headSaved_)
	{
		Transform* tf = headObj_->GetTransform();
		tf->rotate = headDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		headObj_->UpdateWorldMatrix();
	}

	// 頭: Exit では即時復帰しない（ApplyPostUpdate のブレンドに任せる）
	// 右腕
	if (rightArmObj_ && rightArmSaved_)
	{
		Transform* tf = rightArmObj_->GetTransform();
		tf->rotate = rightArmDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		rightArmObj_->UpdateWorldMatrix();
	}

	// 左腕: ローカル位置と回転をデフォルトに戻す
	if (leftArmObj_ && leftArmSaved_)
	{
		Transform* tf = leftArmObj_->GetTransform();
		tf->translate = leftArmDefaultPos_;
		tf->rotate = leftArmDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		leftArmObj_->UpdateLocalMatrix();
		leftArmObj_->UpdateWorldMatrix();
	}

	// 右足
	if (rightFootObj_ && rightFootSaved_)
	{
		Transform* tf = rightFootObj_->GetTransform();
		tf->rotate = rightFootDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		rightFootObj_->UpdateWorldMatrix();
	}

	// 左足
	if (leftFootObj_ && leftFootSaved_)
	{
		Transform* tf = leftFootObj_->GetTransform();
		tf->rotate = leftFootDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		leftFootObj_->UpdateWorldMatrix();
	}
}

void PlayerStateRun::ApplyPostUpdate(Player* player, float deltaTime)
{
	if (!player) return; // ★追加
	if (deltaTime <= 0.0f) return;

	animTimer_ += deltaTime;
	blendTimer_ += deltaTime;
	float blendT = (blendDuration_ > 1e-6f) ? std::clamp(blendTimer_ / blendDuration_, 0.0f, 1.0f) : 1.0f;
	float blendEase = EaseInOutSine(blendT);

	float phase = 0.0f;
	if (stepPeriod_ > 1e-6f) phase = std::fmod(animTimer_, stepPeriod_) / stepPeriod_;

	const float PI = 3.14159265358979323846f;
	float s = std::sin(phase * 2.0f * PI);
	float easedS = EaseSinToSmooth(s);

	// --- 終了ブレンドがアクティブな場合は、Run の動きを徐々にデフォルト（Run が保存しているデフォルト）へ戻す ---
	if (exitBlendActive_)
	{
		exitBlendTimer_ += deltaTime;
		float et = (exitBlendDuration_ > 1e-6f) ? std::clamp(exitBlendTimer_ / exitBlendDuration_, 0.0f, 1.0f) : 1.0f;
		float eease = EaseInOutSine(et);

		// 右腕 -> デフォルト
		if (rightArmObj_ && rightArmSaved_)
		{
			Vector3 targetR = rightArmDefaultRot_;
			Vector3 final = LerpVec(rightArmExitStartRot_, targetR, eease);
			Transform* tf = rightArmObj_->GetTransform(); tf->quaternion = Math::EulerToQuaternion(final); tf->isQuaternionMaster = true; rightArmObj_->UpdateWorldMatrix();
		}

		// 左腕 -> デフォルト
		if (leftArmObj_ && leftArmSaved_)
		{
			Vector3 targetR = leftArmDefaultRot_;
			Vector3 final = LerpVec(leftArmExitStartRot_, targetR, eease);
			Transform* tf = leftArmObj_->GetTransform(); tf->quaternion = Math::EulerToQuaternion(final); tf->isQuaternionMaster = true; leftArmObj_->UpdateWorldMatrix();
		}

		// 右足 -> デフォルト
		if (rightFootObj_ && rightFootSaved_)
		{
			Vector3 targetR = rightFootDefaultRot_;
			Vector3 final = LerpVec(rightFootExitStartRot_, targetR, eease);
			Transform* tf = rightFootObj_->GetTransform(); tf->quaternion = Math::EulerToQuaternion(final); tf->isQuaternionMaster = true; rightFootObj_->UpdateWorldMatrix();
		}

		// 左足 -> デフォルト
		if (leftFootObj_ && leftFootSaved_)
		{
			Vector3 targetR = leftFootDefaultRot_;
			Vector3 final = LerpVec(leftFootExitStartRot_, targetR, eease);
			Transform* tf = leftFootObj_->GetTransform(); tf->quaternion = Math::EulerToQuaternion(final); tf->isQuaternionMaster = true; leftFootObj_->UpdateWorldMatrix();
		}

		// 頭 -> デフォルト（クォータニオンで滑らかに戻す）
		if (headObj_ && headSaved_)
		{
			Transform* tf = headObj_->GetTransform();
			Quaternion targetQ = Math::EulerToQuaternion(headDefaultRot_);
			Quaternion blendedQ = Math::Slerp(headExitStartQuat_, targetQ, eease);
			tf->quaternion = blendedQ;
			tf->isQuaternionMaster = true;
			Matrix4x4 rotMat = Math::MakeRotateQuaternionMatrix(blendedQ);
			tf->rotate = Math::MatrixToEuler(rotMat);
			headObj_->UpdateWorldMatrix();
		}

		// 体の傾き X -> デフォルト（角度差を正規化して滑らかに）
		if (bodyObj_ && bodySaved_)
		{
			Transform* tf = bodyObj_->GetTransform();
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
		if (et >= 1.0f)
		{
			// exitBlendActive_ をリセットしてから状態遷移する
			exitBlendActive_ = false;
			player->ChangeState(std::make_unique<PlayerStateIdle>());
			return;
		}

		// 終了ブレンド中はここで終了
		return;
	}

	// --- 通常の走りアニメーション ---
	if (rightArmObj_ && rightArmSaved_)
	{
		Vector3 targetR = rightArmDefaultRot_;
		targetR.x = rightArmDefaultRot_.x - rightArmAmpRad_ * easedS;
		Vector3 final = LerpVec(rightArmStartRot_, targetR, blendEase);
		Transform* tf = rightArmObj_->GetTransform(); tf->quaternion = Math::EulerToQuaternion(final); tf->isQuaternionMaster = true; rightArmObj_->UpdateWorldMatrix();
	}

	if (leftArmObj_ && leftArmSaved_)
	{
		Vector3 targetR = leftArmDefaultRot_;
		targetR.x = leftArmDefaultRot_.x + leftArmAmpRad_ * easedS;
		Vector3 final = LerpVec(leftArmStartRot_, targetR, blendEase);
		Transform* tf = leftArmObj_->GetTransform(); tf->quaternion = Math::EulerToQuaternion(final); tf->isQuaternionMaster = true; leftArmObj_->UpdateWorldMatrix();
	}

	if (rightFootObj_ && rightFootSaved_)
	{
		Vector3 targetR = rightFootDefaultRot_;
		targetR.x = rightFootDefaultRot_.x + footAmpRad_ * easedS;
		Vector3 final = LerpVec(rightFootStartRot_, targetR, blendEase);
		Transform* tf = rightFootObj_->GetTransform(); tf->quaternion = Math::EulerToQuaternion(final); tf->isQuaternionMaster = true; rightFootObj_->UpdateWorldMatrix();
	}

	if (leftFootObj_ && leftFootSaved_)
	{
		Vector3 targetR = leftFootDefaultRot_;
		targetR.x = leftFootDefaultRot_.x - footAmpRad_ * easedS;
		Vector3 final = LerpVec(leftFootStartRot_, targetR, blendEase);
		Transform* tf = leftFootObj_->GetTransform(); tf->quaternion = Math::EulerToQuaternion(final); tf->isQuaternionMaster = true; leftFootObj_->UpdateWorldMatrix();
	}

	// 頭: 待機の頭振りを使わず、走りでは常に -10deg を基準にする
	if (headObj_ && headSaved_)
	{
		auto DegToRad = [](float d) { return d * 3.14159265358979323846f / 180.0f; };
		Vector3 targetEuler = headDefaultRot_;
		targetEuler.x = headDefaultRot_.x + DegToRad(-10.0f);
		Vector3 final = LerpVec(headStartRot_, targetEuler, blendEase);
		Transform* tf = headObj_->GetTransform(); tf->quaternion = Math::EulerToQuaternion(final); tf->isQuaternionMaster = true; headObj_->UpdateWorldMatrix();
	}
}

// ========================================================
// 攻撃1段目状態 (Attack1)
// ========================================================
void PlayerStateAttack1::Enter(Player* player)
{
	DebugConsole::GetInstance()->AddLog("★ ENTER: Attack1 State");

	if (player) player->SetIsControlActive(false);
	SetSwordActive(player, true);
	animTimer_ = 0.0f;

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

		// 変更点: 攻撃ポーズの Y 回転を「現在の向き（bodyDefaultRot_.y）を基準にした相対角度」にする
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
	if (!player) return; // ★追加

	// フレーム固定で更新している既存スタイルに合わせる（1/60）
	animTimer_ += 1.0f / 60.0f;

	// バッファ消費チェック（既存挙動）
	if (player && player->ConsumeBufferedAttackInput())
	{
		player->SetPendingAttack2(true);
	}

	// 攻撃中にクリックまたは K で 2 段目を予約できるようにする（従来の動作）
	if (player)
	{
		InputManager* im = player->GetInputManager();
		if (im && (im->IsKeyTriggered(DIK_K) || im->IsMouseButtonTriggered(0)))
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
		// 自動で Attack2 に遷移せず、次のクリックで Attack2 を出すためのフラグをセットする
		if (player) player->SetPendingAttack2(true);
		// Idle に戻してプレイヤーの入力を待つ（Exit() がコントロールを再有効化する）
		player->ChangeState(std::make_unique<PlayerStateIdle>());
		return;
	}
}

void PlayerStateAttack1::Exit(Player* player)
{
	DebugConsole::GetInstance()->AddLog("★ EXIT: Attack1 State");

	if (player) player->SetIsControlActive(true);
	SetSwordActive(player, false);
	// 戻す
	if (!initializedParts_) return;

	if (bodyObj_) {
		Transform* tf = bodyObj_->GetTransform();
		tf->translate = bodyDefaultPos_;
		tf->rotate = bodyDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true; bodyObj_->UpdateWorldMatrix();
	}

	if (headObj_) {
		Transform* tf = headObj_->GetTransform();
		tf->translate = headDefaultPos_; tf->rotate = headDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true; headObj_->UpdateWorldMatrix();
	}

	if (rightArmObj_) {
		Transform* tf = rightArmObj_->GetTransform();
		tf->translate = rightArmDefaultPos_;
		tf->rotate = rightArmDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		rightArmObj_->UpdateWorldMatrix();
	}

	if (leftArmObj_) {
		Transform* tf = leftArmObj_->GetTransform();
		tf->translate = leftArmDefaultPos_;
		tf->rotate = leftArmDefaultRot_;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true; leftArmObj_->UpdateLocalMatrix();
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
	// 変更点: Y 回転をデフォルト向きに対する相対値で設定
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
	// 変更点: End もデフォルト向きを基準にした相対角度で設定
	bodyEndRot.y = bodyDefaultRot_.y + DegToRad(-100.0f);
	bodyEndRot.z = DegToRad(-36.0f);

	Vector3 headEndPos{ 0.0f, 0.0f, 0.0f };
	Vector3 headEndRot{ DegToRad(-22.0f), DegToRad(61.0f), 0.0f };

	Vector3 rtArmEndPos{ 0.0f, 0.0f, 0.0f };
	Vector3 rtArmEndRot{ DegToRad(-151.0f), DegToRad(-70.0f), DegToRad(57.0f) };

	// 左手終了位置もZを高めに（元 0.2）、
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
		tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; bodyObj_->UpdateWorldMatrix();
	}

	if (headObj_) {
		Transform* tf = headObj_->GetTransform();
		tf->translate = headDefaultPos_ + LerpVec3(Vector3{ 0,0,0 }, headEndPos, t);
		tf->rotate = LerpVec3(headStartRot_, headEndRot, t);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; headObj_->UpdateWorldMatrix();
	}

	if (rightArmObj_) {
		Transform* tf = rightArmObj_->GetTransform();
		tf->translate = rightArmDefaultPos_ + LerpVec3(rtArmStartPos, rtArmEndPos, t);
		tf->rotate = LerpVec3(rtArmStartRot, rtArmEndRot, t);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; rightArmObj_->UpdateWorldMatrix();
	}

	if (leftArmObj_) {
		Transform* tf = leftArmObj_->GetTransform();
		// 位置アニメーションを無効化: 常にデフォルトのローカル位置を使う
		tf->translate = leftArmDefaultPos_;
		tf->rotate = LerpVec3(ltArmStartRot, ltArmEndRot, t);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; leftArmObj_->UpdateLocalMatrix(); leftArmObj_->UpdateWorldMatrix();
	}

	if (rightFootObj_) {
		Transform* tf = rightFootObj_->GetTransform();
		tf->translate = rightFootDefaultPos_ + LerpVec3(rtFootStartPos, rtFootEndPos, t);
		tf->rotate = LerpVec3(rtFootStartRot, rtFootEndRot, t);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; rightFootObj_->UpdateWorldMatrix();
	}

	if (leftFootObj_) {
		Transform* tf = leftFootObj_->GetTransform();
		// 位置アニメーションを無効化: 常にデフォルトのローカル位置を使う
		tf->translate = leftFootDefaultPos_;
		tf->rotate = LerpVec3(ltFootStartRot, ltFootEndRot, t);
		tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; leftFootObj_->UpdateLocalMatrix(); leftFootObj_->UpdateWorldMatrix();
	}
}

// ========================================================
// 攻撃2段目状態 (Attack2) 実装
// ========================================================
void PlayerStateAttack2::Enter(Player* player)
{
	if (!player) return; 

	DebugConsole::GetInstance()->AddLog("★ ENTER: Attack2 State");

	if (player) player->SetIsControlActive(false);
	SetSwordActive(player, true);
	animTimer_ = 0.0f;

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

	// ============================================
	// ★追加: 攻撃中に入力があれば 3段目 を予約！
	// ============================================
	InputManager* im = player->GetInputManager();
	if (im && (im->IsKeyTriggered(DIK_K) || im->IsMouseButtonTriggered(0))) {
		player->SetPendingAttack2(true); // 変数名は2のまま流用でOK
	}

	animTimer_ += 1.0f / 60.0f;
	float t = std::clamp(animTimer_ / animDuration_, 0.0f, 1.0f);
	float et = EaseInOutSine(t);
	ApplyPose(et);

	if (animTimer_ >= animDuration_)
	{
		// ============================================
		// ★修正: 予約があればAttack3へ！なければIdleへ
		// ============================================
		if (player->ConsumePendingAttack2()) {
			player->ChangeState(std::make_unique<PlayerStateAttack3>());
		}
		else {
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
	// 変更点: Y 回転をプレイヤーの向き（bodyDefaultRot_.y）に対する相対値で指定して、向いている方向で攻撃が出るようにする
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
		bodyEndRot.y = bodyDefaultRot_.y + DegToRad(420.0f);
	}
	else
	{
		bodyEndRot.y = DegToRad(420.0f);
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
// ========================================================
void PlayerStateAttack3::Enter(Player* player)
{
    if (!player) return;
    DebugConsole::GetInstance()->AddLog("★ ENTER: Attack3 State (Thrust!)");

    player->SetIsControlActive(false);
    SetSwordActive(player, true);
    animTimer_ = 0.0f;
    bodyObj_ = player;

    TryFindHead(player, headObj_); TryFindArms(player, leftArmObj_, rightArmObj_); TryFindFeet(player, leftFootObj_, rightFootObj_);
    initializedParts_ = false;

    // ★変更：Enter時に「今のポーズ（Attack2の終わりのポーズ）」をStartRotとして正確に記録する！
    if (bodyObj_) { bodyDefaultPos_ = bodyObj_->GetTransform()->translate; bodyDefaultRot_ = bodyObj_->GetRotation(); bodyStartRot_ = bodyObj_->GetTransform()->rotate; }
    if (headObj_) { Transform* tf = headObj_->GetTransform(); headDefaultPos_ = tf->translate; headDefaultRot_ = headObj_->GetRotation(); headStartRot_ = tf->rotate; }
    if (rightArmObj_) { Transform* tf = rightArmObj_->GetTransform(); rightArmDefaultPos_ = tf->translate; rightArmDefaultRot_ = rightArmObj_->GetRotation(); rtArmStartRot_ = tf->rotate; }
    if (leftArmObj_) { Transform* tf = leftArmObj_->GetTransform(); leftArmDefaultPos_ = tf->translate; leftArmDefaultRot_ = leftArmObj_->GetRotation(); ltArmStartRot_ = tf->rotate; }
    if (rightFootObj_) { Transform* tf = rightFootObj_->GetTransform(); rightFootDefaultPos_ = tf->translate; rightFootDefaultRot_ = rightFootObj_->GetRotation(); rtFootStartRot_ = tf->rotate; }
    if (leftFootObj_) { Transform* tf = leftFootObj_->GetTransform(); leftFootDefaultPos_ = tf->translate; leftFootDefaultRot_ = leftFootObj_->GetRotation(); ltFootStartRot_ = tf->rotate; }

    initializedParts_ = true;
    ApplyPose(0.0f);
}

void PlayerStateAttack3::Update(Player* player)
{
    if (!player) return;
    animTimer_ += 1.0f / 60.0f;
    float t = std::clamp(animTimer_ / animDuration_, 0.0f, 1.0f);
    ApplyPose(t);

    if (animTimer_ >= animDuration_)
    {
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

    if (bodyObj_) { Transform* tf = bodyObj_->GetTransform(); tf->translate = bodyDefaultPos_; tf->rotate = bodyDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; bodyObj_->UpdateWorldMatrix(); }
    if (headObj_) { Transform* tf = headObj_->GetTransform(); tf->translate = headDefaultPos_; tf->rotate = headDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; headObj_->UpdateWorldMatrix(); }
    if (rightArmObj_) { Transform* tf = rightArmObj_->GetTransform(); tf->translate = rightArmDefaultPos_; tf->rotate = rightArmDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; rightArmObj_->UpdateLocalMatrix(); rightArmObj_->UpdateWorldMatrix(); }
    if (leftArmObj_) { Transform* tf = leftArmObj_->GetTransform(); tf->translate = leftArmDefaultPos_; tf->rotate = leftArmDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; leftArmObj_->UpdateLocalMatrix(); leftArmObj_->UpdateWorldMatrix(); }
    if (rightFootObj_) { Transform* tf = rightFootObj_->GetTransform(); tf->translate = rightFootDefaultPos_; tf->rotate = rightFootDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; rightFootObj_->UpdateLocalMatrix(); rightFootObj_->UpdateWorldMatrix(); }
    if (leftFootObj_) { Transform* tf = leftFootObj_->GetTransform(); tf->translate = leftFootDefaultPos_; tf->rotate = leftFootDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; leftFootObj_->UpdateLocalMatrix(); leftFootObj_->UpdateWorldMatrix(); }
}

void PlayerStateAttack3::ApplyPose(float t)
{
	if (!initializedParts_) return;
	auto DegToRad = [](float d) { return d * 3.14159265358979323846f / 180.0f; };
	auto LerpVec3 = [](const Vector3& a, const Vector3& b, float t) {
		return Vector3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
		};
	auto EaseInOutSine = [](float x) { return -(std::cos(3.14159265f * x) - 1.0f) / 2.0f; };
	auto EaseOutCubic = [](float x) { return 1.0f - std::pow(1.0f - x, 3.0f); };

	float baseY = bodyDefaultRot_.y;

	// =========================================================
	// 1. 各キーフレーム（ポーズ）の定義
	// =========================================================

	// --- [Pose 1] タメ ---
	Vector3 bodyPos1 = { 0.0f, -0.2f, -0.5f };
	Vector3 bodyRot1 = bodyStartRot_;
	bodyRot1.y += DegToRad(45.0f);
	bodyRot1.z = DegToRad(10.0f);

	Vector3 headRot1 = { DegToRad(-10.0f), DegToRad(-45.0f), 0.0f };
	Vector3 rtArmRot1 = { DegToRad(45.0f), DegToRad(45.0f), DegToRad(0.0f) };
	Vector3 ltArmRot1 = { DegToRad(30.0f), 0.0f, DegToRad(-20.0f) };
	Vector3 ltFootRot1 = { DegToRad(-10.0f), 0.0f, 0.0f };
	Vector3 rtFootRot1 = { DegToRad(-20.0f), 0.0f, 0.0f };

	// --- [Pose 2] 突き ---
	Vector3 bodyPos2 = { 0.0f, -0.2f, 2.0f };
	Vector3 bodyRot2 = bodyDefaultRot_;
	bodyRot2.y = bodyDefaultRot_.y + DegToRad(360.0f);
	bodyRot2.x = DegToRad(20.0f);

	Vector3 headRot2 = { DegToRad(-20.0f), DegToRad(10.0f), 0.0f };
	Vector3 rtArmRot2 = { DegToRad(-20.0f), DegToRad(0.0f), DegToRad(0.0f) };
	Vector3 ltArmRot2 = { DegToRad(60.0f), 0.0f, DegToRad(20.0f) };
	Vector3 ltFootRot2 = { DegToRad(-40.0f), 0.0f, 0.0f };
	Vector3 rtFootRot2 = { DegToRad(30.0f), 0.0f, 0.0f };

	// --- [Pose 3] 戻し (★修正：瞬間移動を防ぐため、完全に元の待機ポーズに戻す) ---
	Vector3 bodyPos3 = { 0.0f, 0.0f, 0.0f };
	Vector3 bodyRot3 = bodyDefaultRot_;
	bodyRot3.y = bodyDefaultRot_.y + DegToRad(360.0f);

	Vector3 headRot3 = headDefaultRot_;

	// 各パーツを「Enter時に保存した待機ポーズ(Default)」に設定する
	Vector3 rtArmRot3 = rightArmDefaultRot_;
	Vector3 ltArmRot3 = leftArmDefaultRot_;
	Vector3 ltFootRot3 = leftFootDefaultRot_;
	Vector3 rtFootRot3 = rightFootDefaultRot_;

	// =========================================================
	// 2. 4段階のタイムライン計算
	// =========================================================
	Vector3 curBodyPos, curBodyRot, curHeadRot, curRtArmRot, curLtArmRot, curLtFootRot, curRtFootRot;

	float t1 = 0.25f; // タメ
	float t2 = 0.45f; // 突き
	float t3 = 0.65f; // 余韻

	if (t <= t1) {
		float localT = EaseInOutSine(t / t1);
		curBodyPos = LerpVec3(Vector3{ 0,0,0 }, bodyPos1, localT); curBodyRot = LerpVec3(bodyStartRot_, bodyRot1, localT); curHeadRot = LerpVec3(headStartRot_, headRot1, localT);
		curRtArmRot = LerpVec3(rtArmStartRot_, rtArmRot1, localT); curLtArmRot = LerpVec3(ltArmStartRot_, ltArmRot1, localT);
		curLtFootRot = LerpVec3(ltFootStartRot_, ltFootRot1, localT); curRtFootRot = LerpVec3(rtFootStartRot_, rtFootRot1, localT);
	}
	else if (t <= t2) {
		float localT = EaseOutCubic((t - t1) / (t2 - t1));
		curBodyPos = LerpVec3(bodyPos1, bodyPos2, localT); curBodyRot = LerpVec3(bodyRot1, bodyRot2, localT); curHeadRot = LerpVec3(headRot1, headRot2, localT);
		curRtArmRot = LerpVec3(rtArmRot1, rtArmRot2, localT); curLtArmRot = LerpVec3(ltArmRot1, ltArmRot2, localT);
		curLtFootRot = LerpVec3(ltFootRot1, ltFootRot2, localT); curRtFootRot = LerpVec3(rtFootRot1, rtFootRot2, localT);
	}
	else if (t <= t3) {
		curBodyPos = bodyPos2; curBodyRot = bodyRot2; curHeadRot = headRot2;
		curRtArmRot = rtArmRot2; curLtArmRot = ltArmRot2;
		curLtFootRot = ltFootRot2; curRtFootRot = rtFootRot2;
	}
	else {
		float localT = EaseInOutSine((t - t3) / (1.0f - t3));
		curBodyPos = LerpVec3(bodyPos2, bodyPos3, localT); curBodyRot = LerpVec3(bodyRot2, bodyRot3, localT); curHeadRot = LerpVec3(headRot2, headRot3, localT);
		curRtArmRot = LerpVec3(rtArmRot2, rtArmRot3, localT); curLtArmRot = LerpVec3(ltArmRot2, ltArmRot3, localT);
		curLtFootRot = LerpVec3(ltFootRot2, ltFootRot3, localT); curRtFootRot = LerpVec3(rtFootRot2, rtFootRot3, localT);
	}

	// =========================================================
	// 3. 補間適用
	// =========================================================
	if (bodyObj_) {
		Transform* tf = bodyObj_->GetTransform();

		// ★修正: プレイヤーが向いている方向(baseY)に合わせて、踏み込みベクトルを回転させる
		float s = std::sin(baseY);
		float c = std::cos(baseY);
		Vector3 worldOffset;
		worldOffset.x = curBodyPos.x * c + curBodyPos.z * s;
		worldOffset.y = curBodyPos.y;
		worldOffset.z = -curBodyPos.x * s + curBodyPos.z * c;

		tf->translate = bodyDefaultPos_ + worldOffset;

		tf->rotate = curBodyRot;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		bodyObj_->UpdateWorldMatrix();
	}

	if (headObj_) { Transform* tf = headObj_->GetTransform(); tf->rotate = curHeadRot; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; headObj_->UpdateWorldMatrix(); }
	if (rightArmObj_) { Transform* tf = rightArmObj_->GetTransform(); tf->translate = rightArmDefaultPos_; tf->rotate = curRtArmRot; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; rightArmObj_->UpdateLocalMatrix(); rightArmObj_->UpdateWorldMatrix(); }
	if (leftArmObj_) { Transform* tf = leftArmObj_->GetTransform(); tf->translate = leftArmDefaultPos_; tf->rotate = curLtArmRot; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; leftArmObj_->UpdateLocalMatrix(); leftArmObj_->UpdateWorldMatrix(); }
	if (leftFootObj_) { Transform* tf = leftFootObj_->GetTransform(); tf->translate = leftFootDefaultPos_; tf->rotate = curLtFootRot; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; leftFootObj_->UpdateLocalMatrix(); leftFootObj_->UpdateWorldMatrix(); }
	if (rightFootObj_) { Transform* tf = rightFootObj_->GetTransform(); tf->translate = rightFootDefaultPos_; tf->rotate = curRtFootRot; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; rightFootObj_->UpdateLocalMatrix(); rightFootObj_->UpdateWorldMatrix(); }
}

// ========================================================
// 回避ダッシュ状態 (Dash) 実装
// ========================================================
void PlayerStateDash::Enter(Player* player)
{
	if (!player) return;
	DebugConsole::GetInstance()->AddLog("★ ENTER: Dash State (Slide Step)");

	// PlayerMover を動かし続ける（入力制御は Mover 側）
	SetSwordActive(player, false);
	animTimer_ = 0.0f;
	bodyObj_ = player; // bodyObj_ は Root（プレイヤー自身）

	TryFindHead(player, headObj_);
	TryFindArms(player, leftArmObj_, rightArmObj_);
	TryFindFeet(player, leftFootObj_, rightFootObj_);

	initializedParts_ = false;
	if (bodyObj_) { bodyDefaultRot_ = bodyObj_->GetRotation(); }
	if (headObj_) { headDefaultRot_ = headObj_->GetRotation(); }
	if (rightArmObj_) { rightArmDefaultRot_ = rightArmObj_->GetRotation(); }
	if (leftArmObj_) { leftArmDefaultRot_ = leftArmObj_->GetRotation(); }
	if (rightFootObj_) { rightFootDefaultRot_ = rightFootObj_->GetRotation(); }
	if (leftFootObj_) { leftFootDefaultRot_ = leftFootObj_->GetRotation(); }

	initializedParts_ = true;
	ApplyPose(0.0f);

	// スピン初期化（開始角度を保存し、1回転分の目標角度を設定）
	if (spinEnabled_ && bodyObj_) {
		spinStartX_ = bodyObj_->GetRotation().x;
		spinTargetX_ = spinStartX_ + spinTotalRad_;
	}
}

void PlayerStateDash::Update(Player* player)
{
	if (!player) return;
	animTimer_ += 1.0f / 60.0f;
	float t = std::clamp(animTimer_ / animDuration_, 0.0f, 1.0f);
	ApplyPose(t);

	if (animTimer_ >= animDuration_)
	{
		player->ChangeState(std::make_unique<PlayerStateIdle>());
		return;
	}
}

void PlayerStateDash::Exit(Player* player)
{
	if (!initializedParts_) return;

	// 回転(Rotate)だけをデフォルトの姿勢に戻す（Mover が管理する Y は触らない）
	if (bodyObj_) {
		Transform* tf = bodyObj_->GetTransform();
		tf->rotate.x = 0.0f; tf->rotate.z = 0.0f; // Y は Mover が管理するため基本は触らない
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		bodyObj_->UpdateWorldMatrix();
	}
	if (headObj_) { Transform* tf = headObj_->GetTransform(); tf->rotate = headDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; headObj_->UpdateWorldMatrix(); }
	if (rightArmObj_) { Transform* tf = rightArmObj_->GetTransform(); tf->rotate = rightArmDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; rightArmObj_->UpdateWorldMatrix(); }
	if (leftArmObj_) { Transform* tf = leftArmObj_->GetTransform(); tf->rotate = leftArmDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; leftArmObj_->UpdateWorldMatrix(); }
	if (leftFootObj_) { Transform* tf = leftFootObj_->GetTransform(); tf->rotate = leftFootDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; leftFootObj_->UpdateWorldMatrix(); }
	if (rightFootObj_) { Transform* tf = rightFootObj_->GetTransform(); tf->rotate = rightFootDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; rightFootObj_->UpdateWorldMatrix(); }

	// スピンが有効なら、見た目の X を確実に開始角度に戻す（360度回って元の向きに揃える）
	if (player && spinEnabled_) {
		Vector3 r = player->GetRotation();
		r.x = spinStartX_;
		player->SetRotation(r);
	}
}

void PlayerStateDash::ApplyPose(float t)
{
	if (!initializedParts_) return;
	auto DegToRad = [](float d) { return d * 3.14159265358979323846f / 180.0f; };
	auto LerpVec3 = [](const Vector3& a, const Vector3& b, float t) {
		return Vector3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
		};
	auto EaseOutCubic = [](float x) { return 1.0f - std::pow(1.0f - x, 3.0f); };

	// 現在の移動向き（Mover が決定した Y）を取得
	float currentY = bodyObj_->GetRotation().y;

	// --- ポーズ定義 ---
	Vector3 bodyRot1 = { DegToRad(25.0f), currentY, 0.0f };
	Vector3 headRot1 = { DegToRad(-20.0f), 0.0f, 0.0f };
	Vector3 rtArmRot1 = { DegToRad(50.0f), DegToRad(10.0f), DegToRad(10.0f) };
	Vector3 ltArmRot1 = { DegToRad(50.0f), DegToRad(-10.0f), DegToRad(-10.0f) };
	Vector3 ltFootRot1 = { DegToRad(-20.0f), 0.0f, 0.0f };
	Vector3 rtFootRot1 = { DegToRad(20.0f), 0.0f, 0.0f };

	Vector3 bodyRot2 = { DegToRad(-5.0f), currentY, 0.0f };
	Vector3 headRot2 = { DegToRad(5.0f), 0.0f, 0.0f };
	Vector3 rtArmRot2 = { DegToRad(-10.0f), 0.0f, 0.0f };
	Vector3 ltArmRot2 = { DegToRad(-10.0f), 0.0f, 0.0f };
	Vector3 ltFootRot2 = { DegToRad(0.0f), 0.0f, 0.0f };
	Vector3 rtFootRot2 = { DegToRad(0.0f), 0.0f, 0.0f };

	Vector3 curBodyRot, curHeadRot, curRtArmRot, curLtArmRot, curLtFootRot, curRtFootRot;

	float t1 = 0.57f;
	if (t <= t1) {
		float localT = EaseOutCubic(t / t1);
		curBodyRot = LerpVec3(Vector3{ 0.0f, currentY, 0.0f }, bodyRot1, localT);
		curHeadRot = LerpVec3(headDefaultRot_, headRot1, localT);
		curRtArmRot = LerpVec3(rightArmDefaultRot_, rtArmRot1, localT);
		curLtArmRot = LerpVec3(leftArmDefaultRot_, ltArmRot1, localT);
		curLtFootRot = LerpVec3(leftFootDefaultRot_, ltFootRot1, localT);
		curRtFootRot = LerpVec3(rightFootDefaultRot_, rtFootRot1, localT);
	}
	else {
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
		Transform* tf = bodyObj_->GetTransform();

		// スピン処理（animTimer_ と animDuration_ に基づくイーズ）
		if (spinEnabled_) {
			float spinT = (animDuration_ > 1e-6f) ? std::clamp(animTimer_ / animDuration_, 0.0f, 1.0f) : 1.0f;
			float spinEase = EaseOutCubic(spinT);
			// 完全な 360° 回転を表現するため、正規化は使わず単純に加算
			float spinX = spinStartX_ + spinTotalRad_ * spinEase;
			curBodyRot.x = spinX;
		}
		else {
			// Mover が計算した X を上書きしない（通常は Mover は Y を管理）
			curBodyRot.x = bodyObj_->GetRotation().x;
		}

		tf->rotate = curBodyRot;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		bodyObj_->UpdateWorldMatrix();
	}

	if (headObj_) { Transform* tf = headObj_->GetTransform(); tf->rotate = curHeadRot; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; headObj_->UpdateWorldMatrix(); }
	if (rightArmObj_) { Transform* tf = rightArmObj_->GetTransform(); tf->rotate = curRtArmRot; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; rightArmObj_->UpdateWorldMatrix(); }
	if (leftArmObj_) { Transform* tf = leftArmObj_->GetTransform(); tf->rotate = curLtArmRot; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; leftArmObj_->UpdateWorldMatrix(); }
	if (leftFootObj_) { Transform* tf = leftFootObj_->GetTransform(); tf->rotate = curLtFootRot; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; leftFootObj_->UpdateWorldMatrix(); }
	if (rightFootObj_) { Transform* tf = rightFootObj_->GetTransform(); tf->rotate = curRtFootRot; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; rightFootObj_->UpdateWorldMatrix(); }
}

// ========================================================
// 死亡状態 (Dead - バタリ倒れバイオ4風) 実装
// ========================================================
void PlayerStateDead::Enter(Player* player)
{
	if (!player) return;
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
	if (bodyObj_) { bodyDefaultPos_ = bodyObj_->GetTransform()->translate; bodyDefaultRot_ = bodyObj_->GetRotation(); bodyStartRot_ = bodyObj_->GetTransform()->rotate; }
	if (headObj_) { Transform* tf = headObj_->GetTransform(); headDefaultPos_ = tf->translate; headDefaultRot_ = headObj_->GetRotation(); headStartRot_ = tf->rotate; }
	if (rightArmObj_) { Transform* tf = rightArmObj_->GetTransform(); rightArmDefaultPos_ = tf->translate; rightArmDefaultRot_ = rightArmObj_->GetRotation(); rtArmStartRot_ = tf->rotate; }
	if (leftArmObj_) { Transform* tf = leftArmObj_->GetTransform(); leftArmDefaultPos_ = tf->translate; leftArmDefaultRot_ = leftArmObj_->GetRotation(); ltArmStartRot_ = tf->rotate; }
	if (rightFootObj_) { Transform* tf = rightFootObj_->GetTransform(); rightFootDefaultPos_ = tf->translate; rightFootDefaultRot_ = rightFootObj_->GetRotation(); rtFootStartRot_ = tf->rotate; }
	if (leftFootObj_) { Transform* tf = leftFootObj_->GetTransform(); leftFootDefaultPos_ = tf->translate; leftFootDefaultRot_ = leftFootObj_->GetRotation(); ltFootStartRot_ = tf->rotate; }

	initializedParts_ = true;
	ApplyPose(0.0f);
}

void PlayerStateDead::Update(Player* player)
{
	if (!player) return;
	animTimer_ += 1.0f / 60.0f;

	// 3.5秒(animDuration_)で1.0になるように計算
	float t = std::clamp(animTimer_ / animDuration_, 0.0f, 1.0f);
	ApplyPose(t);


}

void PlayerStateDead::Exit(Player* player)
{
	// 復活処理がない限り呼ばれません
}

void PlayerStateDead::ApplyPose(float t)
{
	if (!initializedParts_) return;

	// --- 1. ヘルパー関数群（ラムダ式） ---
	// ★修正：計算式のコロンをスラッシュに直しました
	auto DegToRad = [](float d) { return d * 3.14159265358979323846f / 180.0f; };
	auto LerpVec3 = [](const Vector3& a, const Vector3& b, float t) {
		return Vector3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
		};
	auto EaseOutCubic = [](float x) { return 1.0f - std::pow(1.0f - x, 3.0f); };
	auto EaseInCubic = [](float x) { return x * x * x; };
	auto EaseInOutSine = [](float x) { return -(std::cos(3.14159265f * x) - 1.0f) / 2.0f; };

	float currentY = bodyObj_->GetRotation().y;

	// =========================================================
	// 2. 各キーフレーム（ポーズ）の定義
	// =========================================================

	// --- [Pose 1] 被弾のけぞり ---
	Vector3 bodyPos1 = { 0.0f, 0.0f, -0.2f };
	Vector3 bodyRot1 = { DegToRad(-15.0f), currentY, 0.0f };
	Vector3 headRot1 = headDefaultRot_ + Vector3{ DegToRad(-20.0f), 0.0f, 0.0f };
	Vector3 rtArmRot1 = rightArmDefaultRot_ + Vector3{ DegToRad(-20.0f), 0.0f, DegToRad(30.0f) };
	Vector3 ltArmRot1 = leftArmDefaultRot_ + Vector3{ DegToRad(-20.0f), 0.0f, DegToRad(-10.0f) };
	Vector3 ltFootRot1 = leftFootDefaultRot_ + Vector3{ DegToRad(10.0f), 0.0f, 0.0f };
	Vector3 rtFootRot1 = rightFootDefaultRot_ + Vector3{ DegToRad(10.0f), 0.0f, 0.0f };

	// --- [Pose 2] 地面に激突 ---
	Vector3 bodyPos2 = { 0.0f, -0.8f, 1.0f };
	Vector3 bodyRot2 = { DegToRad(75.0f), currentY, 0.0f };
	Vector3 headRot2 = headDefaultRot_ + Vector3{ DegToRad(-20.0f), DegToRad(45.0f), 0.0f };
	Vector3 rtArmRot2 = rightArmDefaultRot_ + Vector3{ DegToRad(10.0f), 0.0f, DegToRad(45.0f) };
	Vector3 ltArmRot2 = leftArmDefaultRot_ + Vector3{ DegToRad(10.0f), 0.0f, DegToRad(-15.0f) };
	Vector3 ltFootRot2 = leftFootDefaultRot_ + Vector3{ DegToRad(-10.0f), 0.0f, DegToRad(-15.0f) };
	Vector3 rtFootRot2 = rightFootDefaultRot_ + Vector3{ DegToRad(-10.0f), 0.0f, DegToRad(15.0f) };

	// --- [Pose 3] 完全な沈黙 (地面ガード用ポーズ) ---
	Vector3 bodyPos3 = { 0.0f, -1.0f, 1.2f };
	Vector3 bodyRot3 = { DegToRad(85.0f), currentY, 0.0f };
	Vector3 headRot3 = headDefaultRot_ + Vector3{ DegToRad(-40.0f), DegToRad(70.0f), 0.0f };
	Vector3 rtArmRot3 = rightArmDefaultRot_ + Vector3{ DegToRad(10.0f), 0.0f, DegToRad(40.0f) };
	Vector3 ltArmRot3 = leftArmDefaultRot_ + Vector3{ DegToRad(10.0f), 0.0f, DegToRad(-10.0f) };

	// =========================================================
	// 3. 補間計算
	// =========================================================
	Vector3 curBodyPos, curBodyRot, curHeadRot, curRtArmRot, curLtArmRot, curLtFootRot, curRtFootRot;
	float t1 = 0.15f; float t2 = 0.40f; float t3 = 0.85f;

	if (t <= t1) {
		float localT = EaseOutCubic(t / t1);
		curBodyPos = LerpVec3(Vector3{ 0,0,0 }, bodyPos1, localT); curBodyRot = LerpVec3(bodyStartRot_, bodyRot1, localT); curHeadRot = LerpVec3(headStartRot_, headRot1, localT);
		curRtArmRot = LerpVec3(rtArmStartRot_, rtArmRot1, localT); curLtArmRot = LerpVec3(ltArmStartRot_, ltArmRot1, localT);
		curLtFootRot = LerpVec3(ltFootStartRot_, ltFootRot1, localT); curRtFootRot = LerpVec3(rtFootStartRot_, rtFootRot1, localT);
	}
	else if (t <= t2) {
		float localT = EaseInCubic((t - t1) / (t2 - t1));
		curBodyPos = LerpVec3(bodyPos1, bodyPos2, localT); curBodyRot = LerpVec3(bodyRot1, bodyRot2, localT); curHeadRot = LerpVec3(headRot1, headRot2, localT);
		curRtArmRot = LerpVec3(rtArmRot1, rtArmRot2, localT); curLtArmRot = LerpVec3(ltArmRot1, ltArmRot2, localT);
		curLtFootRot = LerpVec3(ltFootRot1, ltFootRot2, localT); curRtFootRot = LerpVec3(rtFootRot1, rtFootRot2, localT);
	}
	else {
		float localT = EaseInOutSine(std::clamp((t - t2) / (t3 - t2), 0.0f, 1.0f));
		curBodyPos = LerpVec3(bodyPos2, bodyPos3, localT); curBodyRot = LerpVec3(bodyRot2, bodyRot3, localT); curHeadRot = LerpVec3(headRot2, headRot3, localT);
		curRtArmRot = LerpVec3(rtArmRot2, rtArmRot3, localT); curLtArmRot = LerpVec3(ltArmRot2, ltArmRot3, localT);
		curLtFootRot = ltFootRot2; curRtFootRot = rtFootRot2;
	}

	// =========================================================
	// 4. 行列適用（プレイヤー本体：地面ガード実装）
	// =========================================================
	if (bodyObj_) {
		Transform* tf = bodyObj_->GetTransform();
		float engineY = tf->translate.y; // 物理重力による現在地

		float s = std::sin(currentY); float c = std::cos(currentY);
		Vector3 worldOffset;
		worldOffset.x = curBodyPos.x * c + curBodyPos.z * s;
		worldOffset.y = curBodyPos.y;
		worldOffset.z = -curBodyPos.x * s + curBodyPos.z * c;

		tf->translate.x = bodyDefaultPos_.x + worldOffset.x;
		tf->translate.z = bodyDefaultPos_.z + worldOffset.z;

		// ★地面ガード：足や体のめり込みを防ぐ最小高度
		const float groundLevel = 0.55f;
		tf->translate.y = (std::max)(groundLevel, engineY + worldOffset.y);

		tf->rotate = curBodyRot;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		bodyObj_->UpdateWorldMatrix();
	}

	// パーツの更新
	if (headObj_) { Transform* tf = headObj_->GetTransform(); tf->rotate = curHeadRot; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; headObj_->UpdateWorldMatrix(); }
	if (rightArmObj_) { Transform* tf = rightArmObj_->GetTransform(); tf->rotate = curRtArmRot; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; rightArmObj_->UpdateWorldMatrix(); }
	if (leftArmObj_) { Transform* tf = leftArmObj_->GetTransform(); tf->rotate = curLtArmRot; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; leftArmObj_->UpdateWorldMatrix(); }
	if (leftFootObj_) { Transform* tf = leftFootObj_->GetTransform(); tf->rotate = curLtFootRot; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; leftFootObj_->UpdateWorldMatrix(); }
	if (rightFootObj_) { Transform* tf = rightFootObj_->GetTransform(); tf->rotate = curRtFootRot; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; rightFootObj_->UpdateWorldMatrix(); }

	// =========================================================
	// 5. 剣の「スピン＆スティック」物理演出
	// =========================================================
	if (swordObj_) {
		Transform* stf = swordObj_->GetTransform();

		if (t >= 0.03f && !isSwordDropped_) {
			isSwordDropped_ = true;
			dropStartTime_ = animTimer_;

			Matrix4x4 wMat = swordObj_->GetWorldMatrix();
			swordDropPos_ = { wMat.m[3][0], wMat.m[3][1], wMat.m[3][2] };
			swordDropRot_ = Math::MatrixToEuler(wMat);
			swordDropScale_.x = Math::Length(Vector3{ wMat.m[0][0], wMat.m[0][1], wMat.m[0][2] });
			swordDropScale_.y = Math::Length(Vector3{ wMat.m[1][0], wMat.m[1][1], wMat.m[1][2] });
			swordDropScale_.z = Math::Length(Vector3{ wMat.m[2][0], wMat.m[2][1], wMat.m[2][2] });

			float s = std::sin(currentY); float c = std::cos(currentY);
			Vector3 localVel = { 3.0f, 6.5f, -2.5f }; // 少し勢いを強化
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
			currentPos.y = swordDropPos_.y + (swordVelocity_.y * elapsed) - (0.5f * gravity * elapsed * elapsed);
			currentPos.z = swordDropPos_.z + swordVelocity_.z * elapsed;

			const float swordGroundY = 0.22f; // 突き刺さる高さ
			if (currentPos.y <= swordGroundY) {
				currentPos.y = swordGroundY;
				isSwordStuck_ = true;
			}
			stf->translate = currentPos;

			if (!isSwordStuck_) {
				stf->rotate.x = swordDropRot_.x + swordSpinSpeed_ * elapsed * 2.5f;
				stf->rotate.z = swordDropRot_.z + swordSpinSpeed_ * elapsed;
			}
			else {
				stf->rotate.x = DegToRad(115.0f); // 刺さった角度
			}
			stf->quaternion = Math::EulerToQuaternion(stf->rotate);
			stf->isQuaternionMaster = true;
			swordObj_->UpdateWorldMatrix();
		}
	}
}

// ========================================================
// 落下攻撃状態 (Plunge Attack) 実装
// ========================================================
void PlayerStatePlungeAttack::Enter(Player* player)
{
	if (!player) return;
	DebugConsole::GetInstance()->AddLog("★ ENTER: Plunge Attack (Genshin Greatsword Style)");

	SetSwordActive(player, true);
	isPlunging_ = false;
	isLanded_ = false;
	recoveryTimer_ = 0.0f;
	bodyObj_ = player;

	TryFindHead(player, headObj_); TryFindArms(player, leftArmObj_, rightArmObj_); TryFindFeet(player, leftFootObj_, rightFootObj_);

	//  剣を探してデフォルト角度と位置を保存
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
	if (bodyObj_) bodyDefaultPos_ = bodyObj_->GetTransform()->translate;
	if (bodyObj_) bodyDefaultRot_ = bodyObj_->GetRotation();
	if (headObj_) headDefaultRot_ = headObj_->GetRotation();
	if (rightArmObj_) rightArmDefaultRot_ = rightArmObj_->GetRotation();
	if (leftArmObj_) leftArmDefaultRot_ = leftArmObj_->GetRotation();
	if (rightFootObj_) rightFootDefaultRot_ = rightFootObj_->GetRotation();
	if (leftFootObj_) leftFootDefaultRot_ = leftFootObj_->GetRotation();

	// --- 重要: 足の当たり属性を確実に有効化（安全側） ---
	if (leftFootObj_)  leftFootObj_->SetCollisionAttribute(kPlayer);
	if (rightFootObj_) rightFootObj_->SetCollisionAttribute(kPlayer);

	initializedParts_ = true;

	Vector3 vel = player->GetVelocity();
	vel.y = 5.0f;
	vel.x = 0.0f; vel.z = 0.0f;
	player->SetVelocity(vel);

	ApplyPose(player);
}

void PlayerStatePlungeAttack::Update(Player* player)
{
	if (!player) return;

	if (!isLanded_) {
		Vector3 vel = player->GetVelocity();

		if (!isPlunging_) {
			// ホップが終わり、落ち始めたら猛スピードで落下
			if (vel.y <= 0.0f) {
				vel.y = -40.0f; // 爆速落下
				player->SetVelocity(vel);
				isPlunging_ = true;
			}
		}
		else {
			// ★着地判定：猛スピード(-40)だったのが、床にぶつかって速度が0に近づいたら着地！
			if (vel.y > -5.0f) {
				isLanded_ = true;
				DebugConsole::GetInstance()->AddLog("Plunge Attack: LANDED! (DOOOM!)");
			}
		}
	}
	else {
		// 着地後の立ち上がり硬直
		recoveryTimer_ += 1.0f / 60.0f;
		if (recoveryTimer_ >= recoveryDuration_) {
			player->ChangeState(std::make_unique<PlayerStateIdle>());
			return;
		}
	}

	ApplyPose(player);
}

void PlayerStatePlungeAttack::Exit(Player* player)
{
	if (!player) return;
	SetSwordActive(player, false);

	// 初期化されていなければ復帰処理を行わない
	if (!initializedParts_) return;

	// 体（プレイヤー本体）の位置: Y は物理(エンジン)管理を維持するため上書きしない
	if (bodyObj_) {
		Transform* tf = bodyObj_->GetTransform();
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
		rightFootObj_->UpdateLocalMatrix();
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

	// 剣は親状態に応じて正しく復帰させる（Unparentedだったらプレイヤーに再アタッチしてローカル座標を戻す、
	// 親が存在する／再アタッチ不可な場合はワールド座標で復帰）
	if (swordObj_) {
		Transform* stf = swordObj_->GetTransform();

		// 保存してあるデフォルトは Enter 時に取ったローカルとワールドの両方
		// drop により親が外れている (GetParent() == nullptr) 場合は
		// - 可能なら bodyObj_ に再アタッチしてローカル位置を復元する（手元に戻す）
		// - bodyObj_ が無ければワールド座標を復元する
		if (swordObj_->GetParent() == nullptr) {
			if (bodyObj_) {
				// 再アタッチする場合は、"保存したワールド座標/回転" を親の逆行列でローカル行列に変換してから適用する。
				// これにより、親が変化してもワールド空間で期待される位置回転を保ちつつローカルに正しく戻せる。
				// 1) 期待するワールド行列を作る（保存したワールド位置/回転、現在のスケールを使用）
				Matrix4x4 desiredWorld = Math::MakeAffineMatrix(stf->scale, swordDefaultWorldRot_, swordDefaultWorldPos_);
				// 2) 親のワールド行列の逆を掛けてローカル行列を得る
				Matrix4x4 parentWorld = bodyObj_->GetWorldMatrix();
				Matrix4x4 invParent = Math::Inverse(parentWorld);
				Matrix4x4 localMat = Math::Multiply(desiredWorld, invParent);
				// 3) localMat から位置・回転・スケールを抜き出して Transform に設定
				Vector3 localPos = { localMat.m[3][0], localMat.m[3][1], localMat.m[3][2] };
				Vector3 localRot = Math::MatrixToEuler(localMat);
				// スケールは列長で復元（必要なら）
				Vector3 localScale = { Math::Length(Vector3{ localMat.m[0][0], localMat.m[0][1], localMat.m[0][2] }),
									   Math::Length(Vector3{ localMat.m[1][0], localMat.m[1][1], localMat.m[1][2] }),
									   Math::Length(Vector3{ localMat.m[2][0], localMat.m[2][1], localMat.m[2][2] }) };

				// 先に Transform のローカル値をセット（SetParent の中で UpdateWorldMatrix が走るため）
				stf->translate = localPos;
				stf->rotate = localRot;
				stf->scale = localScale;
				stf->quaternion = Math::EulerToQuaternion(stf->rotate);
				stf->isQuaternionMaster = true;

				// 再アタッチ -> 内部で UpdateWorldMatrix が呼ばれるはずだが、確実に更新する
				swordObj_->SetParent(bodyObj_);
				swordObj_->UpdateLocalMatrix();
				swordObj_->UpdateWorldMatrix();
			}
			else {
				// 親がなく、再アタッチ先も無い場合はワールド位置で復帰
				stf->translate = swordDefaultWorldPos_;
				stf->rotate = swordDefaultWorldRot_;
				// スケールは現状維持（または保存してあれば使う）
				stf->quaternion = Math::EulerToQuaternion(stf->rotate);
				stf->isQuaternionMaster = true;
				// UpdateWorldMatrix は親が nullptr のときローカル＝ワールドとして扱われる
				swordObj_->UpdateWorldMatrix();
			}
		}
		else {
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

void PlayerStatePlungeAttack::ApplyPose(Player* player)
{
	if (!initializedParts_) return;
	auto DegToRad = [](float d) { return d * 3.14159265358979323846f / 180.0f; };
	auto LerpVec3 = [](const Vector3& a, const Vector3& b, float t) {
		return Vector3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
		};
	auto EaseOutCubic = [](float x) { return 1.0f - std::pow(1.0f - x, 3.0f); };

	float currentY = bodyObj_->GetRotation().y;
	Vector3 curBodyPos = { 0,0,0 }, curBodyRot, curHeadPos{ 0,0,0 }, curHeadRot, curRtArmPos{ 0,0,0 }, curRtArmRot, curLtArmPos{ 0,0,0 }, curLtArmRot, curLtFootPos{ 0,0,0 }, curLtFootRot, curRtFootPos{ 0,0,0 }, curRtFootRot;

	// --- [Pose] 定義（省略せず既存の値をそのまま使う） ---
	Vector3 fallBodyPos = { 0.0f, 0.0f, 0.0f };
	Vector3 fallBodyRot = { DegToRad(0.0f), currentY, DegToRad(0.0f) };

	Vector3 fallHeadPos = { 0.0f, 0.0f, 0.0f };
	Vector3 fallHeadRot = { DegToRad(15.0f), DegToRad(0.0f), DegToRad(0.0f) };

	Vector3 fallRtArmPos = { 0.0f, 0.0f, 0.0f };
	Vector3 fallRtArmRot = { DegToRad(-90.0f), DegToRad(-65.0f), DegToRad(0.0f) };

	Vector3 fallLtArmPos = { 0.0f, 0.0f, 0.0f };
	Vector3 fallLtArmRot = { DegToRad(-90.0f), DegToRad(65.0f), DegToRad(0.0f) };

	Vector3 fallRtFootPos = { 0.0f, 0.0f, 0.0f };
	Vector3 fallRtFootRot = { DegToRad(0.0f), DegToRad(0.0f), DegToRad(0.0f) };

	Vector3 fallLtFootPos = { 0.0f, 0.0f, 0.0f };
	Vector3 fallLtFootRot = { DegToRad(0.0f), DegToRad(0.0f), DegToRad(0.0f) };

	Vector3 landBodyPos = { 0.0f, -0.65f, 0.3f };
	Vector3 landBodyRot = { DegToRad(0.0f), currentY, 0.0f };
	Vector3 landHeadRot = headDefaultRot_ + Vector3{ DegToRad(-20.0f), 0.0f, 0.0f };

	Vector3 landRtArmRot = rightArmDefaultRot_ + Vector3{ DegToRad(20.0f), 0.0f, 0.0f };
	Vector3 landLtArmRot = leftArmDefaultRot_ + Vector3{ DegToRad(20.0f), 0.0f, 0.0f };

	Vector3 landRtFootRot = rightFootDefaultRot_ + Vector3{ DegToRad(-60.0f), 0.0f, DegToRad(20.0f) };
	Vector3 landLtFootRot = leftFootDefaultRot_ + Vector3{ DegToRad(-60.0f), 0.0f, DegToRad(-20.0f) };

	// --- 状態に応じた選択 ---
	if (!isLanded_) {
		curBodyPos = fallBodyPos; curBodyRot = fallBodyRot;
		curHeadPos = fallHeadPos; curHeadRot = fallHeadRot;
		curRtArmPos = fallRtArmPos; curRtArmRot = fallRtArmRot;
		curLtArmPos = fallLtArmPos; curLtArmRot = fallLtArmRot;
		curLtFootPos = fallLtFootPos; curLtFootRot = fallLtFootRot;
		curRtFootPos = fallRtFootPos; curRtFootRot = fallRtFootRot;
	}
	else {
		curBodyPos = landBodyPos; curBodyRot = landBodyRot;
		curHeadPos = fallHeadPos; curHeadRot = fallHeadRot;
		curRtArmPos = fallRtArmPos; curRtArmRot = fallRtArmRot;
		curLtArmPos = fallLtArmPos; curLtArmRot = fallLtArmRot;
		curLtFootPos = leftFootDefaultPos_; curLtFootRot = leftFootDefaultRot_;
		curRtFootPos = rightFootDefaultPos_; curRtFootRot = rightFootDefaultRot_;
		curLtFootRot.x = 0.0f; curRtFootRot.x = 0.0f;
	}

	// --- 本体適用（Yは物理優先で最低高さを守る） ---
	if (bodyObj_) {
		Transform* tf = bodyObj_->GetTransform();
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
		Transform* tf = headObj_->GetTransform();
		tf->translate = headDefaultPos_ + curHeadPos;
		tf->rotate = curHeadRot;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		headObj_->UpdateWorldMatrix();
	}
	if (rightArmObj_) {
		Transform* tf = rightArmObj_->GetTransform();
		tf->translate = curRtArmPos;
		tf->rotate = curRtArmRot;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		rightArmObj_->UpdateWorldMatrix();
	}
	if (leftArmObj_) {
		Transform* tf = leftArmObj_->GetTransform();
		tf->translate = curLtArmPos;
		tf->rotate = curLtArmRot;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		leftArmObj_->UpdateLocalMatrix();
		leftArmObj_->UpdateWorldMatrix();
	}

	// --- 足: ローカル更新→ワールド更新（コライダーに反映） ---
	if (leftFootObj_) {
		Transform* tf = leftFootObj_->GetTransform();
		tf->translate = curLtFootPos;
		tf->rotate = curLtFootRot;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		leftFootObj_->UpdateLocalMatrix();
		leftFootObj_->UpdateWorldMatrix();
		// 着地時は衝突属性を確実に戻す（対策）
		if (isLanded_) leftFootObj_->SetCollisionAttribute(kPlayer);
	}
	if (rightFootObj_) {
		Transform* tf = rightFootObj_->GetTransform();
		tf->translate = curRtFootPos;
		tf->rotate = curRtFootRot;
		tf->quaternion = Math::EulerToQuaternion(tf->rotate);
		tf->isQuaternionMaster = true;
		rightFootObj_->UpdateLocalMatrix();
		rightFootObj_->UpdateWorldMatrix();
		if (isLanded_) rightFootObj_->SetCollisionAttribute(kPlayer);
	}

	// --- 追加補正: 着地時、足のワールドAABBを基準に body の Y を微調整して足が地面に埋まらないようにする ---
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
				Transform* btf = bodyObj_->GetTransform();
				// 足が groundLevel より下にある分だけ本体を持ち上げる
				btf->translate.y += (groundLevel - minFootY);
				bodyObj_->UpdateWorldMatrix();
			}
		}
	}

	// --- 剣処理（既存） ---
	if (swordObj_) {
		Transform* stf = swordObj_->GetTransform();
		Vector3 plungeSwordLocalPos = { 0.45f, -0.14f, 0.0f };
		Vector3 plungeSwordLocalRot = { DegToRad(30.0f), DegToRad(-90.0f), DegToRad(-180.0f) };

		if (!isLanded_) {
			stf->translate = plungeSwordLocalPos;
			stf->rotate = plungeSwordLocalRot;
		}
		else {
			if (recoveryTimer_ < recoveryDuration_) {
				stf->translate = plungeSwordLocalPos;
				stf->rotate = plungeSwordLocalRot;
			}
			else {
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