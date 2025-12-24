#pragma once
#include "Object3d.h" // Object3dを使うため
#include <vector>
#include <string>

class SceneManager;

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

    // 初期化（操作対象のオブジェクトをセット）
    void Initialize(SceneManager* sceneManager);

    // 毎フレーム呼ぶ処理
    void Update();

    // ImGui描画（ここから録画・再生ボタンを押す）
    void DrawImGui();

    // ターゲットの切り替え（プレイヤーで録画して、敵で再生するときに使う）
    void SetTarget(Object3d* target) { target_ = target; }
    State GetState() const { return state_; }

    void Save(const std::string& fileName);
    void Load(const std::string& fileName);

private:
    // 録画開始・停止・再生
    void StartRecording();
    void StopRecording();
    void StartPlaying();

private:
    SceneManager* sceneManager_ = nullptr;
    Object3d* target_ = nullptr; // 操作対象（プレイヤーや敵）

    std::vector<GhostFrame> frames_; // 録画データ（テープ本体）
    int currentFrameIndex_ = 0;      // 今何フレーム目を再生中か

    State state_ = State::Idle;      // 現在の状態
    char fileNameBuffer_[64] = "motion001";
    bool syncCamera_ = false;
    bool wasSyncing_ = false;
};