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
static std::string ToLower(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (unsigned char c : s) out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

// ========================================================
// 再帰検索: 指定ルート配下から左右の足パーツを探す
// ========================================================
static void FindFeetRecursive(Object3d* node, Object3d*& leftOut, Object3d*& rightOut) {
    if (!node) return;
    const auto& children = node->GetChildren();
    for (Object3d* child : children) {
        if (!child) continue;
        std::string name = ToLower(child->GetName());

        bool hasFoot = (name.find("foot") != std::string::npos);
        bool hasRight = (name.find("right") != std::string::npos) || (name.find("_r") != std::string::npos);
        bool hasLeft  = (name.find("left")  != std::string::npos) || (name.find("_l") != std::string::npos);

        if (hasFoot) {
            if (hasRight && !rightOut) {
                rightOut = child;
            } else if (hasLeft && !leftOut) {
                leftOut = child;
            } else {
                if (!leftOut) leftOut = child;
                else if (!rightOut) rightOut = child;
            }
        }

        if (leftOut && rightOut) return;

        // 再帰探索（子の子にいる可能性）
        FindFeetRecursive(child, leftOut, rightOut);
        if (leftOut && rightOut) return;
    }
}

// ========================================================
// グローバル検索: シーン全体から名前で探す（Enter が早く呼ばれるケース対策）
// ========================================================
static void FindFeetInSceneByName(Player* player, Object3d*& leftOut, Object3d*& rightOut) {
    if (!SceneManager::GetInstance()) return;
    auto scene = SceneManager::GetInstance()->GetCurrentScene();
    if (!scene) return;

    // まずは厳密候補
    for (auto& obj : scene->GetObjects()) {
        if (!obj) continue;
        std::string n = ToLower(obj->GetName());
        if (!leftOut) {
            if (n.find("player_leftfoot") != std::string::npos || n.find("player_left_foot") != std::string::npos || n.find("leftfoot") != std::string::npos || (n.find("left") != std::string::npos && n.find("foot") != std::string::npos)) {
                leftOut = obj.get();
            }
        }
        if (!rightOut) {
            if (n.find("player_rightfoot") != std::string::npos || n.find("player_right_foot") != std::string::npos || n.find("rightfoot") != std::string::npos || (n.find("right") != std::string::npos && n.find("foot") != std::string::npos)) {
                // avoid assigning same object for both
                if (obj.get() != leftOut) rightOut = obj.get();
            }
        }
        if (leftOut && rightOut) return;
    }

    // 補助: Player名(prefix)を使って探す
    std::string playerName = ToLower(player->GetName());
    if (!playerName.empty() && !(leftOut && rightOut)) {
        for (auto& obj : scene->GetObjects()) {
            if (!obj) continue;
            std::string n = ToLower(obj->GetName());
            if (!leftOut && n.find(playerName) != std::string::npos && n.find("left") != std::string::npos) leftOut = obj.get();
            if (!rightOut && n.find(playerName) != std::string::npos && n.find("right") != std::string::npos) rightOut = obj.get();
            if (leftOut && rightOut) return;
        }
    }
}

// ========================================================
// ユーティリティ: 足パーツを見つける（まず自分の子孫 -> 見つからなければシーン全体）
// ========================================================
static void TryFindFeet(Player* player, Object3d*& leftOut, Object3d*& rightOut) {
    if (!player) return;
    // 1) Player 配下の子孫から探索
    FindFeetRecursive(player, leftOut, rightOut);
    if (leftOut && rightOut) return;

    // 2) シーン全体から名前で探索（LevelLoader の親付けタイミング対策）
    FindFeetInSceneByName(player, leftOut, rightOut);
}

// ========================================================
// 再帰検索: 腕を探すヘルパー
// ========================================================
static void FindArmsRecursive(Object3d* node, Object3d*& leftArmOut, Object3d*& rightArmOut) {
    if (!node) return;
    const auto& children = node->GetChildren();
    for (Object3d* child : children) {
        if (!child) continue;
        std::string name = ToLower(child->GetName());
        bool hasArm = (name.find("arm") != std::string::npos) || (name.find("upperarm") != std::string::npos) || (name.find("shoulder") != std::string::npos);
        bool isRight = (name.find("right") != std::string::npos) || (name.find("_r") != std::string::npos) || (name.find("r_") == 0);
        bool isLeft  = (name.find("left")  != std::string::npos) || (name.find("_l") != std::string::npos) || (name.find("l_") == 0);

        if (hasArm) {
            if (isRight && !rightArmOut) rightArmOut = child;
            else if (isLeft && !leftArmOut) leftArmOut = child;
            else {
                if (!leftArmOut) leftArmOut = child;
                else if (!rightArmOut) rightArmOut = child;
            }
        }

        if (leftArmOut && rightArmOut) return;
        FindArmsRecursive(child, leftArmOut, rightArmOut);
        if (leftArmOut && rightArmOut) return;
    }
}

static void FindArmsInSceneByName(Player* player, Object3d*& leftArmOut, Object3d*& rightArmOut) {
    if (!SceneManager::GetInstance()) return;
    auto scene = SceneManager::GetInstance()->GetCurrentScene();
    if (!scene) return;
    for (auto& obj : scene->GetObjects()) {
        if (!obj) continue;
        std::string n = ToLower(obj->GetName());
        if (!leftArmOut) {
            if (n.find("player_leftarm") != std::string::npos || (n.find("left") != std::string::npos && n.find("arm") != std::string::npos)) leftArmOut = obj.get();
        }
        if (!rightArmOut) {
            if (n.find("player_rightarm") != std::string::npos || (n.find("right") != std::string::npos && n.find("arm") != std::string::npos)) {
                if (obj.get() != leftArmOut) rightArmOut = obj.get();
            }
        }
        if (leftArmOut && rightArmOut) return;
    }

    // prefix search
    std::string playerName = ToLower(player->GetName());
    if (!playerName.empty()) {
        for (auto& obj : scene->GetObjects()) {
            if (!obj) continue;
            std::string n = ToLower(obj->GetName());
            if (!leftArmOut && n.find(playerName) != std::string::npos && n.find("left") != std::string::npos && n.find("arm") != std::string::npos) leftArmOut = obj.get();
            if (!rightArmOut && n.find(playerName) != std::string::npos && n.find("right") != std::string::npos && n.find("arm") != std::string::npos) rightArmOut = obj.get();
            if (leftArmOut && rightArmOut) return;
        }
    }
}

static void TryFindArms(Player* player, Object3d*& leftArmOut, Object3d*& rightArmOut) {
    if (!player) return;
    FindArmsRecursive(player, leftArmOut, rightArmOut);
    if (leftArmOut && rightArmOut) return;
    FindArmsInSceneByName(player, leftArmOut, rightArmOut);
}

// ========================================================
// 剣の探索ヘルパー
// ========================================================
static void FindSwordRecursive(Object3d* node, Object3d*& swordOut) {
    if (!node) return;
    const auto& children = node->GetChildren();
    for (Object3d* child : children) {
        if (!child) continue;
        std::string name = ToLower(child->GetName());
        if (name.find("sword") != std::string::npos || name.find("katana") != std::string::npos || name.find("blade") != std::string::npos) {
            swordOut = child;
            return;
        }
        FindSwordRecursive(child, swordOut);
        if (swordOut) return;
    }
}
static void FindSwordInSceneByName(Player* player, Object3d*& swordOut) {
    if (!SceneManager::GetInstance()) return;
    auto scene = SceneManager::GetInstance()->GetCurrentScene();
    if (!scene) return;
    for (auto& obj : scene->GetObjects()) {
        if (!obj) continue;
        std::string n = ToLower(obj->GetName());
        if (n.find("sword") != std::string::npos || n.find("katana") != std::string::npos || n.find("blade") != std::string::npos) {
            swordOut = obj.get();
            return;
        }
    }
}
static void TryFindSword(Player* player, Object3d*& swordOut) {
    if (!player) return;
    FindSwordRecursive(player, swordOut);
    if (swordOut) return;
    FindSwordInSceneByName(player, swordOut);
}

// ========================================================
//  頭の探索ヘルパー
// ========================================================
static void FindHeadRecursive(Object3d* node, Object3d*& headOut) {
    if (!node) return;
    const auto& children = node->GetChildren();
    for (Object3d* child : children) {
        if (!child) continue;
        std::string name = ToLower(child->GetName());
        if (name.find("head") != std::string::npos || name.find("neck") != std::string::npos) {
            headOut = child;
            return;
        }
        FindHeadRecursive(child, headOut);
        if (headOut) return;
    }
}
static void FindHeadInSceneByName(Player* player, Object3d*& headOut) {
    if (!SceneManager::GetInstance()) return;
    auto scene = SceneManager::GetInstance()->GetCurrentScene();
    if (!scene) return;
    for (auto& obj : scene->GetObjects()) {
        if (!obj) continue;
        std::string n = ToLower(obj->GetName());
        if (n.find("head") != std::string::npos || n.find("neck") != std::string::npos) {
            headOut = obj.get();
            return;
        }
    }
}
static void TryFindHead(Player* player, Object3d*& headOut) {
    if (!player) return;
    FindHeadRecursive(player, headOut);
    if (headOut) return;
    FindHeadInSceneByName(player, headOut);
}

// ========================================================
// 待機状態 (Idle)
// ========================================================
void PlayerStateIdle::Enter(Player* player) {
    player->PlayAnimation("Idle", false); // Tポーズ
    DebugConsole::GetInstance()->AddLog("★ ENTER: Idle State (searching feet/arms/sword/head)");

    // 初期化
    leftFootObj_ = nullptr;
    rightFootObj_ = nullptr;
    leftFootSaved_ = false;
    rightFootSaved_ = false;
    animTimer_ = 0.0f;
    footStage_ = 0;

    // --- arms 初期化 ---
    leftArmObj_ = nullptr;
    rightArmObj_ = nullptr;
    leftArmSaved_ = false;
    rightArmSaved_ = false;
    leftArmDefaultRot_ = {0,0,0};
    rightArmDefaultRot_ = {0,0,0};

    // --- sword 初期化 (位置のみアニメーション) ---
    swordObj_ = nullptr;
    swordSaved_ = false;
    swordDefaultLocalPos_ = {0,0,0};
    swordDefaultWorldPos_ = {0,0,0};

    // --- head 初期化 ---
    headObj_ = nullptr;
    headSaved_ = false;
    headDefaultRot_ = {0,0,0};

    // すぐに見つかれば保存する (見つからなければ Update で再試行)
    TryFindFeet(player, leftFootObj_, rightFootObj_);

    if (leftFootObj_) {
        leftFootDefaultRot_ = leftFootObj_->GetRotation();
        leftFootSaved_ = true;
        DebugConsole::GetInstance()->AddLog(std::string("PlayerStateIdle: Found left foot = ") + leftFootObj_->GetName());
    } else {
        DebugConsole::GetInstance()->AddLog("PlayerStateIdle: Left foot not found (expecting name like 'Player_leftFoot').");
    }

    if (rightFootObj_) {
        rightFootDefaultRot_ = rightFootObj_->GetRotation();
        rightFootSaved_ = true;
        DebugConsole::GetInstance()->AddLog(std::string("PlayerStateIdle: Found right foot = ") + rightFootObj_->GetName());
    } else {
        DebugConsole::GetInstance()->AddLog("PlayerStateIdle: Right foot not found (expecting name like 'Player_rightFoot').");
    }

    // --- 腕の探索・保存 ---
    TryFindArms(player, leftArmObj_, rightArmObj_);
    if (leftArmObj_) {
        leftArmDefaultRot_ = leftArmObj_->GetRotation();
        leftArmSaved_ = true;
        DebugConsole::GetInstance()->AddLog(std::string("PlayerStateIdle: Found left arm = ") + leftArmObj_->GetName());
    } else {
        DebugConsole::GetInstance()->AddLog("PlayerStateIdle: Left arm not found.");
    }
    if (rightArmObj_) {
        rightArmDefaultRot_ = rightArmObj_->GetRotation();
        rightArmSaved_ = true;
        DebugConsole::GetInstance()->AddLog(std::string("PlayerStateIdle: Found right arm = ") + rightArmObj_->GetName());
    } else {
        DebugConsole::GetInstance()->AddLog("PlayerStateIdle: Right arm not found.");
    }

    // --- 剣の探索・位置保存 ---
    TryFindSword(player, swordObj_);
    if (swordObj_) {
        // ローカルとワールドの両方を記録しておく
        swordDefaultLocalPos_ = swordObj_->GetTransform()->translate;
        swordDefaultWorldPos_ = swordObj_->GetWorldPosition();
        swordSaved_ = true;
        DebugConsole::GetInstance()->AddLog(std::string("PlayerStateIdle: Found sword = ") + swordObj_->GetName());
        // 明示的にデフォルト位置へ戻す（念のため）
        Transform* tf = swordObj_->GetTransform();
        tf->translate = swordDefaultLocalPos_;
        swordObj_->UpdateLocalMatrix();
        swordObj_->UpdateWorldMatrix();
    } else {
        DebugConsole::GetInstance()->AddLog("PlayerStateIdle: Sword not found.");
    }

    // --- 頭の探索・保存 ---
    TryFindHead(player, headObj_);
    if (headObj_) {
        headDefaultRot_ = headObj_->GetRotation();
        headSaved_ = true;
        DebugConsole::GetInstance()->AddLog(std::string("PlayerStateIdle: Found head = ") + headObj_->GetName());
    } else {
        DebugConsole::GetInstance()->AddLog("PlayerStateIdle: Head not found.");
    }

    // 初期ステートパラメータ初期化
    animTimer_ = 0.0f;
    footStage_ = 0;
}

// 緩やかな線形補間ヘルパー
static Vector3 LerpVec(const Vector3& a, const Vector3& b, float t) {
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}

// イージング
static float EaseInOutSine(float t) {
    const float pi = 3.14159265358979323846f;
    return 0.5f * (1.0f - std::cos(pi * t));
}

void PlayerStateIdle::Update(Player* player) {
    Vector3 vel = player->GetVelocity();
    vel.y = 0.0f;
    float speed = Math::Length(vel);

    if (speed > 0.1f) {
        player->ChangeState(std::make_unique<PlayerStateRun>());
        return;
    }

    // パーツ探索
    if (!leftFootObj_ || !rightFootObj_) {
        TryFindFeet(player, leftFootObj_, rightFootObj_);
        if (leftFootObj_ && !leftFootSaved_) {
            leftFootDefaultRot_ = leftFootObj_->GetRotation();
            leftFootSaved_ = true;
            animTimer_ = 0.0f; footStage_ = 0;
        }
        if (rightFootObj_ && !rightFootSaved_) {
            rightFootDefaultRot_ = rightFootObj_->GetRotation();
            rightFootSaved_ = true;
            animTimer_ = 0.0f; footStage_ = 0;
        }
    }

    if (!leftArmObj_ || !rightArmObj_) {
        TryFindArms(player, leftArmObj_, rightArmObj_);
        if (leftArmObj_ && !leftArmSaved_) {
            leftArmDefaultRot_ = leftArmObj_->GetRotation();
            leftArmSaved_ = true;
            animTimer_ = 0.0f; footStage_ = 0;
        }
        if (rightArmObj_ && !rightArmSaved_) {
            rightArmDefaultRot_ = rightArmObj_->GetRotation();
            rightArmSaved_ = true;
            animTimer_ = 0.0f; footStage_ = 0;
        }
    }

    if (!swordObj_) {
        TryFindSword(player, swordObj_);
        if (swordObj_ && !swordSaved_) {
            swordDefaultLocalPos_ = swordObj_->GetTransform()->translate;
            swordDefaultWorldPos_ = swordObj_->GetWorldPosition();
            swordSaved_ = true;
            animTimer_ = 0.0f; footStage_ = 0;
        }
    }

    if (!headObj_) {
        TryFindHead(player, headObj_);
        if (headObj_ && !headSaved_) {
            headDefaultRot_ = headObj_->GetRotation();
            headSaved_ = true;
            animTimer_ = 0.0f; footStage_ = 0;
        }
    }
}

void PlayerStateIdle::Exit(Player* player) {
    // 状態を抜ける時は足を確実にデフォルトに戻す
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

    // 腕をデフォルトへ
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

    // 頭をデフォルトへ戻す
    if (headObj_ && headSaved_) {
        Transform* tf = headObj_->GetTransform();
        tf->rotate = headDefaultRot_;
        tf->quaternion = Math::EulerToQuaternion(headDefaultRot_);
        tf->isQuaternionMaster = true;
        headObj_->UpdateWorldMatrix();
    }

    // 剣をデフォルト位置へ戻す（ローカル優先で復元）
    if (swordObj_ && swordSaved_) {
        Transform* tf = swordObj_->GetTransform();
        tf->translate = swordDefaultLocalPos_;
        swordObj_->UpdateLocalMatrix();
        swordObj_->UpdateWorldMatrix();
    }
}

// ========================================================
// 走り状態 (Run)
// ========================================================
void PlayerStateRun::Enter(Player* player) {
    player->PlayAnimation("Armature|mixamo.com|Layer0", true);
    DebugConsole::GetInstance()->AddLog("★ ENTER: Run State");
}

void PlayerStateRun::Update(Player* player) {
    // 1. 速度計算
    Vector3 rawVel = player->GetVelocity();
    Vector3 flatVel = rawVel;
    flatVel.y = 0.0f; // 重力無視
    float speed = Math::Length(flatVel);

    // 2. なぜ止まないのか、証拠を表示！
    std::stringstream ss;
    ss << "RUN Check | Speed: " << speed
        << " (RawY: " << rawVel.y << ")";
    DebugConsole::GetInstance()->AddLog(ss.str());

    // 3. 判定
    if (speed <= 0.1f) {
        DebugConsole::GetInstance()->AddLog("SUCCESS! Transitioning to Idle...");
        player->ChangeState(std::make_unique<PlayerStateIdle>());
    } else {
    }
}

void PlayerStateRun::Exit(Player* player) {
    DebugConsole::GetInstance()->AddLog("EXIT: Run State");
}

void PlayerStateIdle::ApplyPostUpdate(Player* player, float deltaTime) {
    if (deltaTime <= 0.0f) return;

    // 1) アニメタイマーを実時間ベースで進める（連続位相）
    animTimer_ += deltaTime;

    // ping-pong 位相 (0..1..0)
    float twoDur = animDuration_ * 2.0f;
    float local = (twoDur > 1e-6f) ? std::fmod(animTimer_, twoDur) : 0.0f;
    float t = (animDuration_ > 0.0f) ? (local / animDuration_) : 1.0f;
    if (t > 1.0f) t = 2.0f - t;

    // イージング（足・腕と同じ EaseInOutSine を利用）
    float e = EaseInOutSine(t);

    auto DegToRad = [](float d) { return d * 3.14159265358979323846f / 180.0f; };

    // --- 足・腕アニメーション（イージング適用） ---
    float targetAngle = targetAngleRad_;
    float armZRightRad = DegToRad(5.0f);
    float armZLeftRad  = DegToRad(-5.0f);

    if (leftFootObj_ && leftFootSaved_) {
        Vector3 r = leftFootDefaultRot_;
        r.x = leftFootDefaultRot_.x + targetAngle * e;
        Transform* tf = leftFootObj_->GetTransform();
        tf->quaternion = Math::EulerToQuaternion(r);
        tf->isQuaternionMaster = true;
        leftFootObj_->UpdateWorldMatrix();
    }
    if (rightFootObj_ && rightFootSaved_) {
        Vector3 r = rightFootDefaultRot_;
        r.x = rightFootDefaultRot_.x + targetAngle * e;
        Transform* tf = rightFootObj_->GetTransform();
        tf->quaternion = Math::EulerToQuaternion(r);
        tf->isQuaternionMaster = true;
        rightFootObj_->UpdateWorldMatrix();
    }

    if (leftArmObj_ && leftArmSaved_) {
        Vector3 r = leftArmDefaultRot_;
        r.z = leftArmDefaultRot_.z + armZLeftRad * e;
        Transform* tf = leftArmObj_->GetTransform();
        tf->quaternion = Math::EulerToQuaternion(r);
        tf->isQuaternionMaster = true;
        leftArmObj_->UpdateWorldMatrix();
    }
    if (rightArmObj_ && rightArmSaved_) {
        Vector3 r = rightArmDefaultRot_;
        r.z = rightArmDefaultRot_.z + armZRightRad * e;
        Transform* tf = rightArmObj_->GetTransform();
        tf->quaternion = Math::EulerToQuaternion(r);
        tf->isQuaternionMaster = true;
        rightArmObj_->UpdateWorldMatrix();
    }

    // --- 剣: 毎フレームローカル位置をデフォルトに戻す（アニメ無効化） ---
    if (swordObj_ && swordSaved_) {
        Transform* tf = swordObj_->GetTransform();
        tf->translate = swordDefaultLocalPos_;
        swordObj_->UpdateLocalMatrix();
        swordObj_->UpdateWorldMatrix();
    }

    // --- 頭: イージング位相に基づく目標角を作り、Slerpで滑らかに追従 ---
    if (headObj_ && headSaved_) {
        // 位相 0..pi で cos による +1 -> -1 -> +1
        const float pi = 3.14159265358979323846f;
        float phase = t * pi;
        float h = std::cos(phase);

        Vector3 targetEuler = headDefaultRot_;
        // 振幅は ±2度に設定済み
        float headAmpRad = DegToRad(2.0f);
        targetEuler.x = headDefaultRot_.x + h * headAmpRad;

        Quaternion targetQ = Math::EulerToQuaternion(targetEuler);

        Transform* tf = headObj_->GetTransform();
        Quaternion currentQ = tf->quaternion;

        // フレーム独立の追従係数（指数減衰）
        float alpha = 1.0f - std::expf(-headSmoothSpeed_ * deltaTime);
        alpha = std::clamp(alpha, 0.0f, 1.0f);

        Quaternion blendedQ = Math::Slerp(currentQ, targetQ, alpha);

        tf->quaternion = blendedQ;
        tf->isQuaternionMaster = true;

        // Euler 同期（表示一貫性のため）
        Matrix4x4 rotMat = Math::MakeRotateQuaternionMatrix(blendedQ);
        tf->rotate = Math::MatrixToEuler(rotMat);

        headObj_->UpdateWorldMatrix();
    }
}