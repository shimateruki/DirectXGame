#pragma once
#include "Object3d.h" 
#include <vector>
#include <string>
#include "engine/utility/math/Math.h" 
#include "IEditable.h" 

class SceneManager;
class CameraManager;

// 1フレーム分の動きデータ
struct GhostFrame {
    Vector3 position;
    Vector3 rotation;
    bool triggerAttack = false;
};

class GhostRecorder : public IEditable { 
public:
    // 状態管理用
    enum class State {
        Idle,       // 待機中
        Recording,  // 録画中
        Playing     // 再生中
    };

    // 自動生成用のパラメータ構造体
    struct GenerationParams {
        Vector3 startPos = { 0,0,0 };
        Vector3 startRot = { 0,0,0 };
        Vector3 endPos = { 0,0,0 };
        Vector3 endRot = { 0,0,0 };
        struct Waypoint {
            Vector3 pos;
            Vector3 rot;
        };
        // 中継点のリスト
        std::vector<Waypoint> waypoints;

        float duration = 3.0f;         // 移動にかかる時間
        bool useEasing = false;        // 加減速を使うか
        bool generateRelative = false; // 相対座標として生成するか
        bool useSpline = true;         // スプライン曲線にするか
        int easingType = 0;
    };

public:
    void Initialize(SceneManager* sceneManager);
    void Update();

    // Inspectorに表示するUI描画処理
    void DrawImGui() override;

    // Inspector上部に表示される名前
    std::string GetName() override { return "Ghost Recorder (Cinematic/Path)"; }

    // 3D空間への軌跡プレビュー描画
    void DrawPreview(const Matrix4x4& viewProjection, const Vector2& offset, const Vector2& size);

    void SetTarget(Object3d* target) { target_ = target; }
    State GetState() const { return state_; }

    void Play(const std::string& fileName, bool loop, bool isRelative, bool isCinematic);
    void Stop();

    void Save(const std::string& fileName);
    void Load(const std::string& fileName);

    void SetCameraManager(CameraManager* cameraManager) { cameraManager_ = cameraManager; }

private:
    void StartRecording();
    void StopRecording();
    void StartPlayingInternal();

    Vector3 Lerp(const Vector3& start, const Vector3& end, float t);
    float SmoothStep(float t);
    Vector3 CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t);
    Vector3 GetSplinePoint(const std::vector<Vector3>& points, float t, bool isLoop);
    Vector3 TransformCoord(const Vector3& vec, const Matrix4x4& mat);

private:
    SceneManager* sceneManager_ = nullptr;
    Object3d* target_ = nullptr;

    std::vector<GhostFrame> frames_;
    State state_ = State::Idle;

    size_t currentFrameIndex_ = 0;
    bool isLoop_ = false;
    bool isRelative_ = true;

    GenerationParams genParams_;
    bool isShowPreview_ = true;

    CameraManager* cameraManager_ = nullptr;
    bool isOverrideCamera_ = false;
    Vector3 basePosition_ = {0, 0, 0};
    Vector3 baseRotation_ = {0, 0, 0};
};