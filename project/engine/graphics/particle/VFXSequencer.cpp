#include "VFXSequencer.h"
#include "GPUParticleManager.h"
#include "MeshEffectManager.h"
#include <fstream>
#include <json.hpp>
#include "DebugConsole.h"
#include "AudioPlayer.h"
#include "Easing.h"
using json = nlohmann::json;
void VFXSequencer::Initialize(Object3d* targetObject) {
    targetObject_ = targetObject;
    events_.clear();
    Reset();
}
void VFXSequencer::AddEvent(VFXEventType type, const std::string& presetName, float triggerTime, const Vector3& offset, const Vector3& rotation, const Vector3& scale) {
    // 構造体の順番に合わせて初期化する
    events_.push_back({ type, presetName, triggerTime, offset, rotation, scale, false, {0,5,0}, {0,0,10}, 1.0f, 0, false });
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
        e.isFinished = false;
    }
}
Vector3 CalculateBezier(const Vector3& p0, const Vector3& p1, const Vector3& p2, float t) {
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    Vector3 a = { lerp(p0.x, p1.x, t), lerp(p0.y, p1.y, t), lerp(p0.z, p1.z, t) };
    Vector3 b = { lerp(p1.x, p2.x, t), lerp(p1.y, p2.y, t), lerp(p1.z, p2.z, t) };
    return { lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t) };
}

// ==========================================================
void VFXSequencer::Update(float deltaTime) {
    if (!isPlaying_) return;
    currentTime_ += deltaTime;
    bool allFinished = true;

    for (auto& e : events_) {
        if (!e.isFinished) { // 未完了のイベントのみ処理
            if (currentTime_ >= e.triggerTime) {

                // --- 瞬間処理のグループ (GPUParticle, MeshEffect, SoundEffect) ---
                if (e.type != VFXEventType::MovingParticle && !e.hasFired) {
                    if (e.type == VFXEventType::GPUParticle) {
                        Vector3 spawnPos = e.offset;
                        Matrix4x4 emitMat = Math::MakeIdentity4x4();
                        if (targetObject_) {
                            Matrix4x4 worldMat = targetObject_->GetWorldMatrix();
                            spawnPos = Math::TransformNormal(e.offset, worldMat);
                            spawnPos.x += worldMat.m[3][0]; spawnPos.y += worldMat.m[3][1]; spawnPos.z += worldMat.m[3][2];
                            emitMat = worldMat;
                        }
                        GPUParticleManager::GetInstance()->Emit(e.presetName, spawnPos, emitMat);
                    }
                    else if (e.type == VFXEventType::MeshEffect) {
                        std::string path = "Resources/json/effect/" + e.presetName + ".json";
                        MeshEffectManager::GetInstance()->SpawnEffect(path, targetObject_, e.offset, e.rotation, e.scale);
                    }
                    else if (e.type == VFXEventType::SoundEffect) {
                        std::string path = "Resources/audio/se/" + e.presetName;
                        uint32_t soundHandle = AudioPlayer::GetInstance()->LoadSoundFile(path);
                        AudioPlayer::GetInstance()->PlaySE(soundHandle, false, 1.0f);
                    }
                    e.hasFired = true;
                    e.isFinished = true; // 1回で完了
                }
                // --- ★追加: 継続処理 (軌跡パーティクル) ---
                else if (e.type == VFXEventType::MovingParticle) {
                    e.hasFired = true;

                    // 進行度 (0.0 ～ 1.0) を計算
                    float progress = (currentTime_ - e.triggerTime) / e.duration;
                    if (progress >= 1.0f) {
                        progress = 1.0f;
                        e.isFinished = true; // 移動完了
                    }

                    // イージングを適用
                    float easeT = progress;
                    if (e.easingType == 2) easeT = Easing::OutSine(progress);
                    else if (e.easingType == 4) easeT = Easing::InQuad(progress);


                    // ベジェ曲線でローカル座標を計算
                    Vector3 localPos = CalculateBezier(e.offset, e.controlPoint, e.endOffset, easeT);

                    Vector3 spawnPos = localPos;
                    Matrix4x4 emitMat = Math::MakeIdentity4x4();

                    if (targetObject_) {
                        Matrix4x4 worldMat = targetObject_->GetWorldMatrix();
                        spawnPos = Math::TransformNormal(localPos, worldMat);
                        spawnPos.x += worldMat.m[3][0];
                        spawnPos.y += worldMat.m[3][1];
                        spawnPos.z += worldMat.m[3][2];
                        emitMat = worldMat;
                    }

                    // emitMat（パーティクルの基準行列）の平行移動成分を最終的なspawnPosで上書きする
                    emitMat.m[3][0] = spawnPos.x;
                    emitMat.m[3][1] = spawnPos.y;
                    emitMat.m[3][2] = spawnPos.z;

                    // 毎フレーム Emit して軌跡を作る！
                    GPUParticleManager::GetInstance()->Emit(e.presetName, spawnPos, emitMat);
                }
            }

            // まだ終わっていないイベントがあるなら終了させない
            if (!e.isFinished) {
                allFinished = false;
            }
        }
    }
    if (allFinished) isPlaying_ = false;
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
        eventJson["rotation"] = { e.rotation.x, e.rotation.y, e.rotation.z };
        eventJson["scale"] = { e.scale.x, e.scale.y, e.scale.z };
        eventJson["controlPoint"] = { e.controlPoint.x, e.controlPoint.y, e.controlPoint.z };
        eventJson["endOffset"] = { e.endOffset.x, e.endOffset.y, e.endOffset.z };
        eventJson["duration"] = e.duration;
        eventJson["easingType"] = e.easingType;
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
                if (eventJson.contains("rotation")) {
                    e.rotation.x = eventJson["rotation"][0];
                    e.rotation.y = eventJson["rotation"][1];
                    e.rotation.z = eventJson["rotation"][2];
                }
                if (eventJson.contains("scale")) {
                    e.scale.x = eventJson["scale"][0];
                    e.scale.y = eventJson["scale"][1];
                    e.scale.z = eventJson["scale"][2];
                }
                if (eventJson.contains("controlPoint")) {
                    e.controlPoint.x = eventJson["controlPoint"][0];
                    e.controlPoint.y = eventJson["controlPoint"][1];
                    e.controlPoint.z = eventJson["controlPoint"][2];
                }
                else {
                    e.controlPoint = { 0.0f, 5.0f, 0.0f };
                }
                if (eventJson.contains("endOffset")) {
                    e.endOffset.x = eventJson["endOffset"][0];
                    e.endOffset.y = eventJson["endOffset"][1];
                    e.endOffset.z = eventJson["endOffset"][2];
                }
                else {
                    e.endOffset = { 0.0f, 0.0f, 10.0f };
                }
                if (eventJson.contains("duration")) {
                    e.duration = eventJson["duration"];
                }
                else {
                    e.duration = 1.0f;
                }
                if (eventJson.contains("easingType")) {
                    e.easingType = eventJson["easingType"];
                }
                else {
                    e.easingType = 0;
                }
                e.hasFired = false;
                e.isFinished = false;
                events_.push_back(e);
            }
        }
        if (DebugConsole::GetInstance()) {
            DebugConsole::GetInstance()->AddLog("Loaded VFX Sequence: " + sequenceName);
        }
    }
}