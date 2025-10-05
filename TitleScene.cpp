#include "TitleScene.h"
#include "Framework.h" // InputManager���擾���邽�߂ɃC���N���[�h
#include "externals/imgui/imgui.h"
#include "Game.h"
// Game�N���X����InputManager�̃|�C���^���󂯎�邽�߂̑O���錾


void TitleScene::Initialize() {
    // Game�N���X����InputManager�̃|�C���^���擾
    // �����ӁF���̕��@�͈ꎞ�I�Ȃ��̂ŁA���ǂ��݌v�͈ˑ����̒���(DI)�R���e�i�Ȃǂ��g�����Ƃł�
    if (auto* game = dynamic_cast<Game*>(Game::GetInstance())) {
        inputManager_ = game->GetInputManager();
    }
}

void TitleScene::Finalize() {
    // ���̃V�[���Ŋm�ۂ���������������Ή������
}

void TitleScene::Update() {
    // �X�y�[�X�L�[�������ꂽ��Q�[���v���C�V�[����
    if (inputManager_ && inputManager_->IsKeyTriggered(DIK_SPACE)) {
        RequestNextScene("GAMEPLAY");
    }
}

void TitleScene::Draw() {
    // ImGui���g�����V���v���ȃ^�C�g���\��
    ImGui::Begin("Title");
    ImGui::Text("This is Title Scene.");
    ImGui::Text("Press SPACE to Start!");
    ImGui::End();
}