#pragma once

#include "Object3d.h"

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// エディター共通UIがゲーム固有の生成Factoryを知らずにオブジェクトを作るための登録口。
class EditorObjectFactoryRegistry {
  public:
    enum class Kind { Enemy, Gimmick, Item };

    using Factory = std::function<std::unique_ptr<engine::Object3d>(const std::string&, engine::Object3dCommon*)>;

    struct TypeInfo {
        std::string id;
        std::string displayName;
        std::function<bool(const engine::Object3d&)> matches;
    };

    /// 具体的なクラスを知らずに、ゲーム側で登録された種類を列挙します。
    static void RegisterTypes(Kind kind, std::vector<TypeInfo> types) {
        Types()[static_cast<size_t>(kind)] = std::move(types);
    }

    static const std::vector<TypeInfo>& GetTypes(Kind kind) { return Types()[static_cast<size_t>(kind)]; }

    static void Register(Kind kind, Factory factory) {
        Factories()[static_cast<size_t>(kind)] = std::move(factory);
    }

    static std::unique_ptr<engine::Object3d> Create(Kind kind, const std::string& type, engine::Object3dCommon* common) {
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
