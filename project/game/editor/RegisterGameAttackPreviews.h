#pragma once

#include "game/editor/EnemyAttackPreviewRegistry.h"
#include "EnemySlime.h"
#include "EnemyFireSlime.h"
#include "EnemyThunderSlime.h"
#include "EnemyGiantSlime.h"
#include "EnemyBomber.h"
#include "EnemyWindSlime.h"
#include "game/actor/enemy/boss/prism/EnemyPrismSlime.h"
#include "game/actor/enemy/boss/magma/EnemyMagmaSlime.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.h"
#include "game/actor/enemy/combat/ISequencePreviewEnemy.h"
#include "game/crown/CrownFinaleTuning.h"
#include <cassert>
#include <type_traits>

namespace GameAttackPreviewDetail {
// 専用APIを持たない敵は従来どおり通常AIで実行します。型の不一致は拒否します。
template<class T>
EnemyAttackPreviewDescriptor Make(const char* type, const char* label,
    void (T::*select)(const std::string&) = nullptr,
    const char* (T::*phase)() const = nullptr) {
    EnemyAttackPreviewDescriptor entry;
    entry.type = type;
    entry.displayName = label;
    entry.selectAttack = [select](BaseEnemy& enemy, const std::string& id) {
        auto* concrete = dynamic_cast<T*>(&enemy);
        if (!concrete) return false;
        if constexpr (std::is_base_of_v<ISequencePreviewEnemy, T>)
            concrete->SetSequencePreviewAttackId(id);
        else if (select) (concrete->*select)(id);
        return true;
    };
    entry.phaseName = [phase](const BaseEnemy& enemy) -> const char* {
        const auto* concrete = dynamic_cast<const T*>(&enemy);
        if (!concrete) return nullptr;
        if constexpr (std::is_base_of_v<ISequencePreviewEnemy, T>)
            if (const char* label = concrete->GetSequenceStateLabel()) return label;
        return phase ? (concrete->*phase)() : nullptr;
    };
    return entry;
}
// 本番用の既存SetSpawnCallbackを、そのまま隔離プレビューの追加先へ接続します。
template<class T>
void ConnectSpawns(BaseEnemy& enemy, PreviewEnemySpawn spawn) {
    if (auto* concrete = dynamic_cast<T*>(&enemy)) concrete->SetSpawnCallback(std::move(spawn));
}
} // namespace GameAttackPreviewDetail

// 既存の敵ID・Profile・攻撃IDをそのまま使用します。登録自体は保存や敵生成を行いません。
inline void RegisterGameAttackPreviews() {
    static const bool registered = [] {
        using namespace GameAttackPreviewDetail;
        std::vector<EnemyAttackPreviewDescriptor> entries;
        entries.push_back(Make<EnemySlime>("Slime", "通常スライム（溜めジャンプ急降下）",
            nullptr, &EnemySlime::GetDebugMoveStateName));

        auto fire = Make<EnemyFireSlime>("FireSlime", "炎スライム");
        fire.damageType = DamageType::Fire;
        fire.targetDistance = 3.2f;
        fire.duration = 3.0f;
        fire.canAuthor = true;
        fire.supportsPlayerBinding = true;
        fire.supportsActorControl = true;
        fire.sequenceHelp = "炎スライムの敵AIと炎コピーE入力に接続しています。命令の時刻0は攻撃開始時です。";
        fire.canEditSequence = [](const EnemyAttackDefinition*, bool) { return true; };
        entries.push_back(std::move(fire));

        auto thunder = Make<EnemyThunderSlime>("ThunderSlime", "雷スライム（近距離放電／連続落雷）",
            &EnemyThunderSlime::SetDebugPreviewAttackId, &EnemyThunderSlime::GetDebugAttackPhaseName);
        thunder.damageType = DamageType::Electric;
        thunder.targetDistance = 3.2f;
        thunder.duration = 3.2f;
        entries.push_back(std::move(thunder));

        auto giant = Make<EnemyGiantSlime>("GiantSlime", "巨大スライム（ジャンププレス）");
        giant.targetDistance = 9.0f;
        giant.duration = 5.0f;
        giant.phaseName = [](const BaseEnemy& enemy) -> const char* {
            const auto* concrete = dynamic_cast<const EnemyGiantSlime*>(&enemy);
            return concrete && (concrete->IsHookSplitPulled() || concrete->HasSplit())
                ? concrete->GetDebugHookSplitPhaseName() : nullptr;
        };
        giant.options = {
            {"hook_split_ground", "フック分裂（接地から抵抗）", 8.0f, [] { return 3.5f; }},
            {"hook_split_air", "フック分裂（空中拘束から着地）", 8.0f, [] { return 4.2f; }}
        };
        giant.prepare = [](BaseEnemy& enemy, const EnemyAttackPreviewContext& context) {
            auto* concrete = dynamic_cast<EnemyGiantSlime*>(&enemy);
            if (!concrete || !context.target ||
                (context.optionId != "hook_split_ground" && context.optionId != "hook_split_air")) return;
            if (context.optionId == "hook_split_air") {
                enemy.SetTranslate(context.ground + engine::Vector3{0.0f, 5.5f, 0.0f});
                enemy.SetVelocity({0.0f, -1.5f, 0.0f});
                enemy.SetGrounded(false);
            }
            concrete->BeginHookSplitPull(context.target->GetWorldPosition());
        };
        giant.updateInteraction = [](BaseEnemy& enemy, float dt, const EnemyAttackPreviewContext& context) {
            auto* concrete = dynamic_cast<EnemyGiantSlime*>(&enemy);
            return concrete && context.target &&
                (context.optionId == "hook_split_ground" || context.optionId == "hook_split_air") &&
                concrete->UpdateHookSplitPull(dt, context.target->GetWorldPosition(), context.particles);
        };
        entries.push_back(std::move(giant));

        auto prism = Make<EnemyPrismSlime>("PrismSlime", "プリズムスライム（無属性／炎／雷／風）",
            &EnemyPrismSlime::SetDebugPreviewAttackId, &EnemyPrismSlime::GetDebugAttackPhaseName);
        prism.damageType = DamageType::Explosion;
        prism.targetDistance = 12.0f;
        prism.duration = 4.2f;
        prism.canAuthor = true;
        prism.sequenceHelp = "この敵では炎属性の攻撃枠で実行します。命令の時刻0は詠唱完了時です。複製した攻撃も同じ詠唱とAIを使用します。";
        prism.canEditSequence = [](const EnemyAttackDefinition* attack, bool playerCopy) {
            return !playerCopy && attack && (attack->id == "fire_fan" ||
                !attack->sequence.events.empty() || !attack->sequence.tracks.empty());
        };
        prism.prepare = [](BaseEnemy& enemy, const EnemyAttackPreviewContext& context) {
            if (auto* concrete = dynamic_cast<EnemyPrismSlime*>(&enemy))
                concrete->SetDebugPreviewChantOnly(context.preparationOnly);
        };
        prism.connectSpawns = ConnectSpawns<EnemyPrismSlime>;
        entries.push_back(std::move(prism));

        entries.push_back(Make<EnemyMagmaSlime>("MagmaSlime", "マグマスライム（三連砲／突進／火山プレス）",
            &EnemyMagmaSlime::SetDebugPreviewAttackId, &EnemyMagmaSlime::GetDebugAttackPhaseName));
        auto bomber = Make<EnemyBomber>("Bomber", "ボマースライム（爆弾投げ）");
        bomber.connectSpawns = ConnectSpawns<EnemyBomber>;
        entries.push_back(std::move(bomber));
        entries.push_back(Make<EnemyWindSlime>("WindSlime", "風スライム（暴風ブレス／空中三連風弾）",
            &EnemyWindSlime::SetDebugPreviewAttackId, &EnemyWindSlime::GetDebugAttackPhaseName));

        auto king = Make<EnemyFalseKingSlime>("FalseKingSlime", "偽王スライム（最終ボス・3形態）",
            &EnemyFalseKingSlime::SetDebugPreviewAttackId, &EnemyFalseKingSlime::GetDebugAttackPhaseName);
        king.damageType = DamageType::Explosion;
        king.options = {{"seal_barrage_preview", "第2形態：封印失敗の黒晶墜槍", 15.0f, [] {
            const auto& tuning = CrownFinaleTuning::Get();
            return 1.25f + tuning.sealCastTime + tuning.sealBarrageMovieDuration + tuning.sealBarrageRecovery;
        }, true}};
        entries.push_back(std::move(king));
        return EnemyAttackPreviewRegistry::Register(std::move(entries));
    }();
    assert(registered && "attack preview registration failed");
    (void)registered;
}
