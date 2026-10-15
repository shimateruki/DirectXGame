#pragma once

#include "Object3d.h"

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>

// エディター共通UIがゲーム固有の生成Factoryを知らずにオブジェクトを作るための登録口。
class EditorObjectFactoryRegistry {
  public:
    enum class Kind { Enemy, Gimmick, Item };
    using Factory = std::function<std::unique_ptr<engine::Object3d>(const std::string&, Object3dCommon*)>;

    static void Register(Kind kind, Factory factory) {
        Factories()[static_cast<size_t>(kind)] = std::move(factory);
    }

    static std::unique_ptr<engine::Object3d> Create(Kind kind, const std::string& type, Object3dCommon* common) {
        const auto& factory = Factories()[static_cast<size_t>(kind)];
        return factory ? factory(type, common) : nullptr;
    }

  private:
    static std::array<Factory, 3>& Factories() {
        static std::array<Factory, 3> factories;
        return factories;
    }
};
