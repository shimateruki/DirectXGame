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
	if (leftFootObj_) { leftFootDefaultRot_ = leftFootObj_->GetRotation(); leftFootStartRot_ = leftFootDefaultRot_; leftFootSaved_ = true; }
	if (rightFootObj_) { rightFootDefaultRot_ = rightFootObj_->GetRotation(); rightFootStartRot_ = rightFootDefaultRot_; rightFootSaved_ = true; }

	TryFindArms(player, leftArmObj_, rightArmObj_);
	if (leftArmObj_) { leftArmDefaultRot_ = leftArmObj_->GetRotation(); leftArmStartRot_ = leftArmDefaultRot_; leftArmSaved_ = true; }
	if (rightArmObj_) { rightArmDefaultRot_ = rightArmObj_->GetRotation(); rightArmStartRot_ = rightArmDefaultRot_; rightArmSaved_ = true; }

	TryFindSword(player, swordObj_);
	if (swordObj_) { swordDefaultLocalPos_ = swordObj_->GetTransform()->translate; swordDefaultWorldPos_ = swordObj_->GetWorldPosition(); swordSaved_ = true; }

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
	// 攻撃入力: Kキーで攻撃（左クリックは無効化）
	if (player->GetInputManager()->IsKeyTriggered(DIK_K))
	{
		// pending フラグがセットされていれば 2 段目を出す（フラグは消費される）
		if (player->ConsumePendingAttack2())
		{
			player->ChangeState(std::make_unique<PlayerStateAttack2>());
		}
		else
		{
			player->ChangeState(std::make_unique<PlayerStateAttack1>());
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
	if (!leftFootObj_ || !rightFootObj_) { TryFindFeet(player, leftFootObj_, rightFootObj_); if (leftFootObj_ && !leftFootSaved_) { leftFootDefaultRot_ = leftFootObj_->GetRotation(); leftFootStartRot_ = leftFootDefaultRot_; leftFootSaved_ = true; } if (rightFootObj_ && !rightFootSaved_) { rightFootDefaultRot_ = rightFootObj_->GetRotation(); rightFootStartRot_ = rightFootDefaultRot_; rightFootSaved_ = true; } }
	if (!leftArmObj_ || !rightArmObj_) { TryFindArms(player, leftArmObj_, rightArmObj_); if (leftArmObj_ && !leftArmSaved_) { leftArmDefaultRot_ = leftArmObj_->GetRotation(); leftArmStartRot_ = leftArmDefaultRot_; leftArmSaved_ = true; } if (rightArmObj_ && !rightArmSaved_) { rightArmDefaultRot_ = rightArmObj_->GetRotation(); rightArmStartRot_ = rightArmDefaultRot_; rightArmSaved_ = true; } }
	if (!swordObj_) { TryFindSword(player, swordObj_); if (swordObj_ && !swordSaved_) { swordDefaultLocalPos_ = swordObj_->GetTransform()->translate; swordDefaultWorldPos_ = swordObj_->GetWorldPosition(); swordSaved_ = true; } }
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
	if (leftFootObj_) { Transform* tf = leftFootObj_->GetTransform(); tf->rotate = leftFootDefaultRot_; tf->quaternion = Math::EulerToQuaternion(leftFootDefaultRot_); tf->isQuaternionMaster = true; leftFootObj_->UpdateWorldMatrix(); }
	if (rightFootObj_) { Transform* tf = rightFootObj_->GetTransform(); tf->rotate = rightFootDefaultRot_; tf->quaternion = Math::EulerToQuaternion(rightFootDefaultRot_); tf->isQuaternionMaster = true; rightFootObj_->UpdateWorldMatrix(); }
	if (leftArmObj_) { Transform* tf = leftArmObj_->GetTransform(); tf->rotate = leftArmDefaultRot_; tf->quaternion = Math::EulerToQuaternion(leftArmDefaultRot_); tf->isQuaternionMaster = true; leftArmObj_->UpdateWorldMatrix(); }
	if (rightArmObj_) { Transform* tf = rightArmObj_->GetTransform(); tf->rotate = rightArmDefaultRot_; tf->quaternion = Math::EulerToQuaternion(rightArmDefaultRot_); tf->isQuaternionMaster = true; rightArmObj_->UpdateWorldMatrix(); }
	if (headObj_ && headSaved_) { Transform* tf = headObj_->GetTransform(); tf->rotate = headDefaultRot_; tf->quaternion = Math::EulerToQuaternion(headDefaultRot_); tf->isQuaternionMaster = true; headObj_->UpdateWorldMatrix(); }
	if (swordObj_ && swordSaved_) { Transform* tf = swordObj_->GetTransform(); tf->translate = swordDefaultLocalPos_; swordObj_->UpdateLocalMatrix(); swordObj_->UpdateWorldMatrix(); }

	// ブレンド解除（念のため）
	s_bodyBlendActive = false;
}

void PlayerStateIdle::ApplyPostUpdate(Player* player, float deltaTime)
{
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
		Transform* tf = leftFootObj_->GetTransform(); tf->quaternion = Math::EulerToQuaternion(final); tf->isQuaternionMaster = true; leftFootObj_->UpdateWorldMatrix();
	}
	if (rightFootObj_ && rightFootSaved_)
	{
		Vector3 targetR = rightFootDefaultRot_; targetR.x = rightFootDefaultRot_.x + targetAngle * e;
		Vector3 final = LerpVec(rightFootStartRot_, targetR, blendEase);
		Transform* tf = rightFootObj_->GetTransform(); tf->quaternion = Math::EulerToQuaternion(final); tf->isQuaternionMaster = true; rightFootObj_->UpdateWorldMatrix();
	}

	if (leftArmObj_ && leftArmSaved_)
	{
		Vector3 targetR = leftArmDefaultRot_; targetR.z = leftArmDefaultRot_.z + armZLeftRad * e;
		Vector3 final = LerpVec(leftArmStartRot_, targetR, blendEase);
		Transform* tf = leftArmObj_->GetTransform(); tf->quaternion = Math::EulerToQuaternion(final); tf->isQuaternionMaster = true; leftArmObj_->UpdateWorldMatrix();
	}
	if (rightArmObj_ && rightArmSaved_)
	{
		Vector3 targetR = rightArmDefaultRot_; targetR.z = rightArmDefaultRot_.z + armZRightRad * e;
		Vector3 final = LerpVec(rightArmStartRot_, targetR, blendEase);
		Transform* tf = rightArmObj_->GetTransform(); tf->quaternion = Math::EulerToQuaternion(final); tf->isQuaternionMaster = true; rightArmObj_->UpdateWorldMatrix();
	}

	if (swordObj_ && swordSaved_)
	{
		Transform* tf = swordObj_->GetTransform(); tf->translate = swordDefaultLocalPos_; swordObj_->UpdateLocalMatrix(); swordObj_->UpdateWorldMatrix();
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
	SetSwordActive(player, false);
	DebugConsole::GetInstance()->AddLog("★ ENTER: Run State (custom procedural pose)");

	bodyObj_ = player; bodySaved_ = false;
	headObj_ = nullptr; rightArmObj_ = nullptr; leftArmObj_ = nullptr; rightFootObj_ = nullptr; leftFootObj_ = nullptr;
	rightArmSaved_ = leftArmSaved_ = rightFootSaved_ = leftFootSaved_ = headSaved_ = false;

	animTimer_ = 0.0f;

	TryFindArms(player, leftArmObj_, rightArmObj_);
	TryFindFeet(player, leftFootObj_, rightFootObj_);
	TryFindHead(player, headObj_);

	if (bodyObj_) { Transform* tf = bodyObj_->GetTransform(); bodyDefaultPos_ = tf->translate; bodyDefaultRot_ = tf->rotate; bodySaved_ = true; }
	if (headObj_) {
		Transform* htf = headObj_->GetTransform();
		headDefaultPos_ = htf->translate;
		headDefaultRot_ = headObj_->GetRotation();
		headStartRot_ = htf->rotate;
		headSaved_ = true;
	}
	
	if (rightArmObj_) { rightArmDefaultPos_ = rightArmObj_->GetTransform()->translate; rightArmDefaultRot_ = rightArmObj_->GetRotation(); rightArmStartRot_ = rightArmDefaultRot_; rightArmSaved_ = true; }
	
	if (leftArmObj_) { leftArmDefaultPos_ = leftArmObj_->GetTransform()->translate; leftArmDefaultRot_ = leftArmObj_->GetRotation(); leftArmStartRot_ = leftArmDefaultRot_; leftArmSaved_ = true; }
	
	if (rightFootObj_) { rightFootDefaultPos_ = rightFootObj_->GetTransform()->translate; rightFootDefaultRot_ = rightFootObj_->GetRotation(); rightFootStartRot_ = rightFootDefaultRot_; rightFootSaved_ = true; }
	
	if (leftFootObj_) { leftFootDefaultPos_ = leftFootObj_->GetTransform()->translate; leftFootDefaultRot_ = leftFootObj_->GetRotation(); leftFootStartRot_ = leftFootDefaultRot_; leftFootSaved_ = true; }

	// ブレンド初期化
	blendTimer_ = 0.0f;

	auto DegToRad = [](float d) { return d * 3.14159265358979323846f / 180.0f; };

	// 体の前傾と腕脚の初期姿勢は即時適用してもよい（Enter 時の姿勢）
	if (bodyObj_) { Transform* tf = bodyObj_->GetTransform(); tf->rotate.x = DegToRad(10.0f); tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; bodyObj_->UpdateWorldMatrix(); }
	// 頭は走り中は常に -10deg にする（待機の頭振りを使わない）
	if (headObj_) { Transform* tf = headObj_->GetTransform(); tf->rotate.x = DegToRad(-10.0f); tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; headObj_->UpdateWorldMatrix(); }
	if (rightArmObj_) { Transform* tf = rightArmObj_->GetTransform(); tf->rotate.x = DegToRad(-10.0f); tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; rightArmObj_->UpdateWorldMatrix(); }
	if (leftArmObj_) { Transform* tf = leftArmObj_->GetTransform(); tf->rotate.x = DegToRad(-10.0f); tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; leftArmObj_->UpdateWorldMatrix(); }
	if (rightFootObj_) { Transform* tf = rightFootObj_->GetTransform(); tf->rotate.x = DegToRad(-10.0f); tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; rightFootObj_->UpdateWorldMatrix(); }
	if (leftFootObj_) { Transform* tf = leftFootObj_->GetTransform(); tf->rotate.x = DegToRad(-10.0f); tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; leftFootObj_->UpdateWorldMatrix(); }
}

void PlayerStateRun::Update(Player* player)
{
	// 攻撃入力: Kキーで攻撃（左クリックは無効化）
	if (player->GetInputManager()->IsKeyTriggered(DIK_K))
	{
		player->ChangeState(std::make_unique<PlayerStateAttack1>());
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
	animTimer_ += 1.0f / 60.0f;

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

	if (bodyObj_) { Transform* tf = bodyObj_->GetTransform(); tf->translate = bodyDefaultPos_; tf->rotate = bodyDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; bodyObj_->UpdateWorldMatrix(); }
	if (headObj_) { Transform* tf = headObj_->GetTransform(); tf->translate = headDefaultPos_; tf->rotate = headDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; headObj_->UpdateWorldMatrix(); }
	if (rightArmObj_) { Transform* tf = rightArmObj_->GetTransform(); tf->translate = rightArmDefaultPos_; tf->rotate = rightArmDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; rightArmObj_->UpdateWorldMatrix(); }
	if (leftArmObj_) { Transform* tf = leftArmObj_->GetTransform(); tf->translate = leftArmDefaultPos_; tf->rotate = leftArmDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; leftArmObj_->UpdateWorldMatrix(); }
	if (rightFootObj_) { Transform* tf = rightFootObj_->GetTransform(); tf->translate = rightFootDefaultPos_; tf->rotate = rightFootDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; rightFootObj_->UpdateWorldMatrix(); }
	if (leftFootObj_) { Transform* tf = leftFootObj_->GetTransform(); tf->translate = leftFootDefaultPos_; tf->rotate = leftFootDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; leftFootObj_->UpdateWorldMatrix(); }
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
	animTimer_ += 1.0f / 60.0f;
	float t = std::clamp(animTimer_ / animDuration_, 0.0f, 1.0f);
	float et = EaseInOutSine(t);
	ApplyPose(et);

	if (animTimer_ >= animDuration_)
	{
		player->ChangeState(std::make_unique<PlayerStateIdle>());
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
	if (bodyObj_) { Transform* tf = bodyObj_->GetTransform(); tf->translate = bodyDefaultPos_; tf->rotate = bodyDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; bodyObj_->UpdateWorldMatrix(); }
	if (headObj_) { Transform* tf = headObj_->GetTransform(); tf->translate = headDefaultPos_; tf->rotate = headDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; headObj_->UpdateWorldMatrix(); }
	if (rightArmObj_) { Transform* tf = rightArmObj_->GetTransform(); tf->translate = rightArmDefaultPos_; tf->rotate = rightArmDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; rightArmObj_->UpdateLocalMatrix(); rightArmObj_->UpdateWorldMatrix(); }
	if (leftArmObj_) { Transform* tf = leftArmObj_->GetTransform(); tf->translate = leftArmDefaultPos_; tf->rotate = leftArmDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; leftArmObj_->UpdateLocalMatrix(); leftArmObj_->UpdateWorldMatrix(); }
	if (rightFootObj_) { Transform* tf = rightFootObj_->GetTransform(); tf->translate = rightFootDefaultPos_; tf->rotate = rightFootDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; rightFootObj_->UpdateWorldMatrix(); }
	if (leftFootObj_) { Transform* tf = leftFootObj_->GetTransform(); tf->translate = leftFootDefaultPos_; tf->rotate = leftFootDefaultRot_; tf->quaternion = Math::EulerToQuaternion(tf->rotate); tf->isQuaternionMaster = true; leftFootObj_->UpdateLocalMatrix(); leftFootObj_->UpdateWorldMatrix(); }
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
	Vector3 bodyStartRot = bodyObj_ ? bodyObj_->GetTransform()->rotate : Vector3{0,0,0};

	// Head start: Enterで設定済み
	Vector3 headStartPos{ 0.0f, 0.0f, 0.0f };
	Vector3 headStartRot = headObj_ ? headObj_->GetTransform()->rotate : Vector3{0,0,0};

	// Right arm start: Enterで設定済み
	Vector3 rtArmStartPos = rightArmObj_ ? rightArmObj_->GetTransform()->translate : Vector3{0,0,0};
	Vector3 rtArmStartRot = rightArmObj_ ? rightArmObj_->GetTransform()->rotate : Vector3{0,0,0};

	// Left arm start: Enter で設定した Attack1 終点相当
	Vector3 ltArmStartPos = leftArmObj_ ? leftArmObj_->GetTransform()->translate : Vector3{0,0,0};
	Vector3 ltArmStartRot = leftArmObj_ ? leftArmObj_->GetTransform()->rotate : Vector3{0,0,0};

	// Right foot start
	Vector3 rtFootStartPos = rightFootObj_ ? rightFootObj_->GetTransform()->translate : Vector3{0,0,0};
	Vector3 rtFootStartRot = rightFootObj_ ? rightFootObj_->GetTransform()->rotate : Vector3{0,0,0};

	// Left foot start
	Vector3 ltFootStartPos = leftFootObj_ ? leftFootObj_->GetTransform()->translate : Vector3{0,0,0};
	Vector3 ltFootStartRot = leftFootObj_ ? leftFootObj_->GetTransform()->rotate : Vector3{0,0,0};

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