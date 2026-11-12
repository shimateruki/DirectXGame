#pragma once

#include "ObjectComponent.h"

#include <string>

// 担当：Objectに追従させる主・副メッシュエフェクトの設定名を保持する。
// 関連：ObjectComponent（共通型）、Object3D（保持）、MeshEffectManager（再生）
// 主な入口：GetPrimaryEffect / SetPrimaryEffect / GetSecondaryEffect
class MeshEffectComponent final : public ObjectComponent {
public:
    static constexpr std::string_view kTypeId = "MeshEffect"; // 保存と実行時の型判定に使う固定ID。

    /// <summary>保存と型判定に使うComponent種別を返す。</summary>
    std::string_view GetTypeId() const override { return kTypeId; }

    // 主・副エフェクトのAsset名。空文字列ならその枠は未設定。
    const std::string& GetPrimaryEffect() const { return primaryEffect_; }

    void SetPrimaryEffect(const std::string& name) { primaryEffect_ = name; }

    const std::string& GetSecondaryEffect() const { return secondaryEffect_; }

    void SetSecondaryEffect(const std::string& name) { secondaryEffect_ = name; }

private:
    std::string primaryEffect_; // 主エフェクトの設定名。空なら未設定。
    std::string secondaryEffect_; // 副エフェクトの設定名。空なら未設定。
};
