#pragma once

#include "IEditable.h"
#include "Object3d.h"
#include "SceneManager.h"
#include "VFXSequencer.h"
#include <string>
#include <vector>

struct ActiveEvent {
    int id = 0;
    Object3d* targetObject = nullptr;
};

class GhostDirector : public IEditable {
public:
    struct Track {
        Object3d* target = nullptr;
        std::string targetName;
        std::string pathFileName;
        float delayTime = 0.0f;
        bool hasStarted = false;
    };

    struct VFXTrack {
        Object3d* target = nullptr;
        std::string targetName;
        std::string sequenceName;
        float delayTime = 0.0f;
        bool hasStarted = false;
        VFXSequencer sequencer;
    };

    void Initialize(SceneManager* sceneManager);
    void Update(float deltaTime);
    void DrawImGui() override;
    std::string GetName() override { return "GhostDirector(Cinematic/Path)"; }

    void PlayScenario(bool isLoop = false, bool useImguiTime = false);
    void StopScenario();
    void DrawPreview(const Matrix4x4& viewProjection, const Vector2& offset, const Vector2& size);

    void SaveScenario(const std::string& fileName);
    void LoadScenario(const std::string& fileName);
    bool IsFinished() const;
    int GetActiveEventID() const;
    ActiveEvent GetActiveEvent() const;
    void AdvanceTime(float deltaTime);

private:
    Object3d* ResolveObjectByName(const std::string& name) const;
    void PreparePathTrack(Track& track);
    void PrepareVFXTrack(VFXTrack& track);
    float GetScenarioDuration() const;

    SceneManager* sceneManager_ = nullptr;
    std::vector<Track> tracks_;
    std::vector<VFXTrack> vfxTracks_;
    char scenarioNameBuf_[64] = "boss_attack_1";

    bool isPlaying_ = false;
    float currentScrubTime_ = 0.0f;
    float playTimer_ = 0.0f;
    bool isLooping_ = false;
    bool useImguiTime_ = false;
};
