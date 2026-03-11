#include "PlayerState.h"
#include "Player.h"
#include "InputManager.h"
#include "engine/utility/math/Math.h"
#include "DebugConsole.h"
#include "SceneManager.h"
#include <sstream> // 数字を文字にする用
#include <algorithm>
#include <cctype>

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

    // まずは厳密候補 (Player_leftFoot / Player_rightFoot)
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

    // 補助: Player 名(prefix) を使って探す（例: Player_body が親で foot が子のとき）
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
// 再帰検索 - 腕(Arm)を探すヘルパー
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

// try helpers
static void TryFindArms(Player* player, Object3d*& leftArmOut, Object3d*& rightArmOut) {
    if (!player) return;
    FindArmsRecursive(player, leftArmOut, rightArmOut);
    if (leftArmOut && rightArmOut) return;
    FindArmsInSceneByName(player, leftArmOut, rightArmOut);
}

// ========================================================
// 新規: 剣 (Sword) 探索ヘルパー（位置のみ操作する）
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
// 待機状態 (Idle)
// ========================================================
void PlayerStateIdle::Enter(Player* player) {
    player->PlayAnimation("Idle", false); // Tポーズ
    DebugConsole::GetInstance()->AddLog("★ ENTER: Idle State (searching feet/arms/sword)");

    // 初期化
    leftFootObj_ = nullptr;
    rightFootObj_ = nullptr;
    leftFootSaved_ = false;
    rightFootSaved_ = false;
    footTimer_ = 0.0f;
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
    } else {
        DebugConsole::GetInstance()->AddLog("PlayerStateIdle: Sword not found.");
    }

    // 初期ステートパラメータ初期化
    footTimer_ = 0.0f;
    footStage_ = 0; // 0: toward target, 1: back to default
}

// 緩やかな線形補間ヘルパー
static Vector3 LerpVec(const Vector3& a, const Vector3& b, float t) {
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}

void PlayerStateIdle::Update(Player* player) {
    Vector3 vel = player->GetVelocity();
    vel.y = 0.0f;
    float speed = Math::Length(vel);

    if (speed > 0.1f) {
        player->ChangeState(std::make_unique<PlayerStateRun>());
        return;
    }

    // 足パーツが見つかっていなければ毎フレーム再探索する（LevelLoader の親付けタイミング対策）
    if (!leftFootObj_ || !rightFootObj_) {
        TryFindFeet(player, leftFootObj_, rightFootObj_);

        if (leftFootObj_ && !leftFootSaved_) {
            leftFootDefaultRot_ = leftFootObj_->GetRotation();
            leftFootSaved_ = true;
            DebugConsole::GetInstance()->AddLog(std::string("PlayerStateIdle: Late-found left foot = ") + leftFootObj_->GetName());
            // reset animation so it starts cleanly after late discovery
            footTimer_ = 0.0f; footStage_ = 0;
        }
        if (rightFootObj_ && !rightFootSaved_) {
            rightFootDefaultRot_ = rightFootObj_->GetRotation();
            rightFootSaved_ = true;
            DebugConsole::GetInstance()->AddLog(std::string("PlayerStateIdle: Late-found right foot = ") + rightFootObj_->GetName());
            footTimer_ = 0.0f; footStage_ = 0;
        }
    }

    // 腕を見つけられていなければ再探索
    if (!leftArmObj_ || !rightArmObj_) {
        TryFindArms(player, leftArmObj_, rightArmObj_);
        if (leftArmObj_ && !leftArmSaved_) {
            leftArmDefaultRot_ = leftArmObj_->GetRotation();
            leftArmSaved_ = true;
            DebugConsole::GetInstance()->AddLog(std::string("PlayerStateIdle: Late-found left arm = ") + leftArmObj_->GetName());
            footTimer_ = 0.0f; footStage_ = 0;
        }
        if (rightArmObj_ && !rightArmSaved_) {
            rightArmDefaultRot_ = rightArmObj_->GetRotation();
            rightArmSaved_ = true;
            DebugConsole::GetInstance()->AddLog(std::string("PlayerStateIdle: Late-found right arm = ") + rightArmObj_->GetName());
            footTimer_ = 0.0f; footStage_ = 0;
        }
    }

    // 剣が見つかっていなければ再探索（位置のみ扱う）
    if (!swordObj_) {
        TryFindSword(player, swordObj_);
        if (swordObj_ && !swordSaved_) {
            swordDefaultLocalPos_ = swordObj_->GetTransform()->translate;
            swordDefaultWorldPos_ = swordObj_->GetWorldPosition();
            swordSaved_ = true;
            DebugConsole::GetInstance()->AddLog(std::string("PlayerStateIdle: Late-found sword = ") + swordObj_->GetName());
            footTimer_ = 0.0f; footStage_ = 0;
        }
    }

    // 足・腕・剣アニメーション: デフォルト <-> 目標 を往復ループ
    // 固定刻みで近似
    const float kFixedDelta = 1.0f / 60.0f;
    footTimer_ += kFixedDelta;
    float t = footDuration_ > 0.0f ? (footTimer_ / footDuration_) : 1.0f;
    if (t > 1.0f) t = 1.0f;

    auto DegToRad = [](float d) { return d * 3.14159265358979323846f / 180.0f; };

    // 目標値の設定（度 -> ラジアン）
    float armZRightDeg = 5.0f;   // 右腕 z +5deg
    float armZLeftDeg  = -5.0f;  // 左腕 z -5deg
    float armZRightRad = DegToRad(armZRightDeg);
    float armZLeftRad  = DegToRad(armZLeftDeg);

    // 剣のローカルオフセット（"手のローカル空間" におけるオフセット）
    Vector3 swordLocalOffset = { 0.45f, -1.1f, 0.45f }; // ← 修正: 目標座標を指定値に変更

    // --- 目標ワールド位置を算出 ---
    Vector3 swordTargetWorld = swordDefaultWorldPos_; // fallback
    if (rightArmObj_) {
        // 右腕のワールド行列を使ってローカルオフセット -> ワールド座標に変換
        const Matrix4x4& handWorld = rightArmObj_->GetWorldMatrix();
        swordTargetWorld = Math::Transform(swordLocalOffset, handWorld);
    } else {
        // 右腕が無ければ、プレイヤーのワールド位置を基準にオフセットする（安全策）
        if (player) {
            Vector3 base = player->GetWorldPosition();
            swordTargetWorld = { base.x + swordLocalOffset.x, base.y + swordLocalOffset.y, base.z + swordLocalOffset.z };
        }
    }

    // 足の動き (同じ往復ループロジックを使う)
    if (footStage_ == 0) {
        // デフォルト -> 目標
        if (leftFootObj_) {
            Vector3 r = leftFootDefaultRot_;
            r.x = leftFootDefaultRot_.x + targetAngleRad_ * t;
            Transform* tf = leftFootObj_->GetTransform();
            tf->rotate = r;
            tf->quaternion = Math::EulerToQuaternion(r);
            tf->isQuaternionMaster = true;
            leftFootObj_->UpdateWorldMatrix();
        }
        if (rightFootObj_) {
            Vector3 r = rightFootDefaultRot_;
            r.x = rightFootDefaultRot_.x + targetAngleRad_ * t;
            Transform* tf = rightFootObj_->GetTransform();
            tf->rotate = r;
            tf->quaternion = Math::EulerToQuaternion(r);
            tf->isQuaternionMaster = true;
            rightFootObj_->UpdateWorldMatrix();
        }

        // 腕: デフォルト -> 目標 (z軸回転の加算)
        if (leftArmObj_) {
            Vector3 r = leftArmDefaultRot_;
            r.z = leftArmDefaultRot_.z + armZLeftRad * t;
            Transform* tf = leftArmObj_->GetTransform();
            tf->rotate = r;
            tf->quaternion = Math::EulerToQuaternion(r);
            tf->isQuaternionMaster = true;
            leftArmObj_->UpdateWorldMatrix();
        }
        if (rightArmObj_) {
            Vector3 r = rightArmDefaultRot_;
            r.z = rightArmDefaultRot_.z + armZRightRad * t;
            Transform* tf = rightArmObj_->GetTransform();
            tf->rotate = r;
            tf->quaternion = Math::EulerToQuaternion(r);
            tf->isQuaternionMaster = true;
            rightArmObj_->UpdateWorldMatrix();
        }

        // 剣: 位置のみ デフォルト -> 目標（ワールド空間ベースで補間）
        if (swordObj_) {
            Transform* tf = swordObj_->GetTransform();
            // 剣の親がいる場合はワールド->ローカルを使ってローカル目標を算出して補間
            Object3d* parent = swordObj_->GetParent();
            if (parent) {
                Matrix4x4 invParent = Math::Inverse(parent->GetWorldMatrix());
                Vector3 targetLocal = Math::Transform(swordTargetWorld, invParent);
                Vector3 newLocal = LerpVec(swordDefaultLocalPos_, targetLocal, t);
                tf->translate = newLocal;
            } else {
                // 親がなければ、Transform.translate をワールド座標として扱っているケース
                Vector3 newWorld = LerpVec(swordDefaultWorldPos_, swordTargetWorld, t);
                tf->translate = newWorld;
            }
            // 回転は変更しない
            swordObj_->UpdateLocalMatrix();
            swordObj_->UpdateWorldMatrix();
        }

        if (t >= 1.0f) {
            footStage_ = 1;
            footTimer_ = 0.0f;
        }
    } else { // footStage_ == 1 : 目標 -> デフォルト（戻す）
        if (leftFootObj_) {
            Vector3 r = leftFootDefaultRot_;
            r.x = leftFootDefaultRot_.x + targetAngleRad_ * (1.0f - t);
            Transform* tf = leftFootObj_->GetTransform();
            tf->rotate = r;
            tf->quaternion = Math::EulerToQuaternion(r);
            tf->isQuaternionMaster = true;
            leftFootObj_->UpdateWorldMatrix();
        }
        if (rightFootObj_) {
            Vector3 r = rightFootDefaultRot_;
            r.x = rightFootDefaultRot_.x + targetAngleRad_ * (1.0f - t);
            Transform* tf = rightFootObj_->GetTransform();
            tf->rotate = r;
            tf->quaternion = Math::EulerToQuaternion(r);
            tf->isQuaternionMaster = true;
            rightFootObj_->UpdateWorldMatrix();
        }

        // 腕: 目標 -> デフォルト
        if (leftArmObj_) {
            Vector3 r = leftArmDefaultRot_;
            r.z = leftArmDefaultRot_.z + armZLeftRad * (1.0f - t);
            Transform* tf = leftArmObj_->GetTransform();
            tf->rotate = r;
            tf->quaternion = Math::EulerToQuaternion(r);
            tf->isQuaternionMaster = true;
            leftArmObj_->UpdateWorldMatrix();
        }
        if (rightArmObj_) {
            Vector3 r = rightArmDefaultRot_;
            r.z = rightArmDefaultRot_.z + armZRightRad * (1.0f - t);
            Transform* tf = rightArmObj_->GetTransform();
            tf->rotate = r;
            tf->quaternion = Math::EulerToQuaternion(r);
            tf->isQuaternionMaster = true;
            rightArmObj_->UpdateWorldMatrix();
        }

        // 剣: 目標 -> デフォルト（位置のみ）
        if (swordObj_) {
            Transform* tf = swordObj_->GetTransform();
            Object3d* parent = swordObj_->GetParent();
            if (parent) {
                Matrix4x4 invParent = Math::Inverse(parent->GetWorldMatrix());
                Vector3 targetLocal = Math::Transform(swordTargetWorld, invParent);
                Vector3 newLocal = LerpVec(targetLocal, swordDefaultLocalPos_, t);
                tf->translate = newLocal;
            } else {
                Vector3 newWorld = LerpVec(swordTargetWorld, swordDefaultWorldPos_, t);
                tf->translate = newWorld;
            }
            swordObj_->UpdateLocalMatrix();
            swordObj_->UpdateWorldMatrix();
        }

        if (t >= 1.0f) {
            // ループさせるために再びステージ0へ戻す
            footStage_ = 0;
            footTimer_ = 0.0f;
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
        // ここが出ているなら、速度が0.1より大きい（スティックのドリフトなどの可能性）
    }
}

void PlayerStateRun::Exit(Player* player) {
    DebugConsole::GetInstance()->AddLog("EXIT: Run State");
}