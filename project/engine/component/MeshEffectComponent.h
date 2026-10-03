#pragma once

#include "ObjectComponent.h"

#include <string>

/// <summary>Objectへ追従する2枠のメッシュエフェクト設定を保持する。</summary>
class MeshEffectComponent final : public ObjectComponent {
public:
    static constexpr std::string_view kTypeId = "MeshEffect";

    /// <summary>保存と型判定に使うComponent種別を返す。</summary>
    std::string_view GetTypeId() const override { return kTypeId; }

    // 主・副エフェクトのAsset名。空文字列ならその枠は未設定。
    const std::string& GetPrimaryEffect() const { return primaryEffect_; }
    void SetPrimaryEffect(const std::string& name) { primaryEffect_ = name; }

    const std::string& GetSecondaryEffect() const { return secondaryEffect_; }
    void SetSecondaryEffect(const std::string& name) { secondaryEffect_ = name; }

private:
    std::string primaryEffect_;
    std::string secondaryEffect_;
};
