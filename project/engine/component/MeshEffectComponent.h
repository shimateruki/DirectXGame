#pragma once

#include "ObjectComponent.h"

#include <string>

// MeshEffectComponentは、Objectへ追従する2枠のMesh Effect Assetを所有します。
class MeshEffectComponent final : public ObjectComponent {
public:
    static constexpr std::string_view kTypeId = "MeshEffect";

    std::string_view GetTypeId() const override { return kTypeId; }

    const std::string& GetPrimaryEffect() const { return primaryEffect_; }
    void SetPrimaryEffect(const std::string& name) { primaryEffect_ = name; }

    const std::string& GetSecondaryEffect() const { return secondaryEffect_; }
    void SetSecondaryEffect(const std::string& name) { secondaryEffect_ = name; }

private:
    std::string primaryEffect_;
    std::string secondaryEffect_;
};
