#include "engine/graphics/effect/VfxAssetPath.h"
#include "VfxSequencer.h"
#include <numbers>
#include "AudioPlayer.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "Easing.h"
#include "InputManager.h"
#include "GpuParticleManager.h"
#include "LightManager.h"
#include "MeshEffectManager.h"
#include "engine/graphics/effect/DebrisEffectManager.h"
#include "PostEffect.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <memory>
#include <unordered_map>

using json = nlohmann::json;

namespace {
    constexpr const char* kSequenceDirectory = "Resources/json/vfx_sequence/";
    constexpr float kDegreesToRadians = std::numbers::pi_v<float> / 180.0f;
    float gameplayTimeScale = 1.0f;

    std::vector<std::unique_ptr<VfxSequencer>>& ActiveOneShotSequences() {
        // 呼び出し元がインスタンスを保持しない単発演出を、再生終了まで生存させる。
        static std::vector<std::unique_ptr<VfxSequencer>> sequences;
        return sequences;
    }

    // 停止要求一件の残り秒と時間倍率。停止を解除できるよう残り時間は非スケール時間で減らす。
    // 担当：一つのヒットストップ要求の残り秒数と時間倍率。
    // 関連：VFXSequencerの内部処理だけで使う補助型。
    struct HitStopLayer {
        float remaining = 0.0f;
        float timeScale = 0.0f;
    };

    std::vector<HitStopLayer>& ActiveHitStops() {
        // 複数のヒットストップを重ねられるよう、各要求の残り時間を独立して保持する。
        static std::vector<HitStopLayer> layers;
        return layers;
    }

    // JSONの解析済みイベントを名前ごとに保持し、同じ演出の再読込を減らします。
    std::unordered_map<std::string, std::vector<VfxEvent>>& SequenceEventCache() {
        static std::unordered_map<std::string, std::vector<VfxEvent>> cache;
        return cache;
    }

    // キャッシュの有効性を判定するため、各JSONの最終更新時刻を保持します。
    std::unordered_map<std::string, std::filesystem::file_time_type>& SequenceWriteTimeCache() {
        static std::unordered_map<std::string, std::filesystem::file_time_type> cache;
        return cache;
    }

    void ResetEventRuntimeState(std::vector<VfxEvent>& events) {
        // 再生をやり直す際に発火状態と適用済みの画面効果を初期化する。
        for (auto& event : events) {
            event.hasFired = false;
            event.isFinished = false;
            event.hasAppliedPostPulse = false;
            event.appliedRadialIntensity = 0.0f;
            event.appliedDamageFlash = 0.0f;
            event.appliedChromaticAberration = 0.0f;
            event.appliedWobbleIntensity = 0.0f;
            event.appliedBloomIntensity = 0.0f;
        }
    }

    engine::Vector3 ReadVector3(const json& value, const engine::Vector3& fallback) {
        // 旧データで座標配列が欠けている場合は呼び出し側の既定値を維持する。
        if (!value.is_array() || value.size() < 3)
            return fallback;
        return {
            value[0].get<float>(),
            value[1].get<float>(),
            value[2].get<float>()
        };
    }

    json WriteVector3(const engine::Vector3& value) {
        return json::array({ value.x, value.y, value.z });
    }

    engine::Vector4 ReadVector4(const json& value, const engine::Vector4& fallback) {
        // 古いJSONで色配列が欠けている場合は、イベントの既定色を保持する。
        if (!value.is_array() || value.size() < 4)
            return fallback;
        return {
            value[0].get<float>(),
            value[1].get<float>(),
            value[2].get<float>(),
            value[3].get<float>()
        };
    }

    json WriteVector4(const engine::Vector4& value) {
        return json::array({ value.x, value.y, value.z, value.w });
    }

    float ApplyEventEasing(int easingType, float progress) {
        // 保存済みの曲線番号を再生側の補間へ対応付け、不明な番号は線形扱いにする。
        progress = std::clamp(progress, 0.0f, 1.0f);
        if (easingType == 2)
            return Easing::OutSine(progress);
        if (easingType == 4)
            return Easing::InQuad(progress);
        return progress;
    }

    engine::Vector3 CalculateBezier(const engine::Vector3& p0, const engine::Vector3& p1, const engine::Vector3& p2, float t) {
        // ２回の線形補間から二次ベジェ曲線上の位置を求める。
        auto lerp = [](float a, float b, float ratio) {
            return a + (b - a) * ratio;
            };

        engine::Vector3 a = {
            lerp(p0.x, p1.x, t),
            lerp(p0.y, p1.y, t),
            lerp(p0.z, p1.z, t)
        };

        // 始点から制御点、制御点から終点の二点をそれぞれ求め、最後にその間を補間します。
        engine::Vector3 b = {
            lerp(p1.x, p2.x, t),
            lerp(p1.y, p2.y, t),
            lerp(p1.z, p2.z, t)
        };

        return {
            lerp(a.x, b.x, t),
            lerp(a.y, b.y, t),
            lerp(a.z, b.z, t)
        };
    }

    engine::Vector3 ResolveTargetPosition(engine::Object3D* targetObject, const engine::Vector3& localOffset, engine::Matrix4x4& emitMatrix) {
    // 対象がある場合はローカル位置をワールドへ移し、放出行列も同じ基準にする。
        emitMatrix = engine::Math::MakeIdentity4x4();
        if (!targetObject)
            return localOffset;

        engine::Matrix4x4 worldMat = targetObject->GetWorldMatrix();
        engine::Vector3 spawnPos = engine::Math::TransformNormal(localOffset, worldMat);
        spawnPos.x += worldMat.m[3][0];
        spawnPos.y += worldMat.m[3][1];
        spawnPos.z += worldMat.m[3][2];
        emitMatrix = worldMat;
        return spawnPos;
    }

    engine::Vector3 ResolveSequencerPosition(
        engine::Object3D* targetObject,
        bool useRootPosition,
        const engine::Vector3& rootPosition,
        const engine::Vector3& rootScale,
        const engine::Vector3& rootRotation,
        const engine::Vector3& localOffset,
        engine::Matrix4x4& emitMatrix) {
        // 対象に付ける演出はローカルオフセット、対象なしはルート座標を基準に解決する。
        if (targetObject) {
            engine::Vector3 targetLocalOffset = {
                localOffset.x * rootScale.x,
                localOffset.y * rootScale.y,
                localOffset.z * rootScale.z
            };

            if (useRootPosition) {
                targetLocalOffset.x += rootPosition.x;
                targetLocalOffset.y += rootPosition.y;
                targetLocalOffset.z += rootPosition.z;
            }

            return ResolveTargetPosition(targetObject, targetLocalOffset, emitMatrix);
        }

        engine::Vector3 scaledOffset = {
            localOffset.x * rootScale.x,
            localOffset.y * rootScale.y,
            localOffset.z * rootScale.z
        };

        // 対象を持たない場合は拡縮したオフセットをルートの向きへ回し、その後に基準位置を足します。
        const engine::Matrix4x4 rootRotateMatrix = engine::Math::MakeRotateMatrix(rootRotation);
        engine::Vector3 spawnPos = engine::Math::TransformNormal(scaledOffset, rootRotateMatrix);

        if (useRootPosition) {
            spawnPos.x += rootPosition.x;
            spawnPos.y += rootPosition.y;
            spawnPos.z += rootPosition.z;
        }

        emitMatrix = engine::Math::MakeAffineMatrix(rootScale, rootRotation, spawnPos);
        return spawnPos;
    }

    void RemovePostPulseContribution(VfxEvent& event) {
        if (!event.hasAppliedPostPulse)
            return;

        // 前フレームに加算した分だけ戻し、別イベントの寄与を消さない。
        PostEffect::Params* params = PostEffect::GetInstance()->GetParams();

        if (params) {
            params->radialIntensity -= event.appliedRadialIntensity;
            params->damageFlash -= event.appliedDamageFlash;
            params->chromaticAberration -= event.appliedChromaticAberration;
            params->wobbleIntensity -= event.appliedWobbleIntensity;
            params->bloomIntensity -= event.appliedBloomIntensity;
        }

        event.hasAppliedPostPulse = false;
        event.appliedRadialIntensity = 0.0f;
        event.appliedDamageFlash = 0.0f;
        event.appliedChromaticAberration = 0.0f;
        event.appliedWobbleIntensity = 0.0f;
        event.appliedBloomIntensity = 0.0f;
    }

    // 前フレームの寄与を戻してから現在の包絡線を加え、画面効果が蓄積しないようにします。
    void ApplyPostPulse(VfxEvent& event, float currentTime) {
        PostEffect::Params* params = PostEffect::GetInstance()->GetParams();

        if (!params) {
            event.isFinished = true;
            return;
        }

        // 毎フレーム「前回の寄与を引く→今回の包絡線を足す」を行い、値の蓄積を防ぐ。
        RemovePostPulseContribution(event);

        const float duration = (std::max)(event.duration, 0.01f);
        const float progress = std::clamp((currentTime - event.triggerTime) / duration, 0.0f, 1.0f);

        if (progress >= 1.0f) {
            event.isFinished = true;
            return;
        }

        // 開始の倍率1から終了の倍率0まで減衰させ、イベントごとの最大値に同じ倍率を掛けます。
        const float envelope = 1.0f - ApplyEventEasing(event.easingType, progress);
        event.appliedRadialIntensity = event.radialIntensity * envelope;
        event.appliedDamageFlash = event.damageFlash * envelope;
        event.appliedChromaticAberration = event.chromaticAberration * envelope;
        event.appliedWobbleIntensity = event.wobbleIntensity * envelope;
        event.appliedBloomIntensity = event.bloomIntensity * envelope;

        params->radialIntensity += event.appliedRadialIntensity;
        params->damageFlash += event.appliedDamageFlash;
        params->chromaticAberration += event.appliedChromaticAberration;
        params->wobbleIntensity += event.appliedWobbleIntensity;
        params->bloomIntensity += event.appliedBloomIntensity;
        event.hasAppliedPostPulse = true;
        event.hasFired = true;
    }


    float GetVfxEventEndTime(const VfxEvent& event) {
        // イベントの種類ごとに有効な継続時間を求め、シーケンスの終了判定に使う。
        if ((event.type == VfxEventType::GpuParticle && event.emissionRate > 0) ||
            event.type == VfxEventType::MovingParticle ||
            event.type == VfxEventType::CameraShake ||
            event.type == VfxEventType::PostEffectPulse ||
            event.type == VfxEventType::LightPulse ||
            event.type == VfxEventType::HitStop ||
            event.type == VfxEventType::ControllerRumble ||
            event.type == VfxEventType::CameraFovPulse) {
            return event.triggerTime + (std::max)(event.duration, 0.01f);
        }

        return event.triggerTime + 0.08f;
    }
}

void VfxSequencer::Initialize(engine::Object3D* targetObject) {
    // 以前のイベントとルート座標を消してから、新しい対象へシーケンスを結び直す。
    Reset();
    targetObject_ = targetObject;
    useRootPosition_ = false;
    rootPosition_ = { 0.0f, 0.0f, 0.0f };
    rootScale_ = { 1.0f, 1.0f, 1.0f };
    rootRotation_ = { 0.0f, 0.0f, 0.0f };
    sequenceName_.clear();
    events_.clear();
}

void VfxSequencer::SetRootPosition(const engine::Vector3& rootPosition) {
    rootPosition_ = rootPosition;
    useRootPosition_ = true;
}

void VfxSequencer::SetRootScale(const engine::Vector3& rootScale) {
    // ゼロ倍率を避け、演出のルート変換を常に計算可能にする。
    rootScale_ = {
        (std::max)(0.001f, rootScale.x),
        (std::max)(0.001f, rootScale.y),
        (std::max)(0.001f, rootScale.z)
    };
}

void VfxSequencer::SetRootRotation(const engine::Vector3& rootRotation) {
    rootRotation_ = rootRotation;
}

void VfxSequencer::ClearRootPosition() {
    // 位置の固定を解除する際は、その位置に付随する拡縮と回転も既定値へ戻す。
    rootPosition_ = { 0.0f, 0.0f, 0.0f };
    rootScale_ = { 1.0f, 1.0f, 1.0f };
    rootRotation_ = { 0.0f, 0.0f, 0.0f };
    useRootPosition_ = false;
}

void VfxSequencer::AddEvent(
    VfxEventType type,
    const std::string& presetName,
    float triggerTime,
    const engine::Vector3& offset,
    const engine::Vector3& rotation,
    const engine::Vector3& scale) {
    VfxEvent event;
    event.type = type;
    event.presetName = presetName;
    event.triggerTime = triggerTime;
    event.offset = offset;
    event.rotation = rotation;
    event.scale = scale;
    events_.push_back(event);
}

void VfxSequencer::Play() {
    Reset();
    isPlaying_ = true;
}

void VfxSequencer::Stop() {
    Reset();
}

void VfxSequencer::Reset() {
    currentTime_ = 0.0f;
    isPlaying_ = false;

    for (auto& event : events_) {
        // 加算型の画面効果を取り除いてから発火状態を戻し、再生し直しても効果を積み残さない。
        RemovePostPulseContribution(event);
        event.hasFired = false;
        event.isFinished = false;
    }
}

float VfxSequencer::GetDuration() const {
    // 最後に終了するイベントまでをシーケンス全体の長さとする。
    float duration = 0.0f;

    for (const auto& event : events_) {
        duration = (std::max)(duration, GetVfxEventEndTime(event));
    }

    return duration;
}

// 発火時刻に達した単発・継続イベントを登録順に処理し、全件の終了時に再生を止めます。
void VfxSequencer::Update(float deltaTime) {
    if (!isPlaying_)
        return;
    if (deltaTime <= 0.0001f)
        return;

    float timeStep = deltaTime;
    currentTime_ += timeStep;

    // 時刻に達していないイベントも再生を継続させるため、完了判定へ含める。
    bool allFinished = true;

    for (auto& event : events_) {
        if (event.isFinished)
            continue;

        if (currentTime_ < event.triggerTime) {
            allFinished = false;
            continue;
        }

        // 継続型の効果は発火後も毎フレーム更新し、単発型は最初の1回だけ発行する。
        if (event.type == VfxEventType::GpuParticle && event.emissionRate > 0) {
            event.isFinished = currentTime_ >= event.triggerTime + event.duration;
            const float previousTime = currentTime_ - timeStep;
            // 秒に毎秒発生数を掛け、整数境界を跨いだかを判定します。複数の境界を跨いでも、現在位置での発生は一回だけです。
            if (!event.isFinished && std::floor(currentTime_ * event.emissionRate) !=
                std::floor(previousTime * event.emissionRate)) {
                const auto position = targetObject_ ? targetObject_->GetTranslate() : rootPosition_;
                auto direction = event.emissionDirection;
                // 追従方向を有効にしたときだけXZ方向を対象のヨーで回し、上下方向の成分は保ちます。
                if (event.directionFollowsYaw) {
                    const float yaw = targetObject_ ? targetObject_->GetRotation().y : rootRotation_.y;
                    direction = {direction.x * std::cos(yaw) + direction.z * std::sin(yaw),
                        direction.y, -direction.x * std::sin(yaw) + direction.z * std::cos(yaw)};
                }
                GpuParticleManager::GetInstance()->EmitDirected(
                    event.presetName, position + event.offset, direction, event.emissionScale);
                event.hasFired = true;
            }
        }
        else if (event.type == VfxEventType::PostEffectPulse) {
            // 継続中の画面効果を現在時刻で再評価し、前回このイベントが加えた分を置き換える。
            ApplyPostPulse(event, currentTime_);
        }
        else if (event.type == VfxEventType::LightPulse) {
            // 光源の生成は一度だけ行い、寿命の終了まではイベントを保持する。
            if (!event.hasFired) {
                engine::Matrix4x4 emitMat;
                engine::Vector3 lightPos = ResolveSequencerPosition(targetObject_, useRootPosition_, rootPosition_, rootScale_, rootRotation_, event.offset, emitMat);
                engine::LightManager::GetInstance()->PlayPointLightPulse(
                    lightPos,
                    event.lightColor,
                    event.intensity,
                    event.lightRadius,
                    event.duration,
                    event.lightDecay);
                event.hasFired = true;
            }

            if (currentTime_ >= event.triggerTime + (std::max)(event.duration, 0.01f)) {
                event.isFinished = true;
            }
        }
        else if (event.type == VfxEventType::MovingParticle) {
            event.hasFired = true;

            // 軌道上に粒子を発生させるため、イベントの継続時間中は毎フレーム放出する。
            float duration = (std::max)(event.duration, 0.01f);
            float progress = (currentTime_ - event.triggerTime) / duration;

            if (progress >= 1.0f) {
                // フレームが終了時刻を越えた場合も、曲線の終点で最後の粒子を一度発生させます。
                progress = 1.0f;
                event.isFinished = true;
            }

            // ベジェ曲線上の位置を補間し、毎フレームその位置から粒子を放出する。
            float easeT = ApplyEventEasing(event.easingType, progress);
            engine::Vector3 localPos = CalculateBezier(event.offset, event.controlPoint, event.endOffset, easeT);

            engine::Matrix4x4 emitMat;
            engine::Vector3 spawnPos = ResolveSequencerPosition(targetObject_, useRootPosition_, rootPosition_, rootScale_, rootRotation_, localPos, emitMat);
            emitMat.m[3][0] = spawnPos.x;
            emitMat.m[3][1] = spawnPos.y;
            emitMat.m[3][2] = spawnPos.z;
            GpuParticleManager::GetInstance()->Emit(event.presetName, spawnPos, emitMat);
        }
        else if (!event.hasFired) {
            // それ以外は発火時に一回だけ各システムへ要求し、同フレームで完了にする。
            if (event.type == VfxEventType::GpuParticle) {
                engine::Matrix4x4 emitMat;
                engine::Vector3 spawnPos = ResolveSequencerPosition(targetObject_, useRootPosition_, rootPosition_, rootScale_, rootRotation_, event.offset, emitMat);
                GpuParticleManager::GetInstance()->Emit(event.presetName, spawnPos, emitMat);
            }
            else if (event.type == VfxEventType::Debris) {
                // 一回の発生だけ地面高さと色を調整できるよう、破片設定を一時オブジェクトへ読む。
                DebrisEffectConfig config;
                auto* debris = DebrisEffectManager::GetInstance();
                if (debris->LoadConfig(std::filesystem::path("Resources/json/debris") /
                        std::filesystem::path(std::u8string(event.presetName.begin(), event.presetName.end()) + u8".json"), config)) {
                    engine::Matrix4x4 emitMat;
                    const auto position = ResolveSequencerPosition(targetObject_, useRootPosition_, rootPosition_, rootScale_, rootRotation_, event.offset, emitMat);
                    // ワールドの発生点に対する相対高さを、破片が衝突に使う絶対の地面高さへ変換します。
                    if (event.debrisGroundRelative) config.groundY = position.y + event.debrisGroundOffset;
                    if (event.debrisOverrideColor) config.color = event.debrisColor;
                    debris->SpawnFromConfig(config, position);
                } else {
                    DebugConsole::GetInstance()->AddLog(LogLevel::Warning, "[VFX] debris preset not found: " + event.presetName);
                }
            }
            else if (event.type == VfxEventType::MeshEffect) {
                // 対象ありなら追従エフェクト、対象なしなら解決済み座標で単発生成する。
                std::string path = "Resources/json/effect/" + event.presetName + ".json";
                engine::Object3D* target = targetObject_;
                engine::Vector3 eventScale = {
                    event.scale.x * rootScale_.x,
                    event.scale.y * rootScale_.y,
                    event.scale.z * rootScale_.z
                };

                engine::Vector3 eventRotation = event.rotation;

                if (target) {
                    engine::Vector3 spawnOffset = {
                        event.offset.x * rootScale_.x,
                        event.offset.y * rootScale_.y,
                        event.offset.z * rootScale_.z
                    };

                    if (useRootPosition_) {
                        spawnOffset.x += rootPosition_.x;
                        spawnOffset.y += rootPosition_.y;
                        spawnOffset.z += rootPosition_.z;
                    }

                    eventRotation.x += rootRotation_.x;
                    eventRotation.y += rootRotation_.y;
                    eventRotation.z += rootRotation_.z;
                    MeshEffectManager::GetInstance()->SpawnEffect(path, target, spawnOffset, eventRotation, eventScale);
                } else {
                    engine::Matrix4x4 emitMat;
                    engine::Vector3 spawnPos = ResolveSequencerPosition(
                        targetObject_,
                        useRootPosition_,
                        rootPosition_,
                        rootScale_,
                        rootRotation_,
                        event.offset,
                        emitMat);
                    eventRotation.x += rootRotation_.x;
                    eventRotation.y += rootRotation_.y;
                    eventRotation.z += rootRotation_.z;
                    MeshEffectManager::GetInstance()->SpawnEffectAt(path, spawnPos, eventRotation, eventScale);
                }
            }
            else if (event.type == VfxEventType::SoundEffect) {
                engine::Matrix4x4 emitMatrix;
                engine::Vector3 spawnPosition = ResolveSequencerPosition(
                    targetObject_, useRootPosition_, rootPosition_, rootScale_, rootRotation_, event.offset, emitMatrix);
                // 音の発生位置を解決し、位置を使うかは音イベントの設定に任せる。
                AudioEventSystem::GetInstance()->PlayReference(event.presetName, &spawnPosition);
            }
            else if (event.type == VfxEventType::CameraShake) {
                // 揺れ時間、周波数、各軸の倍率をカメラへ渡し、以降の減衰はカメラ側で進める。
                CameraManager::GetInstance()->PlayShake(event.duration, event.intensity, event.frequency, event.scale);
            }
            else if (event.type == VfxEventType::CameraFovPulse) {
                // イベント素材の強度は度数法で編集するので、カメラへ渡す直前にラジアンへ変換します。
                CameraManager::GetInstance()->PlayFovPulse(event.duration, event.intensity * kDegreesToRadians, event.attackRatio);
            }
            else if (event.type == VfxEventType::ControllerRumble) {
                // 左右モーターの強度と継続秒数を入力基盤へ渡す。
                engine::InputManager::GetInstance()->PlayRumble(event.intensity, event.secondaryIntensity, event.duration);
            }
            else if (event.type == VfxEventType::HitStop) {
                // この種類の強度は時間倍率です。演出要求の時間中だけゲーム更新を減速または停止します。
                RequestHitStop(event.duration, event.intensity);
            }

            event.hasFired = true;
            event.isFinished = true;
        }

        if (!event.isFinished) {
            allFinished = false;
        }
    }

    if (allFinished) {
        isPlaying_ = false;
    }
}

bool VfxSequencer::SaveFile(const std::filesystem::path& path) const {
    // 編集値だけを保存し、再生中の発火状態や加算済みPostEffect値は書き出さない。
    json root;
    root["version"] = 3;
    root["events"] = json::array();

    for (const auto& event : events_) {

        json eventJson;
        eventJson["type"] = static_cast<int>(event.type);
        eventJson["presetName"] = event.presetName;
        eventJson["debrisGroundRelative"] = event.debrisGroundRelative;
        eventJson["debrisGroundOffset"] = event.debrisGroundOffset;
        eventJson["debrisOverrideColor"] = event.debrisOverrideColor;
        eventJson["debrisColor"] = WriteVector4(event.debrisColor);
        eventJson["emissionRate"] = event.emissionRate;
        eventJson["emissionDirection"] = WriteVector3(event.emissionDirection);
        eventJson["directionFollowsYaw"] = event.directionFollowsYaw;
        eventJson["emissionScale"] = event.emissionScale;
        eventJson["triggerTime"] = event.triggerTime;
        eventJson["offset"] = WriteVector3(event.offset);
        eventJson["rotation"] = WriteVector3(event.rotation);
        eventJson["scale"] = WriteVector3(event.scale);
        eventJson["controlPoint"] = WriteVector3(event.controlPoint);
        eventJson["endOffset"] = WriteVector3(event.endOffset);
        eventJson["duration"] = event.duration;
        eventJson["easingType"] = event.easingType;
        eventJson["intensity"] = event.intensity;
        eventJson["frequency"] = event.frequency;
        eventJson["radialIntensity"] = event.radialIntensity;
        eventJson["secondaryIntensity"] = event.secondaryIntensity;
        eventJson["damageFlash"] = event.damageFlash;
        eventJson["attackRatio"] = event.attackRatio;
        eventJson["chromaticAberration"] = event.chromaticAberration;
        eventJson["wobbleIntensity"] = event.wobbleIntensity;
        eventJson["bloomIntensity"] = event.bloomIntensity;
        eventJson["lightRadius"] = event.lightRadius;
        eventJson["lightDecay"] = event.lightDecay;
        eventJson["lightColor"] = WriteVector4(event.lightColor);
        root["events"].push_back(eventJson);
    }

    std::ofstream file(path);
    file << root.dump(4);
    file.close();
    return file.good();
}

// JSON更新時刻が一致すれば解析結果を再利用し、変わっていれば読み直して実行状態を初期化します。
void VfxSequencer::Load(const std::string& sequenceName) {
    sequenceName_ = sequenceName;
    const auto filepath = VfxAssetPath::ResolveDependency(std::filesystem::path(kSequenceDirectory) /
        std::filesystem::path(std::u8string(sequenceName.begin(), sequenceName.end()) + u8".json"));
    std::filesystem::file_time_type currentWriteTime{};
    bool hasWriteTime = false;
    try {
        if (std::filesystem::exists(filepath)) {
            currentWriteTime = std::filesystem::last_write_time(filepath);
            hasWriteTime = true;
        }
    } catch (const std::filesystem::filesystem_error&) {
        hasWriteTime = false;
    }

    Reset();

    // ファイル更新時刻が一致するときだけキャッシュを使い、編集後の内容を見落とさない。
    auto& cache = SequenceEventCache();
    auto& writeTimeCache = SequenceWriteTimeCache();
    auto cacheIt = cache.find(sequenceName);
    auto writeTimeIt = writeTimeCache.find(sequenceName);
    // ファイルの現在時刻が取得できない場合はキャッシュを使わず、読み込み可能かファイル本体を確認します。
    const bool cacheIsFresh =
        cacheIt != cache.end() &&
        hasWriteTime &&
        writeTimeIt != writeTimeCache.end() &&
        writeTimeIt->second == currentWriteTime;
    if (cacheIsFresh) {
        // キャッシュから値をコピーし、再生中の発火状況が次のインスタンスやキャッシュへ伝わらないようにします。
        events_ = cacheIt->second;
        ResetEventRuntimeState(events_);
        Reset();

        if (DebugConsole::GetInstance()) {
            DebugConsole::GetInstance()->AddLog("Loaded VFX Sequence from cache: " + sequenceName);
        }

        return;
    }

    std::ifstream file(filepath);

    // 読み込み失敗時はイベント定義を置き換えません。直前の Reset によって再生は停止しています。
    if (!file.is_open()) {
        return;
    }

    json root;
    file >> root;
    events_.clear();

    // 欠けているフィールドはイベントの初期値を使い、旧バージョンのJSONも読めるようにする。
    if (root.contains("events") && root["events"].is_array()) {
        for (const auto& eventJson : root["events"]) {
            VfxEvent event;
            int type = eventJson.value("type", static_cast<int>(VfxEventType::GpuParticle));
            type = std::clamp(type, static_cast<int>(VfxEventType::GpuParticle), static_cast<int>(VfxEventType::Debris));
            event.type = static_cast<VfxEventType>(type);
            event.presetName = eventJson.value("presetName", std::string{});
            event.debrisGroundRelative = eventJson.value("debrisGroundRelative", false);
            event.debrisGroundOffset = eventJson.value("debrisGroundOffset", 0.0f);
            event.debrisOverrideColor = eventJson.value("debrisOverrideColor", false);
            event.debrisColor = eventJson.contains("debrisColor") ? ReadVector4(eventJson["debrisColor"], event.debrisColor) : event.debrisColor;
            if (!std::isfinite(event.debrisGroundOffset)) throw std::runtime_error("Invalid debris ground offset");
            event.emissionRate = eventJson.value("emissionRate", 0.0f);
            event.emissionScale = eventJson.value("emissionScale", 1.0f);
            event.directionFollowsYaw = eventJson.value("directionFollowsYaw", false);
            if (eventJson.contains("emissionDirection"))
                event.emissionDirection = ReadVector3(eventJson["emissionDirection"], event.emissionDirection);
            if (!std::isfinite(event.emissionRate) || event.emissionRate < 0 ||
                !std::isfinite(event.emissionScale) || event.emissionScale < 0)
                throw std::runtime_error("Invalid particle emission settings");
            event.triggerTime = eventJson.value("triggerTime", 0.0f);
            event.offset = eventJson.contains("offset") ? ReadVector3(eventJson["offset"], event.offset) : event.offset;
            event.rotation = eventJson.contains("rotation") ? ReadVector3(eventJson["rotation"], event.rotation) : event.rotation;
            event.scale = eventJson.contains("scale") ? ReadVector3(eventJson["scale"], event.scale) : event.scale;
            event.controlPoint = eventJson.contains("controlPoint") ? ReadVector3(eventJson["controlPoint"], event.controlPoint) : event.controlPoint;
            event.endOffset = eventJson.contains("endOffset") ? ReadVector3(eventJson["endOffset"], event.endOffset) : event.endOffset;
            event.duration = eventJson.value("duration", 1.0f);
            event.easingType = eventJson.value("easingType", 0);
            event.intensity = eventJson.value("intensity", 0.2f);
            event.frequency = eventJson.value("frequency", 24.0f);
            event.radialIntensity = eventJson.value("radialIntensity", 0.0f);
            event.damageFlash = eventJson.value("damageFlash", 0.0f);
            event.secondaryIntensity = eventJson.value("secondaryIntensity", 0.7f);
            event.chromaticAberration = eventJson.value("chromaticAberration", 0.0f);
            event.attackRatio = eventJson.value("attackRatio", 0.12f);
            event.wobbleIntensity = eventJson.value("wobbleIntensity", 0.0f);
            event.bloomIntensity = eventJson.value("bloomIntensity", 0.0f);
            event.lightRadius = eventJson.value("lightRadius", 9.0f);
            event.lightDecay = eventJson.value("lightDecay", 1.4f);
            event.lightColor = eventJson.contains("lightColor") ? ReadVector4(eventJson["lightColor"], event.lightColor) : event.lightColor;
            events_.push_back(event);
        }
    }

    // 読み込んだ編集値をキャッシュし、実行状態はゼロから再開する。
    ResetEventRuntimeState(events_);
    cache[sequenceName] = events_;

    if (hasWriteTime) {
        writeTimeCache[sequenceName] = currentWriteTime;
    } else {
        writeTimeCache.erase(sequenceName);
    }

    Reset();

    if (DebugConsole::GetInstance()) {
        DebugConsole::GetInstance()->AddLog("Loaded VFX Sequence: " + sequenceName);
    }
}

void VfxSequencer::PlayOneShot(const std::string& sequenceName, const engine::Vector3& position) {
    PlayOneShot(sequenceName, position, { 1.0f, 1.0f, 1.0f });
}

void VfxSequencer::PlayOneShot(const std::string& sequenceName, const engine::Vector3& position, const engine::Vector3& scale) {
    PlayOneShot(sequenceName, position, scale, { 0.0f, 0.0f, 0.0f });
}

void VfxSequencer::PlayOneShot(const std::string& sequenceName, const engine::Vector3& position, const engine::Vector3& scale, const engine::Vector3& rotation) {
    // 呼び出し側が所有しない演出を共有リストへ移し、再生完了まで保持する。
    auto sequence = std::make_unique<VfxSequencer>();
    sequence->Initialize(nullptr);
    sequence->Load(sequenceName);

    if (sequence->GetEvents().empty()) {
        return;
    }

    sequence->SetRootPosition(position);
    sequence->SetRootScale(scale);
    sequence->SetRootRotation(rotation);
    sequence->Play();
    ActiveOneShotSequences().push_back(std::move(sequence));
}

void VfxSequencer::PlayOneShotOnTarget(const std::string& sequenceName, engine::Object3D* targetObject) {
    // 追加変換を指定しない再生は、単位スケールの共通入口へ委譲する。
    PlayOneShotOnTarget(
        sequenceName,
        targetObject,
        { 0.0f, 0.0f, 0.0f },
        { 1.0f, 1.0f, 1.0f },
        { 0.0f, 0.0f, 0.0f });
}

void VfxSequencer::PlayOneShotOnTarget(
    const std::string& sequenceName,
    engine::Object3D* targetObject,
    const engine::Vector3& localOffset,
    const engine::Vector3& scale,
    const engine::Vector3& rotation) {
    if (!targetObject) {
        return;
    }

    auto sequence = std::make_unique<VfxSequencer>();
    sequence->Initialize(targetObject);
    sequence->Load(sequenceName);

    if (sequence->GetEvents().empty()) {
        return;
    }

    sequence->SetRootPosition(localOffset);
    sequence->SetRootScale(scale);
    sequence->SetRootRotation(rotation);
    sequence->Play();
    ActiveOneShotSequences().push_back(std::move(sequence));
}

// 呼び出し元が所有しない単発演出を更新し、再生終了したものだけ解放します。
void VfxSequencer::UpdateOneShots(float deltaTime) {
    auto& sequences = ActiveOneShotSequences();

    for (auto& sequence : sequences) {
        if (sequence) {
            sequence->Update(deltaTime);
        }
    }

    // 再生が終わった単発演出だけを解放する。呼び出し元による寿命管理は不要。
    sequences.erase(
        std::remove_if(
            sequences.begin(),
            sequences.end(),
            [](const std::unique_ptr<VfxSequencer>& sequence) {
                return !sequence || !sequence->IsPlaying();
            }),
        sequences.end());
}

// Sceneの単発演出に加え、共有ヒットストップと振動も解除します。
void VfxSequencer::ClearOneShots() {
    for (auto& sequence : ActiveOneShotSequences()) {
        if (sequence) {
            sequence->Stop();
        }
    }

    ActiveOneShotSequences().clear();

    // シーン切替後にヒットストップや振動が残らないよう、共有フィードバックも戻す。
    ActiveHitStops().clear();
    gameplayTimeScale = 1.0f;
    engine::InputManager::GetInstance()->StopRumble();
}

void VfxSequencer::RequestHitStop(float duration, float timeScale) {
    // 無効な時間は登録せず、重複要求は更新時にまとめて評価する。
    if (duration <= 0.0f) {
        return;
    }

    ActiveHitStops().push_back({ duration, std::clamp(timeScale, 0.0f, 1.0f) });
}

// 実時間でヒットストップの寿命を減らし、重なった要求のうち最も低い倍率を適用します。
void VfxSequencer::UpdateFeedbackRuntime(float unscaledDeltaTime) {
    // ゲーム時間で減らすとヒットストップ自身が解除されにくくなるため、実時間で寿命を進める。
    const float timeStep = (std::max)(unscaledDeltaTime, 0.0f);
    auto& layers = ActiveHitStops();

    for (HitStopLayer& layer : layers) {
        layer.remaining -= timeStep;
    }

    layers.erase(
        std::remove_if(layers.begin(), layers.end(), [](const HitStopLayer& layer) {
            return layer.remaining <= 0.0f;
        }),
        layers.end());

    // 重なっている要求のうち、最も強い減速を現在のゲーム時間倍率に採用する。
    gameplayTimeScale = 1.0f;

    for (const HitStopLayer& layer : layers) {
        gameplayTimeScale = (std::min)(gameplayTimeScale, layer.timeScale);
    }
}

float VfxSequencer::GetGameplayTimeScale() {
    return gameplayTimeScale;
}

#ifdef USE_IMGUI
namespace {
    bool editorVfxSession = false;
    std::vector<std::unique_ptr<VfxSequencer>> savedEditorSequences;
    std::vector<HitStopLayer> savedEditorHitStops;
    float savedEditorGameplayScale = 1;
}
// 本編の単発演出とヒットストップを退避し、編集側を通常の速度から開始する。
void VfxSequencer::BeginEditorSession() {
    if (editorVfxSession) return;
    ActiveOneShotSequences().swap(savedEditorSequences); ActiveHitStops().swap(savedEditorHitStops);
    savedEditorGameplayScale = gameplayTimeScale; gameplayTimeScale = 1; editorVfxSession = true;
}
// 編集側の単発・振動を解除した後、本編で停止していた単発演出と減速状態を戻す。
void VfxSequencer::EndEditorSession() {
    if (!editorVfxSession) return;
    ClearOneShots();
    ActiveOneShotSequences().swap(savedEditorSequences); ActiveHitStops().swap(savedEditorHitStops);
    gameplayTimeScale = savedEditorGameplayScale; editorVfxSession = false;
}
#endif
