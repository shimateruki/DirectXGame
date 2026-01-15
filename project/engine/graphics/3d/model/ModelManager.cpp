#include "ModelManager.h"
#include "ModelCommon.h"
#include <cassert>
#include <filesystem> 

// 静的メンバ変数の実体定義
ModelManager* ModelManager::instance = nullptr;
const std::string ModelManager::kDefaultBaseDirectory = "Resources/3DModel/";
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
    // 1. 既に読み込んでいるかチェック
    if (models_.contains(modelName)) {
        return models_[modelName].get();
    }

    std::string directoryPath;
    std::string fileName;

    // 2. 拡張子があるかチェック (.obj, .gltf, .glb)
    if (modelName.find(".obj") != std::string::npos ||
        modelName.find(".gltf") != std::string::npos ||
        modelName.find(".glb") != std::string::npos) {


        // 拡張子抜きの名前（フォルダ名）を取得
        std::string folderName = std::filesystem::path(modelName).stem().string();

        // ディレクトリパス: "resources/" + "sampleBlock" + "/"
        directoryPath = kDefaultBaseDirectory + folderName + "/";
        fileName = modelName;
    } else {
        // Bパターン: 拡張子がない場合 (既存のOBJ互換用)
        // 例: "Player" -> "resources/Player/Player.obj"
        directoryPath = kDefaultBaseDirectory + modelName + "/";
        fileName = modelName + ".obj";
    }

    // 3. 読み込み実行
    auto newModel = std::make_unique<Model>();
    newModel->Initialize(modelCommon_.get(), directoryPath, fileName);

    // 4. 登録
    models_[modelName] = std::move(newModel);
    return models_[modelName].get();
}

std::vector<std::string> ModelManager::GetLoadedModelNames() const {
    std::vector<std::string> names;
    for (const auto& pair : models_) {
        names.push_back(pair.first);
    }
    return names;
}