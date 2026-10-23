#pragma once

#include "AbstractSceneFactory.h"
#include "Framework.h"
#include "SceneManager.h"

#include <chrono>
#include <memory>
#include <string>

#ifdef USE_IMGUI
#include "engine/core/editor/GameEditorController.h"
#endif

class PostEffect;

/// <summary>Framework上でシーン、エディター、ポストエフェクト、プロファイル計測を統合する。</summary>
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

	void InitializeScene();

	void InitializePostProcess();

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

	void DrawRuntimeFrame(PostEffect* postEffect);

	void DrawSceneToRenderTexture(bool editorMode);

	void DrawCameraEditorPreview(PostEffect* postEffect);

	// シーンの描画結果へ画面効果を適用し、編集用または実行用の出力先へ渡す。
	void ApplyPostEffectPipeline(PostEffect* postEffect, bool outputForEditorGameView);

	// 描画経路のCPU時間と固定FPS待機時間を記録する。
	void RecordDrawProfile(const std::chrono::high_resolution_clock::time_point& startDraw);

	void RecordFixedFpsProfile();

private:
	std::unique_ptr<SceneManager> sceneManager_ = nullptr;
	std::unique_ptr<AbstractSceneFactory> sceneFactory_ = nullptr;

	std::chrono::high_resolution_clock::time_point lastTime_;
	std::chrono::high_resolution_clock::time_point prePostDrawTime_;

	float timeScale_ = 1.0f;
	float fixedUpdateAccumulator_ = 0.0f;
	bool isPlaying_ = false;
	bool initialSceneOverridesPending_ = false;
	std::string currentSceneName_;

	// 計測履歴はミリ秒。ゲーム更新へ渡すdeltaTimeや固定刻みの秒数とは混ぜない。
	float sceneUpdateTimeMs_ = 0.0f;
	float sceneDrawTimeMs_ = 0.0f;
	float updateTimeHistory_[120] = {};
	int timeHistoryIndex_ = 0;

	float drawTimeMs_ = 0.0f;
	float cpuCmdTimeMs_ = 0.0f;
	float drawTimeHistory_[120] = {};

#ifdef USE_IMGUI
	std::unique_ptr<GameEditorController> editorController_;
	EditorFrameState editorFrameState_;
#endif
};
