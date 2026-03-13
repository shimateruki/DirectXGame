#include "VFXSequencer.h"
#include "GPUParticleManager.h"
#include <fstream>
#include <json.hpp>
#include "DebugConsole.h"
using json = nlohmann::json;
void VFXSequencer::Initialize(Transform* targetTransform) {
    targetTransform_ = targetTransform;
    events_.clear();
    Reset();
}

void VFXSequencer::AddEvent(const std::string& presetName, float triggerTime, const Vector3& offset) {
    events_.push_back({ presetName, triggerTime, offset, false });
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
        // まだ発火していないイベントかチェック
        if (!e.hasFired) {
            // 指定した時間を過ぎたら発火！
            if (currentTime_ >= e.triggerTime) {
                Vector3 spawnPos = { 0, 0, 0 };

                if (targetTransform_) {
                    Matrix4x4 worldMat = targetTransform_->matWorld;
                    Math math;
                    // オフセットをキャラクターの向きに合わせて回転
                    Vector3 worldOffset = math.TransformNormal(e.offset, worldMat);

                    spawnPos.x = worldMat.m[3][0] + worldOffset.x;
                    spawnPos.y = worldMat.m[3][1] + worldOffset.y;
                    spawnPos.z = worldMat.m[3][2] + worldOffset.z;
                } else {
                    spawnPos = e.offset; // ターゲットがいなければ絶対座標
                }

                // 魔法の1行でエフェクト召喚！
                GPUParticleManager::GetInstance()->Emit(e.presetName, spawnPos);
                e.hasFired = true; // 発火済みにする
            } else {
                allFired = false; // まだ未来に発火するイベントが残っている
            }
        }
    }

    // 全てのイベントを撃ち終わったら自動で停止する
    if (allFired) {
        isPlaying_ = false;
    }
}



// ==========================================================
//  タイムラインをJSONに保存
// ==========================================================
void VFXSequencer::Save(const std::string& sequenceName) {
    json j;
    j["events"] = json::array(); // 空の配列を作成

    for (const auto& e : events_) {
        json eventJson;
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