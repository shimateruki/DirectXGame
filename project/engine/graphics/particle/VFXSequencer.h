#pragma once
#include <string>
#include <vector>
#include "engine/utility/math/Math.h"
#include "Transform.h"
#include "Object3d.h"

enum class VFXEventType {
    GPUParticle = 0,
    MeshEffect = 1,
    SoundEffect = 2,
    MovingParticle = 3,
    CameraShake = 4,
    PostEffectPulse = 5,
    LightPulse = 6,
    HitStop = 7,
    ControllerRumble = 8,
    CameraFovPulse = 9
};

struct VFXEvent {
    VFXEventType type = VFXEventType::GPUParticle;
    std::string presetName;
    float triggerTime = 0.0f;
    Vector3 offset = { 0.0f, 0.0f, 0.0f };
    Vector3 rotation = { 0.0f, 0.0f, 0.0f };
    Vector3 scale = { 1.0f, 1.0f, 1.0f };
    bool hasFired = false;
    Vector3 controlPoint = { 0.0f, 5.0f, 0.0f };
    Vector3 endOffset = { 0.0f, 0.0f, 10.0f };
    float duration = 1.0f;
    int easingType = 0;
    bool isFinished = false;

    float intensity = 0.2f;
    float secondaryIntensity = 0.7f;
    float frequency = 24.0f;
    float attackRatio = 0.12f;
    float radialIntensity = 0.0f;
    float damageFlash = 0.0f;
    float chromaticAberration = 0.0f;
    float wobbleIntensity = 0.0f;
    float bloomIntensity = 0.0f;
    float lightRadius = 9.0f;
    float lightDecay = 1.4f;
    Vector4 lightColor = { 1.0f, 0.58f, 0.12f, 1.0f };

    bool hasAppliedPostPulse = false;
    float appliedRadialIntensity = 0.0f;
    float appliedDamageFlash = 0.0f;
    float appliedChromaticAberration = 0.0f;
    float appliedWobbleIntensity = 0.0f;
    float appliedBloomIntensity = 0.0f;
};

class VFXSequencer {
public:
    VFXSequencer() = default;
    ~VFXSequencer() = default;

        // シーケンス再生に必要な各VFX管理クラスを設定します。
void Initialize(Object3d* targetObject = nullptr);
        // 再生時間を進め、発火時刻に達したイベントを実行します。
void Update(float deltaTime);

        // 指定したVFXシーケンスの再生を開始します。
void Play();
    void Stop();
    void Reset();

    void AddEvent(
        VFXEventType type,
        const std::string& presetName,
        float triggerTime,
        const Vector3& offset = { 0.0f, 0.0f, 0.0f },
        const Vector3& rotation = { 0.0f, 0.0f, 0.0f },
        const Vector3& scale = { 1.0f, 1.0f, 1.0f });

    bool IsPlaying() const { return isPlaying_; }
    float GetCurrentTime() const { return currentTime_; }
    float GetDuration() const;
    void Save(const std::string& sequenceName);
    void Load(const std::string& sequenceName);
    std::vector<VFXEvent>& GetEvents() { return events_; }
    const std::vector<VFXEvent>& GetEvents() const { return events_; }
    void SetTargetObject(Object3d* targetObject) { targetObject_ = targetObject; }
    Object3d* GetTargetObject() const { return targetObject_; }
    void SetRootPosition(const Vector3& rootPosition);
    void SetRootScale(const Vector3& rootScale);
    void SetRootRotation(const Vector3& rootRotation);
    void ClearRootPosition();

    static void PlayOneShot(const std::string& sequenceName, const Vector3& position);
    static void PlayOneShot(const std::string& sequenceName, const Vector3& position, const Vector3& scale);
    static void PlayOneShot(const std::string& sequenceName, const Vector3& position, const Vector3& scale, const Vector3& rotation);
    static void PlayOneShotOnTarget(const std::string& sequenceName, Object3d* targetObject);
    static void PlayOneShotOnTarget(const std::string& sequenceName, Object3d* targetObject, const Vector3& localOffset, const Vector3& scale, const Vector3& rotation);
    static void UpdateOneShots(float deltaTime);
    static void ClearOneShots();
    static void RequestHitStop(float duration, float timeScale = 0.0f);
    static void UpdateFeedbackRuntime(float unscaledDeltaTime);
    static float GetGameplayTimeScale();

private:
    std::vector<VFXEvent> events_;
    Object3d* targetObject_ = nullptr;
    Vector3 rootPosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 rootScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 rootRotation_ = { 0.0f, 0.0f, 0.0f };

    float currentTime_ = 0.0f;
    bool isPlaying_ = false;
    bool useRootPosition_ = false;
    std::string sequenceName_;
};
