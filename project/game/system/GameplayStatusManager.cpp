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

GameplayStatusManager::CharacterStatus MakeStatus(float maxHp, float attackPower, float speed, float gravity,
                                                  float maxFallSpeed, float jumpPower, float detectionRange,
                                                  const engine::Vector3& scale, const char* modelName, bool morphLimited,
                                                  float morphDuration) {
    // 敵タイプごとの初期値を一つの構造体へ揃え、JSON未設定項目の基準にする。
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

// 設定JSONに数値項目がある場合だけ既存の値を更新します。
void ReadFloat(const json& source, const char* key, float& value) {
    // 数値として読める項目だけを上書きし、欠落・型違いでは初期値を保つ。
    if (source.is_object() && source.contains(key) && source.at(key).is_number()) {
        value = source.at(key).get<float>();
    }
}

// 設定JSONに真偽値項目がある場合だけ既存の値を更新します。
void ReadBool(const json& source, const char* key, bool& value) {
    // Bool型が確認できた場合だけ既定値を変更する。
    if (source.is_object() && source.contains(key) && source.at(key).is_boolean()) {
        value = source.at(key).get<bool>();
    }
}

void ReadString(const json& source, const char* key, std::string& value) {
    // 空のモデル名で既定モデルを消さないよう、非空文字列だけ採用する。
    if (source.is_object() && source.contains(key) && source.at(key).is_string()) {
        const std::string candidate = source.at(key).get<std::string>();
        if (!candidate.empty()) {
            value = candidate;
        }
    }
}

void ReadScale(const json& source, engine::Vector3& value) {
    if (!source.is_object() || !source.contains("scale") || !source.at("scale").is_array()) {
        return;
    }

    const json& array = source.at("scale");
    if (array.size() < 3 || !array[0].is_number() || !array[1].is_number() || !array[2].is_number()) {
        return;
    }

    const engine::Vector3 candidate = {array[0].get<float>(), array[1].get<float>(), array[2].get<float>()};
    // 旧設定の0スケールは「クラス側の値を維持」という意味だったため、
    // 一元管理移行後はタイプ標準値を残します。
    if (candidate.x > 0.0f && candidate.y > 0.0f && candidate.z > 0.0f) {
        value = candidate;
    }
}

void JsonToStatus(const json& source, GameplayStatusManager::CharacterStatus& status) {
    // JSONに存在する値だけを型確認して反映し、古いファイルの不足項目は既定値へ任せる。
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
    if (source.contains("characterMotor") && source.at("characterMotor").is_object()) {
        const json& motor = source.at("characterMotor");
        ReadBool(motor, "continuousCollision", status.motorContinuousCollision);
        ReadBool(motor, "snapToGround", status.motorSnapToGround);
        ReadFloat(motor, "maxSlopeDegrees", status.motorMaxSlopeDegrees);
        ReadFloat(motor, "stepHeight", status.motorStepHeight);
        ReadFloat(motor, "groundProbeDistance", status.motorGroundProbeDistance);
        ReadFloat(motor, "skinWidth", status.motorSkinWidth);
    }
}

json StatusToJson(const GameplayStatusManager::CharacterStatus& status) {
    // キャラクター本体と移動モーターの設定を同じJSON構造へ保存する。
    return json{{"maxHp", status.maxHp},
                {"attackPower", status.attackPower},
                {"speed", status.speed},
                {"gravity", status.gravity},
                {"maxFallSpeed", status.maxFallSpeed},
                {"jumpPower", status.jumpPower},
                {"detectionRange", status.detectionRange},
                {"scale", json::array({status.scale.x, status.scale.y, status.scale.z})},
                {"modelName", status.modelName},
                {"morphLimited", status.morphLimited},
                {"morphDuration", status.morphDuration},
                {"characterMotor",
                 {{"continuousCollision", status.motorContinuousCollision},
                  {"snapToGround", status.motorSnapToGround},
                  {"maxSlopeDegrees", status.motorMaxSlopeDegrees},
                  {"stepHeight", status.motorStepHeight},
                  {"groundProbeDistance", status.motorGroundProbeDistance},
                  {"skinWidth", status.motorSkinWidth}}}};
}

// キャラクターの調整値を読み書きし、既存のHPを保つか初期化するかを呼び出し側へ委ねます。

bool NearlyEqual(float lhs, float rhs) {
    // HPの全快判定では浮動小数点の微小誤差を許容する。
    return std::abs(lhs - rhs) <= 0.0001f;
}
} // namespace

GameplayStatusManager* GameplayStatusManager::GetInstance() {
    // 設定管理はシーンをまたいで同じ値を参照する単一実体とする。
    static GameplayStatusManager instance;
    return &instance;
}

GameplayStatusManager::~GameplayStatusManager() {
    // 終了時に未保存の調整値だけを書き出す。
    SaveIfDirty();
}

void GameplayStatusManager::Initialize() {
    // 新設定を優先し、存在しない場合だけ旧ファイルを一度読み込んで移行する。
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
    // 再読込に失敗した場合は編集中の値を戻し、失敗したJSONで上書きしない。
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
    // プレイヤーと敵タイプ別の設定を一つのファイルへ保存し、成功時だけdirtyを解除する。
    Initialize();

    json root;
    root["schemaVersion"] = 5;
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
    // JSONに項目が無い場合の基準値をタイプ別に用意し、再読込前の土台にする。
    playerStatus_ =
        MakeStatus(5.0f, 1.0f, 27.7f, 50.0f, 60.0f, 24.0f, 20.0f, {2.0f, 2.0f, 2.0f}, "Characters/slime", false, 5.0f);

    enemyStatuses_.clear();
    enemyStatuses_.emplace("Slime", MakeStatus(50.0f, 1.0f, 0.1f, 60.0f, 60.0f, 18.0f, 20.0f, engine::Vector3{2.0f, 2.0f, 2.0f},
                                               "Characters/slime_pink", false, 5.0f));
    enemyStatuses_.emplace("Bomb", MakeStatus(30.0f, 1.0f, 0.04f, 60.0f, 60.0f, 16.0f, 20.0f,
                                              engine::Vector3{0.16f, 0.16f, 0.16f}, "Gimmicks/blob", true, 5.0f));
    enemyStatuses_.emplace("Bomber", MakeStatus(60.0f, 1.15f, 0.0f, 60.0f, 60.0f, 16.0f, 32.0f,
                                                engine::Vector3{2.0f, 2.0f, 2.0f}, "Characters/slime_black", true, 5.0f));
    enemyStatuses_.emplace("FireSlime", MakeStatus(45.0f, 1.0f, 2.35f, 60.0f, 60.0f, 18.0f, 24.0f,
                                                   engine::Vector3{2.0f, 2.0f, 2.0f}, "Characters/slime_red", true, 5.0f));
    enemyStatuses_.emplace("ThunderSlime",
                           MakeStatus(45.0f, 1.0f, 3.0f, 62.0f, 60.0f, 18.0f, 20.0f, engine::Vector3{2.0f, 2.0f, 2.0f},
                                      "Characters/slime_yellow", true, 5.0f));
    enemyStatuses_.emplace("WindSlime", MakeStatus(42.0f, 0.9f, 3.4f, 54.0f, 58.0f, 19.0f, 22.0f,
                                                   engine::Vector3{2.0f, 2.0f, 2.0f}, "Characters/slime_wind", false, 5.0f));
    enemyStatuses_.emplace("GiantSlime", MakeStatus(160.0f, 1.25f, 0.0f, 70.0f, 60.0f, 24.0f, 26.0f,
                                                    engine::Vector3{3.6f, 3.6f, 3.6f}, "Characters/slime_pink", true, 5.0f));
    enemyStatuses_.emplace("PrismSlime", MakeStatus(420.0f, 1.35f, 1.35f, 72.0f, 62.0f, 22.0f, 90.0f,
                                                    engine::Vector3{4.2f, 4.2f, 4.2f}, "Characters/prism_slime", true, 5.0f));
    enemyStatuses_.emplace("MagmaSlime", MakeStatus(360.0f, 1.45f, 1.45f, 72.0f, 62.0f, 22.0f, 90.0f,
                                                    engine::Vector3{3.8f, 3.8f, 3.8f}, "Characters/magma_slime", true, 5.0f));
    enemyStatuses_.emplace("FalseKingSlime",
                           MakeStatus(720.0f, 1.6f, 2.1f, 68.0f, 62.0f, 24.0f, 90.0f, engine::Vector3{1.3f, 1.3f, 1.3f},
                                      "Characters/false_king_slime", true, 5.0f));
    enemyStatuses_.emplace("RingBurner", MakeStatus(180.0f, 1.0f, 0.0f, 0.0f, 60.0f, 10.0f, 30.0f,
                                                    engine::Vector3{1.0f, 1.0f, 1.0f}, "Characters/ring_burner", true, 5.0f));

    dirty_ = true;
}

GameplayStatusManager::CharacterStatus* GameplayStatusManager::FindMutableEnemyStatus(const std::string& enemyType) {
    // 編集時は初期化後の登録済みタイプだけを変更可能な参照として返す。
    Initialize();
    const auto it = enemyStatuses_.find(enemyType);
    return it != enemyStatuses_.end() ? &it->second : nullptr;
}

const GameplayStatusManager::CharacterStatus*
GameplayStatusManager::FindEnemyStatus(const std::string& enemyType) const {
    // 未登録の敵タイプはnullptrで知らせ、別タイプの値を誤適用しない。
    const auto it = enemyStatuses_.find(enemyType);
    return it != enemyStatuses_.end() ? &it->second : nullptr;
}

bool GameplayStatusManager::SaveIfDirty() {
    // 変更がない場合はディスクへ書かず、変更時だけ保存結果を返す。
    return !dirty_ || Save();
}

bool GameplayStatusManager::ApplyPlayerStatus(engine::Object3d* object, bool resetCurrentHp) const {
    // Player以外には適用せず、HPを全快させるかは呼び出し側の指定を守る。
    if (!object || object->GetClassName() != "Player") {
        return false;
    }
    ApplyStatus(object, playerStatus_, resetCurrentHp);
    return true;
}

bool GameplayStatusManager::ApplyEnemyStatus(engine::Object3d* object, bool resetCurrentHp) const {
    // 敵タイプに対応する設定だけを適用し、旧データではParam内のタイプ名も参照する。
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

bool GameplayStatusManager::ApplyManagedStatus(engine::Object3d* object, bool resetCurrentHp) const {
    // クラス名でプレイヤー・敵を振り分け、それ以外のシーン物体には値を適用しない。
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

bool GameplayStatusManager::IsManagedCharacter(const engine::Object3d* object) {
    // 共通ステータス管理の対象はPlayerとEnemyに限定する。
    if (!object) {
        return false;
    }
    return object->GetClassName() == "Player" || object->GetClassName() == "Enemy";
}

const char* GameplayStatusManager::GetSettingsPath() {
    // エディターとランタイムが同じ設定ファイルを参照できるようパスを公開する。
    return kSettingsPath;
}

bool GameplayStatusManager::LoadFromFile(const std::string& path) {
    // 読み込んだ項目を既定値へ重ね、型違いや読込失敗では呼び出し側へ失敗を返す。
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
    // JSON や Editor 入力の極端な値で移動・当たり判定が破綻しないよう範囲を制限する。
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
    status.motorMaxSlopeDegrees = std::clamp(status.motorMaxSlopeDegrees, 0.0f, 89.9f);
    status.motorStepHeight = (std::max)(status.motorStepHeight, 0.0f);
    status.motorGroundProbeDistance = (std::max)(status.motorGroundProbeDistance, 0.0f);
    status.motorSkinWidth = std::clamp(status.motorSkinWidth, 0.0f, 0.25f);
}

// 最大 HP の変更時は現在の残量比を保ち、全快中または明示的なリセット時だけ全快させます。
void GameplayStatusManager::ApplyStatus(engine::Object3d* object, const CharacterStatus& source, bool resetCurrentHp) {
    // HP以外の値は正規化済み設定へ揃え、HPだけは残量比または明示リセットを基準にする。
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
    param.motorContinuousCollision = status.motorContinuousCollision;
    param.motorSnapToGround = status.motorSnapToGround;
    param.motorMaxSlopeDegrees = status.motorMaxSlopeDegrees;
    param.motorStepHeight = status.motorStepHeight;
    param.motorGroundProbeDistance = status.motorGroundProbeDistance;
    param.motorSkinWidth = status.motorSkinWidth;

    if (!status.modelName.empty() && object->GetModelName() != status.modelName) {
        object->SetModel(status.modelName);
    }

    const engine::Vector3 currentScale = object->GetScale();
    if (!NearlyEqual(currentScale.x, status.scale.x) || !NearlyEqual(currentScale.y, status.scale.y) ||
        !NearlyEqual(currentScale.z, status.scale.z)) {
        object->ApplyManagedScale(status.scale);
    }

    if (auto* enemy = dynamic_cast<BaseEnemy*>(object)) {
        enemy->SetDetectionRange(status.detectionRange);
    }
}
