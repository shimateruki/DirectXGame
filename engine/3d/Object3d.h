#pragma once

#include "engine/base/Math.h"
#include "engine/3d/Object3dCommon.h"
#include "engine/3d/Model.h"
#include <wrl.h>
#include <string> // std::string ���g�����߂ɃC���N���[�h

class Object3d {
public: // �����o�N���X�i�\���́j
    struct Transform {
        Vector3 scale;
        Vector3 rotate;
        Vector3 translate;
    };

    struct TransformationMatrix {
        Matrix4x4 WVP;
        Matrix4x4 world;
    };

    struct DirectionalLight {
        Vector4 color;
        Vector3 direction;
        float intensity;
    };

public: // �����o�֐�
    void Initialize(Object3dCommon* common);
    void Update(const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix);
    void Draw(ID3D12GraphicsCommandList* commandList);

    // --- �Z�b�^�[ ---
    void SetModel(Model* model) { model_ = model; }
    // ������ �t�@�C���p�X�Ń��f����ݒ肷��I�[�o�[���[�h��ǉ� ������
    void SetModel(const std::string& filePath);

    // ������ Transform�̃Z�b�^�[��ǉ� ������
    void SetScale(const Vector3& scale) { transform_.scale = scale; }
    void SetRotate(const Vector3& rotate) { transform_.rotate = rotate; }
    void SetTranslate(const Vector3& translate) { transform_.translate = translate; }

    // --- �Q�b�^�[ ---
    Transform* GetTransform() { return &transform_; }
    Model::Material* GetMaterial() { return model_ ? model_->GetMaterial() : nullptr; }
    DirectionalLight* GetDirectionalLight() { return directionalLightData_; }

    // ������ Transform�̃Q�b�^�[��ǉ� ������
    const Vector3& GetScale() const { return transform_.scale; }
    const Vector3& GetRotate() const { return transform_.rotate; }
    const Vector3& GetTranslate() const { return transform_.translate; }


private: // �����o�ϐ�
    Object3dCommon* common_ = nullptr;
    Model* model_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;

    Transform transform_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
};