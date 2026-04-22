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
#include "TutorialDoll.h"
#include"WinApp.h"
#include "GameProgress.h"
#include "SaveDataManager.h"
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
#include <MeshEffectManager.h>
#include"TimeAttackUI.h"
#include <CinematicFade.h>

bool GamePlayScene::s_isRebooting_ = false;

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
	//
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
	MeshEffectManager::GetInstance()->Initialize(object3dCommon_.get());
	// パーティクルで使う画像を読み込み、ハンドル(番号)を保存しておく
	gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/white.png");


	// --- 5. レベルデータ読み込み (JSON) ---
	levelLoader_ = std::make_unique<LevelLoader>();
	levelLoader_->LoadObjectLayout(this, "Resources/json/3Dobject/bossStage.json");
	levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/sprite_layout.json");
	LightManager::GetInstance()->LoadState("Resources/json/light/light_layout.json");
	CameraEditor::GetInstance()->Initialize();
	CameraEditor::GetInstance()->LoadFile("game_camera.json");

	timeAttackUI_ = std::make_unique<TimeAttackUI>();
	timeAttackUI_->Initialize(spriteCommon_.get());

	// --- スプライトの中から探索
	for (auto& sprite : sprites_) {
		if (sprite->GetName() == "playerHpBar") {
			playerHpBarSprite_ = sprite.get();
			playerHpBarMaxWidth_ = sprite->GetSize().x; // 元の長さを記憶！
		}
	}
	// =======================================================
		// ゲームオーバー用UIの取得と初期化 (最初は透明にして隠す)
		// =======================================================
	gameOverTextSprite_ = GetSpriteByName("GameOverText.png");
	restartTextSprite_ = GetSpriteByName("restartText.png");
	titleTextSprite_ = GetSpriteByName("titleText.png");

	auto SetAlphaZero = [](Sprite* sprite) {
		if (sprite) {
			Vector4 color = sprite->GetColor();
			color.w = 0.0f; // 透明度(Alpha)を0に
			sprite->SetColor(color);
		}
		};
	SetAlphaZero(gameOverTextSprite_);
	SetAlphaZero(restartTextSprite_);
	SetAlphaZero(titleTextSprite_);
	isGameOverUiReady_ = false; // フラグのリセット
	for (auto& sprite : sprites_) {
		if (sprite->GetName() == "bossrHpBar") {
			bossHpBarSprite_ = sprite.get();
			bossHpBarMaxWidth_ = sprite->GetSize().x;
			SetAlphaZero(bossHpBarSprite_);
		}
		else if (sprite->GetName() == "bariaHp.png") {
			barrierHpBarSprite_ = sprite.get();
			barrierHpBarMaxWidth_ = sprite->GetSize().x;
			SetAlphaZero(bossHpBarSprite_);
		}
		else if (sprite->GetName() == "bossHpBarback") {
			bossHpBackSprite_ = sprite.get();
			SetAlphaZero(bossHpBackSprite_);
		}
		else if (sprite->GetName() == "bossText") { 
			bossNameSprite_ = sprite.get();
			SetAlphaZero(bossNameSprite_);
		}
	}

	// =======================================================
	// ポーズ用UIの取得と初期化 (最初は透明にして隠す)
	// =======================================================
	poseBackSprite_ = GetSpriteByName("poseBack.png");
	poseTextSprite_ = GetSpriteByName("poseText.png");
	restartPoseTextSprite_ = GetSpriteByName("restartPoseText.png");
	titleTextPoseSprite_ = GetSpriteByName("titleTextPose.png");

	auto SetAlpha = [](Sprite* sprite, float alpha) {
		if (sprite) {
			Vector4 color = sprite->GetColor();
			color.w = alpha;
			sprite->SetColor(color);
		}
		};

	SetAlpha(poseBackSprite_, 0.0f);
	SetAlpha(poseTextSprite_, 0.0f);
	SetAlpha(restartPoseTextSprite_, 0.0f);
	SetAlpha(titleTextPoseSprite_, 0.0f);
	isPaused_ = false;


	// ★ 1. まず objectManager からオブジェクトのリストを取得する！
	auto& objects = objectManager_->GetObjects();

	for (auto it = objects.begin(); it != objects.end(); ++it) {
		if ((*it)->GetName() == "Enemy_BossCore") {
			// 1. 古いボスの「今の住所」をメモ（まだ消さない）
			Object3d* oldAddress = it->get();

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
			CollisionManager::GetInstance()->RemoveObject(oldAddress);
			CollisionManager::GetInstance()->AddObject(newAddress);

			// (B) 子供たちの親を、古い住所から新しい住所へ書き換える
			for (auto& obj : objects) {
				if (obj->GetParent() == oldAddress) {
					obj->SetParent(newAddress);

					// ★ ここを追加！新しいボスにパーツを登録する
					newAddress->AddArmorBlock(obj.get());
				}
			}

			// 3. 最後に実体を差し替える。ここで oldAddress は安全に消滅する
			*it = std::move(newBoss);
			break;
		}
	}

	// =======================================================
	// ★ 進行状況の復元：橋がすでに落ちている場合の処理
	// =======================================================
	if (GameProgress::GetInstance()->hasBridgeDropped) {
		// 1. シーン内の全ての「橋のブロック」を検索して消去・無効化
		auto& objects_ref = objectManager_->GetObjects();
		for (auto& obj : objects_ref) {
			std::string name = obj->GetName();
			// 名前が "Bridge_Block" または "Tutorial_" で始まるオブジェクトを全て対象にする
			if (name.find("Bridge_") != std::string::npos || name.find("Tutorial_") != std::string::npos) {
				obj->SetCollisionAttribute(0);   // 当たり判定を完全に消す
				if (name.find("Bridge_Block") != std::string::npos) { // ブリッジブロックは完全に消す
					obj->SetIsVisible(false);        // 見えなくする
					obj->isDead = true;              // 完全に消す（UpdateやDrawの対象から外す）
				}
			}
			else if (name.find("Battle_Field_Collision_Box_South") != std::string::npos) {
				obj->SetCollisionAttribute(kGround);
			}
		}

		// 2. 演出フラグを立てて、ムービーが二度と再生されないようにする
		this->hasBridgeDropped_ = true;

		// 3. プレイヤーの開始位置をボス前に飛ばし、チュートリアルをスキップ
		if (player_) {
			// 隊長が設定したボス前の座標を適用！
			player_->GetTransform()->translate = { 0.0f, 1.3f, -68.0f };
			player_->UpdateLocalMatrix();
			player_->UpdateWorldMatrix();

			// チュートリアル完了扱いにする（進行度クラスとシーン内フラグの両方を更新）
			GameProgress::GetInstance()->hasFinishedTutorial = true;
			this->hasFinishedTutorial_ = true;
			this->doorOpenProgress_ = 1.0f; // チュートリアル部屋のドアも全開にしておく
		}
	} else {
		// 最初からプレイする場合の完全リセット
		// エディタ等でJSONが書き換わっていた場合でも確実に復活させる
		auto& objects_ref = objectManager_->GetObjects();
		for (auto& obj : objects_ref) {
			std::string name = obj->GetName();
			if (name.find("Tutorial_") != std::string::npos && name.find("Ceiling") == std::string::npos) { // Ceilingは含まない
				obj->SetIsVisible(true);
				obj->SetCollisionAttribute(kGround);
				// ドアは最初は閉まっている状態にする
				if (name == "Tutorial_Door_Left") {
					obj->GetTransform()->translate.x = -5.0f; // 閉まった状態の位置
				}
				else if (name == "Tutorial_Door_Right") {
					obj->GetTransform()->translate.x = 5.0f; // 閉まった状態の位置
				}
				obj->UpdateWorldMatrix();
			}
			else if (name.find("Bridge_") != std::string::npos) { // ブリッジ関連は全て復活させる
				if (name.find("Bridge_Collision") == std::string::npos) {
					obj->SetIsVisible(true);
				}
				obj->SetCollisionAttribute(kGround);
			}
			else if (name.find("Battle_Field_Collision_Box_South") != std::string::npos) { // 南の当たり判定は最初は消しておく（橋が落ちるまでは通れるように）
				obj->SetCollisionAttribute(0);
			}
		}
		this->hasBridgeDropped_ = false;
		this->hasFinishedTutorial_ = false;
		this->doorOpenProgress_ = 0.0f; // ドアを閉める

		// =======================================================
		// ★ チュートリアルプラットフォーム降下演出の初期化
		// =======================================================
		for (auto& obj : objects_ref) {
			if (obj->GetName() == "Tutorial_Platform") {
				this->tutorialPlatform_ = obj.get();
				// 初期位置を y:100 に (念のため)
				obj->GetTransform()->translate.y = 100.0f;
				obj->UpdateWorldMatrix();
				break;
			}
		}

		if (this->tutorialPlatform_ && player_) {
			// プレイヤーをプラットフォームの真上に配置
			// 本来の重力時の位置関係を維持するため、現状の差分をオフセットとして記録
			Vector3 platformPos = this->tutorialPlatform_->GetTransform()->translate;
			
			// プレイヤーを初期位置へ (x, z はプラットフォームに合わせ、y は適切な高さへ)
			// ユーザーの 94.7f という数値は、プラットフォーム 100.0f に対して -5.3f のオフセットを示唆
			this->tutorialPlatformOffset_ = -5.3f;
			player_->GetTransform()->translate = { 0.0f, platformPos.y + tutorialPlatformOffset_, -244.0f };
			player_->UpdateLocalMatrix();
			player_->UpdateWorldMatrix();

			// 演出開始
			movieState_ = MovieState::kTutorialPlatformDescent;
			movieTimer_ = 0.0f;

			// 重力に任せると跳ねるため、物理を無効化して手動更新にする
			player_->SetIsControlActive(false);
			player_->SetIsPhysicsActive(false);
		}
	}

	// =======================================================
	 // ★ リスタート演出（電脳リブート）と完全初期化
	 // =======================================================
	SceneManager* scm = SceneManager::GetInstance();
	PostEffect::GetInstance()->ResetToBaseParams();

	if (scm->ShouldSkipFade()) {
		CinematicFade::GetInstance()->StartOpen(0.3f);
		scm->ResetSkipFade();
	}
	else {
		CinematicFade::GetInstance()->StartOpen(0.5f);
	}
	dxCommon_->FlushCommandQueue(false);
}

void GamePlayScene::Finalize() {
	MeshEffectManager::GetInstance()->Clear();
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
	// ---------------------------------------------------------
	// 1. ポーズの切り替え判定 (ゲームオーバー時はポーズ不可)
	// ---------------------------------------------------------
	bool isGameOver = (player_ && player_->GetHp() <= 0.0f);

	// 【Pキー】 か パッドの【STARTボタン】でポーズ切り替え
	if (!isGameOver && (inputManager_->IsKeyTriggered(DIK_P) || inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_START))) {
		isPaused_ = !isPaused_; // フラグを反転

		// 文字用のアルファ値 (1.0 = 完全不透明, 0.0 = 完全透明)
		float textAlpha = isPaused_ ? 1.0f : 0.0f;

		// 背景用のアルファ値 (0.6 = 半透明。もっと薄くしたければ 0.4 や 0.5 に！)
		float backAlpha = isPaused_ ? 0.6f : 0.0f;
		auto SetAlpha = [](Sprite* sprite, float a) {
			if (sprite) { Vector4 c = sprite->GetColor(); c.w = a; sprite->SetColor(c); }
			};
		// ★背景だけ backAlpha を使うように変更
		SetAlpha(poseBackSprite_, backAlpha);
		SetAlpha(poseTextSprite_, textAlpha);
		SetAlpha(restartPoseTextSprite_, textAlpha);
		SetAlpha(titleTextPoseSprite_, textAlpha);

		// 選択位置をリセット
		currentPauseMenuIndex_ = (int)PauseMenuIndex::Restart;
	}

	// ---------------------------------------------------------
	// 2. ポーズ中のUI操作と遷移
	// ---------------------------------------------------------
	if (isPaused_) {
		// 上下キーで項目切り替え
		if (inputManager_->IsKeyTriggered(DIK_UP) || inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_UP)) {
			currentPauseMenuIndex_--;
			if (currentPauseMenuIndex_ < 0) currentPauseMenuIndex_ = (int)PauseMenuIndex::Max - 1;
		}
		if (inputManager_->IsKeyTriggered(DIK_DOWN) || inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_DOWN)) {
			currentPauseMenuIndex_++;
			if (currentPauseMenuIndex_ >= (int)PauseMenuIndex::Max) currentPauseMenuIndex_ = 0;
		}

		// 選択中の項目をハイライト
		Vector4 normalColor = { 0.5f, 0.5f, 0.5f, 1.0f };
		Vector4 selectColor = { 1.0f, 1.0f, 1.0f, 1.0f };

		if (restartPoseTextSprite_) restartPoseTextSprite_->SetColor(currentPauseMenuIndex_ == (int)PauseMenuIndex::Restart ? selectColor : normalColor);
		if (titleTextPoseSprite_) titleTextPoseSprite_->SetColor(currentPauseMenuIndex_ == (int)PauseMenuIndex::Title ? selectColor : normalColor);

		// 決定ボタンで遷移
		if (inputManager_->IsKeyTriggered(DIK_SPACE) || inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {

			PostEffect::Params* postParams = PostEffect::GetInstance()->GetParams();
			postParams->dangerVignette = 0.0f;
			postParams->blackout = 0.0f;

			if (currentPauseMenuIndex_ == (int)PauseMenuIndex::Restart) {
				SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
			}
			else if (currentPauseMenuIndex_ == (int)PauseMenuIndex::Title) {
				SceneManager::GetInstance()->ChangeScene("TITLE");
			}
		}
		for (auto& sprite : sprites_) {
			sprite->Update();
		}
		// =======================================================
		// ★超重要：ポーズ中はここで関数を強制終了し、ゲームの時間を止める！
		// =======================================================
		return;
	}
	if (isRestartTransition_ || isTitleTransition_) {
		restartTimer_ += deltaTime;
		float transitionDuration = 1.0f;
		float t = std::clamp(restartTimer_ / transitionDuration, 0.0f, 1.0f);

		PostEffect::Params* postParams = PostEffect::GetInstance()->GetParams();

		// ★ ここはそのまま（縦に潰れる処理）
		postParams->crtShutdown = t;



		// 完全に終了（1秒経過）したらシーンをリロード
		if (restartTimer_ >= transitionDuration) {
			if (isRestartTransition_) {
				SceneManager::GetInstance()->ChangeScene("GAMEPLAY", true);
			}
			else {
				SceneManager::GetInstance()->ChangeScene("TITLE", true);
			}
		}

		return;
	}
	// =======================================================
	// チュートリアルドアの処理
	// =======================================================
	if (!hasFinishedTutorial_) {
		for (auto& obj : objectManager_->GetObjects()) {
			if (obj->GetName() == "Tutorial_Doll") {
				TutorialDoll* doll = dynamic_cast<TutorialDoll*>(obj.get());
				if (doll && doll->HasBeenDefeatedAtLeastOnce()) {
					hasFinishedTutorial_ = true;
					break;
				}
			}
		}
	}

	if (hasFinishedTutorial_) {
		if (doorOpenProgress_ < 1.0f) {
			doorOpenProgress_ += deltaTime * 0.5f; // 2秒で開く
			if (doorOpenProgress_ > 1.0f) {
				doorOpenProgress_ = 1.0f;
				// ドアが完全に開いた瞬間モデルを消しておく
				for (auto& obj : objectManager_->GetObjects()) {
					if (obj->GetName() == "Tutorial_Door_Left") {
						obj->SetIsVisible(false);
						obj->SetCollisionAttribute(0); // 当たり判定も消す
						obj->isDead = true; // 完全に消す
					}
					else if (obj->GetName() == "Tutorial_Door_Right") {
						std::string name = obj->GetName();
						if (name.find("Tutorial_Door") != std::string::npos && name.find("Wall") == std::string::npos) { // Wallは残す
							obj->SetIsVisible(false);
							obj->SetCollisionAttribute(0); // 当たり判定も消す
							obj->isDead = true; // 完全に消す
						}
					}
				}
			}
		}
		for (auto& obj : objectManager_->GetObjects()) {
			if (obj->GetName() == "Tutorial_Door_Left") {
				Transform* trans = obj->GetTransform();
				trans->translate.x = -5.0f - 15.0f * doorOpenProgress_;
				trans->isQuaternionMaster = false;
				obj->UpdateWorldMatrix();
			}
			else if (obj->GetName() == "Tutorial_Door_Right") {
				Transform* trans = obj->GetTransform();
				trans->translate.x = 5.0f + 15.0f * doorOpenProgress_; 
				trans->isQuaternionMaster = false;
				obj->UpdateWorldMatrix();
			}
		}
	}


	static Math math;
	LightEditor::GetInstance()->Update();

	Camera* camera = CameraManager::GetInstance()->GetMainCamera();

	// =================================================================
	// ムービーの制御
	// =================================================================
	if (movieState_ == MovieState::kBridgeDrop) {
		// ムービー開始時の初期化
		if (movieTimer_ == 0.0f) {
			movieStoredPlayerPos_ = player_->GetWorldPosition();
			player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
			player_->SetIsControlActive(false);
			player_->SetIsPhysicsActive(false);
		}

		movieTimer_ += deltaTime;

		// プレイヤーの座標を強制固定 (Player::Update側でも物理が無効化されているため)
		player_->SetTranslate(movieStoredPlayerPos_);

		// ムービー開始から1.5秒後にブリッジブロックの崩落演出を開始する

		// カメラ制御は GhostRecorder に任せるため、ブロックの崩落演出のみ実行する
		if (movieTimer_ > 1.5f) {
                // まず親の当たり判定を無効化する（プレイヤーが落ちるように）
                for (auto& obj : objectManager_->GetObjects()) {
                    if (obj->GetName() == "Bridge_Block_Front") {
                        obj->SetCollisionAttribute(0);
                    }
                }

				for (auto& obj : objectManager_->GetObjects()) {
					if (obj->GetName() == "Bridge_Block_Center") {
						Transform* trans = obj->GetTransform();
						trans->translate.y -= 26.0f * deltaTime;
						trans->rotate.x -= 1.0f * deltaTime; // 自然な傾き（下へ折れ曲がる）
						trans->isQuaternionMaster = false;
						obj->UpdateWorldMatrix();
					} else if (movieTimer_ > 2.0f && obj->GetName() == "Bridge_Block_Back") {
                        // 少し遅れて奥のブロックもさらに崩れる
                        Transform* trans = obj->GetTransform();
						trans->translate.y -= 32.0f * deltaTime;
						trans->rotate.x += 1.8f * deltaTime; // 折れ曲がる
						trans->isQuaternionMaster = false;
						obj->UpdateWorldMatrix();
                    } else if (movieTimer_ > 2.5f && obj->GetName() == "Bridge_Block_Front") {
                        // 最後に手前の親ブロックごと崩落する
                        Transform* trans = obj->GetTransform();
                        trans->translate.y -= 48.0f * deltaTime;
                        trans->rotate.x += 0.6f * deltaTime; 
                        trans->isQuaternionMaster = false;
                        obj->UpdateWorldMatrix();
                    }
				}
			}

			// ムービー終了判定
            // (ブリッジブロックの物理的な落下演出自体はカメラが終わる頃まで続く想定)
			if (movieTimer_ >= 5.5f) {
				auto& objects_ref = objectManager_->GetObjects();
				for (auto& obj : objects_ref) {
					std::string name = obj->GetName();
					// 名前が "Bridge_Block" または "Tutorial_" で始まるオブジェクトを全て対象にする
					if (name.find("Bridge_") != std::string::npos || name.find("Tutorial_") != std::string::npos) {
						obj->SetCollisionAttribute(0);   // 当たり判定を完全に消す
						if (name.find("Bridge_Block") != std::string::npos) { // ブリッジブロックは完全に消す
							obj->SetIsVisible(false);        // 見えなくする
							obj->isDead = true;              // 完全に消す（UpdateやDrawの対象から外す）
						}
					}
					else if (name.find("Battle_Field_Collision_Box_South") != std::string::npos) { // 南の当たり判定を復活させる（橋が落ちた後は通れなくする）
						obj->SetCollisionAttribute(kGround);
					}
				}
				movieState_ = MovieState::kNone;
				player_->SetIsControlActive(true);
				player_->SetIsPhysicsActive(true);
				GameProgress::GetInstance()->hasBridgeDropped = true;
			}

		// ムービー中は通常のプレイヤー入力やカメラ操作をスキップ
	}
	else if (movieState_ == MovieState::kTutorialPlatformDescent) {
		// =======================================================
		// ★ チュートリアルプラットフォーム降下演出の実装
		// =======================================================
		if (tutorialPlatform_ && player_) {
			// 操作無効化、重力無効（手動で吸着させるため）
			player_->SetIsControlActive(false);
			player_->SetIsPhysicsActive(false);

			// プラットフォームを降下させる
			Transform* trans = tutorialPlatform_->GetTransform();
			if (trans->translate.y > 29.6f) {
				// 降下速度 (1秒間に約15ユニット程度。100->29.6 なので約4.7秒)
				trans->translate.y -= 15.0f * deltaTime;
				if (trans->translate.y < 29.6f) {
					trans->translate.y = 29.6f;
				}
				tutorialPlatform_->UpdateWorldMatrix();
			}
			else {
				// 到着
				movieState_ = MovieState::kNone;
				player_->SetIsControlActive(true);
				player_->SetIsPhysicsActive(true); // 物理復帰
			}

			// プレイヤーのY座標をプラットフォームに同期（重力の代わりに手動で吸着）
			player_->GetTransform()->translate.y = trans->translate.y + tutorialPlatformOffset_;
			player_->UpdateWorldMatrix();
		} else {
			// 万が一対象がいない場合は即終了
			movieState_ = MovieState::kNone;
			if (player_) player_->SetIsPhysicsActive(true);
		}
	}

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
				float screenWidth = static_cast<float>(WinApp::kClientWidth);
				float screenHeight = static_cast<float>(WinApp::kClientHeight);

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

				// =======================================================
				// ★ 1. マウスの移動量と、ゲームパッドの右スティック入力を両方取得！
				// =======================================================
				Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
				Vector2 rightStick = inputManager_->GetRightStick();

				// =======================================================
				// ★ 2. カメラ感度を倍率に変換する！
				// =======================================================
				int sens = CameraEditor::GetInstance()->GetCameraSensitivity();
				float speedMultiplier = 1.0f + (sens * 0.1f);

				// =======================================================
				// ★ 3. 入力値に感度を掛け算して「最終的な移動量」を出す！
				// =======================================================
				Vector2 totalDelta;
				totalDelta.x = (mouseDelta.x + rightStick.x * 15.0f) * speedMultiplier;
				totalDelta.y = (mouseDelta.y - rightStick.y * 15.0f) * speedMultiplier; // スティックの上下は反転

#ifdef USE_IMGUI
				// ★ デバッグ(Develop)環境: UI操作の誤爆を防ぐため「右クリック中」または「スティック入力中」のみ回転
				if (inputManager_->IsMouseButtonPressed(1) || rightStick.x != 0.0f || rightStick.y != 0.0f) {
					if (totalDelta.x != 0.0f || totalDelta.y != 0.0f) {
						camera->AddRotation(totalDelta);
					}
				}
#else
				// ★ Release環境限定: 右クリック不要！操作した分だけ回転する
				if (totalDelta.x != 0.0f || totalDelta.y != 0.0f) {
					camera->AddRotation(totalDelta);
				}
#endif
			}
		}
	// --- 全体更新 ---
	CameraManager::GetInstance()->Update();
	particleSystem_->Update(deltaTime);
	objectManager_->Update(deltaTime); // オブジェクト一括更新

	if (boss_) {
		boss_->ActuallySpawnShards();
	}
  
	if (timeAttackUI_) {
		timeAttackUI_->Update(deltaTime);
	}
	//// 溜まった発生命令をもとに、GPUに計算（Compute Shader）を走らせる
	GPUParticleManager::GetInstance()->Update(deltaTime);
	for (auto& sprite : sprites_) {
		sprite->Update();
	}
	// =========================================================
		// 💀 ゲームオーバー画面のフェードインとメニュー選択
		// =========================================================
	if (player_ && player_->GetHp() <= 0.0f) {

		// プレイヤーの点滅演出(3.5秒)が終わったら処理開始
		if (player_->GetDeathTimer() > 3.5f) {

			// --- 1. テキストのフェードイン ---
			if (!isGameOverUiReady_) {
				bool allFadedIn = true;

				auto FadeInSprite = [deltaTime, &allFadedIn](Sprite* sprite) {
					if (sprite) {
						Vector4 color = sprite->GetColor();
						if (color.w < 1.0f) {
							color.w += deltaTime * 0.5f; // 徐々に不透明にする
							if (color.w > 1.0f) color.w = 1.0f;
							sprite->SetColor(color);
							allFadedIn = false; // まだ透明なやつがいればフラグを下ろす
						}
					}
					};

				FadeInSprite(gameOverTextSprite_);
				FadeInSprite(restartTextSprite_);
				FadeInSprite(titleTextSprite_);

				// 全部の文字が完全に出現したら準備完了！
				if (allFadedIn) {
					isGameOverUiReady_ = true;
				}
			}
			// --- 2. メニュー選択とシーン遷移 ---
			else {
				InputManager* input = InputManager::GetInstance();

				// 上下キーで項目切り替え (パッドの十字キーにも対応)
				if (input->IsKeyTriggered(DIK_UP) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_UP)) {
					currentGameOverMenuIndex_--;
					if (currentGameOverMenuIndex_ < 0) currentGameOverMenuIndex_ = (int)GameOverMenuIndex::Max - 1;
				}
				if (input->IsKeyTriggered(DIK_DOWN) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_DOWN)) {
					currentGameOverMenuIndex_++;
					if (currentGameOverMenuIndex_ >= (int)GameOverMenuIndex::Max) currentGameOverMenuIndex_ = 0;
				}

				// 選択中の項目をハイライト (選ばれてるほうを白、そうでないほうを少し暗くする)
				Vector4 normalColor = { 0.5f, 0.5f, 0.5f, 1.0f };
				Vector4 selectColor = { 1.0f, 1.0f, 1.0f, 1.0f };

				if (restartTextSprite_) {
					restartTextSprite_->SetColor(currentGameOverMenuIndex_ == (int)GameOverMenuIndex::Restart ? selectColor : normalColor);
				}
				if (titleTextSprite_) {
					titleTextSprite_->SetColor(currentGameOverMenuIndex_ == (int)GameOverMenuIndex::Title ? selectColor : normalColor);
				}
				// 決定ボタンで遷移！
				if (inputManager_->IsKeyTriggered(DIK_SPACE) || inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {

					// 共通のUI透明化ラムダ式
					auto SetAlphaZero = [](Sprite* sprite) {
						if (sprite) {
							Vector4 color = sprite->GetColor();
							color.w = 0.0f;
							sprite->SetColor(color);
						}
						};

					if (currentGameOverMenuIndex_ == (int)GameOverMenuIndex::Restart) {
						isRestartTransition_ = true;
						restartTimer_ = 0.0f;

						SetAlphaZero(gameOverTextSprite_);
						SetAlphaZero(restartTextSprite_);
						SetAlphaZero(titleTextSprite_);
					}
					else if (currentGameOverMenuIndex_ == (int)GameOverMenuIndex::Title) {
					
						isTitleTransition_ = true;
						restartTimer_ = 0.0f;

						SetAlphaZero(gameOverTextSprite_);
						SetAlphaZero(restartTextSprite_);
						SetAlphaZero(titleTextSprite_);
					}
				}
			}
		}
	}
	BulletManager::GetInstance()->Update(deltaTime);
	CollisionManager::GetInstance()->Update();
	MeshEffectManager::GetInstance()->Update(deltaTime);
	UpdateUI();

	// ========================================================
	// ★ ボス登場ムービー中の監視処理（時間で強制終了！）
	// ========================================================
	if (isBossMoviePlaying_ && boss_) {

		// ★ タイマーを進める！
		movieTimer_ += deltaTime;

		// プレイヤーがズレないように固定し続ける
		if (player_) {
			player_->SetTranslate(movieStoredPlayerPos_);
			player_->UpdateWorldMatrix();
		}

		// ====================================================
		// ★ 修正：全体時間を 3.0f から 4.0f に伸ばす！（1秒の待機が増えたため）
		// ====================================================
		if (movieTimer_ >= 4.0f) {
			isBossMoviePlaying_ = false;

			if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
				camera->EndOverride(1.0f);
			}

			if (player_) {
				player_->SetIsControlActive(true);
				player_->SetIsPhysicsActive(true);
			}

			boss_->StartBattle();
			if (timeAttackUI_) {
				timeAttackUI_->Start();
			}
		}
	}
	if (boss_) {
		// ボスが完全に消滅し、かつまだクリアシーケンスに入っていなければ開始
		if (boss_->IsCompletelyDead() && !isGameClearSequence_) {
			isGameClearSequence_ = true;
			gameClearTimer_ = 0.0f;

			// タイマーを止める
			if (timeAttackUI_) {
				timeAttackUI_->Stop();
			}
			float clearTime = timeAttackUI_->GetCurrentTime();
			SaveDataManager::GetInstance()->RecordClearTime(clearTime);

			DebugConsole::GetInstance()->AddLog("クリアタイムを保存しました: " + std::to_string(clearTime) + " 秒");
			DebugConsole::GetInstance()->AddLog("【GAME CLEAR】 クリア演出開始！");
		}
	}

	// クリアシーケンス中の処理
	if (isGameClearSequence_) {
		gameClearTimer_ += deltaTime;

		// ボス消滅から 2.0 秒後に「CLEAR」シーンへ遷移！
		if (gameClearTimer_ > 2.0f) {
			SceneManager::GetInstance()->ChangeScene("GAMECLEAR");
		}
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

	// =========================================================
	// ★ ここに「完全自動カリング」のロジックを挿入！
	// =========================================================
	Frustum frustum = camera->GetFrustum();
	Math math;
	int drawCount = 0;
	int totalCount = 0;

	auto IsVisible = [&](Object3d* obj) {
		if (!obj->GetIsVisible()) return false;

		// 1. オブジェクトから Model を取得 
		// ※もし Object3d に GetModel() が無ければ追加してください！ ( return model_; など )
		Model* model = obj->GetModel(); 
		if (!model) return true; // モデルが無い(空の)場合は安全のため描画を通す

		// 2. モデル本来のサイズ（ローカルAABB）を取得
		Vector3 lMin = model->GetLocalAabbMin();
		Vector3 lMax = model->GetLocalAabbMax();

		// 3. ローカルの「箱の8つの角（頂点）」を作成
		Vector3 corners[8] = {
			{lMin.x, lMin.y, lMin.z}, {lMax.x, lMin.y, lMin.z},
			{lMin.x, lMax.y, lMin.z}, {lMax.x, lMax.y, lMin.z},
			{lMin.x, lMin.y, lMax.z}, {lMax.x, lMin.y, lMax.z},
			{lMin.x, lMax.y, lMax.z}, {lMax.x, lMax.y, lMax.z}
		};

		// 4. ワールド行列を使って、8つの角すべてをゲーム空間の座標に変換する
		Matrix4x4 wm = obj->GetWorldMatrix();
		Vector3 wMin = { FLT_MAX, FLT_MAX, FLT_MAX };
		Vector3 wMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

		for (int i = 0; i < 8; ++i) {
			// math.Transform で座標に行列を掛ける
			Vector3 wPos = math.Transform(corners[i], wm); 

			// 変換後の8つの点から、ワールド空間での新たな min / max を見つける
			wMin.x = (std::min)(wMin.x, wPos.x);
			wMin.y = (std::min)(wMin.y, wPos.y);
			wMin.z = (std::min)(wMin.z, wPos.z);
			wMax.x = (std::max)(wMax.x, wPos.x);
			wMax.y = (std::max)(wMax.y, wPos.y);
			wMax.z = (std::max)(wMax.z, wPos.z);
		}

		// 回転したことで箱が大きくなっても問題なし！確実にオブジェクトを包み込むAABBが完成。
		return math.IntersectFrustumAABB(frustum, wMin, wMax);
	};

	// --- 1. 不透明描画 ---
	for (auto& obj : objects) {
		
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
		
		totalCount++;
		// ★ カリング判定！
		if (IsVisible(obj.get())) {
			obj->Draw(pointLightRes, spotLightRes);
			drawCount++;
		}
	}

	// --- 2. 中間描画 (弾・デバッグ) ---
	BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
	if (debugEditor_) debugEditor_->DrawPreview(pointLightResource_.Get(), spotLightResource_.Get());
	LightEditor::GetInstance()->Draw3D();
	MeshEffectManager::GetInstance()->Draw(pointLightRes, spotLightRes);
	
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
			totalCount++;
			// ★ カリング判定！
			if (IsVisible(obj.get())) {
				obj->Draw(pointLightRes, spotLightRes);
				drawCount++;
			}
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
				// ★ フォグの箱自体も画面外なら描画しないように最適化！
				if (IsVisible(obj.get())) {
					obj->DrawLocalFog(dxCommon_->GetDepthSrvHandle());
				}
			}
		}
		dxCommon_->PostDrawLocalFog();
	}

	// =======================================================
	// 5. GPUパーティクルの描画！
	// =======================================================
	dxCommon_->UpdateGrabTexture();

	GPUParticleManager::GetInstance()->Draw(
		dxCommon_->GetCommandList(),
		camera->GetViewMatrix(),
		camera->GetProjectionMatrix(),
		gpuParticleTexHandle_,
		dxCommon_->GetDepthSrvHandle()
	);



	// ★ カリングがどれくらい効いているか確認用のログ 
	// DebugConsole::GetInstance()->AddLog("DrawCount: " + std::to_string(drawCount) + " / Total: " + std::to_string(totalCount));
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
	if (timeAttackUI_ && hasBossAppeared_ && !isBossMoviePlaying_) {
		timeAttackUI_->Draw();
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
		// =======================================================
		// ボスUIの表示・非表示制御
		// ムービーが終了（!isBossMoviePlaying_）したら表示する
		// =======================================================
		float alpha = (hasBossAppeared_ && !isBossMoviePlaying_) ? 1.0f : 0.0f;

		auto SetAlpha = [](Sprite* s, float a) {
			if (s) { Vector4 c = s->GetColor(); c.w = a; s->SetColor(c); }
			};

		SetAlpha(bossHpBarSprite_, alpha);
		SetAlpha(barrierHpBarSprite_, alpha);
		SetAlpha(bossHpBackSprite_, alpha); 
		SetAlpha(bossNameSprite_, alpha);   
		// --- A. メインHPバーの同期 ---
		if (bossHpBarSprite_) {
			float hpRatio = std::clamp(boss_->GetHp() / boss_->GetMaxHp(), 0.0f, 1.0f);
			bossHpBarSprite_->SetSize({ bossHpBarMaxWidth_ * hpRatio, bossHpBarSprite_->GetSize().y });
		}

		// --- B. バリアHPバーの同期 ---
		if (barrierHpBarSprite_) {
			float bRatio = std::clamp(boss_->GetBarrierHp() / boss_->GetMaxBarrierHp(), 0.0f, 1.0f);
			barrierHpBarSprite_->SetSize({ barrierHpBarMaxWidth_ * bRatio, barrierHpBarSprite_->GetSize().y });
		}
	}
}

void GamePlayScene::StartBridgeDropMovie() {
	if (movieState_ != MovieState::kNone || hasBridgeDropped_) return;
	
	movieState_ = MovieState::kBridgeDrop;
	movieTimer_ = 0.0f;
    hasBridgeDropped_ = true;

	// CinematicCamera を探してムービーを再生する
	for (auto& obj : objectManager_->GetObjects()) {
		if (obj->GetName() == "Cinematic_Camera_Bridge") {
			if (obj->recorder_) {
                // Play(fileName, loop, isRelative, isCinematic)
				obj->recorder_->Play("bridge_movie", false, false, true);
			}
			break;
		}
	}
}

// ========================================================
// ★ ボス登場ムービーの開始処理
// ========================================================
void GamePlayScene::StartBossAppearanceMovie() {
	if (isBossMoviePlaying_ || !boss_ || hasBossAppeared_) return;

	isBossMoviePlaying_ = true;
	hasBossAppeared_ = true;
	movieTimer_ = 0.0f;

	// プレイヤーを固定
	if (player_) {
		movieStoredPlayerPos_ = player_->GetWorldPosition();
		player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
		player_->SetIsControlActive(false);
		player_->SetIsPhysicsActive(false);
	}

	// ====================================================
	// ★ 追加：a.json（カメラのアニメーション）を再生する！
	// ====================================================
	for (auto& obj : objectManager_->GetObjects()) {
		if (obj->GetName() == "Cinematic_Camera_Boss") { // ボス用のシネマティックカメラオブジェクトを用意しておく
			if (obj->recorder_) {
				// "a" という名前のJSONを再生！
				obj->recorder_->Play("a", false, false, true);
			}
			break;
		}
	}

	// ボス側にはカメラ移動以外の演出（ブロックが集まる等）だけをやらせる
	boss_->StartAppearance();
}