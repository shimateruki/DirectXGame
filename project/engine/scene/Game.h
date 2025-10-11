#pragma once

#include "engine/base/Framework.h" // 基底クラスをインクルード
#include "engine/audio/AudioPlayer.h"
#include "engine/3d/debugCamera.h"
#include "engine/3d/Object3dCommon.h"
#include "engine/2d/SpriteCommon.h"
#include "engine/3d/Object3d.h"
#include "engine/2d/Sprite.h"

#include <memory>
#include <vector>

// Frameworkを継承した、このゲーム独自のクラス
class Game : public Framework {
public:
   /// <summary>
   /// 初期化
   /// </summary>
   void Initialize() override;

   /// <summary>
   /// 終了処理
   /// </summary>
   void Finalize() override;

protected:
   /// <summary>
   /// 毎フレームの更新処理
   /// </summary>
   void Update() override;

   /// <summary>
   /// 描画処理
   /// </summary>
   void Draw() override;

private:
   

   // --- 描画オブジェクト関連 ---
   std::unique_ptr<DebugCamera> debugCamera_;
   std::unique_ptr<Object3dCommon> object3dCommon_;
   std::unique_ptr<SpriteCommon> spriteCommon_;

   std::vector<std::unique_ptr<Object3d>> objects_;
   std::unique_ptr<Sprite> sprite_;

   AudioPlayer::AudioHandle bgmHandle_ = 0;
};