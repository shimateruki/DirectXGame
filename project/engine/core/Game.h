#pragma once

#include "AbstractSceneFactory.h"
#include "Framework.h"
#include "SceneManager.h"

#include <chrono>
#include <memory>
#include <string>

#ifdef USE_IMGUI
#include "engine/editor/core/GameEditorController.h"
#endif

class PostEffect;

// 担当：Framework上でシーン更新、Editor、画面効果、時間計測を統合する。
// 関連：Framework（共通ループ）、SceneManager（遷移）、GameEditorController（Editor）
// 主な入口：Initialize / Update / Draw / Finalize
class Game : public Framework {
public:
	/// <summary>エンジン基盤、シーン、画面効果とエディター機能を初期化する。</summary>
	void Initialize() override;

	/// <summary>シーンとエンジン基盤のリソースを終了する。</summary>
	void Finalize() override;

protected:
	/// <summary>入力、ゲームシステム、シーン、プロファイル計測を進める。</summary>
	void Update() override;

	/// <summary>現在の実行モードに合わせてゲーム画面または編集画面を描画する。</summary>
	void Draw() override;

private:
	// 初期化はエンジン基盤、シーン、画面効果、編集機能の順に分ける。
	void InitializeEngineServices();

	// シーン生成表とSceneManagerを初期化する。
	void InitializeScene();

	// シーン描画に使う画面効果を初期化する。
	void InitializePostProcess();

	// USE_IMGUI時のEditor接続と各ツールを初期化する。
	void InitializeEditorTools();

	// 起動設定を読んで再生モードと最初のシーンを決める。
	void ConfigureInitialPlayState();

	// シーン遷移の完了を待ち、起動時だけ必要な配置上書きを適用する。
	void ApplyInitialSceneOverrides();

	/// <summary>起動設定から最初に開くシーン名を決める。</summary>
	std::string ResolveStartSceneName() const;

	/// <summary>前フレームからの経過秒を計算する。</summary>
	float CalculateDeltaTime();

	// 編集画面とゲーム実行中では、更新対象と固定刻みの扱いが異なる。
	void UpdateEditorFrame(float deltaTime);

	// deltaTimeは実フレーム秒、finalDeltaTimeはゲーム進行用。遷移中の更新では実時間を使う。
	void UpdateGameSystems(float deltaTime, float finalDeltaTime);

	// 停止・遷移中は蓄積を消し、動作中だけ固定刻みでシーンを進める。引数の時間単位は秒。
	void RunFixedUpdates(float finalDeltaTime, bool replayFrozen, bool sceneTransitioning);

	// 更新開始からの経過と固定FPSの待機時間を記録する。
	void RecordUpdateProfile(const std::chrono::high_resolution_clock::time_point& startUpdate);

	// 編集画面とゲーム実行画面では描画先と後処理の合成経路を切り替える。
	void DrawEditorFrame(PostEffect* postEffect);

	// ゲーム実行用のシーン描画と画面効果を合成する。
	void DrawRuntimeFrame(PostEffect* postEffect);

	// 編集・実行モードに応じたシーンを描画テクスチャへ出力する。
	void DrawSceneToRenderTexture(bool editorMode);

	// 編集対象カメラの別視点プレビューを描画する。
	void DrawCameraEditorPreview(PostEffect* postEffect);

	// シーンの描画結果へ画面効果を適用し、編集用または実行用の出力先へ渡す。
	void ApplyPostEffectPipeline(PostEffect* postEffect, bool outputForEditorGameView);

	// 描画経路のCPU時間と固定FPS待機時間を記録する。
	void RecordDrawProfile(const std::chrono::high_resolution_clock::time_point& startDraw);

	// 固定FPSの待機処理にかかった時間を記録する。
	void RecordFixedFpsProfile();

private:
	std::unique_ptr<SceneManager> sceneManager_ = nullptr; // 現在シーンと遷移処理を所有する。
	std::unique_ptr<AbstractSceneFactory> sceneFactory_ = nullptr; // シーン名と生成関数の登録表を所有する。

	std::chrono::high_resolution_clock::time_point lastTime_; // 前回フレームの時刻。
	std::chrono::high_resolution_clock::time_point prePostDrawTime_; // 描画区間の時間計測基準。

	float timeScale_ = 1.0f; // ゲーム時間の倍率。1で等速。
	float fixedUpdateAccumulator_ = 0.0f; // 固定更新へ渡していない蓄積秒数。
	bool isPlaying_ = false; // EditorのPlay実行中か。
	bool initialSceneOverridesPending_ = false; // 起動時の配置上書きが未適用か。
	std::string currentSceneName_; // 現在処理しているシーン名。

	// 計測履歴はミリ秒。ゲーム更新へ渡すdeltaTimeや固定刻みの秒数とは混ぜない。
	float sceneUpdateTimeMs_ = 0.0f;
	float sceneDrawTimeMs_ = 0.0f; // シーン描画のCPU時間（ミリ秒）。
	float updateTimeHistory_[120] = {}; // 更新時間の直近120件（ミリ秒）。
	int timeHistoryIndex_ = 0; // 計測履歴へ次に書き込む位置。

	float drawTimeMs_ = 0.0f; // 描画処理全体のCPU時間（ミリ秒）。
	float cpuCmdTimeMs_ = 0.0f; // CPUコマンド処理の計測値（ミリ秒）。
	float drawTimeHistory_[120] = {}; // 描画時間の直近120件（ミリ秒）。

#ifdef USE_IMGUI
	std::unique_ptr<GameEditorController> editorController_; // Editorとゲーム本体の接続を所有する。
	EditorFrameState editorFrameState_; // カメラ入力にも影響するEditor操作状態。
#endif
};
