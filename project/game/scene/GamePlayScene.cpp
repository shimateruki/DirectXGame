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
#include"DebugConsole.h"
#include <cassert>

#ifdef _DEBUG
#include "ParticleEditor.h"
#endif

// --- JSON (保存機能) ---
#include <fstream>
#include <string>
#include "json.hpp" 


void GamePlayScene::Initialize() {
	using json = nlohmann::json;

	// --- 基盤クラスのポインタを保持 ---
	dxCommon_ = DirectXCommon::GetInstance();
	inputManager_ = InputManager::GetInstance();
	audioPlayer_ = AudioPlayer::GetInstance();

	// --- 各種初期化 ---
	bgmHandle_ = audioPlayer_->LoadSoundFile("resouces/bgm/Alarm02.mp3");
	CameraManager::GetInstance()->Initialize();
	CameraManager::GetInstance()->SetInputManager(inputManager_);
	spriteCommon_ = std::make_unique<SpriteCommon>();
	spriteCommon_->Initialize(dxCommon_);
	object3dCommon_ = std::make_unique<Object3dCommon>();
	object3dCommon_->Initialize(dxCommon_);
	particleCommon_ = std::make_unique<ParticleCommon>();
	particleCommon_->Initialize(dxCommon_);
	particleSystem_ = std::make_unique<ParticleSystem>();
	particleSystem_->Initialize(particleCommon_.get(), "resouces/sprite/white.png");

	// --- オブジェクトの生成 ---
	auto plane = std::make_unique<Object3d>();
	plane->Initialize(object3dCommon_.get());
	plane->SetModel("plane");
	plane->SetName("Plane");
	plane->SetStatic(true);
	objects_.emplace_back(std::move(plane));

	auto playerObj = std::make_unique<Player>();
	playerObj->Initialize(object3dCommon_.get(), inputManager_);
	playerObj->SetModel("block");
	playerObj->SetTranslate({ 2.0f, 0.0f, 0.0f });
	playerObj->SetName("Player");
	playerObj->SetStatic(false);
	player_ = playerObj.get();
	objects_.emplace_back(std::move(playerObj));

	auto enemy = std::make_unique<Object3d>();
	enemy->Initialize(object3dCommon_.get());
	enemy->SetModel("bunny");
	enemy->SetTranslate({ 2.0f, 0.0f, 0.0f });
	enemy->SetName("Enemy");
	enemy->SetStatic(true);
	objects_.emplace_back(std::move(enemy));

	for (int i = 0; i < 2000; ++i) {
		auto block = std::make_unique<Object3d>();
		block->Initialize(object3dCommon_.get());
		block->SetModel("block");
		block->SetTranslate({ -4.0f, 0.0f, (float)i * 1.8f - 4.0f });
		block->SetName("Block_" + std::to_string(i));
		block->SetStatic(true);
		objects_.emplace_back(std::move(block));
	}

	// --- カメラの設定 ---
	Camera* camera = CameraManager::GetInstance()->GetMainCamera();
	camera->SetTarget(&objects_[1]->GetTransform()->translate);
#ifndef _DEBUG 
	camera->SetFollowMode(Camera::FollowMode::kAimable);
	camera->ConfigAimable(10.0f, 2.0f, 20.0f);
#endif

	// --- 衝突判定の設定 ---
	CollisionManager::GetInstance()->ClearObjects();
	objects_[1]->SetCollisionAttribute(kPlayer);
	objects_[1]->SetCollisionMask(~kPlayer);
	CollisionManager::GetInstance()->AddObject(objects_[1].get());

	objects_[2]->SetCollisionAttribute(kEnemy);
	objects_[2]->SetCollisionMask(~kEnemy);
	objects_[2]->SetColliderType(ColliderType::kSphere);
	objects_[2]->SetCollisionRadius(1.0f);
	CollisionManager::GetInstance()->AddObject(objects_[2].get());
	objects_[0]->SetCollisionAttribute(kGround);
	objects_[0]->SetCollisionMask(~kGround);
	objects_[0]->SetColliderType(ColliderType::kAABB);
	objects_[0]->SetCollisionSize({ 10.0f, 0.1f, 10.0f });
	CollisionManager::GetInstance()->AddObject(objects_[0].get());


	for (size_t i = 3; i < objects_.size(); ++i) {
		objects_[i]->SetCollisionAttribute(kGround);
		objects_[i]->SetCollisionMask(~kGround);
		objects_[i]->SetColliderType(ColliderType::kAABB);
		objects_[i]->SetCollisionSize({ 1.0f, 1.0f, 1.0f });
		CollisionManager::GetInstance()->AddObject(objects_[i].get());
	}
	// --- スプライトの生成 ---
	uint32_t monsterBallHandle = Sprite::LoadTexture("monsterBall.png");
	auto monsterBallSprite = std::make_unique<Sprite>();
	monsterBallSprite->Initialize(spriteCommon_.get(), monsterBallHandle);
	monsterBallSprite->SetPosition({ 200.0f, 360.0f });
	monsterBallSprite->SetSize({ 100.0f, 100.0f });
	monsterBallSprite->SetName("MonsterBall");
	sprites_.push_back(std::move(monsterBallSprite));

	uint32_t flameHandle = Sprite::LoadTexture("sample.png");
	auto flameSprite = std::make_unique<Sprite>();
	flameSprite->Initialize(spriteCommon_.get(), flameHandle);
	flameSprite->SetAnimation(4, 0.15f, true);
	flameSprite->Play();
	flameSprite->SetPosition({ 640.0f, 360.0f });
	flameSprite->SetSize({ 64.0f,64.0f });
	sprites_.push_back(std::move(flameSprite));


	// --- レイアウト読み込み ---
	LoadObjectLayout("scene_layout.json");
	LoadSpriteLayout("sprite_layout.json");



	// --- イベント購読 ---
	EventManager::GetInstance()->Subscribe(
		[this](const PlayerHitEvent& event) {
			this->OnPlayerHit(event);
		}
	);

	//コマンドリストが安全に閉じるためのやつないとバグる
	dxCommon_->FlushCommandQueue(false);
}

void GamePlayScene::Finalize() {


	CollisionManager::GetInstance()->ClearObjects();

	particleSystem_.reset();
	particleCommon_.reset();
	sprites_.clear();
	spriteCommon_.reset();
	objects_.clear();
	object3dCommon_.reset();
}

void GamePlayScene::Update(float deltaTime) {



#ifndef _DEBUG
	Camera* camera = CameraManager::GetInstance()->GetMainCamera();
	Camera::FollowMode currentMode = camera->GetFollowMode();

	if (currentMode == Camera::FollowMode::kAimable) {
		float wheelDelta = inputManager_->GetMouseWheelDelta();
		if (wheelDelta != 0.0f) {
			camera->AddZoom(wheelDelta);
		}
	}
	if (currentMode == Camera::FollowMode::kAimable || currentMode == Camera::FollowMode::kFirstPerson) {
		if (inputManager_->IsMouseButtonPressed(1)) {
			Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
			if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
				camera->AddRotation(mouseDelta);
			}
		}
	}
#endif



	// --- 常に実行される更新 ---
	CameraManager::GetInstance()->Update();

	particleSystem_->Update(deltaTime);

	// --- 1. ゲームロジック (オブジェクト・スプライト) 更新 ---
	for (auto& obj : objects_) {
		obj->Update();
	}

	if (player_) {
		player_->UpdateLocalMatrix();
	}
	for (const auto& object : objects_) {
		object->UpdateLocalMatrix();
	}

	if (player_) {
		player_->UpdateWorldMatrix();
	}
	for (const auto& object : objects_) {
		object->UpdateWorldMatrix();
	}

	for (auto& sprite : sprites_) {
		sprite->Update();
	}

	if (inputManager_->IsKeyTriggered(DIK_P)) {
		particleSystem_->SpawnParticles(
			{ 0.0f, 1.0f, 0.0f }, 100,
			5.0f, nullptr, 1.0f,
			{ 1.0f, 0.5f, 0.0f, 1.0f }, { 1.0f, 1.0f, 0.0f, 0.0f },
			0.5f, 3.0f,
			1.0f, 0.1f
		);
	}

	// --- 2. 物理 (衝突判定) 更新 ---
	CollisionManager::GetInstance()->Update();

	ProcessRemovals();
}

void GamePlayScene::Draw() {

	// --- Releaseビルド時の一人称視点判定 ---
	bool isFirstPerson = false;
#ifndef _DEBUG
	Camera* camera = CameraManager::GetInstance()->GetMainCamera();
	if (camera->GetTarget() && camera->GetFollowMode() == Camera::FollowMode::kFirstPerson) {
		isFirstPerson = true;
	}
#endif

	// --- 3Dオブジェクト描画 ---
	object3dCommon_->SetGraphicsCommand();
	for (size_t i = 0; i < objects_.size(); ++i) {
		if (isFirstPerson && i == 1) {
			continue;
		}
		objects_[i]->Draw();
	}

	// --- スプライト描画 ---
	spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
	for (auto& sprite : sprites_) {
		sprite->Draw();
	}


	particleSystem_->Draw();
}

void GamePlayScene::OnPlayerHit(const PlayerHitEvent& event) {
	uint32_t attribute = event.hitObject->GetCollisionAttribute();

	// ★ ヘルパー関数で法線を「面」に変換
	CollisionFace face = GetCollisionFace(event.normal);

	// --- 敵に当たった場合の処理 ---
	if (attribute & kEnemy) {

		switch (face) {
		case CollisionFace::kTop: // 敵を上から踏んだ
			DebugConsole::GetInstance()->AddLog("ENEMY HIT! (face: Top)");
			// (ここに踏んだ時の処理を実装予定)
			break;

		case CollisionFace::kLeft:
			DebugConsole::GetInstance()->AddLog("ENEMY HIT! (face: Left)");
			// (ここに横から当たった時の処理を実装予定)
			break;
		case CollisionFace::kRight:
			DebugConsole::GetInstance()->AddLog("ENEMY HIT! (face: Right)");
			// (ここに横から当たった時の処理を実装予定)
			break;
		case CollisionFace::kFront:
			DebugConsole::GetInstance()->AddLog("ENEMY HIT! (face: Front)");
			// (ここに横から当たった時の処理を実装予定)
			break;
		case CollisionFace::kBack:
			DebugConsole::GetInstance()->AddLog("ENEMY HIT! (face: Back)");
			// (ここに横から当たった時の処理を実装予定)
			break;
		case CollisionFace::kBottom: // 敵の下から頭突きした
			DebugConsole::GetInstance()->AddLog("ENEMY HIT! (face: Bottom)");
			// (ここに下から当たった時の処理を実装予定)
			break;
		case CollisionFace::kOther:  // 斜めに当たった
			DebugConsole::GetInstance()->AddLog("ENEMY HIT! (face: Other)");
			// (ここに斜めに当たった時の処理を実装予定)
			break;
		default:
			DebugConsole::GetInstance()->AddLog("ENEMY HIT! (face: Default/Unknown)");
			break;
		}
	}
}



#pragma region Editor Functions
void GamePlayScene::LoadObjectLayout(const std::string& filename) {
	using json = nlohmann::json;
	std::ifstream file(filename);

	if (!file.is_open()) {
		std::string warnMsg = "Warning: Could not open " + filename + " for Object layout.\n";
		OutputDebugStringA(warnMsg.c_str());
		return;
	}

	json sceneData;
	try {
		sceneData = json::parse(file);
		if (sceneData.contains("objects") && sceneData["objects"].is_array()) {
			for (const auto& objData : sceneData["objects"]) {
				if (!objData.contains("name") || !objData["name"].is_string()) continue;
				std::string name = objData["name"].get<std::string>();

				Object3d* targetObject = nullptr;
				for (auto& obj : objects_) {
					if (obj && !obj->GetName().empty() && obj->GetName() == name) {
						targetObject = obj.get();
						break;
					}
				}

				if (targetObject) {
					Object3d::Transform* transform = targetObject->GetTransform();

					if (objData.contains("position") && objData["position"].is_array() && objData["position"].size() == 3) {
						transform->translate.x = objData["position"][0].get<float>();
						transform->translate.y = objData["position"][1].get<float>();
						transform->translate.z = objData["position"][2].get<float>();
					}
					if (objData.contains("rotation") && objData["rotation"].is_array() && objData["rotation"].size() == 3) {
						transform->rotate.x = objData["rotation"][0].get<float>();
						transform->rotate.y = objData["rotation"][1].get<float>();
						transform->rotate.z = objData["rotation"][2].get<float>();
					}
					if (objData.contains("scale") && objData["scale"].is_array() && objData["scale"].size() == 3) {
						transform->scale.x = objData["scale"][0].get<float>();
						transform->scale.y = objData["scale"][1].get<float>();
						transform->scale.z = objData["scale"][2].get<float>();
					}
					targetObject->Update();
				}
			}
		}
	}
	catch (json::parse_error& e) {
		OutputDebugStringA(("Failed to parse " + filename + "\n").c_str());
		OutputDebugStringA(e.what());
		OutputDebugStringA("\n");
	}

	file.close();
}

void GamePlayScene::LoadSpriteLayout(const std::string& filename) {
	using json = nlohmann::json;
	std::ifstream file(filename);

	if (!file.is_open()) {
		std::string warnMsg = "Warning: Could not open " + filename + "\n";
		OutputDebugStringA(warnMsg.c_str());
		return;
	}

	json layoutData;
	try {
		layoutData = json::parse(file);

		if (layoutData.contains("sprites") && layoutData["sprites"].is_array()) {
			for (const auto& spriteData : layoutData["sprites"]) {

				if (!spriteData.contains("name") || !spriteData["name"].is_string()) {
					continue;
				}
				std::string name = spriteData["name"];

				Sprite* targetSprite = nullptr;
				for (auto& sprite : sprites_) {
					if (sprite && !sprite->GetName().empty() && sprite->GetName() == name) {
						targetSprite = sprite.get();
						break;
					}
				}

				if (targetSprite) {
					if (spriteData.contains("position") && spriteData["position"].is_array() && spriteData["position"].size() == 2) {
						targetSprite->SetPosition({
							spriteData["position"][0].get<float>(),
							spriteData["position"][1].get<float>()
							});
					}
					if (spriteData.contains("size") && spriteData["size"].is_array() && spriteData["size"].size() == 2) {
						targetSprite->SetSize({
							spriteData["size"][0].get<float>(),
							spriteData["size"][1].get<float>()
							});
					}
					if (spriteData.contains("anchor") && spriteData["anchor"].is_array() && spriteData["anchor"].size() == 2) {
						targetSprite->SetAnchorPoint({
							spriteData["anchor"][0].get<float>(),
							spriteData["anchor"][1].get<float>()
							});
					}
					if (spriteData.contains("color") && spriteData["color"].is_array() && spriteData["color"].size() == 4) {
						targetSprite->SetColor({
							spriteData["color"][0].get<float>(),
							spriteData["color"][1].get<float>(),
							spriteData["color"][2].get<float>(),
							spriteData["color"][3].get<float>()
							});
					}
					targetSprite->Update();
				}
			}
		}

	}
	catch (json::parse_error& e) {
		OutputDebugStringA("Failed to parse sprite_layout.json\n");
		OutputDebugStringA(e.what());
		OutputDebugStringA("\n");
	}

	file.close();
}


void GamePlayScene::AddObject(std::unique_ptr<Object3d> object) {
	if (object == nullptr) {
		return;
	}
	CollisionManager::GetInstance()->AddObject(object.get());
	objects_.emplace_back(std::move(object));
}



void GamePlayScene::RequestRemoveObject(Object3d* object) {
	if (object) {
		removalList_.push_back(object);
	}
}

void GamePlayScene::ProcessRemovals() {
	if (removalList_.empty()) {
		return;
	}

	CollisionManager* colManager = CollisionManager::GetInstance();

	// 1. CollisionManager から削除
	for (Object3d* obj : removalList_) {
		colManager->RemoveObject(obj);
	}

	// 2. objects_ (unique_ptr) リストから削除
	auto it = std::remove_if(objects_.begin(), objects_.end(),
		[this](const std::unique_ptr<Object3d>& p) {
			for (Object3d* removalObj : removalList_) {
				if (p.get() == removalObj) {
					return true; // 削除対象
				}
			}
			return false;
		}
	);
	objects_.erase(it, objects_.end());

	// 3. 予約リストをクリア
	removalList_.clear();
}
#pragma endregion
