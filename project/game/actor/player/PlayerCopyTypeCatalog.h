#pragma once

#include "engine/utility/math/Math.h"

#include <array>
#include <string_view>

// コピー能力として保存・再適用できるスライム種の共通定義です。
// プレイヤー、コピー記憶台、エディターで同じ識別子と見た目を共有します。
struct PlayerCopyTypeDescriptor {
    std::string_view enemyType;
    std::string_view modelName;
    Vector4 displayColor;
};

namespace PlayerCopyTypeCatalog {
    const std::array<PlayerCopyTypeDescriptor, 5>& GetAll();
    const PlayerCopyTypeDescriptor* Find(std::string_view enemyType);
    bool IsSupported(std::string_view enemyType);
}
