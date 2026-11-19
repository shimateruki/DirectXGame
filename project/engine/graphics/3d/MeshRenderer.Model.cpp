#include "MeshRenderer.h"
#include "ModelManager.h"

namespace engine {

void MeshRenderer::SetModel(Model* model) {
    visualModelOverride_ = nullptr;
    model_ = model;
    // ポインターで直接指定した場合、以前の名前でモデルを参照しないよう名前を破棄します。
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
    // 名前からモデルを取得し、主マテリアルの初期値を描画用の設定へコピーします。
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
    // 描画差し替えが設定されている間はそちらを使い、元モデルへの参照は保持します。
    return visualModelOverride_ ? visualModelOverride_ : model_;
}

} // namespace engine
