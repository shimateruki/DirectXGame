#define NOMINMAX
#include "GameplayStatusManager.h"

#include "BaseEnemy.h"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr const char* kSettingsPath = "Resources/json/gameplay/status_settings.json";
constexpr const char* kLegacySettingsPath = "Resources/json/gameplay/status_presets.json";

GameplayStatusManager::CharacterStatus MakeStatus(
    float maxHp,
    float attackPower,
    float speed,
    float gravity,
    float maxFallSpeed,
    float jumpPower,
    float detectionRange,
    const Vector3& scale,
    const char* modelName,
    bool morphLimited,
    float morphDuration) {
    GameplayStatusManager::CharacterStatus status;
    status.maxHp = maxHp;
    status.attackPower = attackPower;
    status.speed = speed;
    status.gravity = gravity;
    status.maxFallSpeed = maxFallSpeed;
    status.jumpPower = jumpPower;
    status.detectionRange = detectionRange;
    status.scale = scale;
    status.modelName = modelName ? modelName : "";
    status.morphLimited = morphLimited;
    status.morphDuration = morphDuration;
    return status;
}

void ReadFloat(const json& source, const char* key, float& value) {
    if (source.is_object() && source.contains(key) && source.at(key).is_number()) {
        value = source.at(key).get<float>();
    }
}

void ReadBool(const json& source, const char* key, bool& value) {
    if (source.is_object() && source.contains(key) && source.at(key).is_boolean()) {
        value = source.at(key).get<bool>();
    }
}

void ReadString(const json& source, const char* key, std::string& value) {
    if (source.is_object() && source.contains(key) && source.at(key).is_string()) {
        const std::string candidate = source.at(key).get<std::string>();
        if (!candidate.empty()) {
            value = candidate;
        }
    }
}

void ReadScale(const json& source, Vector3& value) {
    if (!source.is_object() || !source.contains("scale") || !source.at("scale").is_array()) {
        return;
    }

    const json& array = source.at("scale");
    if (array.size() < 3 || !array[0].is_number() || !array[1].is_number() || !array[2].is_number()) {
        return;
    }

    const Vector3 candidate = {
        array[0].get<float>(),
        array[1].get<float>(),
        array[2].get<float>()
    };
    // 旧設定の0スケールは「クラス側の値を維持」という意味だったため、
    // 一元管理移行後はタイプ標準値を残します。
    if (candidate.x > 0.0f && candidate.y > 0.0f && candidate.z > 0.0f) {
        value = candidate;
    }
}

void JsonToStatus(const json& source, GameplayStatusManager::CharacterStatus& status) {
    ReadFloat(source, "maxHp", status.maxHp);
    ReadFloat(source, "attackPower", status.attackPower);
    ReadFloat(source, "speed", status.speed);
    ReadFloat(source, "gravity", status.gravity);
    ReadFloat(source, "maxFallSpeed", status.maxFallSpeed);
    ReadFloat(source, "jumpPower", status.jumpPower);
    ReadFloat(source, "detectionRange", status.detectionRange);
    ReadScale(source, status.scale);
    ReadString(source, "modelName", status.modelName);
    ReadBool(source, "morphLimited", status.morphLimited);
    ReadFloat(source, "morphDuration", status.morphDuration);
}

json StatusToJson(const GameplayStatusManager::CharacterStatus& status) {
    return json{
        { "maxHp", status.maxHp },
        { "attackPower", status.attackPower },
        { "speed", status.speed },
        { "gravity", status.gravity },
        { "maxFallSpeed", status.maxFallSpeed },
        { "jumpPower", status.jumpPower },
        { "detectionRange", status.detectionRange },
        { "scale", json::array({ status.scale.x, status.scale.y, status.scale.z }) },
        { "modelName", status.modelName },
        { "morphLimited", status.morphLimited },
        { "morphDuration", status.morphDuration }
    };
}

bool NearlyEqual(float lhs, float rhs) {
    return std::abs(lhs - rhs) <= 0.0001f;
}
}

GameplayStatusManager* GameplayStatusManager::GetInstance() {
    static GameplayStatusManager instance;
    return &instance;
}

GameplayStatusManager::~GameplayStatusManager() {
    SaveIfDirty();
}

void GameplayStatusManager::Initialize() {
    if (initialized_) {
        return;
    }

    ResetToDefaults();
    dirty_ = false;
    bool migratedLegacySettings = false;
    if (!LoadFromFile(kSettingsPath)) {
        // 旧ファイルが残る環境では一度だけ移行して、以降は新設定を使います。
        if (LoadFromFile(kLegacySettingsPath)) {
            migratedLegacySettings = true;
        }
    }
    initialized_ = true;
    if (migratedLegacySettings) {
        dirty_ = true;
        Save();
    }
}

bool GameplayStatusManager::Reload() {
    Initialize();

    const CharacterStatus previousPlayer = playerStatus_;
    const auto previousEnemies = enemyStatuses_;
    ResetToDefaults();
    dirty_ = false;
    if (LoadFromFile(kSettingsPath)) {
        return true;
    }

    playerStatus_ = previousPlayer;
    enemyStatuses_ = previousEnemies;
    return false;
}

bool GameplayStatusManager::Save() {
    Initialize();

    json root;
    root["schemaVersion"] = 4;
    root["player"] = StatusToJson(playerStatus_);
    root["enemies"] = json::object();
    for (const auto& [enemyType, status] : enemyStatuses_) {
        root["enemies"][enemyType] = StatusToJson(status);
    }

    try {
        fs::create_directories(fs::path(kSettingsPath).parent_path());
        std::ofstream file(kSettingsPath);
        if (!file) {
            return false;
        }
        file << root.dump(4);
        dirty_ = false;
        return true;
    } catch (...) {
        return false;
    }
}

void GameplayStatusManager::ResetToDefaults() {
    playerStatus_ = MakeStatus(5.0f, 1.0f, 27.7f, 50.0f, 60.0f, 24.0f, 20.0f,
        { 2.0f, 2.0f, 2.0f }, "Characters/slime", false, 5.0f);

    enemyStatuses_.clear();
    enemyStatuses_.emplace("Slime", MakeStatus(50.0f, 1.0f, 0.1f, 60.0f, 60.0f, 18.0f, 20.0f,
        Vector3{ 2.0f, 2.0f, 2.0f }, "Characters/slime_pink", false, 5.0f));
    enemyStatuses_.emplace("Bomb", MakeStatus(30.0f, 1.0f, 0.04f, 60.0f, 60.0f, 16.0f, 20.0f,
        Vector3{ 0.16f, 0.16f, 0.16f }, "Gimmicks/blob", true, 5.0f));
    enemyStatuses_.emplace("Bomber", MakeStatus(60.0f, 1.15f, 0.0f, 60.0f, 60.0f, 16.0f, 32.0f,
        Vector3{ 2.0f, 2.0f, 2.0f }, "Characters/slime_black", true, 5.0f));
    enemyStatuses_.emplace("Mushroom", MakeStatus(35.0f, 1.0f, 2.1f, 60.0f, 60.0f, 16.0f, 16.0f,
        Vector3{ 1.0f, 1.0f, 1.0f }, "Primitives/cylinder", true, 5.0f));
    enemyStatuses_.emplace("FireSlime", MakeStatus(45.0f, 1.0f, 2.35f, 60.0f, 60.0f, 18.0f, 24.0f,
        Vector3{ 2.0f, 2.0f, 2.0f }, "Characters/slime_red", true, 5.0f));
    enemyStatuses_.emplace("ThunderSlime", MakeStatus(45.0f, 1.0f, 3.0f, 62.0f, 60.0f, 18.0f, 20.0f,
        Vector3{ 2.0f, 2.0f, 2.0f }, "Characters/slime_yellow", true, 5.0f));
    enemyStatuses_.emplace("GiantSlime", MakeStatus(160.0f, 1.25f, 0.0f, 70.0f, 60.0f, 24.0f, 26.0f,
        Vector3{ 3.6f, 3.6f, 3.6f }, "Characters/slime_pink", true, 5.0f));
    enemyStatuses_.emplace("Bat", MakeStatus(25.0f, 0.8f, 2.6f, 0.0f, 60.0f, 10.0f, 24.0f,
        Vector3{ 0.6f, 0.6f, 0.6f }, "Characters/bat", true, 5.0f));
    enemyStatuses_.emplace("BeamDrone", MakeStatus(45.0f, 1.0f, 4.0f, 0.0f, 60.0f, 10.0f, 30.0f,
        Vector3{ 0.85f, 0.85f, 0.85f }, "Characters/eye", true, 5.0f));
    enemyStatuses_.emplace("BossCore", MakeStatus(1000.0f, 1.5f, 0.05f, 0.0f, 60.0f, 10.0f, 20.0f,
        Vector3{ 1.0f, 1.0f, 1.0f }, "Stages/block", true, 5.0f));

    dirty_ = true;
}

GameplayStatusManager::CharacterStatus* GameplayStatusManager::FindMutableEnemyStatus(const std::string& enemyType) {
    Initialize();
    const auto it = enemyStatuses_.find(enemyType);
    return it != enemyStatuses_.end() ? &it->second : nullptr;
}

const GameplayStatusManager::CharacterStatus* GameplayStatusManager::FindEnemyStatus(const std::string& enemyType) const {
    const auto it = enemyStatuses_.find(enemyType);
    return it != enemyStatuses_.end() ? &it->second : nullptr;
}

bool GameplayStatusManager::SaveIfDirty() {
    return !dirty_ || Save();
}

bool GameplayStatusManager::ApplyPlayerStatus(Object3d* object, bool resetCurrentHp) const {
    if (!object || object->GetClassName() != "Player") {
        return false;
    }
    ApplyStatus(object, playerStatus_, resetCurrentHp);
    return true;
}

bool GameplayStatusManager::ApplyEnemyStatus(Object3d* object, bool resetCurrentHp) const {
    if (!object || object->GetClassName() != "Enemy") {
        return false;
    }

    std::string enemyType = object->GetEnemyType();
    if (enemyType.empty() && object->param_.has_value()) {
        enemyType = object->param_->enemyType;
    }
    const CharacterStatus* status = FindEnemyStatus(enemyType);
    if (!status) {
        return false;
    }

    ApplyStatus(object, *status, resetCurrentHp);
    object->SetEnemyType(enemyType);
    if (!object->param_.has_value()) {
        object->param_.emplace();
    }
    object->param_->enemyType = enemyType;
    return true;
}

bool GameplayStatusManager::ApplyManagedStatus(Object3d* object, bool resetCurrentHp) const {
    if (!object) {
        return false;
    }
    if (object->GetClassName() == "Player") {
        return ApplyPlayerStatus(object, resetCurrentHp);
    }
    if (object->GetClassName() == "Enemy") {
        return ApplyEnemyStatus(object, resetCurrentHp);
    }
    return false;
}

bool GameplayStatusManager::IsManagedCharacter(const Object3d* object) {
    if (!object) {
        return false;
    }
    return object->GetClassName() == "Player" || object->GetClassName() == "Enemy";
}

const char* GameplayStatusManager::GetSettingsPath() {
    return kSettingsPath;
}

bool GameplayStatusManager::LoadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }

    try {
        json root;
        file >> root;
        if (!root.is_object()) {
            return false;
        }

        JsonToStatus(root.value("player", json::object()), playerStatus_);
        if (root.contains("enemies") && root["enemies"].is_object()) {
            for (auto& [enemyType, status] : enemyStatuses_) {
                if (root["enemies"].contains(enemyType)) {
                    JsonToStatus(root["enemies"].at(enemyType), status);
                }
            }
        }

        Normalize(playerStatus_);
        for (auto& [enemyType, status] : enemyStatuses_) {
            (void)enemyType;
            Normalize(status);
        }
        dirty_ = false;
        return true;
    } catch (...) {
        return false;
    }
}

void GameplayStatusManager::Normalize(CharacterStatus& status) {
    status.maxHp = (std::max)(status.maxHp, 1.0f);
    status.attackPower = (std::max)(status.attackPower, 0.0f);
    status.speed = (std::max)(status.speed, 0.0f);
    status.maxFallSpeed = (std::max)(status.maxFallSpeed, 0.0f);
    status.jumpPower = (std::max)(status.jumpPower, 0.0f);
    status.detectionRange = (std::max)(status.detectionRange, 0.0f);
    status.scale.x = (std::max)(status.scale.x, 0.001f);
    status.scale.y = (std::max)(status.scale.y, 0.001f);
    status.scale.z = (std::max)(status.scale.z, 0.001f);
    status.morphDuration = (std::max)(status.morphDuration, 0.1f);
}

void GameplayStatusManager::ApplyStatus(Object3d* object, const CharacterStatus& source, bool resetCurrentHp) {
    if (!object) {
        return;
    }

    CharacterStatus status = source;
    Normalize(status);

    if (!object->param_.has_value()) {
        object->param_.emplace();
    }
    auto& param = object->param_.value();

    const float previousMaxHp = (std::max)(param.maxHp, 1.0f);
    const float previousHp = std::clamp(param.hp, 0.0f, previousMaxHp);
    const bool wasFullHp = NearlyEqual(previousHp, previousMaxHp);
    const float healthRatio = previousHp / previousMaxHp;

    param.maxHp = status.maxHp;
    if (resetCurrentHp || wasFullHp) {
        param.hp = param.maxHp;
    } else {
        param.hp = std::clamp(param.maxHp * healthRatio, 0.0f, param.maxHp);
    }
    param.attackPower = status.attackPower;
    param.speed = status.speed;
    param.gravity = status.gravity;
    param.maxFallSpeed = status.maxFallSpeed;
    param.jumpPower = status.jumpPower;
    param.detectionRange = status.detectionRange;
    param.morphLimited = status.morphLimited;
    param.morphDuration = status.morphDuration;

    if (!status.modelName.empty() && object->GetModelName() != status.modelName) {
        object->SetModel(status.modelName);
    }

    const Vector3 currentScale = object->GetScale();
    if (!NearlyEqual(currentScale.x, status.scale.x) ||
        !NearlyEqual(currentScale.y, status.scale.y) ||
        !NearlyEqual(currentScale.z, status.scale.z)) {
        object->ApplyManagedScale(status.scale);
    }

    if (auto* enemy = dynamic_cast<BaseEnemy*>(object)) {
        enemy->SetDetectionRange(status.detectionRange);
    }
}
