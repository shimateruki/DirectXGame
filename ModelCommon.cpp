//================================================================
// ModelCommon.cpp (�V�K�쐬)
//================================================================
#include "ModelCommon.h"
#include <cassert>

void ModelCommon::Initialize(DirectXCommon* dxCommon) {
    // ������NULL�`�F�b�N
    assert(dxCommon);
    // �����o�[�ϐ��ɐݒ�
    dxCommon_ = dxCommon;
}