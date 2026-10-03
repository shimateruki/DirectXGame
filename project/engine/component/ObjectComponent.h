#pragma once

#include <string_view>

/// <summary>Object3dが所有する実行時Componentの最小共通型。</summary>
class ObjectComponent {
public:
    virtual ~ObjectComponent() = default;

    /// <summary>保存と実行時の型判定に使う安定した種別IDを返す。</summary>
    virtual std::string_view GetTypeId() const = 0;
};
