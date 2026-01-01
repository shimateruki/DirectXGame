#pragma once
#include "Object3d.h" 
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

    // 初期化
    void Initialize(SceneManager* sceneManager);

    // 毎フレーム呼ぶ処理
    void Update();

    // ImGui描画
    void DrawImGui();

    // ターゲットの切り替え
    void SetTarget(Object3d* target) { target_ = target; }
    State GetState() const { return state_; }

    void Play(const std::string& fileName, bool loop, bool isRelative = true);

    // 停止
    void Stop();

    // 保存・読み込み
    void Save(const std::string& fileName);
    void Load(const std::string& fileName);

private:
    void StartRecording();
    void StopRecording();

    // 内部用の再生開始（ImGuiボタン用）
    void StartPlayingInternal();

private:
    SceneManager* sceneManager_ = nullptr;
    Object3d* target_ = nullptr;
    std::vector<GhostFrame> frames_;
    State state_ = State::Idle;
    int currentFrameIndex_ = 0;

    bool isLoop_ = false;           // ループするか？
    bool isRelative_ = true;        // 相対座標モードか？

    Vector3 startPos_ = { 0,0,0 };        // 再生を開始した瞬間のターゲットの座標
    Vector3 startRot_ = { 0,0,0 };        // 再生を開始した瞬間のターゲットの回転
    Vector3 firstFramePos_ = { 0,0,0 };   // 録画データの最初のフレームの座標
    Vector3 firstFrameRot_ = { 0,0,0 };   // 録画データの最初のフレームの回転
};