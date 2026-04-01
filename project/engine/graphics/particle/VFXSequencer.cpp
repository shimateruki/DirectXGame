#include "VFXSequencer.h"
#include "GPUParticleManager.h"
#include "MeshEffectManager.h"
#include <fstream>
#include <json.hpp>
#include "DebugConsole.h"
using json = nlohmann::json;
void VFXSequencer::Initialize(Object3d* targetObject) {
    targetObject_ = targetObject;
    events_.clear();
    Reset();
}
void VFXSequencer::AddEvent(VFXEventType type, const std::string& presetName, float triggerTime, const Vector3& offset) {
    // 構造体の順番に合わせて { type, presetName, triggerTime, offset, hasFired } とする
    events_.push_back({ type, presetName, triggerTime, offset, false });
}

void VFXSequencer::Play() {
    Reset(); // 最初から再生するためにリセット
    isPlaying_ = true;
}

void VFXSequencer::Stop() {
    isPlaying_ = false;
}

void VFXSequencer::Reset() {
    currentTime_ = 0.0f;
    isPlaying_ = false;
    for (auto& e : events_) {
        e.hasFired = false; // 全イベントを未発火に戻す
    }
}
void VFXSequencer::Update(float deltaTime) {
    if (!isPlaying_) return;
    currentTime_ += deltaTime;
    bool allFired = true;

    for (auto& e : events_) {
        if (!e.hasFired) {
            if (currentTime_ >= e.triggerTime) {
                if (e.type == VFXEventType::GPUParticle) {
                    // --- ① パーティクルの発生処理 ---
                    Vector3 spawnPos = e.offset;
                    Matrix4x4 emitMat = Math::MakeIdentity4x4();
                    if (targetObject_) {
                        Matrix4x4 worldMat = targetObject_->GetWorldMatrix();
                        spawnPos = Math::TransformNormal(e.offset, worldMat);
                        spawnPos.x += worldMat.m[3][0];
                        spawnPos.y += worldMat.m[3][1];
                        spawnPos.z += worldMat.m[3][2];
                        emitMat = worldMat;
                    }
                    GPUParticleManager::GetInstance()->Emit(e.presetName, spawnPos, emitMat);
                }
                else if (e.type == VFXEventType::MeshEffect) {
                    // --- ② メッシュエフェクトの発生処理 ---
                    std::string path = "Resources/json/effect/" + e.presetName + ".json";

                    // ★修正: 引数の targetObject_ を削除！
                    // エフェクト側(JSON)で設定された TargetName に完全に任せます
                    MeshEffectManager::GetInstance()->SpawnEffect(path);
                }
                e.hasFired = true;
            }
            else {
                allFired = false;
            }
        }
    }
    if (allFired) isPlaying_ = false;
}
// ==========================================================
//  タイムラインをJSONに保存
// ==========================================================
void VFXSequencer::Save(const std::string& sequenceName) {
    json j;
    j["events"] = json::array(); // 空の配列を作成

    for (const auto& e : events_) {
        json eventJson;
        eventJson["type"] = static_cast<int>(e.type);
        eventJson["presetName"] = e.presetName;
        eventJson["triggerTime"] = e.triggerTime;
        eventJson["offset"] = { e.offset.x, e.offset.y, e.offset.z };
        j["events"].push_back(eventJson);
    }

    std::string filepath = "Resources/json/vfx_sequence/" + sequenceName + ".json";

    // フォルダがなければ作る (C++17 filesystem)
    std::filesystem::create_directories("Resources/json/vfx_sequence/");

    std::ofstream file(filepath);
    if (file.is_open()) {
        file << j.dump(4);
        file.close();
        if (DebugConsole::GetInstance()) {
            DebugConsole::GetInstance()->AddLog("Saved VFX Sequence: " + sequenceName);
        }
    }
}

// ==========================================================
//  タイムラインをJSONから復元
// ==========================================================
void VFXSequencer::Load(const std::string& sequenceName) {
    std::string filepath = "Resources/json/vfx_sequence/" + sequenceName + ".json";
    std::ifstream file(filepath);
    if (file.is_open()) {
        json j;
        file >> j;
        file.close();

        events_.clear(); // 現在のリストをリセット

        if (j.contains("events") && j["events"].is_array()) {
            for (const auto& eventJson : j["events"]) {
                VFXEvent e;
                if (eventJson.contains("type")) {
                    e.type = static_cast<VFXEventType>(eventJson["type"].get<int>());
                }
                else {
                    e.type = VFXEventType::GPUParticle;
                }
                if (eventJson.contains("presetName")) e.presetName = eventJson["presetName"];
                if (eventJson.contains("triggerTime")) e.triggerTime = eventJson["triggerTime"];
                if (eventJson.contains("offset")) {
                    e.offset.x = eventJson["offset"][0];
                    e.offset.y = eventJson["offset"][1];
                    e.offset.z = eventJson["offset"][2];
                }
                e.hasFired = false;
                events_.push_back(e);
            }
        }
        if (DebugConsole::GetInstance()) {
            DebugConsole::GetInstance()->AddLog("Loaded VFX Sequence: " + sequenceName);
        }
    }
}