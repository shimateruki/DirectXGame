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
#include "ProfilerManager.h"
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
#include "IconsFontAwesome5.h"
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
#include "Fade.h"
#include "StageManager.h"
#include "GameDataManager.h"

#include <algorithm>
#include <cmath>

GamePlayScene::GamePlayScene() {}
GamePlayScene::~GamePlayScene() {}

void GamePlayScene::Initialize() {
	using json = nlohmann::json;

	// --- 1. エンジン基盤・リソース初期化 ---
	dxCommon_ = DirectXCommon::GetInstance();
	inputManager_ = InputManager::GetInstance();
	audioPlayer_ = AudioPlayer::GetInstance();

	LOG("Game Initialized!");

	const StageData& currentStage = StageManager::GetInstance()->GetCurrentStage();
	bgmHandle_ = audioPlayer_->LoadSoundFile(currentStage.bgmPath);

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
	particleSystem_->Initialize(particleCommon_.get(), "Resources/sprite/common/circle2.png");

	ParticleManager::GetInstance()->Initialize(particleSystem_.get());

	gameRule_ = std::make_unique<GameRule>();
	gameRule_->Initialize(this);

	LightEditor::GetInstance()->SetObject3dCommon(object3dCommon_.get());

	// --- 3. サブシステム初期化 ---
	objectManager_ = std::make_unique<ObjectManager>();

	lockOnSystem_ = std::make_unique<LockOnSystem>();
	lockOnSystem_->Initialize(inputManager_);
	uint32_t lockOnTex = TextureManager::GetInstance()->Load("Resources/sprite/ui/hud/lockOn.png"); 
	lockOnSprite_ = std::make_unique<Sprite>();
	lockOnSprite_->Initialize(spriteCommon_.get(), lockOnTex);
	lockOnSprite_->SetAnchorPoint({ 0.5f, 0.5f }); // 画像の中心を基準にする
	lockOnSprite_->SetSize({ 64.0f, 64.0f });      // アイコンのサイズ（適宜調整！）
	BulletManager::GetInstance()->Initialize(object3dCommon_.get(), CollisionManager::GetInstance());

	GPUParticleManager::GetInstance()->Initialize(dxCommon_);
	GPUParticleManager::GetInstance()->LoadAllPresets("Resources/json/gpu_particles/");
	// パーティクルで使う画像を読み込み、ハンドル(番号)を保存しておく
	gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");

	// 1. キューブマップ（DDS）の読み込み
	// TextureManagerがDDS対応していればこれでハンドルが取れます
	skyboxTextureHandle_ = TextureManager::GetInstance()->Load("Resources/output_skybox.dds");

	// 2. スカイボックスの生成と初期化
	skybox_ = std::make_unique<Skybox>();
	// object3dCommon_ は GamePlayScene が持っている共通クラスを渡します
	skybox_->Initialize(object3dCommon_.get(), skyboxTextureHandle_);

	// --- 5. レベルデータ読み込み (JSON) ---
	levelLoader_ = std::make_unique<LevelLoader>();
	levelLoader_->LoadObjectLayout(this, currentStage.levelPath);
	levelLoader_->LoadSpriteLayout(this, currentStage.spritePath);
	LightManager::GetInstance()->LoadState("Resources/json/light/light_layout.json");
	CameraEditor::GetInstance()->Initialize();
	CameraEditor::GetInstance()->LoadFile("game_camera.json");
	InitializeGameplayHUD();
	if (GameDataManager::GetInstance()->ConsumeRespawnIrisInRequest()) {
		Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
		Vector2 irisCenter = { 0.5f, 0.5f };
		if (cam && player_) {
			cam->Update();
			Vector3 worldPos = player_->GetWorldPosition();
			worldPos.y += 1.0f;
			Vector3 ndc = Math::Transform(worldPos, cam->GetViewProjectionMatrix());
			const bool isOnScreen =
				std::isfinite(ndc.x) &&
				std::isfinite(ndc.y) &&
				std::isfinite(ndc.z) &&
				ndc.z >= 0.0f &&
				std::abs(ndc.x) <= 1.15f &&
				std::abs(ndc.y) <= 1.15f;
			if (isOnScreen) {
				irisCenter = {
					std::clamp((ndc.x + 1.0f) * 0.5f, 0.15f, 0.85f),
					std::clamp((1.0f - ndc.y) * 0.5f, 0.12f, 0.88f)
				};
			}
		}
		Fade::GetInstance()->StartIrisIn(1.25f, irisCenter);
	}

	// 課題用アニメーションモデルの生成
	animatedCube_ = std::make_unique<Object3d>();
	animatedCube_->Initialize(object3dCommon_.get());
	animatedCube_->SetModel("Samples/walk"); 
	
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
	hudLifeMeter_.reset();
	hudLifeMeterDigit_.reset();
	hudLifeIcon_.reset();
	hudLifeXIcon_.reset();
	for (auto& digit : hudLifeDigits_) {
		digit.reset();
	}
	hudCoinIcon_.reset();
	hudCoinXIcon_.reset();
	for (auto& digit : hudCoinDigits_) {
		digit.reset();
	}
	lifeLostIcon_.reset();
	lifeLostXIcon_.reset();
	for (auto& digit : lifeLostDigits_) {
		digit.reset();
	}
	lifeLostBackdrop_.reset();
}

void GamePlayScene::Update(float deltaTime) {
	// ゴール時の処理
	if (isGoal_) {
		// クリア状況を保存
		int currentStage = StageManager::GetInstance()->GetCurrentStageIndex();
		GameDataManager::GetInstance()->MarkStageCleared(currentStage);

		// スターコインの保存
		for (int i = 0; i < 3; i++) {
			if (sessionStarCoins_[i]) {
				GameDataManager::GetInstance()->MarkStarCoinCollected(currentStage, i);
			}
		}

		deltaTime = 0.0f; // 時を止める

		if (inputManager_->IsKeyTriggered(DIK_SPACE)) {
			SceneManager::GetInstance()->ChangeScene("SELECT");
			return;
		}
	}

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
				if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
					camera->AddRotation(mouseDelta);
				}
			}
	}
	// --- 全体更新 ---
	ProfilerManager::GetInstance()->SetObjectList(&objectManager_->GetObjects());
	CameraManager::GetInstance()->Update();
	particleSystem_->Update(deltaTime);
	objectManager_->Update(deltaTime); // オブジェクト一括更新

	// 溜まった発生命令をもとに、GPUに計算（Compute Shader）を走らせる
	GPUParticleManager::GetInstance()->Update(deltaTime);
	for (auto& sprite : sprites_) {
		sprite->Update();
	}
	BulletManager::GetInstance()->Update(deltaTime);
	{
		PROFILE_SCOPE("衝突判定");
		CollisionManager::GetInstance()->Update();
	}
	UpdateUI(deltaTime);
	if (inputManager_->IsKeyTriggered(DIK_SPACE)) {
		Vector3 effectPos = { 0.0f, 2.0f, 0.0f };
		// 星型ヒット(8個) + 斬撃(3個) を同時発動
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
	// Rキー = 手動コード(MeshEffect)によるリング波紋エフェクト
	if (inputManager_->IsKeyTriggered(DIK_R)) {
		Vector3 effectPos = { 0.0f, 2.0f, 0.0f };
		MeshEffectManager::GetInstance()->SpawnRingWaveEffect(effectPos);
	}
	// Tキー = Cylinder + 横UVスクロール + 色アニメ = ポータルエフェクト
	if (inputManager_->IsKeyTriggered(DIK_T)) {
		Vector3 effectPos = { 0.0f, 1.5f, 0.0f };
		MeshEffectManager::GetInstance()->SpawnPortalEffect(effectPos, 5.0f);
	}
	// Yキー = 常に表示される「旅の扉（ワープポータル）」を生成
	if (inputManager_->IsKeyTriggered(DIK_Y)) {
		Vector3 spawnPos = { 0.0f, 0.01f, 10.0f }; // デフォルト位置
		if (player_) {
			Vector3 playerPos = player_->GetWorldPosition();
			float yaw = player_->GetRotation().y;
			// プレイヤーの3.0m前方床に設置
			spawnPos = {
				playerPos.x + std::sin(yaw) * 3.0f,
				playerPos.y,
				playerPos.z + std::cos(yaw) * 3.0f
			};
		}
		// 常に表示される渦巻床（リング）と、空間歪みの光柱（円柱）を生成
		MeshEffectManager::GetInstance()->SpawnEffectAt("Resources/json/effect/effect_warp_gate_floor.json", spawnPos, { 0.0f, 0.0f, 0.0f });
		MeshEffectManager::GetInstance()->SpawnEffectAt("Resources/json/effect/effect_warp_gate_pillar.json", spawnPos, { 0.0f, 0.0f, 0.0f });
	}

	if (animatedCube_) {
		animatedCube_->Update(deltaTime);
	}
}


void GamePlayScene::Draw() {
	// --- 一人称視点（カメラのめり込み）判定 ---
	bool isFirstPerson = false;
	Camera* camera = CameraManager::GetInstance()->GetMainCamera();
#ifndef _DEBUG
	if (camera->GetFollowTarget() && camera->GetFollowMode() == Camera::FollowMode::kFirstPerson) {
		isFirstPerson = true;
	}
#endif

	// =========================================================
	// カメラがプレイヤーに近すぎたら、強制的に「非表示(一人称扱い)」にする
	// =========================================================
	if (!isFirstPerson && player_ && camera) {
		Vector3 pPos = player_->GetWorldPosition();
		pPos.y += 1.0f; // プレイヤーの胸の高さを基準にする
		Vector3 cPos = camera->GetEye();
		Vector3 toCam = { cPos.x - pPos.x, cPos.y - pPos.y, cPos.z - pPos.z };
		float dist = std::sqrt(toCam.x * toCam.x + toCam.y * toCam.y + toCam.z * toCam.z);

		// 距離が 3.0m 未満なら、プレイヤーを完全に消す判定フラグを立てる
		if (dist < 3.0f) {
			isFirstPerson = true;
		}
	}

	ID3D12Resource* pointLightRes = LightManager::GetInstance()->GetPointLightResource();
	ID3D12Resource* spotLightRes = LightManager::GetInstance()->GetSpotLightResource();

	auto& objects = objectManager_->GetObjects();

	// =======================================================
	// 1. 不透明モデル描画
	// =======================================================
	object3dCommon_->SetGraphicsCommand();
	object3dCommon_->SetPipelineState(BlendMode::kNone); // 不透明設定

	for (auto& obj : objects) {
		if (!IsVisible(obj.get())) continue;

		// プレイヤー本体および「子パーツ（緑のブロック等）」かどうかの判定
		bool isPlayerPart = false;
		if (isFirstPerson) {
			Object3d* current = obj.get();
			while (current) {
				if (current == player_) { isPlayerPart = true; break; }
				current = current->GetParent();
			}
		}

		// プレイヤーの一部なら描画をスキップ
		if (isPlayerPart) {
			continue;
		}

		// 半透明マテリアル(1)、ローカルフォグ(7)、流体マテリアル(8以上)はここでは描画しない
		if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7 || obj->GetMaterialType() >= 8) continue;

		obj->Draw(pointLightRes, spotLightRes);
	}
	
	// フックマーカーの描画（プレイヤーがカメラ外に判定されて非表示になってもマーカーだけは描画する）
	if (player_ && player_->GetHookMarker()) {
		player_->GetHookMarker()->Draw(pointLightRes, spotLightRes);
	}

	if (animatedCube_) {
		animatedCube_->Draw(pointLightRes, spotLightRes);
	}

	// =======================================================
	// 2. 中間描画 (弾・デバッグUI・背景など)
	// =======================================================
	BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
	LightEditor::GetInstance()->Draw3D();
	if (skybox_) {
		skybox_->Draw(camera->GetConstantBuffer());
	}

	// =======================================================
	// 3. 半透明モデル描画
	// =======================================================
	for (auto& obj : objects) {
		bool isPlayerPart = false;
		if (isFirstPerson) {
			Object3d* current = obj.get();
			while (current) {
				if (current == player_) { isPlayerPart = true; break; }
				current = current->GetParent();
			}
		}

		if (isPlayerPart) {
			continue;
		}

		if (obj->GetMaterialType() == 1) { // 半透明マテリアルのみ描画
			obj->Draw(pointLightRes, spotLightRes);
		}
	}
	particleSystem_->Draw();

	// =======================================================
	// 4. ローカルフォグ (霧の箱) の描画
	// =======================================================
	bool hasFog = false;
	for (auto& obj : objects) {
		if (obj->GetMaterialType() == 7) {
			hasFog = true;
			break;
		}
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
	// 5. GPUパーティクル / 流体 (水・マグマ・氷) の描画
	// =======================================================
	bool hasFluid = false;
	for (auto& obj : objects) {
		if (obj->GetMaterialType() >= 8 && obj->GetMaterialType() <= 20) {
			hasFluid = true;
			break;
		}
	}

	bool hasGPUParticles = !GPUParticleManager::GetInstance()->IsEmpty();
	bool grabUpdated = false;
	if (hasFluid || hasGPUParticles) {
		// 画面をキャプチャして背景テクスチャにする (必要な時だけ1回呼ぶ)
		dxCommon_->UpdateGrabTexture();
		grabUpdated = true;

		if (hasFluid) {
			for (auto& obj : objects) {
				bool isPlayerPart = false;
				if (isFirstPerson) {
					Object3d* current = obj.get();
					while (current) {
						if (current == player_) { isPlayerPart = true; break; }
						current = current->GetParent();
					}
				}
				if (isPlayerPart) continue; // 流体の場合は分身になることはないので単純スキップ

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
				else if (matType == 11) {
					obj->DrawFire(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 12) {
					obj->DrawLaser(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 13) {
					obj->DrawSlimeGel(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 14) {
					obj->DrawShockwave(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 15) {
					obj->DrawLiquidContact(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 16) {
					obj->DrawDamageCrack(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 17) {
					obj->DrawUpdraft(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 18) {
					obj->DrawStunBind(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 19) {
					obj->DrawCrownUnlock(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 20) {
					obj->DrawPoisonSpore(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 21) {
					obj->DrawCloud(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
			}
		}

		if (hasGPUParticles) {
			GPUParticleManager::GetInstance()->Draw(
				dxCommon_->GetCommandList(),
				camera->GetViewMatrix(),
				camera->GetProjectionMatrix(),
				gpuParticleTexHandle_,
				dxCommon_->GetDepthSrvHandle()
			);
		}
	}

	// =======================================================
	// 6. メッシュエフェクト（アタッチ済み）の描画
	//    エフェクトの歪み(Distortion)はGrabTextureを参照するため、
	//    Object3d::Draw() の中ではなくGrabTexture更新後にここで描画する。
	// =======================================================
	{
		bool hasMeshEffects = false;
		for (auto& obj : objects) {
			if (!obj->GetMeshEffect1Name().empty() || !obj->GetMeshEffect2Name().empty()) {
				hasMeshEffects = true;
				break;
			}
		}
		if (hasMeshEffects) {
			// GrabTextureがまだ更新されていなければ、ここで更新する
			if (!grabUpdated) {
				dxCommon_->UpdateGrabTexture();
			}
			// 各オブジェクトのアタッチ済みエフェクトを描画
			for (auto& obj : objects) {
				if (!obj->GetIsVisible()) continue;
				ID3D12Resource* ptLight = pointLightRes;
				ID3D12Resource* spLight = spotLightRes;
				obj->DrawAttachedEffects(ptLight, spLight);
			}
		}
	}
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
	if (player_) {
		player_->DrawUI();
	}
	DrawGameplayHUD();
}


void GamePlayScene::DrawShadow() {
	if (objectManager_) {

		objectManager_->DrawShadow();
	}
}

void GamePlayScene::UpdateUI(float deltaTime) {
	UpdateGameplayHUD(deltaTime);
	UpdateLifeLostPresentation(deltaTime);
}

std::unique_ptr<Sprite> GamePlayScene::CreateGameplayHUDSprite(const std::string& texturePath, const Vector2& position, const Vector2& size, const Vector2& anchor, const Vector4& color) {
	auto sprite = std::make_unique<Sprite>();
	sprite->Initialize(spriteCommon_.get(), texturePath);
	sprite->SetPosition(position);
	sprite->SetSize(size);
	sprite->SetAnchorPoint(anchor);
	sprite->SetColor(color);
	sprite->SetVisible(true);
	sprite->Update();
	return sprite;
}

void GamePlayScene::InitializeGameplayHUD() {
	hudLifeMeter_ = CreateGameplayHUDSprite(
		"Resources/sprite/ui/hud/life_meter_6.png",
		{ static_cast<float>(WinApp::kClientWidth) - 118.0f, 92.0f },
		{ 138.0f, 138.0f },
		{ 0.5f, 0.5f },
		{ 1.0f, 1.0f, 1.0f, 0.96f }
	);
	hudLifeMeterDigit_ = CreateGameplayHUDSprite(
		"Resources/sprite/number/big6.png",
		{ static_cast<float>(WinApp::kClientWidth) - 118.0f, 95.0f },
		{ 52.0f, 76.0f },
		{ 0.5f, 0.5f },
		{ 1.0f, 0.88f, 0.20f, 1.0f }
	);
	hudLifeIcon_ = CreateGameplayHUDSprite(
		"Resources/sprite/title/slime_save_icon.png",
		{ 38.0f, 100.0f },
		{ 50.0f, 50.0f },
		{ 0.0f, 0.5f },
		{ 1.0f, 1.0f, 1.0f, 0.96f }
	);
	hudLifeXIcon_ = CreateGameplayHUDSprite(
		"Resources/sprite/ui/hud/xUi.png",
		{ 88.0f, 100.0f },
		{ 36.0f, 36.0f },
		{ 0.5f, 0.5f },
		{ 1.0f, 0.96f, 0.62f, 0.96f }
	);

	for (auto& digit : hudLifeDigits_) {
		digit = CreateGameplayHUDSprite(
			"Resources/sprite/number/0.png",
			{ 100.0f, 100.0f },
			{ 26.0f, 38.0f },
			{ 0.5f, 0.5f },
			{ 1.0f, 0.95f, 0.56f, 1.0f }
		);
	}

	hudCoinIcon_ = CreateGameplayHUDSprite(
		"Resources/sprite/ui/hud/coin_icon.png",
		{ 38.0f, 154.0f },
		{ 48.0f, 48.0f },
		{ 0.0f, 0.5f },
		{ 1.0f, 1.0f, 1.0f, 0.96f }
	);
	hudCoinXIcon_ = CreateGameplayHUDSprite(
		"Resources/sprite/ui/hud/xUi.png",
		{ 88.0f, 154.0f },
		{ 36.0f, 36.0f },
		{ 0.5f, 0.5f },
		{ 1.0f, 0.90f, 0.42f, 0.96f }
	);
	for (auto& digit : hudCoinDigits_) {
		digit = CreateGameplayHUDSprite(
			"Resources/sprite/number/0.png",
			{ 100.0f, 154.0f },
			{ 26.0f, 38.0f },
			{ 0.5f, 0.5f },
			{ 1.0f, 0.86f, 0.28f, 1.0f }
		);
	}
	lifeLostIcon_ = CreateGameplayHUDSprite(
		"Resources/sprite/title/slime_save_icon.png",
		{ static_cast<float>(WinApp::kClientWidth) * 0.5f - 78.0f, static_cast<float>(WinApp::kClientHeight) * 0.5f },
		{ 96.0f, 96.0f },
		{ 0.5f, 0.5f },
		{ 1.0f, 1.0f, 1.0f, 0.0f }
	);
	lifeLostXIcon_ = CreateGameplayHUDSprite(
		"Resources/sprite/ui/hud/xUi.png",
		{ static_cast<float>(WinApp::kClientWidth) * 0.5f + 10.0f, static_cast<float>(WinApp::kClientHeight) * 0.5f },
		{ 64.0f, 64.0f },
		{ 0.5f, 0.5f },
		{ 1.0f, 0.96f, 0.62f, 0.0f }
	);
	for (auto& digit : lifeLostDigits_) {
		digit = CreateGameplayHUDSprite(
			"Resources/sprite/number/big0.png",
			{ static_cast<float>(WinApp::kClientWidth) * 0.5f + 88.0f, static_cast<float>(WinApp::kClientHeight) * 0.5f },
			{ 56.0f, 82.0f },
			{ 0.5f, 0.5f },
			{ 1.0f, 0.92f, 0.38f, 0.0f }
		);
	}
	lifeLostBackdrop_ = CreateGameplayHUDSprite(
		"Resources/sprite/common/white.png",
		{ static_cast<float>(WinApp::kClientWidth) * 0.5f, static_cast<float>(WinApp::kClientHeight) * 0.5f },
		{ static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight) },
		{ 0.5f, 0.5f },
		{ 0.0f, 0.0f, 0.0f, 0.0f }
	);
	lifeLostPresentationActive_ = false;
	lifeLostPresentationFinished_ = true;
	lifeLostBlackHold_ = false;
	lifeLostNumberDropped_ = false;
	lifeLostPresentationTimer_ = 0.0f;

	hudPreviousHp_ = player_ ? player_->GetHp() : 0.0f;
	hudDamagePulseTimer_ = 0.0f;
	hudDisplayedLife_ = 6;
	UpdateGameplayHUD(0.0f);
}

void GamePlayScene::SetGameplayHUDNumber(std::array<std::unique_ptr<Sprite>, 2>& digits, int value, const Vector2& rightAlignedPosition, float digitHeight, const Vector4& color, bool visible) {
	value = std::clamp(value, 0, 99);

	const std::array<int, 2> digitValues = { value / 10, value % 10 };
	const int digitCount = value >= 10 ? 2 : 1;
	const float digitWidth = digitHeight * 0.68f;
	const float spacing = digitWidth * 0.82f;
	const float totalWidth = digitCount == 2 ? spacing + digitWidth : digitWidth;
	const float startX = rightAlignedPosition.x - totalWidth + digitWidth * 0.5f;

	for (int i = 0; i < 2; ++i) {
		Sprite* sprite = digits[i].get();
		if (!sprite) continue;

		const bool digitVisible = visible && (digitCount == 2 || i == 1);
		sprite->SetVisible(digitVisible);
		if (!digitVisible) {
			continue;
		}

		const int sourceIndex = digitCount == 2 ? i : 1;
		const int digit = digitValues[sourceIndex];
		const uint32_t handle = Sprite::LoadTexture("number/" + std::to_string(digit) + ".png");
		sprite->SetTextureHandle(handle);
		sprite->SetPosition({ startX + (sourceIndex - (2 - digitCount)) * spacing, rightAlignedPosition.y });
		sprite->SetSize({ digitWidth, digitHeight });
		sprite->SetColor(color);
		sprite->Update();
	}
}

void GamePlayScene::UpdateGameplayHUD(float deltaTime) {
	const bool visible = player_ != nullptr;
	const float maxHp = player_ ? std::max(player_->GetMaxHp(), 1.0f) : 1.0f;
	const float hp = player_ ? std::clamp(player_->GetHp(), 0.0f, maxHp) : 0.0f;
	const float hpRate = hp / maxHp;
	int lifeValue = hp <= 0.0f ? 0 : static_cast<int>(std::ceil(hpRate * 6.0f));
	lifeValue = std::clamp(lifeValue, 0, 6);

	if (visible && (hp < hudPreviousHp_ - 0.01f || lifeValue != hudDisplayedLife_)) {
		hudDamagePulseTimer_ = 0.28f;
	}
	hudDisplayedLife_ = lifeValue;
	hudPreviousHp_ = hp;
	hudDamagePulseTimer_ = std::max(0.0f, hudDamagePulseTimer_ - deltaTime);

	const float pulse = hudDamagePulseTimer_ > 0.0f ? std::sin(hudDamagePulseTimer_ * 70.0f) : 0.0f;
	const float lifePulse = hudDamagePulseTimer_ > 0.0f ? 1.0f + std::abs(pulse) * 0.08f : 1.0f;
	const Vector2 meterCenter = { static_cast<float>(WinApp::kClientWidth) - 118.0f, 92.0f };

	if (hudLifeMeter_) {
		const uint32_t handle = Sprite::LoadTexture("ui/hud/life_meter_" + std::to_string(lifeValue) + ".png");
		hudLifeMeter_->SetTextureHandle(handle);
		hudLifeMeter_->SetVisible(visible);
		hudLifeMeter_->SetPosition(meterCenter);
		hudLifeMeter_->SetSize({ 138.0f * lifePulse, 138.0f * lifePulse });
		hudLifeMeter_->SetRotation(pulse * 0.03f);
		hudLifeMeter_->SetColor({ 1.0f, 1.0f, 1.0f, visible ? 0.96f : 0.0f });
		hudLifeMeter_->Update();
	}
	if (hudLifeMeterDigit_) {
		const uint32_t handle = Sprite::LoadTexture("number/big" + std::to_string(lifeValue) + ".png");
		hudLifeMeterDigit_->SetTextureHandle(handle);
		hudLifeMeterDigit_->SetVisible(visible);
		hudLifeMeterDigit_->SetPosition({ meterCenter.x, meterCenter.y + 3.0f });
		hudLifeMeterDigit_->SetSize({ 52.0f * lifePulse, 76.0f * lifePulse });
		hudLifeMeterDigit_->SetColor(lifeValue <= 1 ? Vector4{ 1.0f, 0.35f, 0.25f, 1.0f } : Vector4{ 1.0f, 0.88f, 0.20f, 1.0f });
		hudLifeMeterDigit_->Update();
	}
	if (hudLifeIcon_) {
		hudLifeIcon_->SetVisible(visible);
		hudLifeIcon_->SetPosition({ 38.0f, 100.0f });
		hudLifeIcon_->SetSize({ 50.0f * lifePulse, 50.0f * lifePulse });
		hudLifeIcon_->SetColor({ 1.0f, 1.0f, 1.0f, 0.96f });
		hudLifeIcon_->Update();
	}
	if (hudLifeXIcon_) {
		hudLifeXIcon_->SetVisible(visible);
		hudLifeXIcon_->SetPosition({ 88.0f, 100.0f });
		hudLifeXIcon_->SetSize({ 36.0f, 36.0f });
		hudLifeXIcon_->SetColor({ 1.0f, 0.96f, 0.62f, visible ? 0.96f : 0.0f });
		hudLifeXIcon_->Update();
	}

	const int lives = GameDataManager::GetInstance()->GetLives();
	SetGameplayHUDNumber(
		hudLifeDigits_,
		lives,
		{ 150.0f, 100.0f },
		40.0f,
		{ 1.0f, 0.95f, 0.56f, 1.0f },
		visible
	);

	if (hudCoinIcon_) {
		hudCoinIcon_->SetVisible(visible);
		hudCoinIcon_->SetPosition({ 38.0f, 154.0f });
		hudCoinIcon_->SetSize({ 48.0f, 48.0f });
		hudCoinIcon_->SetColor({ 1.0f, 1.0f, 1.0f, visible ? 0.96f : 0.0f });
		hudCoinIcon_->Update();
	}
	if (hudCoinXIcon_) {
		hudCoinXIcon_->SetVisible(visible);
		hudCoinXIcon_->SetPosition({ 88.0f, 154.0f });
		hudCoinXIcon_->SetSize({ 36.0f, 36.0f });
		hudCoinXIcon_->SetColor({ 1.0f, 0.90f, 0.42f, visible ? 0.96f : 0.0f });
		hudCoinXIcon_->Update();
	}

	const int coins = GameDataManager::GetInstance()->GetCoins();
	SetGameplayHUDNumber(
		hudCoinDigits_,
		coins,
		{ 150.0f, 154.0f },
		40.0f,
		{ 1.0f, 0.86f, 0.28f, 1.0f },
		visible
	);
}

void GamePlayScene::StartLifeLostPresentation(int beforeLives, int afterLives) {
	lifeLostPresentationActive_ = true;
	lifeLostPresentationFinished_ = false;
	lifeLostBlackHold_ = true;
	lifeLostNumberDropped_ = false;
	lifeLostPresentationTimer_ = 0.0f;
	lifeLostBeforeLives_ = std::clamp(beforeLives, 0, 99);
	lifeLostAfterLives_ = std::clamp(afterLives, 0, 99);
	lifeLostIrisCenter_ = { 0.5f, 0.5f };
	if (player_) {
		Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
		if (cam) {
			Vector3 worldPos = player_->GetWorldPosition();
			worldPos.y += 1.0f;
			Vector3 ndc = Math::Transform(worldPos, cam->GetViewProjectionMatrix());
			lifeLostIrisCenter_ = {
				std::clamp((ndc.x + 1.0f) * 0.5f, 0.08f, 0.92f),
				std::clamp((1.0f - ndc.y) * 0.5f, 0.08f, 0.92f)
			};
		}
	}
}

void GamePlayScene::HideLifeLostPresentationOverlay() {
	lifeLostPresentationActive_ = false;
	lifeLostPresentationFinished_ = true;
	lifeLostBlackHold_ = false;
	if (lifeLostIcon_) lifeLostIcon_->SetVisible(false);
	if (lifeLostXIcon_) lifeLostXIcon_->SetVisible(false);
	if (lifeLostBackdrop_) lifeLostBackdrop_->SetVisible(false);
	for (auto& digit : lifeLostDigits_) {
		if (digit) digit->SetVisible(false);
	}
}

void GamePlayScene::UpdateLifeLostPresentation(float deltaTime) {
	if (!lifeLostPresentationActive_) {
		if (lifeLostBlackHold_) {
			const float screenW = static_cast<float>(WinApp::kClientWidth);
			const float screenH = static_cast<float>(WinApp::kClientHeight);
			if (lifeLostBackdrop_) {
				lifeLostBackdrop_->SetVisible(true);
				lifeLostBackdrop_->SetPosition({ screenW * 0.5f, screenH * 0.5f });
				lifeLostBackdrop_->SetSize({ screenW, screenH });
				lifeLostBackdrop_->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });
				lifeLostBackdrop_->Update();
			}
			if (lifeLostIcon_) lifeLostIcon_->SetVisible(false);
			if (lifeLostXIcon_) lifeLostXIcon_->SetVisible(false);
			for (auto& digit : lifeLostDigits_) {
				if (digit) digit->SetVisible(false);
			}
			return;
		}
		if (lifeLostIcon_) lifeLostIcon_->SetVisible(false);
		if (lifeLostXIcon_) lifeLostXIcon_->SetVisible(false);
		if (lifeLostBackdrop_) lifeLostBackdrop_->SetVisible(false);
		for (auto& digit : lifeLostDigits_) {
			if (digit) digit->SetVisible(false);
		}
		return;
	}

	lifeLostPresentationTimer_ += deltaTime;
	const float t = lifeLostPresentationTimer_;
	constexpr float kNumberDropTime = 1.15f;
	constexpr float kFadeOutStartTime = 2.35f;
	constexpr float kFadeOutDuration = 0.35f;
	constexpr float kEndTime = 2.85f;
	if (t >= kEndTime) {
		lifeLostPresentationActive_ = false;
		lifeLostPresentationFinished_ = true;
		lifeLostBlackHold_ = true;
		if (lifeLostIcon_) lifeLostIcon_->SetVisible(false);
		if (lifeLostXIcon_) lifeLostXIcon_->SetVisible(false);
		for (auto& digit : lifeLostDigits_) {
			if (digit) digit->SetVisible(false);
		}
		return;
	}

	if (t >= kNumberDropTime) {
		lifeLostNumberDropped_ = true;
	}

	float alpha = std::clamp(t / 0.25f, 0.0f, 1.0f);
	if (t > kFadeOutStartTime) {
		alpha = std::clamp(1.0f - (t - kFadeOutStartTime) / kFadeOutDuration, 0.0f, 1.0f);
	}

	const int displayLives = lifeLostNumberDropped_ ? lifeLostAfterLives_ : lifeLostBeforeLives_;
	const float screenW = static_cast<float>(WinApp::kClientWidth);
	const float screenH = static_cast<float>(WinApp::kClientHeight);
	const float centerX = screenW * 0.5f;
	const float centerY = screenH * 0.5f;
	const float settlePulse = lifeLostNumberDropped_
		? 1.0f + std::max(0.0f, 1.0f - (t - kNumberDropTime) / 0.42f) * 0.22f
		: 1.0f + std::sin(t * 7.0f) * 0.035f;

	if (lifeLostBackdrop_) {
		lifeLostBackdrop_->SetVisible(true);
		lifeLostBackdrop_->SetPosition({ centerX, centerY });
		lifeLostBackdrop_->SetSize({ screenW, screenH });
		lifeLostBackdrop_->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });
		lifeLostBackdrop_->Update();
	}

	if (lifeLostIcon_) {
		lifeLostIcon_->SetVisible(true);
		lifeLostIcon_->SetPosition({ centerX - 82.0f, centerY });
		lifeLostIcon_->SetSize({ 96.0f * settlePulse, 96.0f * settlePulse });
		lifeLostIcon_->SetColor({ 1.0f, 1.0f, 1.0f, alpha });
		lifeLostIcon_->Update();
	}
	if (lifeLostXIcon_) {
		lifeLostXIcon_->SetVisible(true);
		lifeLostXIcon_->SetPosition({ centerX + 6.0f, centerY + 3.0f });
		lifeLostXIcon_->SetSize({ 64.0f, 64.0f });
		lifeLostXIcon_->SetColor({ 1.0f, 0.95f, 0.58f, alpha });
		lifeLostXIcon_->Update();
	}

	const int tens = displayLives / 10;
	const int ones = displayLives % 10;
	const bool showTens = displayLives >= 10;
	const float digitHeight = 86.0f * settlePulse;
	const float digitWidth = digitHeight * 0.68f;
	const float spacing = digitWidth * 0.82f;
	const float rightX = centerX + 132.0f;
	const float totalWidth = showTens ? spacing + digitWidth : digitWidth;
	const float startX = rightX - totalWidth + digitWidth * 0.5f;
	const std::array<int, 2> values = { tens, ones };

	for (int i = 0; i < 2; ++i) {
		Sprite* digit = lifeLostDigits_[i].get();
		if (!digit) continue;

		const bool digitVisible = showTens || i == 1;
		digit->SetVisible(digitVisible);
		if (!digitVisible) continue;

		const int sourceIndex = showTens ? i : 1;
		const uint32_t handle = Sprite::LoadTexture("number/big" + std::to_string(values[sourceIndex]) + ".png");
		digit->SetTextureHandle(handle);
		digit->SetPosition({ startX + (sourceIndex - (2 - (showTens ? 2 : 1))) * spacing, centerY + 2.0f });
		digit->SetSize({ digitWidth, digitHeight });
		digit->SetColor(lifeLostNumberDropped_
			? Vector4{ 1.0f, 0.65f, 0.22f, alpha }
			: Vector4{ 1.0f, 0.92f, 0.38f, alpha });
		digit->Update();
	}
}

void GamePlayScene::DrawGameplayHUD() {
	if (hudLifeIcon_) hudLifeIcon_->Draw();
	if (hudLifeXIcon_) hudLifeXIcon_->Draw();
	for (auto& digit : hudLifeDigits_) {
		if (digit) digit->Draw();
	}
	if (hudCoinIcon_) hudCoinIcon_->Draw();
	if (hudCoinXIcon_) hudCoinXIcon_->Draw();
	for (auto& digit : hudCoinDigits_) {
		if (digit) digit->Draw();
	}
	if (hudLifeMeter_) hudLifeMeter_->Draw();
	if (hudLifeMeterDigit_) hudLifeMeterDigit_->Draw();
	DrawLifeLostPresentation();
}

void GamePlayScene::DrawLifeLostPresentation() {
	if (!lifeLostPresentationActive_ && !lifeLostBlackHold_) return;
	if (lifeLostBackdrop_) lifeLostBackdrop_->Draw();
	if (!lifeLostPresentationActive_) return;
	if (lifeLostIcon_) lifeLostIcon_->Draw();
	if (lifeLostXIcon_) lifeLostXIcon_->Draw();
	for (auto& digit : lifeLostDigits_) {
		if (digit) digit->Draw();
	}
}



void GamePlayScene::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text(ICON_FA_INFO_CIRCLE " Scene: GamePlay");
    ImGui::Separator();

    if (ImGui::CollapsingHeader(ICON_FA_MAP " Stage Selection", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& stages = StageManager::GetInstance()->GetStages();
        int currentIndex = StageManager::GetInstance()->GetCurrentStageIndex();

        std::vector<const char*> stageNames;
        for (const auto& s : stages) stageNames.push_back(s.name.c_str());

        if (ImGui::Combo("Select Stage", &currentIndex, stageNames.data(), (int)stageNames.size())) {
            StageManager::GetInstance()->SetCurrentStage(currentIndex);
        }

        if (ImGui::Button(ICON_FA_SYNC " Reload Scene with Selected Stage", ImVec2(-1, 30))) {
            SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_TROPHY " Stage Status", ImGuiTreeNodeFlags_DefaultOpen)) {
        int currentStage = StageManager::GetInstance()->GetCurrentStageIndex();
        bool isCleared = GameDataManager::GetInstance()->IsStageCleared(currentStage);

        ImGui::Text("Stage ID: %d", currentStage);
        ImGui::SameLine();
        if (isCleared) ImGui::TextColored(ImVec4(0, 1, 0, 1), "[ CLEARED ]");
        else ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "[ NOT CLEARED ]");

        ImGui::Separator();
        ImGui::Text("Star Coins (Session):");
        for (int i = 0; i < 3; i++) {
            ImGui::SameLine();
            if (sessionStarCoins_[i]) {
                ImGui::TextColored(ImVec4(1, 0.9f, 0, 1), ICON_FA_STAR);
            }
            else {
                ImGui::TextDisabled(ICON_FA_STAR);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Star Coin %d", i);
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_USER " Player Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (player_) {
            Vector3 pos = player_->GetTranslate();
            ImGui::Text("Position: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
            
            float hp = player_->GetHp();
            float maxHp = player_->GetMaxHp();
            ImGui::ProgressBar(hp / maxHp, ImVec2(-1, 0), "HP");

            // --- Debug HP Control ---
            if (ImGui::Button(ICON_FA_AMBULANCE " HPを1にする (Debug)")) {
                if (player_->param_.has_value()) {
                    player_->param_->hp = 1.0f;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_SKULL " HPを0にする (Debug)")) {
                if (player_->param_.has_value()) {
                    player_->param_->hp = 0.0f;
                }
            }

            ImGui::Separator();
            int lives = GameDataManager::GetInstance()->GetLives();
            int coins = GameDataManager::GetInstance()->GetCoins();
            ImGui::Text(ICON_FA_HEART " Remaining Lives: %d", lives);
            ImGui::Text(ICON_FA_COINS " Coins: %d / 100", coins);
            if (ImGui::Button("Reset Lives to 3")) {
                GameDataManager::GetInstance()->ResetLives();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Coins")) {
                GameDataManager::GetInstance()->ResetCoins();
            }
        }
        else {
            ImGui::TextDisabled("Player not found");
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_GHOST " Scene Events", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button(ICON_FA_PLAY " Start Bridge Drop Movie", ImVec2(-1, 30))) {
            StartBridgeDropMovie();
        }
    }
    
    ImGui::Separator();
    ImGui::TextDisabled("※この項目は GamePlayScene::DrawImGui() で編集可能です");
#endif
}

void GamePlayScene::StartBridgeDropMovie() {

}
bool GamePlayScene::IsVisible(Object3d* obj) {
    if (!obj) return false;
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) return true;

    // モデルデータに基づいた正確なワールド空間AABBを取得
    AABB worldAabb = obj->GetModelWorldAABB();

    return Math::IntersectFrustumAABB(camera->GetFrustum(), worldAabb.min, worldAabb.max);
}
