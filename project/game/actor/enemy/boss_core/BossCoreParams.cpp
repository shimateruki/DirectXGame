#include "BossCoreShared.h"

void BossCore::LoadAttackParams() {
    std::string filePath = "Resources/json/enemy/boss_attack_params.json";
    if (!std::filesystem::exists(filePath)) {
        // デフォルトの攻撃パターンを初期設定
        attackParams_.phase1Attacks = {
            { 1, 30 }, // 突進 (Rush)
            { 2, 30 }, // 射撃 (Shoot)
            { 3, 30 }, // ハンマー (Hammer)
            { 4, 30 }  // 壁 (Wall)
        };
        attackParams_.phase2Attacks = {
            { 5, 30 }, // 人型 (Humanoid)
            { 6, 30 }, // レーザー (Laser)
            { 7, 30 }, // 吸収 (Absorb)
            { 9, 30 }  // ファンネル (Funnels)
        };
        SaveAttackParams(); // デフォルト値で作成
        return;
    }

    std::ifstream ifs(filePath);
    if (ifs.is_open()) {
        json j;
        ifs >> j;
        attackParams_.FromJson(j);
    }

    // 古いJSONなどで空の場合はデフォルトを流し込む
    if (attackParams_.phase1Attacks.empty()) {
        attackParams_.phase1Attacks = {
            { 1, 30 },
            { 2, 30 },
            { 3, 30 },
            { 4, 30 }
        };
    }
    if (attackParams_.phase2Attacks.empty()) {
        attackParams_.phase2Attacks = {
            { 5, 30 },
            { 6, 30 },
            { 7, 30 },
            { 9, 30 }
        };
    }
}

void BossCore::SaveAttackParams() {
    std::string dirPath = "Resources/json/enemy";
    if (!std::filesystem::exists(dirPath)) {
        std::filesystem::create_directories(dirPath);
    }

    std::string filePath = dirPath + "/boss_attack_params.json";
    std::ofstream ofs(filePath);
    if (ofs.is_open()) {
        json j;
        attackParams_.ToJson(j);
        ofs << j.dump(4);
    }
}

