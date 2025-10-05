#pragma once

#include "Model.h"
#include <string>
#include <map>
#include <memory>

class DirectXCommon;
class ModelCommon;

// ���f���f�[�^���Ǘ�����N���X�i�V���O���g���j
class ModelManager {
public:
    /// <summary>
    /// �V���O���g���C���X�^���X�̎擾
    /// </summary>
    static ModelManager* GetInstance();

    /// <summary>
    /// ������
    /// </summary>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// �I������
    /// </summary>
    void Finalize();

    /// <summary>
    /// ���f���̓ǂݍ���
    /// </summary>
    /// <param name="filePath">���f���̃t�@�C���p�X</param>
    void LoadModel(const std::string& filePath);

    /// <summary>
    /// ���f���f�[�^���擾
    /// </summary>
    /// <param name="filePath">���f���̃t�@�C���p�X</param>
    /// <returns>�����������f���f�[�^�B������Ȃ����nullptr</returns>
    Model* FindModel(const std::string& filePath);

private:
    ModelManager() = default;
    ~ModelManager() = default;
    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;

private:
    static ModelManager* instance;

    std::unique_ptr<ModelCommon> modelCommon_;
    std::map<std::string, std::unique_ptr<Model>> models_;
};