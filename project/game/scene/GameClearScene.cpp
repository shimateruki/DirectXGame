
#define NOMINMAX
#include "GameClearScene.h"
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
#include <LightEditor.h>


void GameClearScene::Initialize() {
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
	bgmHandle_ = audioPlayer_->LoadSoundFile("Resources/bgm/Alarm02.mp3");
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
	LightEditor::GetInstance()->SetObject3dCommon(object3dCommon_.get());




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
	LoadObjectLayout("Resources/json/3Dobject/gameClearScene.json");
	LoadSpriteLayout("Resources/json/sprite/gameClearScene.json");
	LightManager::GetInstance()->LoadState("Resources/json/light/gameClearScene.json");



	//コマンドリストが安全に閉じるためのやつないとバグる
	dxCommon_->FlushCommandQueue(false);
}

void GameClearScene::Finalize() {


	CollisionManager::GetInstance()->ClearObjects();
	BulletManager::GetInstance()->Finalize();
	particleSystem_.reset();
	particleCommon_.reset();
	sprites_.clear();
	spriteCommon_.reset();
	objects_.clear();
	object3dCommon_.reset();
}


void GameClearScene::Update(float deltaTime) {

	static Math math;
	LightEditor::GetInstance()->Update();
	

	// --- 常に実行される更新 ---
	CameraManager::GetInstance()->Update();

	particleSystem_->Update(deltaTime);



	// --- 1. ゲームロジック (オブジェクト・スプライト) 更新 ---
	for (auto& obj : objects_) {
		obj->Update(deltaTime);
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

}

void GameClearScene::Draw() {

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
	LightEditor::GetInstance()->Draw3D();

	// --- スプライト描画 ---
	spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
	for (auto& sprite : sprites_) {
		sprite->Draw();
	}


	particleSystem_->Draw();
}



#pragma region Editor Functions
void GameClearScene::LoadObjectLayout(const std::string& filename) {
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

				//  SpawnerもInvisibleBoxと同じく「モデルなし」にする
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
	

	}
	catch (json::parse_error& e) {
		OutputDebugStringA(("Failed to parse " + filename + "\n").c_str());
	}
	file.close();
}


void GameClearScene::LoadSpriteLayout(const std::string& filename) {
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


void GameClearScene::AddObject(std::unique_ptr<Object3d> object) {
	if (object == nullptr) {
		return;
	}
	CollisionManager::GetInstance()->AddObject(object.get());
	pendingObjects_.push_back(std::move(object));
}



void GameClearScene::RequestRemoveObject(Object3d* object) {
	if (object) {
		removalList_.push_back(object);
	}
}

void GameClearScene::ProcessRemovals() {
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
std::vector<Object3d*> GameClearScene::FindEnemies() {
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
Object3d* GameClearScene::FindBestLockOnTarget(Camera* camera) {
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
/// 静的な壁/床ブロックを生成し、衝突判定に登録するヘルパー関数
/// </summary>
std::unique_ptr<Object3d> GameClearScene::CreateStaticBlock(
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


