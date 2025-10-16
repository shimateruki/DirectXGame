#include "ModelManager.h"
#include "ModelCommon.h"
#include <cassert>

// 静的メンバ変数の実体定義
ModelManager* ModelManager::instance = nullptr;
const std::string ModelManager::kDefaultBaseDirectory = "resouces/3DModel/";
const std::string ModelManager::kDefaultModelExtension = ".obj";

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
    models_.clear(); // モデルを全て解放
    modelCommon_.reset(); // ModelCommonを解放
    delete instance;
    instance = nullptr;
}


Model* ModelManager::LoadModel(const std::string& modelName) {
    // 1. 過去に読み込み済みのモデルか検索
    auto it = models_.find(modelName);
    if (it != models_.end()) {
        return it->second.get();
    }

    // 2. 新しく読み込む
    const std::string filePath = kDefaultBaseDirectory + modelName + "/" + modelName + kDefaultModelExtension;

    // ファイルパスからディレクトリとファイル名を分割
    std::string directoryPath = kDefaultBaseDirectory + modelName;
    std::string fileName = modelName + kDefaultModelExtension;

    auto newModel = std::make_unique<Model>();
    // 注意: Model::Initializeにはディレクトリパスとファイル名を渡す
    newModel->Initialize(modelCommon_.get(), directoryPath, fileName);

    // 3. 読み込んだモデルを登録して返す
    auto result = models_.emplace(modelName, std::move(newModel));
    return result.first->second.get();
}