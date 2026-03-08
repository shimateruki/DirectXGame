#define NOMINMAX
#include "GamePlayScene.h"
#include "DirectXCommon.h"
#include "InputManager.h"
#include "AudioPlayer.h"
#include "Object3dCommon.h"
#include "SpriteCommon.h"
#include "Object3d.h"
#include "Sprite.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "ParticleSystem.h"
#include "imgui.h"
#include "LightManager.h"
#include <EventManager.h>
#include "SceneManager.h"
#include "DebugConsole.h"
#include <cassert>
#include "BulletManager.h"
#include "MoveStrategy3D.h"
#include "MoveStrategy2D.h"
#include "LevelLoader.h"
#include "LockOnSystem.h"
#include "GameRule.h"
#include "ObjectManager.h" // 追加
#include "easing.h" // 追加

#ifdef _DEBUG
#include "ParticleEditor.h"
#endif

// --- JSON (保存機能) ---
#include <fstream>
#include <string>
#include "json.hpp"
#include <numbers>
#include <CameraEditor.h>
#include <BaseEnemy.h>
#include <EnemyFactory.h>
#include <EnemySpawner.h>
#include <LightEditor.h>
#include <ParticleManager.h>
#include <GPUParticleManager.h>

GamePlayScene::GamePlayScene() {}
GamePlayScene::~GamePlayScene() {}

void GamePlayScene::Initialize() {
	using json = nlohmann::json;

	// --- 1. エンジン基盤・リソース初期化 ---
	dxCommon_ = DirectXCommon::GetInstance();
	inputManager_ = InputManager::GetInstance();
	audioPlayer_ = AudioPlayer::GetInstance();

	LOG("Game Initialized!");
	TextureManager::GetInstance()->Load("Resources/sprite/a.png");
	TextureManager::GetInstance()->Load("Resources/sprite/b.png");
	TextureManager::GetInstance()->Load("Resources/sprite/c.png");
	bgmHandle_ = audioPlayer_->LoadSoundFile("Resources/bgm/Alarm02.mp3");

	// --- 2. 各種マネージャ初期化 ---
	CameraManager::GetInstance()->Initialize();
	CameraManager::GetInstance()->SetInputManager(inputManager_);

	spriteCommon_ = std::make_unique<SpriteCommon>();
	spriteCommon_->Initialize(dxCommon_);

	object3dCommon_ = std::make_unique<Object3dCommon>();
	object3dCommon_->Initialize(dxCommon_);

	particleCommon_ = std::make_unique<ParticleCommon>();
	particleCommon_->Initialize(dxCommon_);

	particleSystem_ = std::make_unique<ParticleSystem>();
	particleSystem_->Initialize(particleCommon_.get(), "Resources/sprite/white.png");

	ParticleManager::GetInstance()->Initialize(particleSystem_.get());

	gameRule_ = std::make_unique<GameRule>();
	gameRule_->Initialize(this);

	LightEditor::GetInstance()->SetObject3dCommon(object3dCommon_.get());

	// --- 3. サブシステム初期化 ---
	objectManager_ = std::make_unique<ObjectManager>();

	lockOnSystem_ = std::make_unique<LockOnSystem>();
	lockOnSystem_->Initialize(inputManager_);

	BulletManager::GetInstance()->Initialize(object3dCommon_.get(), CollisionManager::GetInstance());

	GPUParticleManager::GetInstance()->Initialize(dxCommon_);

	// パーティクルで使う画像を読み込み、ハンドル(番号)を保存しておく
	gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/white.png");


	// --- 5. レベルデータ読み込み (JSON) ---
	levelLoader_ = std::make_unique<LevelLoader>();
	levelLoader_->LoadObjectLayout(this, "Resources/json/3Dobject/scene_layout.json");
	levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/sprite_layout.json");

	LightManager::GetInstance()->LoadState("Resources/json/light/light_layout.json");
	CameraEditor::GetInstance()->Initialize();
	CameraEditor::GetInstance()->LoadFile("game_camera.json");



	dxCommon_->FlushCommandQueue(false);
}

void GamePlayScene::Finalize() {
	CollisionManager::GetInstance()->ClearObjects();
	BulletManager::GetInstance()->Finalize();
	particleSystem_.reset();
	particleCommon_.reset();
	sprites_.clear();
	spriteCommon_.reset();
	object3dCommon_.reset();
	objectManager_.reset();
	lockOnSystem_.reset();
}

void GamePlayScene::Update(float deltaTime) {
	static Math math;
	LightEditor::GetInstance()->Update();

	Camera* camera = CameraManager::GetInstance()->GetMainCamera();

	// --- ロックオン & カメラ制御 ---
	lockOnSystem_->Update(objectManager_->GetObjects(), camera, player_);
	CameraEditor::GetInstance()->Update(player_, lockOnSystem_->IsLockingOn());

	// 自由カメラモード以外の操作
	if (!CameraEditor::GetInstance()->IsEditorMode()) {
		Camera::FollowMode currentMode = camera->GetFollowMode();

		// 右クリック回転
		if (currentMode == Camera::FollowMode::kAimable || currentMode == Camera::FollowMode::kFirstPerson) {
			if (inputManager_->IsMouseButtonPressed(1)) {
				Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
				if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
					camera->AddRotation(mouseDelta);
				}
			}
		}
	}

	// --- 全体更新 ---
	CameraManager::GetInstance()->Update();
	particleSystem_->Update(deltaTime);
	objectManager_->Update(deltaTime); // オブジェクト一括更新

	{
		// アニメーション管理用の変数
		static int bossPhase = 1;
		static float timer = 0.0f;
		static Vector3 startPos = { 0,0,0 };
		static Vector3 targetPos = { 0,0,0 };

		// ★リセット検知用の変数
		static bool wasPlaying = false;
		bool isPlaying = SceneManager::GetInstance ()->IsPlaying ();

		// 【ここが重要！】再生ボタンが押された瞬間（false -> true）にリセット
		if (isPlaying && !wasPlaying) {
			bossPhase = 1;
			timer = 0.0f;
			// 他にリセットしたい数値があればここに書く
		}
		wasPlaying = isPlaying; // 今の状態を保存して次フレームへ

		// エディター停止中は何もしない
		if (!isPlaying) {

			Object3d *boss = nullptr;
			// 名前でボスを探す
			for (auto &obj : objectManager_->GetObjects ()) {
				if (obj->GetName () == "Enemy_BossCore") {
					boss = obj.get ();
					break;
				}
			}

			if (boss && player_) {
				// --- 1. 指定位置 (x = -50) まで移動 ---
				if (bossPhase == 1) {
					if (timer == 0.0f) startPos = boss->GetTranslate ();

					timer += deltaTime;
					float duration = 1.5f;
					float t = std::min (timer / duration, 1.0f);
					float easedT = Easing::OutExpo (t);

					Vector3 pos = boss->GetTranslate ();
					// X座標を -50 へ移動
					pos.x = Math::Lerp (startPos.x, -50.0f, easedT);
					boss->SetTranslate (pos);

					if (t >= 1.0f) {
						bossPhase = 2;
						timer = 0.0f;
						startPos = boss->GetTranslate ();
					}
				}
				// --- 2. シェイク（溜め）処理 ---
				else if (bossPhase == 2) {
					timer += deltaTime;
					float duration = 1.0f; // 1秒間溜める
					float t = std::min (timer / duration, 1.0f);

					Vector3 pos = startPos;
					// 微振動を加える
					float shake = 0.3f;
					pos.x += ((float)rand () / RAND_MAX * 2.0f - 1.0f) * shake;
					pos.y += ((float)rand () / RAND_MAX * 2.0f - 1.0f) * shake;
					boss->SetTranslate (pos);

					if (t >= 1.0f) {
						bossPhase = 3;
						timer = 0.0f;
						startPos = boss->GetTranslate ();
						// ★突進の瞬間の「プレイヤーの場所」をロックオン！
						targetPos = player_->GetWorldPosition ();
					}
				}
				// --- 3. プレイヤーに突撃（必中狙い） ---
				else if (bossPhase == 3) {
					timer += deltaTime;
					float duration = 1.0f; // 0.4秒で超高速突進
					float t = std::min (timer / duration, 1.0f);

					// 溜めた力を一気に解放する InExpo
					float easedT = Easing::InExpo (t);

					// 待機地点(startPos)から、ロックオンしたプレイヤー(targetPos)へ
					Vector3 pos = Math::Lerp (startPos, targetPos, easedT);
					boss->SetTranslate (pos);

					if (t >= 1.0f) {
						bossPhase = 4; // 攻撃終了
					}
				}
			}
		}

	}


	// 例：座標(0, 5, 0) から、上方向(0, 10, 0) に向けて毎フレーム500個噴き出す
	GPUParticleManager::GetInstance()->Emit(
		{ 0.0f, 5.0f, 0.0f },  // 発生座標
		{ 0.0f, 10.0f, 0.0f }, // 飛ぶ方向
		500,                   // 発生数
		2.0f,                  // 寿命 (2秒で消える)
		5.0f,                   // 散らばり具合,
	{1.0f, 0.5f, 0.0f, 1.0f} // 色 (オレンジ)
	);

	// ★追加: 溜まった発生命令をもとに、GPUに計算（Compute Shader）を走らせる
	GPUParticleManager::GetInstance()->Update(deltaTime);
	for (auto& sprite : sprites_) {
		sprite->Update();
	}

	BulletManager::GetInstance()->Update(deltaTime);
	CollisionManager::GetInstance()->Update();
}


void GamePlayScene::Draw() {
	// --- 一人称視点判定 ---
	bool isFirstPerson = false;
	Camera* camera = CameraManager::GetInstance()->GetMainCamera();
#ifndef _DEBUG
	
	if (camera->GetFollowTarget() && camera->GetFollowMode() == Camera::FollowMode::kFirstPerson) {
		isFirstPerson = true;
	}
#endif

	ID3D12Resource* pointLightRes = LightManager::GetInstance()->GetPointLightResource();
	ID3D12Resource* spotLightRes = LightManager::GetInstance()->GetSpotLightResource();
	object3dCommon_->SetGraphicsCommand();

	auto& objects = objectManager_->GetObjects();

	// --- 1. 不透明描画 ---
	for (auto& obj : objects) {
		if (isFirstPerson && obj.get() == player_) continue;
		if (obj->GetMaterialType() == 1) continue; // 透明はスキップ
		obj->Draw(pointLightRes, spotLightRes);
	}

	// --- 2. 中間描画 (弾・デバッグ) ---
	BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
	if (debugEditor_) debugEditor_->DrawPreview(pointLightResource_.Get(), spotLightResource_.Get());
	LightEditor::GetInstance()->Draw3D();

	// --- 3. 透明描画 ---
	for (auto& obj : objects) {
		if (isFirstPerson && obj.get() == player_) continue;
		if (obj->GetMaterialType() == 1) { // 透明のみ描画
			obj->Draw(pointLightRes, spotLightRes);
		}
	}
	particleSystem_->Draw();

	// =======================================================
	// : GPUパーティクルの描画！
	// =======================================================

	// 定数バッファではなく、View行列とProjection行列をそのまま渡す！
	//GPUParticleManager::GetInstance()->Draw(
	//	dxCommon_->GetCommandList(),
	//	camera->GetViewMatrix(),
	//	camera->GetProjectionMatrix(),
	//	gpuParticleTexHandle_
	//);
}

// ====================================================================
// UI描画専用の関数
// ====================================================================
void GamePlayScene::DrawUI() {
	// --- 4. 2D描画 (UIスプライト) ---
	spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
	for (auto& sprite : sprites_) {
		sprite->Draw();
	}
}