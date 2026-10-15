#include "engine/component/Collider.h"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

// 描画を初期化せず、プレビューで使用する実Colliderと旧簡易判定の不一致を再現します。
int main() {
    try {
        std::ifstream file("Resources/json/3Dobject/stage1_player.json");
        nlohmann::json source; file >> source;
        const auto& saved = source.at("objects").at(0).at("collider");
        ColliderConfig targetConfig;
        targetConfig.type = static_cast<ColliderType>(saved.at("type").get<int>());
        const auto& center = saved.at("center");
        const auto& size = saved.at("size");
        targetConfig.center = {center[0], center[1], center[2]};
        targetConfig.size = {size[0], size[1], size[2]};
        Transform targetTransform;
        targetTransform.UpdateMatrix();
        Collider target(&targetTransform);
        target.SetConfig(targetConfig);
        target.SetScaleOverride({2, 2, 2}); // Playerの初期managedBaseScaleと同じ。
        target.SetMask(0); // 通常の接触応答を隔離しても、形状自体は有効です。
        Transform projectileTransform;
        projectileTransform.translate = {0, 0.35f, -1.60f};
        projectileTransform.UpdateMatrix();
        Collider projectile(&projectileTransform);
        ColliderConfig projectileConfig;
        projectileConfig.type = ColliderType::kSphere;
        projectileConfig.size = {0.6f, 0.6f, 0.6f};
        projectile.SetConfig(projectileConfig);
        projectile.SetScaleOverride({1, 1, 1});
        if (!projectile.CheckCollision(&target).isColliding)
            throw std::runtime_error("actual player collider contact missing");
        // 見た目の倍率が2でも、旧式の原点中心球では接触が成立しない位置です。
        if (engine::Math::Length(projectileTransform.translate) <= 1.0f + projectile.GetRadius())
            throw std::runtime_error("old preview mismatch not reproduced");
        projectileTransform.translate.z = -3;
        projectileTransform.UpdateMatrix();
        if (projectile.CheckCollision(&target).isColliding)
            throw std::runtime_error("separated projectile reported contact");
        targetConfig.type = ColliderType::kNone;
        target.SetConfig(targetConfig);
        projectileTransform.translate = {};
        projectileTransform.UpdateMatrix();
        if (projectile.CheckCollision(&target).isColliding)
            throw std::runtime_error("disabled collider reported contact");
        std::cout << "Preview collision passed: real player offset/scale contact, old false negative, separation, disabled shape\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
