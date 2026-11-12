#pragma once

#include "Object3D.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// 担当：ゲーム固有型の生成関数と表示用型一覧を、Editorへ登録する。
// 関連：Object3D（生成結果）、HierarchyWindow / PresetManager（生成要求）
// 主な入口：Register / RegisterTypes / GetTypes / Create
class EditorObjectFactoryRegistry {
  public:
    enum class Kind { Enemy, Gimmick, Item };

    using Factory = std::function<std::unique_ptr<engine::Object3D>(const std::string&, engine::Object3DCommon*)>;

    /// <summary>同じ種類台帳を使う、作成メニューとSpawnerの候補一覧。</summary>
    enum class TypeList { Creation, Spawner };

    // 生成・Spawner候補の表示可否と並び順。
    // 関連：EditorObjectFactoryRegistry（この型を使う機能の本体）
    struct TypeListOptions {
        bool visible = true;
        int order = 1000; // 個別指定のない追加種類は、既存の優先候補より後ろへ並ぶ。
    };

    // 種類ID・表示名・型判定と用途別の一覧表示条件。
    // 関連：EditorObjectFactoryRegistry（この型を使う機能の本体）
    struct TypeInfo {
        std::string id;
        std::string displayName;
        std::function<bool(const engine::Object3D&)> matches;
        TypeListOptions creation;
        TypeListOptions spawner;
    };

    /// 具体的なクラスを知らずに、ゲーム側で登録された種類を列挙します。
    static void RegisterTypes(Kind kind, std::vector<TypeInfo> types) {
        Types()[static_cast<size_t>(kind)] = std::move(types);
    }

    static const std::vector<TypeInfo>& GetTypes(Kind kind) { return Types()[static_cast<size_t>(kind)]; }

    /// <summary>ゲーム側の用途別設定で候補を絞り、指定順に返す。登録更新をまたいで参照を保持しない。</summary>
    static std::vector<const TypeInfo*> GetTypes(Kind kind, TypeList list) {
        const auto options = [list](const TypeInfo& type) -> const TypeListOptions& {
            return list == TypeList::Creation ? type.creation : type.spawner;
        };
        std::vector<const TypeInfo*> result;
        for (const auto& type : GetTypes(kind)) {
            if (options(type).visible) result.push_back(&type);
        }
        // 同順位では元の登録順を維持し、候補追加だけで既存の順序を崩さない。
        std::stable_sort(result.begin(), result.end(), [&options](const TypeInfo* lhs, const TypeInfo* rhs) {
            return options(*lhs).order < options(*rhs).order;
        });
        return result;
    }


    // 同じカテゴリの生成関数を置き換える。捕捉した外部参照の寿命は登録側で保証する。
    static void Register(Kind kind, Factory factory) {
        Factories()[static_cast<size_t>(kind)] = std::move(factory);
    }

    // 生成物の所有権を呼び出し側へ渡す。Factory 未登録なら nullptr。
    static std::unique_ptr<engine::Object3D> Create(Kind kind, const std::string& type, engine::Object3DCommon* common) {
        const auto& factory = Factories()[static_cast<size_t>(kind)];
        return factory ? factory(type, common) : nullptr;
    }

  private:
    static std::array<std::vector<TypeInfo>, 3>& Types() {
        static std::array<std::vector<TypeInfo>, 3> types;
        return types;
    }
    static std::array<Factory, 3>& Factories() {
        static std::array<Factory, 3> factories;
        return factories;
    }
};
