#pragma once
#ifdef USE_IMGUI
#include <cstdint>
#include <functional>
#include <utility>
#include <string>
class SceneManager;
class ProjectWindow;
namespace engine { class Object3d; }

// 実画面で選んだ対象の確認を管理する。具体的な敵・入力・JSON形式には依存しない。
class SelectedActorSession {
public:
    struct Playback { bool playing = false, loop = false; float speed = 1, duration = 5, time = 0; };
    struct Services {
        // Editor初期化と終了。Sceneとセッションは借用し、ゲーム側で所有しない。
        std::function<void(SceneManager*, SelectedActorSession&)> initialize;
        std::function<void()> finalize;
        // 選択変更で種類に対応する編集データを開く。描画はInspector内に埋め込む。
        std::function<void(engine::Object3d&)> select;
        // 同一Objectの種類設定が変わった場合も編集データを再選択する。
        std::function<std::string(const engine::Object3d&)> identity;
        std::function<void(ProjectWindow*)> draw;
        // 実時間の保守更新。保存待ち・ドラフト反映を扱い、AIの時間は進めない。
        std::function<void(float)> maintain;
        // 開始成功時だけtrue。更新秒数は共通セッションが固定刻みで渡す。
        std::function<bool()> begin;
        std::function<void(float)> step;
        // 元の設定を保ち、一時実体と確認用の弾・演出を片付ける。
        std::function<void()> end;
        // 画面が保持する再生条件を読み、時間管理を別々に実装しない。
        std::function<Playback()> playback;
        std::function<void(bool)> setPlaying;
        // Game Viewの画面上の矩形。範囲表示・配置ギズモだけを描く。
        std::function<void(float,float,float,float)> overlay;
    };
    static void RegisterServices(Services services) { ServicesTable() = std::move(services); }
    void Initialize(SceneManager* manager);
    void Finalize();
    void SetBeforeSceneChange(std::function<void()> call) { beforeSceneChange_ = std::move(call); }
    void PrepareSceneChange() { Stop(); if (beforeSceneChange_) beforeSceneChange_(); SetTarget(nullptr); }
    // falseは対象未選択・削除済み・通常Play中・ゲーム側準備失敗を表す。
    bool Begin();
    void Stop();
    void Restart();
    void Seek(float seconds);
    void StepFrame();
    void Update(float realDeltaTime);
    void DrawInspector(engine::Object3d* target, ProjectWindow* assets);
    void DrawOverlay(float x,float y,float width,float height);
    void SetTarget(engine::Object3d* target);
    engine::Object3d* GetTarget() const;
    bool IsActive() const { return active_; }
    static SelectedActorSession* Current() { return current_; }
private:
    static Services& ServicesTable() { static Services services; return services; }
    std::function<void()> beforeSceneChange_;
    std::string targetKind_;
    SceneManager* manager_ = nullptr;
    // ポインタを保持せず、現在のSceneの所有一覧からIDを解決する。
    uint64_t targetId_ = 0, sceneGeneration_ = 0;
    float accumulator_ = 0;
    bool active_ = false, changing_ = false;
    inline static SelectedActorSession* current_ = nullptr;
};
#endif
