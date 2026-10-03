#include "game/actor/enemy/boss/core/BossCore.Internal.h"

using namespace BossCoreImplementation;

void BossCore::Initialize(Object3dCommon* common, const std::string& modelName) {
    // 親クラス(BaseEnemy)の初期化
    BaseEnemy::Initialize(common, modelName);

    // 乱数のシードを現在時刻で設定
    // これを呼ぶことで、毎回違う行動パターンを生成する
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    // 演出・攻撃パターン管理用ディレクターの生成
    director_ = std::make_unique<GhostDirector>();
    if (sceneManager_) {
        director_->Initialize(sceneManager_);
    }

    originalColor_ = GetColor();

    // ==========================================
    // レーザー用の円柱モデルを6本生成しておく
    // ==========================================
    for (int i = 0; i < 6; ++i) {
        auto beam = std::make_unique<Object3d>();
        beam->Initialize(common_);         // エンジンの共通データで初期化
        beam->SetModel("Primitives/cylinder");        // ※円柱モデルのファイル名に合わせてください！
        beam->SetScale({ 0.0f, 0.0f, 0.0f }); // 最初は見えないようにする
        beam->SetCollisionAttribute(0);    // 当たり判定なし
        beam->SetCollisionMask(0);
        laserBeams_.push_back(std::move(beam));
    }
}

void BossCore::Update(float deltaTime) {
    // ボスの全体スピード倍率
    // 以前の仕様に合わせ、ボスの時間経過を1.5倍速で進める
    deltaTime *= 1.5f;

    // ==========================================
    // 0キーで時間停止（ザ・ワールド）機能！
    // ==========================================
    InputManager* input = InputManager::GetInstance();

    if (input->IsKeyTriggered(DIK_1)) {
        s_isTimeStopped_ = !s_isTimeStopped_; // 押すたびに切り替え

        if (s_isTimeStopped_) {
            DebugConsole::GetInstance()->AddLog("【TIME STOP】 ボスの時間が止まった…！");
        }
        else {
            DebugConsole::GetInstance()->AddLog("【TIME RESUME】 時は動き出す！");
        }
    }

    // 時間停止中は、このフレームの経過時間を 0 に設定する
    if (s_isTimeStopped_) {
        deltaTime = 0.0f;
    }

    // ここから下はボス本体、装甲ブロック、攻撃フェーズをまとめて更新する
    float preTimer = colorResetTimer_;

    // 1. 基本更新（行列計算など）
    BaseEnemy::Update(deltaTime);

    if (target_ && damageCooldownTimer_ <= 0.0f && state_ != State::Weak) {
        // ① 腕の先などにある剣を確実に見つける！
        Object3d* weapon = FindWeaponRecursive(target_);

        // ② 振られている剣を見つけたら、ブロックとの当たり判定をチェック！
        if (weapon) {
            for (Object3d* block : armorBlocks_) {
                if (!block) continue;

                // 待機中のブロックは「壁(kGround)」になっているので、一瞬だけ判定を全開放
                uint32_t originalMask = block->GetCollisionMask();
                block->SetCollisionMask(0xFFFFFFFF);

                CollisionInfo info = block->CheckCollision(weapon);

                // 判定が終わったらすぐに元のマスクに戻す
                block->SetCollisionMask(originalMask);

                if (info.isColliding) {
                    TakeBarrierDamage(10.0f); // バリアに10ダメージ！
                    break;
                }
            }
        }
    }

    // ブロックの色を元に戻す処理
    if (preTimer > 0.0f && colorResetTimer_ <= 0.0f) {
        for (Object3d* block : armorBlocks_) {
            if (block) block->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        }
    }

    // ゲーム再生中は待機タイマーを常に進める
    if (SceneManager::GetInstance()->IsPlaying()) {
        s_globalIdleTimer += deltaTime; // deltaTimeが0ならタイマーも停止する
    }

    // 飛んでいるブロックの更新
    UpdateFlyingBlocks(deltaTime); // ★ 弾も空中でピタッと止まる！

    // アニメーションシーケンスを優先実行
    UpdateAnimationSequence(deltaTime); // アニメーションも現在位置で停止する

    if (animPhase_ != 0 && animPhase_ != 4) {
        return;
    }

    // ==========================================
     // 4. 通常のステート更新（アニメーション中以外に動く）
     // ==========================================
    if (isFirstFrame_) {
        originalColor_ = GetColor();
        // ① まず、子供たちの中から WarningArea を見つけ出す！
        for (Object3d* child : GetChildren()) {
            if (child->GetName() == "WarningArea") {
                warningArea_ = child;
                warningArea_->SetParent(nullptr); // 親子関係を解除
                warningArea_->SetScale({ 0.0f, 0.0f, 0.0f }); // 最初は見えないようにする

                // ==========================================
                // WarningArea の当たり判定を完全に処刑する（幽霊化）！！
                // ==========================================
                warningArea_->SetCollisionAttribute(0); // 自分の属性を「無し(0)」にする！
                warningArea_->SetCollisionMask(0);      // ぶつかる相手を「無し(0)」にする！

                // ==========================================
                // 絶対に斜めにならないよう、角度を完全に平ら(0,0,0)に強制リセット！
                // ==========================================
                warningArea_->SetRotation({ 0.0f, 0.0f, 0.0f });
                warningArea_->GetTransform()->isQuaternionMaster = false;

                // ==========================================
                // ② 見つけたら、装甲ブロックのリスト（armorBlocks_）から完全に追放（はく奪）する！
                // ==========================================
                for (auto it = armorBlocks_.begin(); it != armorBlocks_.end(); ) {
                    if (*it == warningArea_) {
                        it = armorBlocks_.erase(it); // リストから消去！
                    }
                    else {
                        ++it;
                    }
                }

                DebugConsole::GetInstance()->AddLog("🟢 WarningArea を取得し、リストからはく奪しました！");
                break;
            }
        }

        // ③ リストから「はく奪」した【後】に、状態をIdleに切り替える！
        // （これでWarningAreaは ChangeState の属性上書きに巻き込まれません！）
        ChangeState(State::Idle);
        isFirstFrame_ = false;
    }

    switch (state_) {
    case State::Idle:   UpdateIdle(deltaTime);   break;
    case State::Attack: UpdateAttack(deltaTime); break;
    case State::Weak:   UpdateWeak(deltaTime);   break;
    }
}

// =================================================================
// ステート(状態)管理
// =================================================================
void BossCore::ChangeState(State nextState) {
    state_ = nextState;

    // コア本体とブロックの属性設定
    // =======================================================
    uint32_t coreAttribute;
    if (state_ == State::Attack) {
        // 攻撃中：触れるとダメージ ＋ 絶対すり抜けない壁
        coreAttribute = kEnemyAttack | kGround;
    }
    else if (state_ == State::Weak) {
        // ダウン中：剣で殴れる ＋ 絶対すり抜けない壁
        coreAttribute = kEnemy | kGround;
    }
    else {
        // 待機中：ただの壁（体当たりしてもノーダメージ）
        coreAttribute = kGround;
    }

    // ① コア本体の属性を設定し、さらに「全ての子パーツ」にも属性を同期させる！
    // これにより、コアの見た目メッシュなどの子パーツに触れてもダメージを受けなくなります。
    SetCollisionAttribute(coreAttribute);
    for (Object3d* child : GetChildren()) {
        if (child) {
            child->SetCollisionAttribute(coreAttribute);
        }
    }

    if (state_ == State::Attack) {
        SetColor(originalColor_);
    }

    // ② 周りのブロックの属性（攻撃中は「敵の攻撃＋壁」、それ以外は「ただの壁」）
    // 装甲ブロックは上で子パーツとしても処理されますが、念のためここで確実に上書きします。
    uint32_t blockAttribute = (state_ == State::Attack) ? (kEnemyAttack | kGround) : kGround;

    for (Object3d* block : armorBlocks_) {
        if (block) {
            block->SetCollisionAttribute(blockAttribute);
            if (state_ == State::Attack) {
                block->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
            }
        }
    }

    // ==========================================
    // 状態移行に合わせて、攻撃アニメーションをスタート！
    // ==========================================
    switch (state_) {
    case State::Idle:
        // 待機状態に入った瞬間にタイマーをリセット（ここから2秒数え始めます）
        animTimer_ = 0.0f;
        break;

    case State::Attack: {
        // 順番に攻撃する。
        static int nextAttackPattern = 0;
        int nextAttack = nextAttackPattern;
        nextAttackPattern++;
        if (nextAttackPattern > 6) {
            nextAttackPattern = 1; // 4番の次は1番に戻る
        }
        nextAttackPattern = 6;

        // 選ばれた攻撃モードをセットし、対応するPhaseからアニメーション開始！
        attackMode_ = nextAttack;
        animTimer_ = 0.0f;
        shotCount_ = 0; // コンボや射撃カウントもリセット

        if (attackMode_ == 1) {
            animPhase_ = 1;
        }
        else if (attackMode_ == 2) {
            animPhase_ = 10;
        }
        else if (attackMode_ == 3) {
            animPhase_ = 20;
        }
        else if (attackMode_ == 4) {
            animPhase_ = 39;
        }
        else if (attackMode_ == 5) {
            animPhase_ = 50;
        }
        else if (attackMode_ == 6) {
            animPhase_ = 60; 
        }
        break;
    }

    case State::Weak:
        animTimer_ = 0.0f;
        break;
    }
}
// =================================================================
// 各ステートの個別更新処理
// =================================================================

void BossCore::UpdateIdle(float deltaTime) {
    // 一定時間待機後、攻撃ステートへ自動移行
    // ==========================================
    animTimer_ += deltaTime;

    // 例：2.0秒待機したら攻撃へ（タイクラーさんのお好みで秒数は調整してください！）
    if (animTimer_ >= 2.0f) {
        ChangeState(State::Attack);
    }
}

void BossCore::UpdateAttack(float deltaTime) {
    // アニメーションの終了を監視
    // UpdateAnimationSequence() の中で攻撃が完了すると attackMode_ が 0 に戻るのを利用。
    // ==========================================

    // 攻撃が完全に終了して待機状態(0)に戻ったら、Idleステートへ移行！
    if (attackMode_ == 0) {
        ChangeState(State::Idle);
    }
}

void BossCore::UpdateWeak(float deltaTime) {
    animTimer_ += deltaTime;

    // --- 演出1：ボス本体のシェイク ---
    float shakeX = std::sin(animTimer_ * 50.0f) * 0.05f;
    float shakeZ = std::cos(animTimer_ * 45.0f) * 0.05f;
    SetRotation({ shakeX, GetRotation().y, shakeZ });

    // --- 演出2：全体の色を暗くする ---
    SetColor({ 0.3f, 0.3f, 0.3f, 1.0f });

    // ブロックを飛散させるアニメーション
    // ==========================================
    float scatterDuration = 0.8f; // 0.8秒かけて弾け飛ぶ
    float t = std::min(animTimer_ / scatterDuration, 1.0f);
    float easeT = Easing::OutExpo(t); // 「バッ！」と広がる動き

    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
            // スタート位置からランダムな目標位置へ補間
            Vector3 currentPos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
            armorBlocks_[i]->SetTranslate(currentPos);

            // スタン中っぽく、各ブロックを適当な方向にゆっくり回転させ続ける
            Vector3 rot = armorBlocks_[i]->GetRotation();
            rot.x += 1.0f * deltaTime;
            rot.y += 1.5f * deltaTime;
            armorBlocks_[i]->SetRotation(rot);
            armorBlocks_[i]->SetColor({ 0.3f, 0.3f, 0.3f, 1.0f }); // ブロックも暗くする
        }
    }

    // --- 3秒経過で復帰 ---
    if (animTimer_ >= 10.0f) {
        animTimer_ = 0.0f;
        SetRotation({ 0.0f, GetRotation().y, 0.0f });
        SetColor(originalColor_);
        ChangeState(State::Idle); // ここで各ブロックは IdleOrbit に戻るようになります
    }
}

// 攻撃モードごとの長いフェーズ演出をここで一括管理する
void BossCore::UpdateFlyingBlocks(float deltaTime) {
    // 飛翔管理には本体の内部状態を渡さず、回収時の見た目だけを指定します。
    armorFlight_.Update(*this, target_, armorBlocks_.size(), deltaTime, [](Object3d* block, int index) {
        const OrbitData orbit = GetIdleOrbit(index);
        block->SetTranslate(orbit.pos);
        block->SetScale(orbit.scale);
        block->SetRotation(orbit.rot);
        block->GetTransform()->isQuaternionMaster = false;
    });
}

// ==========================================
// バリアのダメージ＆スタン(ダウン)処理
// ==========================================
// バリアHPへのダメージとダウン遷移
void BossCore::TakeBarrierDamage(float damage) {
    barrierHp_ -= damage;

    // デバッグコンソールに分かりやすく残りHPを表示！
    DebugConsole::GetInstance()->AddLog("【HIT!】 Barrier Damaged! 残りHP: " + std::to_string(barrierHp_) + " / " + std::to_string(maxBarrierHp_));

    // 無敵タイマーと色リセットタイマーをセット
    damageCooldownTimer_ = 0.5f;
    colorResetTimer_ = 0.15f;

    // 全ブロックを赤く光らせてダメージを受けた感を出す！
    for (Object3d* block : armorBlocks_) {
        if (block) block->SetColor({ 1.0f, 0.0f, 0.0f, 1.0f });
    }

    if (barrierHp_ <= 0.0f) {
        DebugConsole::GetInstance()->AddLog("Barrier BROKEN!");
        barrierHp_ = maxBarrierHp_;

        animPhase_ = 0;
        attackMode_ = 0;
        animTimer_ = 0.0f;

        // ① 弾けている最中のブロック（射撃中など）を全て停止させる
        armorFlight_.Clear();

        // ② 拡散用のデータを準備する
        blockStartPos_.clear();
        blockTargetPos_.clear();

        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            Object3d* block = armorBlocks_[i];
            if (block) {
                // 親子関係は維持したまま（ローカル座標で計算）
                block->SetParent(this);

                // 現在の位置をスタート地点として保存
                blockStartPos_.push_back(block->GetTranslate());

                // 弾け飛ぶ方向をランダムに決定（半径 10.0f 〜 15.0f の範囲）
                float angle = (static_cast<float>(rand()) / RAND_MAX) * 2.0f * std::numbers::pi_v<float>;
                float distance = 10.0f + (static_cast<float>(rand()) / RAND_MAX) * 5.0f;
                float height = ((static_cast<float>(rand()) / RAND_MAX) * 10.0f) - 5.0f; // 上下にもばらけさせる

                Vector3 scatterPos = {
                    std::cos(angle) * distance,
                    height,
                    std::sin(angle) * distance
                };
                blockTargetPos_.push_back(scatterPos);
            }
        }

        SetRotation({ 0.0f, GetRotation().y, 0.0f });
        ChangeState(State::Weak);
    }
}



bool BossCore::OnCollision(Object3d* other) {
    uint32_t attribute = other->GetCollisionAttribute();

    // プレイヤーの剣(kPlayerAttack)が当たった場合は、衝突判定を確認して
    // BaseEnemy のダメージ処理に委譲する（＝剣でダメージを受ける）
    if (attribute & kPlayerAttack) {
        CollisionInfo info = CheckCollision(other);
        if (!info.isColliding) {
            return false;
        }
        // ここで BaseEnemy::OnCollision を呼ぶことで DamageEvent 発行など既存の処理を再利用
        return BaseEnemy::OnCollision(other);
    }

    // それ以外は従来通り BaseEnemy に委譲
    return BaseEnemy::OnCollision(other);
}
