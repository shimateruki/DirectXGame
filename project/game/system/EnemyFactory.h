#pragma once

#include "BaseEnemy.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Object3dCommon;

/// 敵タイプ名と生成処理を登録するFactoryです。初期状態では具体的な敵を登録しません。
/// Creatorは受け取ったObject3dCommonで初期化済みのインスタンスを返してください。
class EnemyFactory {
public:
    using Creator = std::function<std::unique_ptr<BaseEnemy>(Object3dCommon*)>;

    static EnemyFactory* GetInstance();

    /// 同名登録はCreatorを置き換えますが、エディター表示順は最初の登録位置を維持します。
    bool Register(const std::string& typeName, Creator creator);
    bool Unregister(const std::string& typeName);
    void Clear();
    bool IsRegistered(const std::string& typeName) const;
    std::vector<std::string> GetRegisteredTypes() const;
    std::unique_ptr<BaseEnemy> CreateEnemy(
        const std::string& typeName,
        Object3dCommon* common) const;

private:
    EnemyFactory() = default;
    std::unordered_map<std::string, Creator> creators_;
    std::vector<std::string> registrationOrder_;
};
