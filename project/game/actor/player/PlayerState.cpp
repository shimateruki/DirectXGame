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
    if (headObj_) { headDefaultRot_ = headObj_->GetRotation(); headStartRot_ = headDefaultRot_; headSaved_ = true; }

    animTimer_ = 0.0f;
    footStage_ = 0;
}

void PlayerStateIdle::Update(Player* player)
{
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
    if (!headObj_) { TryFindHead(player, headObj_); if (headObj_ && !headSaved_) { headDefaultRot_ = headObj_->GetRotation(); headStartRot_ = headDefaultRot_; headSaved_ = true; } }
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
    DebugConsole::GetInstance()->AddLog("★ ENTER: Run State (custom procedural pose)");

    bodyObj_ = player; bodySaved_ = false;
    headObj_ = nullptr; rightArmObj_ = nullptr; leftArmObj_ = nullptr; rightFootObj_ = nullptr; leftFootObj_ = nullptr;
    rightArmSaved_ = leftArmSaved_ = rightFootSaved_ = leftFootSaved_ = headSaved_ = false;

    animTimer_ = 0.0f;

    TryFindArms(player, leftArmObj_, rightArmObj_);
    TryFindFeet(player, leftFootObj_, rightFootObj_);
    TryFindHead(player, headObj_);

    if (bodyObj_) { Transform* tf = bodyObj_->GetTransform(); bodyDefaultPos_ = tf->translate; bodyDefaultRot_ = tf->rotate; bodySaved_ = true; }
    if (headObj_) { headDefaultPos_ = headObj_->GetTransform()->translate; headDefaultRot_ = headObj_->GetRotation(); headStartRot_ = headDefaultRot_; headSaved_ = true; }
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