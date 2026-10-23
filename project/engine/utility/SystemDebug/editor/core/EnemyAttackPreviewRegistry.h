#pragma once

#include "Event.h"
#include "engine/utility/math/Math.h"
#include <memory>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>
#include <utility>

class BaseEnemy;
class BaseScene;
struct EnemyAttackDefinition;
namespace engine { class Object3d; class ParticleSystem; }
using PreviewEnemySpawn = std::function<void(std::unique_ptr<BaseEnemy>)>;

// 一回の準備・更新にだけ渡す非所有参照です。生成物の所有権はspawn経由でSceneへ移します。
struct EnemyAttackPreviewContext {
    engine::Object3d* target = nullptr;
    engine::ParticleSystem* particles = nullptr;
    engine::Vector3 ground{};
    std::string optionId;
    bool preparationOnly = false;
};

// 保存用の攻撃定義とは分離した、プレビュー専用の追加確認項目です。
struct EnemyAttackPreviewOption {
    std::string id;
    std::string displayName;
    float targetDistance = 6.0f;
    std::function<float()> duration;
    bool suppressProfile = false;
};

// 敵の実装はゲーム側のコールバックへ閉じ込め、エディターは種類名で分岐しません。
struct EnemyAttackPreviewDescriptor {
    std::string type;
    std::string displayName;
    std::function<bool(BaseEnemy&, const std::string&)> selectAttack;
    std::function<const char*(const BaseEnemy&)> phaseName;
    std::function<bool(const EnemyAttackDefinition*, bool)> canEditSequence;
    std::function<void(BaseEnemy&, const EnemyAttackPreviewContext&)> prepare;
    std::function<void(BaseEnemy&, PreviewEnemySpawn)> connectSpawns;
    std::function<bool(BaseEnemy&, float, const EnemyAttackPreviewContext&)> updateInteraction;
    bool canAuthor = false;
    bool supportsPlayerBinding = false;
    bool supportsActorControl = false;
    std::string sequenceHelp;
    DamageType damageType = DamageType::Physical;
    float targetDistance = 6.0f;
    float duration = 4.6f;
    std::vector<EnemyAttackPreviewOption> options;
};

class EnemyAttackPreviewRegistry {
public:
    struct StatusTarget { std::string type, displayName; };

    // 編集内容と保存先はゲーム側に任せます。空のtypeはPlayer共通値を表します。
    struct StatusEditor {
        std::function<std::vector<StatusTarget>()> targets;
        std::function<bool(BaseScene*, const std::string&)> draw;
        std::function<void()> savePending;
    };
    static void RegisterStatusEditor(StatusEditor editor) { StatusServices() = std::move(editor); }
    static const StatusEditor& GetStatusEditor() { return StatusServices(); }

    // 一括検証後に追記します。不正な登録や重複では、既存の一覧を変更しません。
    static bool Register(std::vector<EnemyAttackPreviewDescriptor> entries) {
        std::unordered_set<std::string> types;
        for (const auto& entry : Entries()) types.insert(entry.type);
        for (const auto& entry : entries) {
            if (entry.type.empty() || entry.displayName.empty() || !entry.selectAttack ||
                !types.insert(entry.type).second) return false;
            std::unordered_set<std::string> options;
            for (const auto& option : entry.options) {
                if (option.id.empty() || option.displayName.empty() || !option.duration ||
                    !options.insert(option.id).second) return false;
            }
        }
        for (auto& entry : entries) Entries().push_back(std::move(entry));
        return true;
    }

    static const std::vector<EnemyAttackPreviewDescriptor>& GetEntries() { return Entries(); }

    // 未登録なら nullptr。定義は内部一覧の借用参照で、追加登録後は取得し直す。
    static const EnemyAttackPreviewDescriptor* Find(const std::string& type) {
        for (const auto& entry : Entries()) if (entry.type == type) return &entry;
        return nullptr;
    }

    static const EnemyAttackPreviewOption* FindOption(const std::string& type, const std::string& id) {
        if (const auto* entry = Find(type))
            for (const auto& option : entry->options) if (option.id == id) return &option;
        return nullptr;
    }

private:
    static StatusEditor& StatusServices() {
        static StatusEditor editor;
        return editor;
    }
    static std::vector<EnemyAttackPreviewDescriptor>& Entries() {
        static std::vector<EnemyAttackPreviewDescriptor> entries;
        return entries;
    }
};
