#pragma once

// �C��: Framework.h �̃p�X�𐳂������̂ɕύX
#include "engine/base/Framework.h" // ���N���X���C���N���[�h
#include "engine/audio/AudioPlayer.h"
#include "engine/3d/debugCamera.h"
#include "engine/3d/Object3dCommon.h"
#include "engine/2d/SpriteCommon.h"
#include "engine/3d/Object3d.h"
#include "engine/2d/Sprite.h"

#include <memory>
#include <vector>

// Framework���p�������A���̃Q�[���Ǝ��̃N���X
class Game : public Framework {
public:
   /// <summary>
   /// ������
   /// </summary>
   void Initialize() override;

   /// <summary>
   /// �I������
   /// </summary>
   void Finalize() override;

protected:
   /// <summary>
   /// ���t���[���̍X�V����
   /// </summary>
   void Update() override;

   /// <summary>
   /// �`�揈��
   /// </summary>
   void Draw() override;

private:
   // --- �I�[�f�B�I�֘A ---
   std::unique_ptr<AudioPlayer> audioPlayer_;
   Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
   IXAudio2MasteringVoice* masteringVoice_ = nullptr;
   SoundData soundData1_{};
   bool audioPlayedOnce_ = false;

   // --- �`��I�u�W�F�N�g�֘A ---
   std::unique_ptr<DebugCamera> debugCamera_;
   std::unique_ptr<Object3dCommon> object3dCommon_;
   std::unique_ptr<SpriteCommon> spriteCommon_;

   std::vector<std::unique_ptr<Object3d>> objects_;
   std::unique_ptr<Sprite> sprite_;
};