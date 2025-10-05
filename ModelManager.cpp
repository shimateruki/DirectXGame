#include "ModelManager.h"
#include "ModelCommon.h"
#include <cassert>

// �ÓI�����o�ϐ��̎��̒�`
ModelManager* ModelManager::instance = nullptr;

ModelManager* ModelManager::GetInstance() {
    if (instance == nullptr) {
        instance = new ModelManager();
    }
    return instance;
}

void ModelManager::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    modelCommon_ = std::make_unique<ModelCommon>();
    modelCommon_->Initialize(dxCommon);
}

void ModelManager::Finalize() {
    // �C���X�^���X�����
    delete instance;
    instance = nullptr;
}

void ModelManager::LoadModel(const std::string& filePath) {
    // ���ɓǂݍ��ݍς݂̏ꍇ�͑������^�[��
    if (models_.contains(filePath)) {
        return;
    }

    // ���f���̐����Ə�����
    auto model = std::make_unique<Model>();
    // �t�@�C���p�X����f�B���N�g���p�X�ƃt�@�C�����𕪊�
    std::string directoryPath = filePath.substr(0, filePath.find_last_of('/'));
    std::string fileName = filePath.substr(filePath.find_last_of('/') + 1);

    // Model�N���X��Initialize���Ăяo��
    model->Initialize(modelCommon_.get(), directoryPath, fileName);

    // �R���e�i�Ɋi�[
    models_.insert(std::make_pair(filePath, std::move(model)));
}

Model* ModelManager::FindModel(const std::string& filePath) {
    // filePath�ɑΉ����郂�f�����R���e�i�ɂ���ΕԂ�
    if (models_.contains(filePath)) {
        return models_.at(filePath).get();
    }
    // ������Ȃ����nullptr��Ԃ�
    return nullptr;
}