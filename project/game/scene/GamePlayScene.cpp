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
#include "ObjectManager.h" 
#include "BossCore.h"
#include"WinApp.h"
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
#include <SrvManager.h>

GamePlayScene::GamePlayScene() {}
GamePlayScene::~GamePlayScene() {}

void GamePlayScene::Initialize() {
	using json = nlohmann::json;

	// --- 1. エンジン基盤・リソース初期化 ---
	dxCommon_ = DirectXCommon::GetInstance();
	inputManager_ = InputManager::GetInstance();
	audioPlayer_ = AudioPlayer::GetInstance();

	LOG("Game Initialized!");

	bgmHandle_ = audioPlayer_->LoadSoundFile("Resources/bgm/Alarm02.mp3");

	// --- 2. 各種マネージャ初期化 ---
	EventManager::GetInstance()->ClearAllListeners();
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
	uint32_t lockOnTex = TextureManager::GetInstance()->Load("Resources/sprite/lockOn.png"); 
	lockOnSprite_ = std::make_unique<Sprite>();
	lockOnSprite_->Initialize(spriteCommon_.get(), lockOnTex);
	lockOnSprite_->SetAnchorPoint({ 0.5f, 0.5f }); // 画像の中心を基準にする
	lockOnSprite_->SetSize({ 64.0f, 64.0f });      // アイコンのサイズ（適宜調整！）
	BulletManager::GetInstance()->Initialize(object3dCommon_.get(), CollisionManager::GetInstance());

	GPUParticleManager::GetInstance()->Initialize(dxCommon_);
	GPUParticleManager::GetInstance()->LoadAllPresets();
	// パーティクルで使う画像を読み込み、ハンドル(番号)を保存しておく
	gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/white.png");


	// --- 5. レベルデータ読み込み (JSON) ---
	levelLoader_ = std::make_unique<LevelLoader>();
	levelLoader_->LoadObjectLayout(this, "Resources/json/3Dobject/bossStage.json");
	levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/sprite_layout.json");
	LightManager::GetInstance()->LoadState("Resources/json/light/light_layout.json");
	CameraEditor::GetInstance()->Initialize();
	CameraEditor::GetInstance()->LoadFile("game_camera.json");



	// --- スプライトの中から探索
	for (auto& sprite : sprites_) {
		if (sprite->GetName() == "playerHpBar") {
			playerHpBarSprite_ = sprite.get();
			playerHpBarMaxWidth_ = sprite->GetSize().x; // 元の長さを記憶！
		}
	}
	for (auto& sprite : sprites_) {
		if (sprite->GetName() == "bossrHpBar") {
			bossHpBarSprite_ = sprite.get();
			bossHpBarMaxWidth_ = sprite->GetSize().x;
		}
		else if (sprite->GetName() == "bariaHp.png") {
			barrierHpBarSprite_ = sprite.get();
			barrierHpBarMaxWidth_ = sprite->GetSize().x;
		}
	}



	// ★ 1. まず objectManager からオブジェクトのリストを取得する！
	auto &objects = objectManager_->GetObjects ();

	for (auto it = objects.begin (); it != objects.end (); ++it) {
		if ((*it)->GetName () == "Enemy_BossCore") {
			// 1. 古いボスの「今の住所」をメモ（まだ消さない）
			Object3d *oldAddress = it->get ();

			// 2. 新しい BossCore を準備（まだリストには入れない）
			auto newBoss = std::make_unique<BossCore> ();
			newBoss->SetSceneManager (SceneManager::GetInstance ());
			newBoss->Initialize (object3dCommon_.get (), oldAddress->GetModelName ());
			newBoss->CopyFrom (oldAddress); // 座標などをコピー
			newBoss->SetTarget (player_);
			this->boss_ = newBoss.get();
			BossCore *newAddress = newBoss.get ();

			// ★★★ ここが重要：古いボスが消える「前」に全てを繋ぎ直す ★★★

			// (A) 当たり判定マネージャから古いボスを抹消し、新しいボスを登録する
			// ※ もし Remove/Add 関数がない場合は、後述の「強硬手段」を使ってください
			CollisionManager::GetInstance ()->RemoveObject (oldAddress);
			CollisionManager::GetInstance ()->AddObject (newAddress);

			// (B) 子供たちの親を、古い住所から新しい住所へ書き換える
			for (auto &obj : objects) {
				if (obj->GetParent () == oldAddress) {
					obj->SetParent (newAddress);

					// ★ ここを追加！新しいボスにパーツを登録する
					newAddress->AddArmorBlock (obj.get ());
				}
			}

			// 3. 最後に実体を差し替える。ここで oldAddress は安全に消滅する
			*it = std::move (newBoss);
			break;
		}
		
	}

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
	// =================================================================
	// ロックオンアイコンの 2.5D 追従計算 (World To Screen)
	// =================================================================
	Object3d* target = lockOnSystem_->GetTarget();

	if (target && lockOnSystem_->IsLockingOn()) {
		isDrawLockOn_ = true;

		// =======================================================
		// ：AABB(当たり判定)から「真の中心」と「大きさ」を取得！
		// =======================================================
		AABB aabb = target->GetAABB();

		// ① ターゲットの「真の中心座標」を計算
		Vector3 targetCenter;
		targetCenter.x = (aabb.min.x + aabb.max.x) * 0.5f;
		targetCenter.y = (aabb.min.y + aabb.max.y) * 0.5f;
		targetCenter.z = (aabb.min.z + aabb.max.z) * 0.5f;

		// ② カメラのビュー行列とプロジェクション行列を掛け合わせる
		Matrix4x4 viewProj = math.Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());

		// ③ ワールド座標(中心) → クリップ座標 (W除算) の計算
		float w = targetCenter.x * viewProj.m[0][3] + targetCenter.y * viewProj.m[1][3] + targetCenter.z * viewProj.m[2][3] + viewProj.m[3][3];

		// カメラの後ろ（画面外）にいる時は表示しない
		if (w > 0.001f) {
			Vector3 ndc;
			ndc.x = (targetCenter.x * viewProj.m[0][0] + targetCenter.y * viewProj.m[1][0] + targetCenter.z * viewProj.m[2][0] + viewProj.m[3][0]) / w;
			ndc.y = (targetCenter.x * viewProj.m[0][1] + targetCenter.y * viewProj.m[1][1] + targetCenter.z * viewProj.m[2][1] + viewProj.m[3][1]) / w;

			float screenWidth = WinApp::kClientWidth;
			float screenHeight = WinApp::kClientHeight;

			float screenX = (ndc.x + 1.0f) * 0.5f * screenWidth;
			float screenY = (1.0f - ndc.y) * 0.5f * screenHeight;

			lockOnSprite_->SetPosition({ screenX, screenY });

			// =======================================================
			// ：オブジェクトの大きさに応じたアイコンサイズの自動調整！
			// =======================================================
			float objSizeX = aabb.max.x - aabb.min.x;
			float objSizeY = aabb.max.y - aabb.min.y;
			float objSizeZ = aabb.max.z - aabb.min.z;
			float maxObjSize = std::max({ objSizeX, objSizeY, objSizeZ });

			float baseSize = maxObjSize * 25.0f;
			float distanceScale = 20.0f / w;

			float finalSize = baseSize * distanceScale;
			finalSize = std::max(32.0f, std::min(finalSize, 256.0f));

			lockOnSprite_->SetSize({ finalSize, finalSize });

			// （おまけ）ロックオンアイコンを毎フレーム少し回転させると超カッコよくなります
			float currentRot = lockOnSprite_->GetRotation();
			lockOnSprite_->SetRotation(currentRot + 2.0f * deltaTime);

			lockOnSprite_->Update();
		}
		else {
			isDrawLockOn_ = false; // カメラの裏にいる時は消す
		}
	}
	else {
		// =======================================================
		// ロックオンしていない時は確実に表示をオフにする！
		// =======================================================
		isDrawLockOn_ = false;
	}

	// 自由カメラモード以外の操作
	if (!CameraEditor::GetInstance()->IsEditorMode()) {
		Camera::FollowMode currentMode = camera->GetFollowMode();

		if (currentMode == Camera::FollowMode::kAimable || currentMode == Camera::FollowMode::kFirstPerson) {
			Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();

#ifdef USE_IMGUI
			// ★ デバッグ(Develop)環境: UI操作の誤爆を防ぐため「右クリック中」のみ回転
			if (inputManager_->IsMouseButtonPressed(1)) {
				if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
					camera->AddRotation(mouseDelta);
				}
			}
#else
			// ★ Release環境限定: 右クリック不要！マウスを動かすだけで回転する
			if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
				camera->AddRotation(mouseDelta);
			}
#endif
		}
	}
	// --- 全体更新 ---
	CameraManager::GetInstance()->Update();
	particleSystem_->Update(deltaTime);
	objectManager_->Update(deltaTime); // オブジェクト一括更新

	// 例：座標(0, 5, 0) から、上方向(0, 10, 0) に向けて毎フレーム500個噴き出す
	//GPUParticleManager::GetInstance()->Emit(
	//	{ 0.0f, 5.0f, 0.0f },  // 発生座標
	//	{ 0.0f, 10.0f, 0.0f }, // 飛ぶ方向
	//	500,                   // 発生数
	//	2.0f,                  // 寿命 (2秒で消える)
	//	5.0f,                   // 散らばり具合,
	//{1.0f, 0.5f, 0.0f, 1.0f} // 色 (オレンジ)
	//);

	//// ★追加: 溜まった発生命令をもとに、GPUに計算（Compute Shader）を走らせる
	//GPUParticleManager::GetInstance()->Update(deltaTime);
	for (auto& sprite : sprites_) {
		sprite->Update();
	}

	BulletManager::GetInstance()->Update(deltaTime);
	CollisionManager::GetInstance()->Update();
	UpdateUI();
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

	// =========================================================
	// ★ 追加: カメラがプレイヤーに近すぎたら、強制的に「非表示(一人称扱い)」にする！
	// =========================================================
	if (!isFirstPerson && player_ && camera) {
		Vector3 pPos = player_->GetWorldPosition();
		pPos.y += 1.0f; // プレイヤーの胸の高さを基準にする
		Vector3 cPos = camera->GetEye();
		Vector3 toCam = { cPos.x - pPos.x, cPos.y - pPos.y, cPos.z - pPos.z };
		float dist = std::sqrt(toCam.x * toCam.x + toCam.y * toCam.y + toCam.z * toCam.z);

		// 距離が 3.0m 未満なら、プレイヤーを完全に消す！
		if (dist < 3.0f) {
			isFirstPerson = true;
		}
	}

	ID3D12Resource* pointLightRes = LightManager::GetInstance()->GetPointLightResource();
	ID3D12Resource* spotLightRes = LightManager::GetInstance()->GetSpotLightResource();
	object3dCommon_->SetGraphicsCommand();

	auto& objects = objectManager_->GetObjects();

	// --- 1. 不透明描画 ---
	for (auto& obj : objects) {
		// =========================================================
		// ★ 修正: プレイヤー本体だけでなく「子パーツ（緑のブロック等）」も巻き込んで消す！
		// =========================================================
		bool isPlayerPart = false;
		if (isFirstPerson) {
			Object3d* current = obj.get();
			while (current) {
				if (current == player_) { isPlayerPart = true; break; }
				current = current->GetParent();
			}
		}
		if (isPlayerPart) continue; // プレイヤーの一部なら描画をスキップ！

		if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7) continue;
		obj->Draw(pointLightRes, spotLightRes);
	}

	// --- 2. 中間描画 (弾・デバッグ) ---
	BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
	if (debugEditor_) debugEditor_->DrawPreview(pointLightResource_.Get(), spotLightResource_.Get());
	LightEditor::GetInstance()->Draw3D();

	// --- 3. 透明描画 ---
	for (auto& obj : objects) {
		// ここでも同じくプレイヤー関連をスキップ
		bool isPlayerPart = false;
		if (isFirstPerson) {
			Object3d* current = obj.get();
			while (current) {
				if (current == player_) { isPlayerPart = true; break; }
				current = current->GetParent();
			}
		}
		if (isPlayerPart) continue;

		if (obj->GetMaterialType() == 1) { // 透明のみ描画
			obj->Draw(pointLightRes, spotLightRes);
		}
	}
	particleSystem_->Draw();

	// =======================================================
	// 4. ローカルフォグ (霧の箱) の描画！
	// =======================================================
	bool hasFog = false;
	for (auto& obj : objects) {
		if (obj->GetMaterialType() == 7) hasFog = true;
	}

	if (hasFog) {
		dxCommon_->PreDrawLocalFog();
		for (auto& obj : objects) {
			if (obj->GetMaterialType() == 7) {
				obj->DrawLocalFog(dxCommon_->GetDepthSrvHandle());
			}
		}
		dxCommon_->PostDrawLocalFog();
	}

	// =======================================================
	// 5. GPUパーティクルの描画！
	// =======================================================
	dxCommon_->UpdateGrabTexture();
	dxCommon_->PreDrawLocalFog();

	GPUParticleManager::GetInstance()->Draw(
		dxCommon_->GetCommandList(),
		camera->GetViewMatrix(),
		camera->GetProjectionMatrix(),
		gpuParticleTexHandle_,
		dxCommon_->GetDepthSrvHandle()
	);

	dxCommon_->PostDrawLocalFog();
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
	if (isDrawLockOn_ && lockOnSprite_) {
		lockOnSprite_->Draw();
	}
}


void GamePlayScene::DrawShadow() {
	if (objectManager_) {

		objectManager_->DrawShadow();
	}
}

void GamePlayScene::UpdateUI() {
	// 1. プレイヤーのHP同期
	if (player_ && playerHpBarSprite_) {
		float currentHp = player_->GetHp();
		float maxHp = player_->GetMaxHp();

		// 割合を計算 (0.0f ～ 1.0f の間に制限してエラーを防ぐ)
		float hpRatio = std::clamp(currentHp / maxHp, 0.0f, 1.0f);

		// スプライトの幅を更新
		Vector2 newSize = playerHpBarSprite_->GetSize();
		newSize.x = playerHpBarMaxWidth_ * hpRatio;
		playerHpBarSprite_->SetSize(newSize);
	}
	if (boss_) {
		// A. メインHPバーの同期
		if (bossHpBarSprite_) {
			float hpRatio = std::clamp(boss_->GetHp() / boss_->GetMaxHp(), 0.0f, 1.0f);
			bossHpBarSprite_->SetSize({ bossHpBarMaxWidth_ * hpRatio, bossHpBarSprite_->GetSize().y });
		}

		// B. バリアHPバーの同期
		if (barrierHpBarSprite_) {
			float bRatio = std::clamp(boss_->GetBarrierHp() / boss_->GetMaxBarrierHp(), 0.0f, 1.0f);
			barrierHpBarSprite_->SetSize({ barrierHpBarMaxWidth_ * bRatio, barrierHpBarSprite_->GetSize().y });
		}
	}
}