#include "ModelManager.h"
#include "ModelCommon.h"
#include <cassert>

// 静的メンバ変数の実体定義
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
    // インスタンスを解放
    delete instance;
    instance = nullptr;
}

void ModelManager::LoadModel(const std::string& filePath) {
    // 既に読み込み済みの場合は早期リターン
    if (models_.contains(filePath)) {
        return;
    }

    // モデルの生成と初期化
    auto model = std::make_unique<Model>();
    // ファイルパスからディレクトリパスとファイル名を分割
    std::string directoryPath = filePath.substr(0, filePath.find_last_of('/'));
    std::string fileName = filePath.substr(filePath.find_last_of('/') + 1);

    // ModelクラスのInitializeを呼び出し
    model->Initialize(modelCommon_.get(), directoryPath, fileName);

    // コンテナに格納
    models_.insert(std::make_pair(filePath, std::move(model)));
}

Model* ModelManager::FindModel(const std::string& filePath) {
    // filePathに対応するモデルがコンテナにあれば返す
    if (models_.contains(filePath)) {
        return models_.at(filePath).get();
    }
    // 見つからなければnullptrを返す
    return nullptr;
}