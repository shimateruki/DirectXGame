#pragma once
#ifdef USE_IMGUI
#include <cstdint>
#include <functional>
#include <utility>
#include <string>
class SceneManager;
class ProjectWindow;
namespace engine { class Object3D; }

// 担当：選択対象だけの動作確認と再生操作を管理する。
// 関連：GameEditorController / DebugEditor（UI）、SceneManager（対象の生存確認）
// 主な入口：Initialize / Begin / Update / Stop / Finalize
// 具体的な敵・入力・JSON形式は、登録されたServicesへ委ねる。
class SelectedActorSession {
public:
    // 単体確認の再生条件と現在位置。duration・timeは秒、speedは時間倍率。
    // 関連：SelectedActorSession（この型を使う機能の本体）
    struct Playback { bool playing = false, loop = false; float speed = 1, duration = 5, time = 0; };
    // ゲーム固有の編集・再生処理を接続するコールバック群。
    // 関連：SelectedActorSession（この型を使う機能の本体）
    struct Services {
        // Editor初期化と終了。Sceneとセッションは借用し、ゲーム側で所有しない。
        std::function<void(SceneManager*, SelectedActorSession&)> initialize;
        std::function<void()> finalize;
        // 選択変更で種類に対応する編集データを開く。描画はInspector内に埋め込む。
        std::function<void(engine::Object3D&)> select;
        // 同一Objectの種類設定が変わった場合も編集データを再選択する。
        std::function<std::string(const engine::Object3D&)> identity;
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
    // ゲーム固有の編集・確認処理を共通セッションへ登録する。
    static void RegisterServices(Services services) { ServicesTable() = std::move(services); }
    // SceneManagerを借用し、登録された初期化処理を呼ぶ。
    void Initialize(SceneManager* manager);
    // 確認を停止して終了処理を呼び、シーン・対象参照を解除する。
    void Finalize();
    void SetBeforeSceneChange(std::function<void()> call) { beforeSceneChange_ = std::move(call); }
    // 単体確認を停止して切替前処理を呼び、対象参照を解除する。
    void PrepareSceneChange() { Stop(); if (beforeSceneChange_) beforeSceneChange_(); SetTarget(nullptr); }
    // falseは対象未選択・削除済み・通常Play中・ゲーム側準備失敗を表す。
    bool Begin();
    // 確認用の実体を片付け、元対象の一時非表示を解除する。
    void Stop();
    // 開始処理をやり直し、確認を先頭から再開する。
    void Restart();
    // 先頭から固定刻みで再計算する。移動先は秒、0～60秒に制限する。
    void Seek(float seconds);
    // 単体確認を一フレーム分だけ進める。
    void StepFrame();
    // 実時間の保守更新と、再生倍率を適用した1/60秒刻みの確認を進める。
    void Update(float realDeltaTime);
    // 対象の変更を反映し、ゲーム側の専用編集UIを表示する。
    void DrawInspector(engine::Object3D* target, ProjectWindow* assets);
    // Game Viewの指定矩形へ、確認用の補助表示を重ねる。
    void DrawOverlay(float x,float y,float width,float height);
    // 対象ID・シーン世代を更新する。対象や種類が変われば確認を停止する。
    void SetTarget(engine::Object3D* target);
    // 現在シーンのIDから対象を解決する。世代変更・削除後はnullptr。
    engine::Object3D* GetTarget() const;
    bool IsActive() const { return active_; }
    // 現在接続している共通セッションを返す。未初期化ならnullptr。
    static SelectedActorSession* Current() { return current_; }
private:
    // ゲーム側から接続された共通コールバック表を返す。
    static Services& ServicesTable() { static Services services; return services; }
    std::function<void()> beforeSceneChange_;
    std::string targetKind_; // 同じ対象の種類変更も検出する識別値。
    SceneManager* manager_ = nullptr; // シーンの取得元。借用参照。
    // ポインタを保持せず、現在のSceneの所有一覧からIDを解決する。
    uint64_t targetId_ = 0, sceneGeneration_ = 0;
    float accumulator_ = 0; // 固定更新前の蓄積秒数。
    bool active_ = false, changing_ = false; // 確認中かと、開始・停止の再入防止。
    inline static SelectedActorSession* current_ = nullptr; // 現在接続中のセッション。未接続ならnullptr。
};
#endif
