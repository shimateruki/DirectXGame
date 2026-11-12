#include "MeshRenderer.h"
#include "ModelManager.h"

void MeshRenderer::SetModel(Model* model) {
    visualModelOverride_ = nullptr;
    model_ = model;
    modelName_.clear();

    if (model_ && materialData_) {
        if (const Model::MaterialData* material = model_->GetPrimaryMaterialData()) {
            SetColor(material->baseColorFactor);
            materialData_->roughness = material->roughness;
            materialData_->metallic = material->metallic;
            materialData_->enableNormalMap = material->hasNormalMap ? 1 : 0;
        }
    }
}

void MeshRenderer::SetModel(const std::string& modelName) {
    visualModelOverride_ = nullptr;
    modelName_ = modelName;
    model_ = ModelManager::GetInstance()->LoadModel(modelName);

    if (model_ && materialData_) {
        if (const Model::MaterialData* material = model_->GetPrimaryMaterialData()) {
            SetColor(material->baseColorFactor);
            materialData_->roughness = material->roughness;
            materialData_->metallic = material->metallic;
            materialData_->enableNormalMap = material->hasNormalMap ? 1 : 0;
        }
    }
}

Model* MeshRenderer::ResolveDrawModel() const {
    return visualModelOverride_ ? visualModelOverride_ : model_;
}
