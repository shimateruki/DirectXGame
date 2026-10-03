#pragma once

#include <iterator>

namespace EnemyAttackPreviewConstants {
// 攻撃プレビューの対象とUIが共有する識別子・表示名です。
constexpr const char* kCopyTypes[] = {"Slime", "FireSlime", "ThunderSlime", "WindSlime", "Bomber", "Crown"};
constexpr const char* kCopyLabels[] = {"ピンク", "炎", "雷", "風", "ボム", "覇王"};
constexpr const char* kCopyActions[] = {
    "コピー開始", "左クリック", "左長押し → 離す", "右クリック / ガード",
    "E 技", "コピー解除", "待機・常駐エフェクト", "ピンク: 3段コンボ"
};
constexpr float kFixedPreviewStep = 1.0f / 60.0f;
constexpr float kMinLoopDuration = 0.5f;
constexpr float kMaxLoopDuration = 12.0f;
constexpr float kDefaultTargetDistanceLimit = 18.0f;
constexpr float kPi = 3.1415926535f;
constexpr const char* kEnemyTypes[] = {
    "Slime",
    "FireSlime",
    "ThunderSlime",
    "GiantSlime",
    "PrismSlime",
    "MagmaSlime",
    "Bomber",
    "WindSlime",
    "FalseKingSlime",
};

constexpr const char* kEnemyLabels[] = {
    "通常スライム（溜めジャンプ急降下）",
    "炎スライム",
    "雷スライム（近距離放電／連続落雷）",
    "巨大スライム（ジャンププレス）",
    "プリズムスライム（無属性／炎／雷／風）",
    "マグマスライム（三連砲／突進／火山プレス）",
    "ボマースライム（爆弾投げ）",
    "風スライム（暴風ブレス／空中三連風弾）",
    "偽王スライム（最終ボス・3形態）",
};

static_assert(std::size(kCopyTypes) == std::size(kCopyLabels));
static_assert(std::size(kEnemyTypes) == std::size(kEnemyLabels));

} // namespace EnemyAttackPreviewConstants
