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
#include"MeshEffectManager.h"
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
#include <PostEffect.h>

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
	particleSystem_->Initialize(particleCommon_.get(), "Resources/sprite/circle2.png");

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
	GPUParticleManager::GetInstance()->LoadAllPresets("Resources/json/gpu_particles/");
	// パーティクルで使う画像を読み込み、ハンドル(番号)を保存しておく
	gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/white.png");

	// 1. キューブマップ（DDS）の読み込み
	// TextureManagerがDDS対応していればこれでハンドルが取れます
	skyboxTextureHandle_ = TextureManager::GetInstance()->Load("Resources/output_skybox.dds");

	// 2. スカイボックスの生成と初期化
	skybox_ = std::make_unique<Skybox>();
	// object3dCommon_ は GamePlayScene が持っている共通クラスを渡します
	skybox_->Initialize(object3dCommon_.get(), skyboxTextureHandle_);

	// --- 5. レベルデータ読み込み (JSON) ---
	levelLoader_ = std::make_unique<LevelLoader>();
	levelLoader_->LoadObjectLayout(this, "Resources/json/3Dobject/bossStage.json");
	levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/sprite_layout.json");
	LightManager::GetInstance()->LoadState("Resources/json/light/light_layout.json");
	CameraEditor::GetInstance()->Initialize();
	CameraEditor::GetInstance()->LoadFile("game_camera.json");

	// ★課題用アニメーションモデルの生成
	animatedCube_ = std::make_unique<Object3d>();
	animatedCube_->Initialize(object3dCommon_.get());
	animatedCube_->SetModel("walk"); 
	
	// モデルが持つ最初のアニメーション名を自動で取得して設定する
	if (animatedCube_->GetModel() && !animatedCube_->GetModel()->GetModelData().animations.empty()) {
		animatedCube_->animName_ = animatedCube_->GetModel()->GetModelData().animations[0].name;
	}
	
	animatedCube_->isAnimLoop_ = true;
	animatedCube_->SetTranslate({0.0f, 0.0f, 0.0f}); // 少し位置を調整
	animatedCube_->SetScale({2.0f, 2.0f, 2.0f});

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

	// --- ポストエフェクト更新 ---
	PostEffect::GetInstance()->Update(deltaTime);
	Camera* activeCamera = CameraManager::GetInstance()->GetActiveCamera();
	if (activeCamera) {
		PostEffect::GetInstance()->GetParams()->projectionInverse = Math::Inverse(activeCamera->GetProjectionMatrix());
	}

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

				float screenWidth = static_cast<float>(::WinApp::kClientWidth);
				float screenHeight = static_cast<float>(::WinApp::kClientHeight);
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

	// 溜まった発生命令をもとに、GPUに計算（Compute Shader）を走らせる
	GPUParticleManager::GetInstance()->Update(deltaTime);
	for (auto& sprite : sprites_) {
		sprite->Update();
	}
	BulletManager::GetInstance()->Update(deltaTime);
	CollisionManager::GetInstance()->Update();
	UpdateUI();
	if (inputManager_->IsKeyTriggered(DIK_SPACE)) {
		Vector3 effectPos = { 0.0f, 2.0f, 0.0f };
		// ★課題: 星型ヒット(8個) + 斬撃(3個) を同時発動
		particleSystem_->SpawnStarHitEffect(effectPos);
		particleSystem_->SpawnSlashEffect(effectPos);
	}
	// 個別確認用 (ENTER=星型のみ, E=斬撃のみ, R=リング波紋)
	if (inputManager_->IsKeyTriggered(DIK_RETURN)) {
		Vector3 effectPos = { 0.0f, 2.0f, 0.0f };
		particleSystem_->SpawnStarHitEffect(effectPos);
	}
	if (inputManager_->IsKeyTriggered(DIK_E)) {
		Vector3 effectPos = { 0.0f, 2.0f, 0.0f };
		particleSystem_->SpawnSlashEffect(effectPos);
	}
	// ★課題: Rキー = 手動コード(MeshEffect)によるリング波紋エフェクト
	if (inputManager_->IsKeyTriggered(DIK_R)) {
		Vector3 effectPos = { 0.0f, 2.0f, 0.0f };
		MeshEffectManager::GetInstance()->SpawnRingWaveEffect(effectPos);
	}
	// ★課題: Tキー = Cylinder + 横UVスクロール + 色アニメ = ポータルエフェクト
	if (inputManager_->IsKeyTriggered(DIK_T)) {
		Vector3 effectPos = { 0.0f, 1.5f, 0.0f };
		MeshEffectManager::GetInstance()->SpawnPortalEffect(effectPos, 5.0f);
	}

	if (animatedCube_) {
		animatedCube_->Update(deltaTime);
	}
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
	// ★ カメラがプレイヤーに近すぎたら、強制的に「非表示(一人称扱い)」にする！
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
		//  プレイヤー本体だけでなく「子パーツ（緑のブロック等）」も巻き込んで消す！
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

		if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7 || obj->GetMaterialType() >= 8) continue;
		obj->Draw(pointLightRes, spotLightRes);
	}

	if (animatedCube_) {
		animatedCube_->Draw(pointLightRes, spotLightRes);
	}

	// --- 2. 中間描画 (弾・デバッグ) ---
	BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
	if (debugEditor_) debugEditor_->DrawPreview(pointLightResource_.Get(), spotLightResource_.Get());
	LightEditor::GetInstance()->Draw3D();
	if (skybox_) {
		skybox_->Draw(camera->GetConstantBuffer());
	}
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
	bool hasFluid = false;
	for (auto& obj : objects) {
		if (obj->GetMaterialType() >= 8 && obj->GetMaterialType() <= 10) hasFluid = true;
	}

	if (hasFluid) {
		// 画面をキャプチャしてテクスチャにする
		dxCommon_->UpdateGrabTexture();

		for (auto& obj : objects) {
			bool isPlayerPart = false;
			if (isFirstPerson) {
				Object3d* current = obj.get();
				while (current) {
					if (current == player_) { isPlayerPart = true; break; }
					current = current->GetParent();
				}
			}
			if (isPlayerPart) continue;

			int matType = obj->GetMaterialType();
			if (matType == 8) {
				obj->DrawWater(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
			}
			else if (matType == 9) {
				obj->DrawMagma(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
			}
			else if (matType == 10) {
				obj->DrawIce(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
			}
		}
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

}

void GamePlayScene::StartBridgeDropMovie() {

}