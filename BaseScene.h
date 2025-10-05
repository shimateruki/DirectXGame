#pragma once

#include <string>

// �V�[���̊��N���X�i�C���^�[�t�F�[�X�j
class BaseScene {
public:
    virtual ~BaseScene() = default;

    /// <summary>
    /// ������
    /// </summary>
    virtual void Initialize() = 0;

    /// <summary>
    /// �I������
    /// </summary>
    virtual void Finalize() = 0;

    /// <summary>
    /// ���t���[���X�V
    /// </summary>
    virtual void Update() = 0;

    /// <summary>
    /// �`��
    /// </summary>
    virtual void Draw() = 0;

    /// <summary>
    /// ���̃V�[�������N�G�X�g
    /// </summary>
    /// <param name="sceneName">���̃V�[����</param>
    void RequestNextScene(const std::string& sceneName) { nextSceneName_ = sceneName; }

    /// <summary>
    /// ���̃V�[�������擾
    /// </summary>
    /// <returns>���̃V�[����</returns>
    const std::string& GetNextSceneName() const { return nextSceneName_; }

protected:
    std::string nextSceneName_; // ���̃V�[����
};