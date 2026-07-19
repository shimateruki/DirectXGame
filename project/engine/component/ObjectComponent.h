#pragma once

#include <string_view>

// ObjectComponentは、Object3dが所有する実体Componentの最小共通型です。
// Editor用Registryとは分離し、Runtime側の型識別と段階的な機能分割に使います。
class ObjectComponent {
public:
    virtual ~ObjectComponent() = default;
    virtual std::string_view GetTypeId() const = 0;
};
