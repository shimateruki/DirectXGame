#pragma once
#include "Object3d.h" 
#include <vector>
#include <string>
#include "engine/utility/math/Math.h" 

class SceneManager;
class CameraManager;
// 1フレーム分の動きデータ
struct GhostFrame {
    Vector3 position;
    Vector3 rotation;
    bool triggerAttack = false;
};

class GhostRecorder {
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
        // 中継点のリスト (可変長)
        std::vector<Waypoint> waypoints;

        float duration = 3.0f;        // 移動にかかる時間
        bool useEasing = false;       // イージング(加減速)を使うか
        bool generateRelative = false; // 相対座標(0起点)として生成するか
        bool useSpline = true;        // 直線でつなぐか、曲線(スプライン)にするか
    };

public:
    // 初期化
    void Initialize(SceneManager* sceneManager);

    // 毎フレーム呼ぶ処理
    void Update();



    // ImGui描画 (操作パネル)
    void DrawImGui();

    //  3D空間への軌跡プレビュー描画
    void DrawPreview(const Matrix4x4& viewProjection);

    // ターゲットの切り替え
    void SetTarget(Object3d* target) { target_ = target; }
    State GetState() const { return state_; }

    // 再生・停止
    void Play(const std::string& fileName, bool loop, bool isRelative, bool isCinematic);
    void Stop();

    // 保存・読み込み
    void Save(const std::string& fileName);
    void Load(const std::string& fileName);

    void SetCameraManager(CameraManager* cameraManager) { cameraManager_ = cameraManager; }

private:
    void StartRecording();
    void StopRecording();

    // 内部用の再生開始（ImGuiボタン用）
    void StartPlayingInternal();


    // 線形補間
    Vector3 Lerp(const Vector3& start, const Vector3& end, float t);

    // イージング (SmoothStep)
    float SmoothStep(float t);

    // Catmull-Romスプライン (4点を使ってp1-p2間を補間)
    Vector3 CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t);

    // 複数の点を通るスプライン上の座標を取得する関数
    Vector3 GetSplinePoint(const std::vector<Vector3>& points, float t, bool isLoop);

    //  座標変換ヘルパー (World -> Screen変換用)
    Vector3 TransformCoord(const Vector3& vec, const Matrix4x4& mat);

private:
    SceneManager* sceneManager_ = nullptr;
    Object3d* target_ = nullptr;

    // 録画/生成されたフレームデータ
    std::vector<GhostFrame> frames_;

    State state_ = State::Idle;

    // 再生制御用
    size_t currentFrameIndex_ = 0;
    bool isLoop_ = false;       // ループ再生するか
    bool isRelative_ = true;    // 相対座標モードか

    // 自動生成用パラメータ
    GenerationParams genParams_;

    //  プレビュー表示フラグ
    bool isShowPreview_ = true;

    CameraManager* cameraManager_ = nullptr; 

    // カメラ乗っ取りフラグ (UIで切り替えられるようにする)
    bool isOverrideCamera_ = false;
};
