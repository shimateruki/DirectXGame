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
#include "BulletManager.h"
#include "MoveStrategy3D.h" 
#include "MoveStrategy2D.h"

#ifdef _DEBUG
#include "ParticleEditor.h"
#endif

// --- JSON (保存機能) ---
#include <fstream>
#include <string>
#include "json.hpp" 

// 乱数生成器
static std::random_device rd_scene;
static std::mt19937 gen_scene(rd_scene());
static std::uniform_real_distribution<float> dis_color(0.0f, 1.0f); // 0.0～1.0 の色用
static std::uniform_real_distribution<float> dis_speed(3.0f, 8.0f); // 3.0～8.0 の速度用

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
	particleSystem_->Initialize(particleCommon_.get(), "resouces/sprite/particle.png");

	// --- オブジェクトの生成 ---

	auto playerObj = std::make_unique<Player>();
	playerObj->Initialize(object3dCommon_.get(), inputManager_, particleSystem_.get());
	playerObj->SetModel("block");
	playerObj->SetTranslate({ 2.0f, 0.0f, 0.0f });
	playerObj->SetName("Player");
	playerObj->SetStatic(false);
	player_ = playerObj.get();
	playerObj->SetMoveStrategy(std::make_unique<MoveStrategy3D>());
	objects_.emplace_back(std::move(playerObj));

	auto enemy = std::make_unique<Object3d>();
	enemy->Initialize(object3dCommon_.get());
	enemy->SetModel("bunny");
	enemy->SetTranslate({ 2.0f, 0.0f, 0.0f });
	enemy->SetName("Enemy");
	enemy->SetStatic(true);
	objects_.emplace_back(std::move(enemy));

	//平面生成
	auto heimen = std::make_unique<Object3d>();
	heimen->Initialize(object3dCommon_.get());
	heimen->SetModel("heimen");
	heimen->SetTranslate({ 2.0f, -1.0f, 0.0f });
	heimen->SetName("Enemy");
	heimen->SetStatic(true);
	objects_.emplace_back(std::move(heimen));

	auto fence = std::make_unique<Object3d>();
	fence->Initialize(object3dCommon_.get());
	fence->SetModel("fence");
	fence->SetTranslate({ 2.0f, 2.0f, 0.0f });
	fence->SetName("Enemy");
	fence->SetStatic(true);
	objects_.emplace_back(std::move(fence));

	const float blockSize = 2.0f; // ブロックの1辺のサイズ 
	const int fieldWidth = 30;  // X方向 (幅) の床の数
	const int fieldDepth = 30;  // Z方向 (奥行) の床の数
	const int wallHeight = 5;   // Y方向 (高さ) の壁の数

	// フィールドの中心が (0, 0, 0) 付近になるようオフセットを計算
	const float offsetX = (fieldWidth * blockSize) / 2.0f;
	const float offsetZ = (fieldDepth * blockSize) / 2.0f;

	// 衝突判定用のハーフサイズ
	const Vector3 blockHalfSize = { blockSize / 2.0f, blockSize / 2.0f, blockSize / 2.0f };


	// --- 2. 壁 (Walls) の生成 (Y= 0 ～ wallHeight) ---
	for (int y = 0; y < wallHeight; ++y) {
		float posY = y * blockSize; // 0.0, 2.0, 4.0 ...

		// (A) X軸に沿った壁 (奥: Z+ と 手前: Z-)
		for (int x = 0; x < fieldWidth; ++x) {
			float posX = (x * blockSize) - offsetX + (blockSize / 2.0f);

			// 奥の壁 (Z+)
			float posZ_Back = (fieldDepth * blockSize) - offsetZ - (blockSize / 2.0f);
			objects_.push_back(CreateStaticBlock(
				{ posX, posY, posZ_Back },
				"Wall_Back_" + std::to_string(x) + "_" + std::to_string(y),
				blockHalfSize
			));

			// 手前の壁 (Z-)
			float posZ_Front = -offsetZ + (blockSize / 2.0f);
			objects_.push_back(CreateStaticBlock(
				{ posX, posY, posZ_Front },
				"Wall_Front_" + std::to_string(x) + "_" + std::to_string(y),
				blockHalfSize
			));
		}

		// (B) Z軸に沿った壁 
		for (int z = 1; z < fieldDepth - 1; ++z) {
			float posZ = (z * blockSize) - offsetZ + (blockSize / 2.0f);

			// 右の壁 (X+)
			float posX_Right = (fieldWidth * blockSize) - offsetX - (blockSize / 2.0f);
			objects_.push_back(CreateStaticBlock(
				{ posX_Right, posY, posZ },
				"Wall_Right_" + std::to_string(z) + "_" + std::to_string(y),
				blockHalfSize
			));

			// 左の壁 (X-)
			float posX_Left = -offsetX + (blockSize / 2.0f);
			objects_.push_back(CreateStaticBlock(
				{ posX_Left, posY, posZ },
				"Wall_Left_" + std::to_string(z) + "_" + std::to_string(y),
				blockHalfSize
			));
		}
	}

	// --- カメラの設定 ---
	Camera* camera = CameraManager::GetInstance()->GetMainCamera();

#ifndef _DEBUG 
	camera->SetFollowTarget(player_);
	camera->SetFollowMode(Camera::FollowMode::kAimable);//カメラのモード設定
	camera->ConfigAimable(15.0f, 5.0f, 25.0f);//距離
	camera->SetLockOnOffset({ 0.0f, 4.0f, -12.0f });//ロックオン時のカメラ
#endif

	// --- 衝突判定の設定 ---
	CollisionManager::GetInstance()->ClearObjects();
	objects_[0]->SetCollisionAttribute(kPlayer);
	objects_[0]->SetCollisionMask(~kPlayer);
	CollisionManager::GetInstance()->AddObject(objects_[0].get());

	objects_[1]->SetCollisionAttribute(kEnemy);
	objects_[1]->SetCollisionMask(~kEnemy);
	objects_[1]->SetColliderType(ColliderType::kAABB);
	objects_[1]->SetCollisionSize({ 1.0f, 1.0f, 1.0f });
	CollisionManager::GetInstance()->AddObject(objects_[1].get());

	objects_[2]->SetCollisionAttribute(kGround);
	objects_[2]->SetCollisionMask(~kGround);
	objects_[2]->SetColliderType(ColliderType::kAABB);
	objects_[2]->SetCollisionSize({ 50.0f, 0.5f, 50.0f });
	CollisionManager::GetInstance()->AddObject(objects_[2].get());


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
	//弾の初期化
	BulletManager::GetInstance()->Initialize(
		object3dCommon_.get(),
		CollisionManager::GetInstance()
	);

	// --- レイアウト読み込み ---
	LoadObjectLayout("scene_layout.json");
	LoadSpriteLayout("sprite_layout.json");



	// --- イベント購読 ---
	EventManager::GetInstance()->Subscribe(
		[this](const PlayerHitEvent& event) {
			this->OnPlayerHit(event);
		}
	);

	EventManager::GetInstance()->Subscribe(
		[this](const BulletHitEvent& event) {
			this->OnBulletHit(event);
		}
	);

	//コマンドリストが安全に閉じるためのやつないとバグる
	dxCommon_->FlushCommandQueue(false);
}

void GamePlayScene::Finalize() {


	CollisionManager::GetInstance()->ClearObjects();
	BulletManager::GetInstance()->Finalize();
	particleSystem_.reset();
	particleCommon_.reset();
	sprites_.clear();
	spriteCommon_.reset();
	objects_.clear();
	object3dCommon_.reset();
}


void GamePlayScene::Update(float deltaTime) {

	static Math math;

#ifndef _DEBUG
	Camera* camera = CameraManager::GetInstance()->GetMainCamera();
	Camera::FollowMode currentMode = camera->GetFollowMode();

	if (currentMode == Camera::FollowMode::kAimable) {
		float wheelDelta = inputManager_->GetMouseWheelDelta();
		if (wheelDelta != 0.0f) {
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


	// Player の更新 先にロックオン状態を更新

	UpdateLockOn();

	// --- 1. ゲームロジック (オブジェクト・スプライト) 更新 ---
	for (auto& obj : objects_) {
		obj->Update(deltaTime);
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
		// 1. この爆発の「色」をランダムに1つ決める
		float r = dis_color(gen_scene);
		float g = dis_color(gen_scene);
		float b = dis_color(gen_scene);
		// (少し明るく補正)
		float total = r + g + b;
		if (total < 1.0f) {
			r += (1.0f - total) / 2.0f;
			g += (1.0f - total) / 2.0f;
		}

		Vector4 randomColor = { r, g, b, 1.0f };
		Vector4 endColor = { r * 0.5f, g * 0.5f, b * 0.5f, 0.0f }; // 少し暗くして消える

		// 2. この爆発の「速度」もランダムに決める
		float randomSpeed = dis_speed(gen_scene);
 
        particleSystem_->SpawnParticles(  
           {1.0f, 2.0f, -1.0f }, 100,  
           randomSpeed,         
           nullptr,             
           0.0f,              
           randomColor,         
           endColor,            
           1.0f, 2.0f,            
           0.3f,                
           0.05f                                 
		);
	}


//弾の発射処理
	if (inputManager_->IsMouseButtonTriggered(0)) { 

		Camera* camera = CameraManager::GetInstance()->GetMainCamera();
		Vector3 startPos = player_->GetWorldPosition();
		startPos.y += 1.0f; // 少し上から

		Vector3 direction;

		// (1) ロックオン状態に応じて発射方向を切り替え
		if (isLockingOn_ && lockOnTarget_) {
			// (A) ロックオン中： 敵の方向を向く
			Vector3 enemyPos = lockOnTarget_->GetWorldPosition();
			direction = enemyPos - startPos;
		} else {
			// (B) 通常時： カメラの視点方向
			direction = camera->GetTargetPoint() - camera->GetEye();
		}

		// (2) NaNチェック 
		if (math.Length(direction) < 0.001f) {
			direction = { 0.0f, 0.0f, 1.0f }; // デフォルト前方
		} else {
			direction = math.Normalize(direction);
		}


		const float bulletSpeed = 120.0f;
		Vector3 velocity = direction * bulletSpeed;

		//弾の呼び出し
		BulletManager::GetInstance()->Fire(
			startPos, velocity,
			kAttributePlayerBullet,
			kEnemy | kGround, 
			"block", 1.0f, 120
		);
	}

	//弾の更新
	BulletManager::GetInstance()->Update(deltaTime);

	// --- 2. 物理 (衝突判定) 更新 ---
	CollisionManager::GetInstance()->Update();
	//オブジェクト削除関数
	ProcessRemovals();
}

void GamePlayScene::Draw() {

	// --- Releaseビルド時の一人称視点判定 ---
	bool isFirstPerson = false;
#ifndef _DEBUG
	Camera* camera = CameraManager::GetInstance()->GetMainCamera();

	if (camera->GetFollowTarget() && camera->GetFollowMode() == Camera::FollowMode::kFirstPerson) {
		isFirstPerson = true;
	}
#endif

	// --- 3Dオブジェクト描画 ---
	object3dCommon_->SetGraphicsCommand();
	for (size_t i = 0; i < objects_.size(); ++i) {
		// (i == 1 がプレイヤー という前提)
		if (isFirstPerson && i == 1) {
			continue; 
		}
		objects_[i]->Draw();
	}

	BulletManager::GetInstance()->Draw();

	// --- スプライト描画 ---
	spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
	for (auto& sprite : sprites_) {
		sprite->Draw();
	}


	particleSystem_->Draw();
}

#pragma region ヒット時処理の関数
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

void GamePlayScene::OnBulletHit(const BulletHitEvent& event) {

	// (デバッグ用：衝突した相手の属性を取得)
	uint32_t attribute = event.hitObject->GetCollisionAttribute();

	if (attribute & (kEnemy | kGround)) {
		DebugConsole::GetInstance()->AddLog("BULLET HIT!");
		particleSystem_->SpawnParticles(
			event.bullet->GetWorldPosition(),
			10,
			2.0f,
			nullptr,
			180.0f,   
			{ 1.0f, 0.8f, 0.2f, 1.0f },
			{ 0.3f, 0.1f, 0.0f, 0.0f },
			0.3f, 0.5f,
			1.0f,             
			0.1f             
		);
		


	}
	
	
}
#pragma endregion

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

#pragma region ターゲット処理関数
/// <summary>
/// シーン内の敵リストを取得する
/// </summary>
std::vector<Object3d*> GamePlayScene::FindEnemies() {
	std::vector<Object3d*> enemies;
	for (const auto& obj : objects_) {
		// kEnemy 属性 を持ち、プレイヤー自身 ではないオブジェクトを検索
		if ((obj->GetCollisionAttribute() & kEnemy) && (obj.get() != player_)) {
			enemies.push_back(obj.get());
		}
	}
	return enemies;
}

/// <summary>
/// ロックオン対象として最適な敵を探す
/// </summary>
Object3d* GamePlayScene::FindBestLockOnTarget(Camera* camera) {
	static Math math;
	std::vector<Object3d*> enemies = FindEnemies();
	if (enemies.empty() || !player_) { return nullptr; }

	Object3d* bestTarget = nullptr;
	float maxDot = -2.0f; // 内積の最小値(-1)より小さく

	Vector3 playerPos = player_->GetWorldPosition();
	Vector3 cameraForward = math.Normalize(camera->GetTargetPoint() - camera->GetEye());

	const float maxLockOnDistance = 50.0f; // ロックオン最大距離
	const float minLockOnDot = 0.5f;     // 視界（約60度）の外は無視

	for (Object3d* enemy : enemies) {
		Vector3 enemyPos = enemy->GetWorldPosition();
		Vector3 toEnemy = enemyPos - playerPos;
		float distance = math.Length(toEnemy);

		// 遠すぎる敵は除外
		if (distance > maxLockOnDistance || distance < 0.1f) {
			continue;
		}

		Vector3 toEnemyNormalized = toEnemy / distance;
		float dot = math.Dot(cameraForward, toEnemyNormalized);

		// 視界内で、最もカメラ正面に近い敵を選ぶ
		if (dot > minLockOnDot && dot > maxDot) {

			//どこかでカメラがめり込んだ時の対処をしたいここで
			RaycastHit hit = CollisionManager::GetInstance()->Raycast(
				playerPos,          // 開始点
				toEnemyNormalized,  // 方向
				distance,           // 最大距離 (敵までの距離)
				kGround             // 対象：地面・壁
			);

			// ★ ヒットしなかった場合のみ、ロックオン対象とする
			if (!hit.isHit) {
				maxDot = dot;
				bestTarget = enemy;
			}
	

		}
	}
	return bestTarget;
}


/// <summary>
/// ロックオン処理
/// </summary>
void GamePlayScene::UpdateLockOn() {
	if (!player_ || !inputManager_) { return; }

	Camera* camera = CameraManager::GetInstance()->GetMainCamera();
	if (!camera) { return; }

	// (1) ★ Zキー でロックオン開始/解除
	if (inputManager_->IsKeyTriggered(DIK_Z)) { 
		isLockingOn_ = !isLockingOn_;

		if (isLockingOn_) {
			// (2) ロックオン開始 -> 対象を検索
			lockOnTarget_ = FindBestLockOnTarget(camera);
			if (lockOnTarget_ == nullptr) { // 対象が見つからなければ解除
				isLockingOn_ = false;
			}
		}

		if (!isLockingOn_) {
			// (3) ロックオン解除
			lockOnTarget_ = nullptr;
			camera->SyncRotationToCurrentView();
			camera->SetLockOnTarget(nullptr); // カメラの注視点を解除
			camera->SetFollowMode(Camera::FollowMode::kAimable); // 通常モードに戻す
		}

		player_->SetLockOn(isLockingOn_); // Playerに状態を通知
	}

	// (4) ロックオン中の更新
	if (isLockingOn_) {
		if (lockOnTarget_ == nullptr) {
			isLockingOn_ = false;
			player_->SetLockOn(false);
			camera->SetFollowMode(Camera::FollowMode::kAimable);
			return;
		}

		// (5) プレイヤーの向きを敵に向ける
		Vector3 playerPos = player_->GetWorldPosition();
		Vector3 enemyPos = lockOnTarget_->GetWorldPosition();
		Vector3 toEnemy = enemyPos - playerPos;
		toEnemy.y = 0.0f; // 高低差は無視

		// プレイヤーのY軸回転を敵の方向に向ける 
		player_->SetRotationY(std::atan2(toEnemy.x, toEnemy.z));
		// (6) カメラをロックオンモードに設定
		camera->SetFollowMode(Camera::FollowMode::kLockOn);
		camera->SetLockOnTarget(lockOnTarget_); // 注視対象を敵にセット
	}
}


/// <summary>
/// 静的な壁/床ブロックを生成し、衝突判定に登録するヘルパー関数
/// </summary>
std::unique_ptr<Object3d> GamePlayScene::CreateStaticBlock(
	const Vector3& position,
	const std::string& name,
	const Vector3& collisionHalfSize)
{
	auto block = std::make_unique<Object3d>();
	block->Initialize(object3dCommon_.get());
	block->SetModel("block");
	block->SetTranslate(position);
	block->SetName(name);
	block->SetStatic(true);

	// ★ 衝突判定も同時に設定
	block->SetCollisionAttribute(kGround);
	block->SetCollisionMask(~kGround);
	block->SetColliderType(ColliderType::kAABB);
	block->SetCollisionSize(collisionHalfSize);
	CollisionManager::GetInstance()->AddObject(block.get());

	return block;
}
#pragma endregion