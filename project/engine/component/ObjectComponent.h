#pragma once

#include <string_view>

// 担当：Object3dに付加する機能の共通型と、保存用の種別IDを定義する。
// 関連：Object3D（保持）、PathMoverComponent / ParticleEmitterComponent（派生）
// 主な入口：GetTypeId
class ObjectComponent {
public:
    virtual ~ObjectComponent() = default;

    /// <summary>保存と実行時の型判定に使う安定した種別IDを返す。</summary>
    virtual std::string_view GetTypeId() const = 0;
};
