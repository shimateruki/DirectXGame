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
#include <numbers>
#include <CameraEditor.h>
#include <BaseEnemy.h>
#include <EnemyFactory.h>
#include <EnemySpawner.h>


void GamePlayScene::Initialize() {
	using json = nlohmann::json;

	// --- 基盤クラスのポインタを保持 ---
	dxCommon_ = DirectXCommon::GetInstance();
	inputManager_ = InputManager::GetInstance();
	audioPlayer_ = AudioPlayer::GetInstance();

	ModelManager::GetInstance()->LoadModel("player");
	ModelManager::GetInstance()->LoadModel("teapot");
	ModelManager::GetInstance()->LoadModel("multiMaterial");
	ModelManager::GetInstance()->LoadModel("sampleBlock.gltf");
	ModelManager::GetInstance()->LoadModel("saka");
	LOG("Game Initialized!");
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
	gameRule_ = std::make_unique<GameRule>();
	gameRule_->Initialize(this);


	// --- オブジェクトの生成 ---
	auto playerObj = std::make_unique<Player>();
	playerObj->Initialize(object3dCommon_.get(), inputManager_, particleSystem_.get());
	playerObj->SetModel("sample");
	playerObj->SetTranslate({ 2.0f, 0.0f, 0.0f });
	playerObj->SetName("Player");
	playerObj->SetStatic(false);
	player_ = playerObj.get();
	playerObj->SetMoveStrategy(std::make_unique<MoveStrategy3D>());
	CollisionManager::GetInstance()->AddObject(playerObj.get());
	objects_.emplace_back(std::move(playerObj));



	auto terrain = std::make_unique<Object3d>();
	terrain->Initialize(object3dCommon_.get());
	terrain->SetModel("terrain");
	terrain->SetTranslate({ 2.0f, 0.0f, 0.0f });
	terrain->SetName("terrain");
	terrain->SetStatic(true);
	CollisionManager::GetInstance()->AddObject(terrain.get());
	objects_.emplace_back(std::move(terrain));

	// Factoryで作った敵（unique_ptr<BaseEnemy>）を受け取る
	std::unique_ptr<BaseEnemy> newEnemy =
		EnemyFactory::GetInstance()->CreateEnemy("Slime", object3dCommon_.get());

	if (newEnemy) {
		newEnemy->SetTranslate({ 10.0f, 0.0f, 10.0f });
		newEnemy->SetTarget(player_);
		CollisionManager::GetInstance()->AddObject(newEnemy.get());
		objects_.push_back(std::move(newEnemy));
	}




 CameraEditor::GetInstance()->Initialize();

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
	LoadObjectLayout("resouces/json/scene_layout.json");
	LoadSpriteLayout("resouces/json/sprite_layout.json");
	LightManager::GetInstance()->LoadState("resouces/json/light_layout.json");



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

	// ▼CameraEditorの設定を反映
	CameraEditor::GetInstance()->Update(player_, isLockingOn_);

	// 「エディタモード（自由カメラ）」じゃない時だけ、プレイヤーのマウス操作を受け付ける
	if (!CameraEditor::GetInstance()->IsEditorMode()) {

		Camera* camera = CameraManager::GetInstance()->GetMainCamera();
		Camera::FollowMode currentMode = camera->GetFollowMode(); // 現在のモード取得（EditorのUpdateでセットされているはず）

		// ズーム処理（マウスホイール）
		if (currentMode == Camera::FollowMode::kAimable) {
			float wheelDelta = inputManager_->GetMouseWheelDelta();
			if (wheelDelta != 0.0f) {
		
			}
		}

		// 回転処理（右クリックしながらマウス移動）
		if (currentMode == Camera::FollowMode::kAimable || currentMode == Camera::FollowMode::kFirstPerson) {
			// ※ 右クリック(1) 押下中のみ回転
			if (inputManager_->IsMouseButtonPressed(1)) {
				Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
				if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
					camera->AddRotation(mouseDelta);
				}
			}
		}
	}

	if (inputManager_->IsKeyTriggered(DIK_UPARROW) || inputManager_->IsKeyTriggered(DIK_DOWNARROW)) {

		// 1. 今いるプレイヤーを全員リストアップする
		std::vector<Player*> playerList;
		for (auto& obj : objects_) {
			// dynamic_cast で Player クラスだけを抽出
			if (auto p = dynamic_cast<Player*>(obj.get())) {
				playerList.push_back(p);
			}
		}

		if (!playerList.empty()) {
			// 2. 現在の主役がリストの何番目にいるか探す
			int currentIndex = 0;
			for (int i = 0; i < playerList.size(); ++i) {
				if (playerList[i] == player_) {
					currentIndex = i;
					break;
				}
			}

			// 3. 次の番号を計算 (UPなら+1, DOWNなら-1)
			int nextIndex = currentIndex;
			if (inputManager_->IsKeyTriggered(DIK_UPARROW)) {
				nextIndex = (currentIndex + 1) % playerList.size(); // 末尾まで行ったら0に戻る
			} else {
				// マイナスの余り計算は少し特殊なのでこう書くと安全
				nextIndex = (currentIndex - 1 + playerList.size()) % playerList.size();
			}

			// 4. 交代！
			SwitchActivePlayer(playerList[nextIndex]);
		}
	}




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
		particleSystem_->SpawnParticles(
			{ 0.0f, 1.0f, 0.0f }, 100,
			5.0f, nullptr, 1.0f,
			{ 1.0f, 0.5f, 0.0f, 1.0f }, { 1.0f, 1.0f, 0.0f, 0.0f },
			0.5f, 3.0f,
			1.0f, 0.1f
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

	if (!pendingObjects_.empty()) {
		for (auto& pendingObj : pendingObjects_) {
			objects_.push_back(std::move(pendingObj));
		}
		pendingObjects_.clear(); // 空にする
	}

	//オブジェクト削除関数
	ProcessRemovals();



#ifdef USE_IMGUI
	ImGui::Begin("Debug Control");

	// ==========================================================
	// 1. プレイヤー設定 (Transform & Material)
	// ==========================================================
	if (ImGui::CollapsingHeader("Player Settings")) {
		if (player_) {
			// Transform (位置・回転・スケール)
			if (ImGui::TreeNode("Transform")) {
				// 位置 (表示のみ)
				Vector3 pos = player_->GetWorldPosition();
				ImGui::DragFloat3("Position", &pos.x);

				// スケール (変更可能)
				Vector3 scale = player_->GetScale();
				if (ImGui::DragFloat3("Scale", &scale.x, 0.01f, 0.1f, 10.0f)) {
					player_->SetScale(scale);
				}
				ImGui::TreePop();
			}

			// Material (色・反射設定)
			Model* model = player_->GetModel();
			if (model) {
				Model::Material* material = model->GetMaterial();
				if (material) {
					if (ImGui::TreeNode("Material")) {
						// ライティング種類の選択
						const char* lightingTypes[] = { "None", "Lambert", "Phong" };
						int currentType = material->selectedLighting;
						if (ImGui::Combo("Lighting Type", &currentType, lightingTypes, IM_ARRAYSIZE(lightingTypes))) {
							material->selectedLighting = currentType;
						}

						// Phongの時だけ光沢度を調整
						if (material->selectedLighting == 2) {
							ImGui::DragFloat("Shininess", &material->shininess, 1.0f, 1.0f, 256.0f);
						}

						// 色
						ImGui::ColorEdit4("Base Color", &material->color.x);
						ImGui::TreePop();
					}
				}
			}

			// 平行光源 (Directional Light) ※プレイヤーが持っている平行光源設定
			Object3d::DirectionalLight* dirLight = player_->GetDirectionalLightData();
			if (dirLight) {
				if (ImGui::TreeNode("Directional Light (Local)")) {
					ImGui::DragFloat3("Direction", &dirLight->direction.x, 0.01f, -1.0f, 1.0f);
					ImGui::ColorEdit4("Color", &dirLight->color.x);
					ImGui::DragFloat("Intensity", &dirLight->intensity, 0.01f, 0.0f, 5.0f);
					ImGui::TreePop();
				}
			}
		}
	}

	

	ImGui::End();
#endif


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

	// ライトのリソースを取得
	ID3D12Resource* pointLightRes = LightManager::GetInstance()->GetPointLightResource();
	ID3D12Resource* spotLightRes = LightManager::GetInstance()->GetSpotLightResource();

	// --- 3Dオブジェクト描画 ---
	object3dCommon_->SetGraphicsCommand();
	for (size_t i = 0; i < objects_.size(); ++i) {
		// (i == 1 がプレイヤー という前提)
		if (isFirstPerson && i == 1) {
			continue; 
		}
		objects_[i]->Draw(pointLightRes, spotLightRes);
	}

	BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);

	debugEditor_->DrawPreview(pointLightResource_.Get(), spotLightResource_.Get());


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
	std::map<Object3d*, std::string> parentPendingList;

	try {
		sceneData = json::parse(file);

		if (sceneData.contains("objects") && sceneData["objects"].is_array()) {

			for (const auto& objData : sceneData["objects"]) {
				if (!objData.contains("name") || !objData["name"].is_string()) continue;
				std::string name = objData["name"].get<std::string>();

				// 1. 既存チェック
				Object3d* targetObject = nullptr;
				for (auto& obj : objects_) {
					if (obj && !obj->GetName().empty() && obj->GetName() == name) {
						targetObject = obj.get();
						break;
					}
				}

				// 2. 新規生成 
				if (!targetObject) {
					if (object3dCommon_) {
						std::unique_ptr<Object3d> newObj = nullptr;

						// A. 敵の種類
						std::string enemyType = "";
						if (objData.contains("enemyType") && objData["enemyType"].is_string()) {
							enemyType = objData["enemyType"].get<std::string>();
						}

						// B. クラスタイプ
						std::string type = "Model";
						if (objData.contains("type") && objData["type"].is_string()) {
							type = objData["type"].get<std::string>();
						}

						// --- 生成分岐 ---

						// パターンA: 敵 (Factory)
						if (!enemyType.empty()) {
							auto enemy = EnemyFactory::GetInstance()->CreateEnemy(enemyType, object3dCommon_.get());
							// 敵にも種類情報をセットしておく（保存時に使うため）
							if (enemy) {
								enemy->SetEnemyType(enemyType);
								// dynamic_castでBaseEnemyか確認し、プレイヤーをセット
								if (auto base = dynamic_cast<BaseEnemy*>(enemy.get())) {
									base->SetTarget(player_);
								}
								newObj = std::move(enemy);
							}
						}
						// パターンB: プレイヤー
						else if (type == "Player" || name == "Player") {
							auto player = std::make_unique<Player>();
							player->Initialize(object3dCommon_.get(), inputManager_, particleSystem_.get());
							player->SetMoveStrategy(std::make_unique<MoveStrategy3D>());

							// 一旦ポインタ保持（あとでSwitchActivePlayerに使う）
							player_ = player.get();
							newObj = std::move(player);
						}
						// パターンC: スポーナー (今回の主役)
						else if (type == "Spawner") {
							auto spawner = std::make_unique<EnemySpawner>();

							// 1. デフォルト設定
							std::string spawnEnemyType = "Goblin";
							float interval = 3.0f;
							int maxCount = 5;

							// 2. JSONの "param" ブロックから設定を読み込む
							if (objData.contains("param") && objData["param"].is_object()) {
								auto& p = objData["param"];

								if (p.contains("enemyType") && p["enemyType"].is_string()) {
									spawnEnemyType = p["enemyType"].get<std::string>();
								}
								if (p.contains("interval") && p["interval"].is_number()) {
									interval = p["interval"].get<float>();
								}
								if (p.contains("maxCount") && p["maxCount"].is_number()) {
									maxCount = p["maxCount"].get<int>();
								}
							}

							// 3. 初期化
							spawner->Initialize(object3dCommon_.get(), spawnEnemyType, interval, maxCount);

							// 4. クラス名と透明設定 (ここで確実に設定！)
							spawner->SetClassName("Spawner");
							spawner->SetModel(nullptr);
							spawner->SetIsVisible(false);

							// 5. コールバック設定：ここが「白い箱」にならないための重要ポイント
							// ※ EnemySpawner側が spawnPos (Vector3) を渡してくれる前提です
							spawner->SetOnSpawnCallback([this, spawnEnemyType](const Vector3& spawnPos) {

								// Factoryを使って本物の敵クラス(Goblin等)を作る
								auto newEnemy = EnemyFactory::GetInstance()->CreateEnemy(spawnEnemyType, object3dCommon_.get());

								if (newEnemy) {
									// 座標をセット
									newEnemy->SetTranslate(spawnPos);
									newEnemy->SetEnemyType(spawnEnemyType);

									// ★重要: プレイヤー情報を渡してAIを動かす
									if (auto base = dynamic_cast<BaseEnemy*>(newEnemy.get())) {
										base->SetTarget(player_);
									}

									// シーンに追加 (AddObject内で pendingObjects_ に入るので安全)
									this->AddObject(std::move(newEnemy));
								}
								});

							newObj = std::move(spawner);
						}
						// パターンD: 通常オブジェクト
						else {
							newObj = std::make_unique<Object3d>();
							newObj->Initialize(object3dCommon_.get());
						}

						if (newObj) {
							newObj->SetName(name);
							std::string currentClass = newObj->GetClassName();
							// EnemyやPlayer以外の汎用オブジェクトならクラス名をセット
							if (currentClass != "Enemy" && currentClass != "Player" && currentClass != "Spawner") {
								newObj->SetClassName(type);
							}

							targetObject = newObj.get();
							AddObject(std::move(newObj));
						}
					}
				}

				if (!targetObject) continue;

				// 3. モデル・表示設定
				std::string type = targetObject->GetClassName(); // 最新のクラス名を取得

				// ★修正: SpawnerもInvisibleBoxと同じく「モデルなし」にする
				// これによりJSONに "modelName": "cube" と書いてあっても無視して透明にする
				if (type == "InvisibleBox" || type == "Spawner") {
					targetObject->SetModel(nullptr);
					targetObject->SetIsVisible(false);
				} else {
					targetObject->SetIsVisible(true);
					if (objData.contains("modelName") && objData["modelName"].is_string()) {
						std::string modelName = objData["modelName"].get<std::string>();
						// モデルが変わる場合のみロード
						if (targetObject->GetModelName() != modelName) {
							ModelManager::GetInstance()->LoadModel(modelName);
							targetObject->SetModel(modelName);
						}
					}
				}

				// 4. Transform
				Object3d::Transform* transform = targetObject->GetTransform();
				if (objData.contains("position") && objData["position"].is_array()) {
					transform->translate.x = objData["position"][0];
					transform->translate.y = objData["position"][1];
					transform->translate.z = objData["position"][2];
				}
				if (objData.contains("rotation") && objData["rotation"].is_array()) {
					transform->rotate.x = objData["rotation"][0];
					transform->rotate.y = objData["rotation"][1];
					transform->rotate.z = objData["rotation"][2];
				}
				if (objData.contains("scale") && objData["scale"].is_array()) {
					transform->scale.x = objData["scale"][0];
					transform->scale.y = objData["scale"][1];
					transform->scale.z = objData["scale"][2];
				}

				// 5. Collider
				if (objData.contains("collider")) {
					json colData = objData["collider"];
					Object3d::ColliderConfig config = targetObject->GetColliderConfig();

					if (colData.contains("type")) config.type = (ColliderType)colData["type"].get<int>();
					if (colData.contains("center")) {
						config.center.x = colData["center"][0];
						config.center.y = colData["center"][1];
						config.center.z = colData["center"][2];
					}
					if (colData.contains("size")) {
						config.size.x = colData["size"][0];
						config.size.y = colData["size"][1];
						config.size.z = colData["size"][2];
					}
					targetObject->SetColliderConfig(config);
				}

				if (objData.contains("collisionAttribute")) targetObject->SetCollisionAttribute(objData["collisionAttribute"]);
				if (objData.contains("collisionMask")) targetObject->SetCollisionMask(objData["collisionMask"]);

				// 6. Events & Params
				if (objData.contains("eventID")) targetObject->SetEventType(static_cast<EventType>(objData["eventID"]));
				if (objData.contains("targetID")) targetObject->SetTargetID(objData["targetID"]);
				if (objData.contains("myEventID")) targetObject->SetEventID(objData["myEventID"]);

				// paramの読み込み (Spawner設定などもここで読む)
				if (objData.contains("param") && objData["param"].is_object()) {
					// まだ作られてなければ作成
					if (!targetObject->param_.has_value()) {
						targetObject->param_.emplace();
					}
					json paramData = objData["param"];
					auto& p = targetObject->param_.value();

					// 物理・ステータス系
					if (paramData.contains("hp")) p.hp = paramData["hp"];
					if (paramData.contains("maxHp")) p.maxHp = paramData["maxHp"];
					if (paramData.contains("speed")) p.speed = paramData["speed"];
					if (paramData.contains("gravity")) p.gravity = paramData["gravity"];
					if (paramData.contains("jumpPower")) p.jumpPower = paramData["jumpPower"];
					if (paramData.contains("maxFallSpeed")) p.maxFallSpeed = paramData["maxFallSpeed"];

					// Spawner系 (保存した値を書き戻す)
					if (paramData.contains("enemyType")) p.enemyType = paramData["enemyType"];
					if (paramData.contains("interval")) p.interval = paramData["interval"];
					if (paramData.contains("maxCount")) p.maxCount = paramData["maxCount"];
				}

				// 7. Animation
				if (objData.contains("animName")) targetObject->animName_ = objData["animName"].get<std::string>();
				if (objData.contains("isAnimLoop")) targetObject->isAnimLoop_ = objData["isAnimLoop"].get<bool>();
				if (objData.contains("isAnimRelative")) targetObject->isAnimRelative_ = objData["isAnimRelative"].get<bool>();

				targetObject->InitializeRecorder(sceneManager_);
				if (!targetObject->animName_.empty()) {
					targetObject->recorder_->Play(targetObject->animName_, targetObject->isAnimLoop_, targetObject->isAnimRelative_);
				}

				// 8. 親子関係保留
				if (objData.contains("parentName") && objData["parentName"].is_string()) {
					std::string pName = objData["parentName"].get<std::string>();
					if (!pName.empty()) parentPendingList[targetObject] = pName;
				}
			}
		}

		// 9. 親子解決
		for (auto const& [childObj, parentName] : parentPendingList) {
			Object3d* parentObj = nullptr;
			for (auto& obj : objects_) {
				if (obj && obj->GetName() == parentName) {
					parentObj = obj.get();
					break;
				}
			}
			if (parentObj) childObj->SetParent(parentObj);
		}
		if (player_) {
			SwitchActivePlayer(player_);
		}

	}
	catch (json::parse_error& e) {
		OutputDebugStringA(("Failed to parse " + filename + "\n").c_str());
	}
	file.close();
}


void GamePlayScene::LoadSpriteLayout(const std::string& filename) {
	using json = nlohmann::json;
	std::ifstream file(filename);

	if (!file.is_open()) {
		// 初回起動時などファイルがない場合は警告だけで抜ける
		std::string warnMsg = "Warning: Could not open " + filename + "\n";
		OutputDebugStringA(warnMsg.c_str());
		return;
	}

	json layoutData;
	try {
		layoutData = json::parse(file);

		if (layoutData.contains("sprites") && layoutData["sprites"].is_array()) {
			for (const auto& spriteData : layoutData["sprites"]) {

				// 名前がないデータはスキップ
				if (!spriteData.contains("name") || !spriteData["name"].is_string()) {
					continue;
				}
				std::string name = spriteData["name"];

				// 1. 既存のリストから同じ名前のスプライトを探す
				Sprite* targetSprite = nullptr;
				for (auto& sprite : sprites_) {
					if (sprite && !sprite->GetName().empty() && sprite->GetName() == name) {
						targetSprite = sprite.get();
						break;
					}
				}

				// 2. ★追加部分★ 見つからなかった場合、新しく生成する
				if (!targetSprite) {
					if (spriteCommon_) { // SpriteCommonを持っている前提
						// テクスチャ名の取得 (保存されている場合)
						std::string textureFile = "";
						if (spriteData.contains("texture") && spriteData["texture"].is_string()) {
							textureFile = spriteData["texture"];
						}

						// テクスチャ読込 (ファイル名があれば読み込む、なければとりあえず0)
						uint32_t handle = 0;
						if (!textureFile.empty()) {
							handle = Sprite::LoadTexture(textureFile);
						}

						// 生成と初期化
						auto newSprite = std::make_unique<Sprite>();
						newSprite->Initialize(spriteCommon_.get(), handle); // handleが0だと白画像などになるかも
						newSprite->SetName(name);

						// リストに追加
						targetSprite = newSprite.get();
						sprites_.push_back(std::move(newSprite));
					}
				}

				// 3. パラメータの適用 (既存・新規共通)
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
	pendingObjects_.push_back(std::move(object));
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




void GamePlayScene::SwitchActivePlayer(Player* newMainPlayer) {
	if (!newMainPlayer) return;

	// シーンが持つ「現在の主役」変数を書き換える
	this->player_ = newMainPlayer;

	// 共通の更新処理を行うラムダ関数
	auto updateObjectSettings = [&](Object3d* obj) {
		if (!obj) return;

		// --- 操作権限の更新 ---
		if (auto p = dynamic_cast<Player*>(obj)) {
			// 指定された人だけ true (操作可能) にする
			bool isActive = (p == newMainPlayer);
			p->SetIsControlActive(isActive);
		}

		// --- 敵のターゲット更新 ---
		if (auto enemy = dynamic_cast<BaseEnemy*>(obj)) {
			enemy->SetTarget(newMainPlayer);
		}
		};

	// 1. メインリストの更新
	for (auto& obj : objects_) {
		updateObjectSettings(obj.get());
	}

	for (auto& obj : pendingObjects_) {
		updateObjectSettings(obj.get());
	}
}