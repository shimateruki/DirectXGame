#include "ModelManager.h"
#include "ModelCommon.h"
#include <cassert>
#include <filesystem> 
#include <algorithm>
#include "ProfilerManager.h"
#include <chrono>


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
    if (models_.contains(modelName)) {
        return models_[modelName].get();
    }

    std::filesystem::path path(modelName);
    std::string parentPath = path.parent_path().string();
    std::replace(parentPath.begin(), parentPath.end(), '\\', '/');
    std::string stem = path.stem().string();
    std::string ext = path.extension().string();

    // 事前ロードは拡張子なしのキーで登録されるため、gltf指定でも同じモデルを再利用する
    if (!ext.empty()) {
        std::string extensionlessKey = parentPath.empty() ? stem : parentPath + "/" + stem;
        std::replace(extensionlessKey.begin(), extensionlessKey.end(), '\\', '/');
        if (models_.contains(extensionlessKey)) {
            return models_[extensionlessKey].get();
        }
        const bool isFolderMainModel = !parentPath.empty() &&
            std::filesystem::path(parentPath).filename().string() == stem;
        if (isFolderMainModel && models_.contains(parentPath)) {
            return models_[parentPath].get();
        }
    }

    std::string directoryPath;
    std::string fileName;

    // ==========================================
    // ★ 修正：モデル自身のフォルダ名(stem)もしっかりパスに含める！
    // ==========================================
    if (!parentPath.empty()) {
        // "enemy_core_shards" + "/" + "enemy_core1" + "/"
        directoryPath = kDefaultBaseDirectory + parentPath + "/" + stem + "/";
    }
    else {
        directoryPath = kDefaultBaseDirectory + stem + "/";
    }

    if (!ext.empty()) {
        fileName = path.filename().string();
        std::string legacyDirectory = directoryPath;
        std::string directDirectory = kDefaultBaseDirectory + (parentPath.empty() ? "" : parentPath + "/");
        if (std::filesystem::exists(legacyDirectory + fileName)) {
            directoryPath = legacyDirectory;
        }
        else if (std::filesystem::exists(directDirectory + fileName)) {
            directoryPath = directDirectory;
        }
    }
    else {
        const std::string objName = stem + ".obj";
        const std::string gltfName = stem + ".gltf";
        const std::string glbName = stem + ".glb";

        if (std::filesystem::exists(directoryPath + objName)) {
            fileName = objName;
        }
        else if (std::filesystem::exists(directoryPath + gltfName)) {
            fileName = gltfName;
        }
        else if (std::filesystem::exists(directoryPath + glbName)) {
            fileName = glbName;
        }
        else if (std::filesystem::exists(directoryPath)) {
            for (const auto& entry : std::filesystem::directory_iterator(directoryPath)) {
                if (!entry.is_regular_file()) continue;
                std::string candidateExt = entry.path().extension().string();
                if (candidateExt == ".obj" || candidateExt == ".gltf" || candidateExt == ".glb") {
                    fileName = entry.path().filename().string();
                    break;
                }
            }
            if (fileName.empty()) {
                fileName = objName;
            }
        }
        else {
            fileName = objName;
        }
    }

    std::string fullPath = directoryPath + fileName;
    OutputDebugStringA(("【LoadModel】探索パス: " + fullPath + "\n").c_str());

    auto start = std::chrono::high_resolution_clock::now();

    auto newModel = std::make_unique<Model>();
    newModel->Initialize(modelCommon_.get(), directoryPath, fileName);

    models_[modelName] = std::move(newModel);

    auto end = std::chrono::high_resolution_clock::now();
    float duration = std::chrono::duration<float, std::milli>(end - start).count();
    ProfilerManager::GetInstance()->RecordLoadTime("Model", modelName, duration);

    return models_[modelName].get();
}

bool ModelManager::ReloadModel(const std::string& modelName) {
    models_.erase(modelName);
    return LoadModel(modelName) != nullptr;
}

std::vector<std::string> ModelManager::GetLoadedModelNames() const {
    std::vector<std::string> names;
    for (const auto& pair : models_) {
        names.push_back(pair.first);
    }
    return names;
}

#include <Windows.h> // ★ OutputDebugStringA を使うために追加

// ---------------------------------------------------------
// ★修正版：フォルダ内を自動スキャンして一括ロード（ログ出力付き）
// ---------------------------------------------------------
void ModelManager::LoadAllModels() {
    if (!std::filesystem::exists(kDefaultBaseDirectory)) return;

    OutputDebugStringA("=== 【ModelManager】事前ロード開始 ===\n");

    for (const auto& entry : std::filesystem::recursive_directory_iterator(kDefaultBaseDirectory)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            if (ext == ".obj" || ext == ".gltf" || ext == ".glb") {

                // ファイルの本当の場所をそのまま使う！
                std::string dirPath = entry.path().parent_path().string() + "/";
                std::string fileName = entry.path().filename().string();

                // ==========================================
                // ★ 修正：LoadModelを使わず、相対パスから完璧な「登録キー」を自動生成する
                // ==========================================
                std::filesystem::path relPath = std::filesystem::relative(entry.path(), kDefaultBaseDirectory);
                std::string keyName = relPath.parent_path().string(); // フォルダまでのパスを取得
                std::replace(keyName.begin(), keyName.end(), '\\', '/'); // Windowsの「\」を「/」に統一

                // 登録キー例: "enemy_core_shards/enemy_core1"
                if (!models_.contains(keyName)) {
                    auto start = std::chrono::high_resolution_clock::now();

                    auto newModel = std::make_unique<Model>();
                    newModel->Initialize(modelCommon_.get(), dirPath, fileName);
                    models_[keyName] = std::move(newModel);

                    auto end = std::chrono::high_resolution_clock::now();
                    float duration = std::chrono::duration<float, std::milli>(end - start).count();
                    ProfilerManager::GetInstance()->RecordLoadTime("Model", keyName, duration);

                    std::string logMsg = "[事前ロード成功] キー: [" + keyName + "] パス: " + dirPath + fileName + "\n";
                    OutputDebugStringA(logMsg.c_str());
                }
            }
        }
    }
    OutputDebugStringA("=== 【ModelManager】事前ロード終了 ===\n");
}
