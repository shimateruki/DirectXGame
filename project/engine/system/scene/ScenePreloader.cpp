#include "ScenePreloader.h"

#include "ModelManager.h"
#include "TextureManager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <Windows.h>

namespace {
std::string NormalizePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.rfind("./", 0) == 0) {
        path.erase(0, 2);
    }
    return path;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

template <class T, class KeySelector>
void AddUnique(std::vector<T>& values, T value, KeySelector keySelector) {
    const auto key = keySelector(value);
    if (key.empty()) {
        return;
    }
    const auto found = std::find_if(values.begin(), values.end(), [&](const T& candidate) {
        return keySelector(candidate) == key;
    });
    if (found == values.end()) {
        values.push_back(std::move(value));
    }
}

void AddUniquePath(std::vector<std::string>& paths, const std::string& path) {
    AddUnique(paths, NormalizePath(path), [](const std::string& value) { return value; });
}

bool IsTextureKey(const std::string& lowerKey) {
    return lowerKey == "texture" ||
        lowerKey.find("texturepath") != std::string::npos ||
        lowerKey.find("texturename") != std::string::npos ||
        lowerKey.find("normalmap") != std::string::npos ||
        lowerKey.find("ormmap") != std::string::npos ||
        lowerKey.find("albedomap") != std::string::npos ||
        lowerKey.find("emissivemap") != std::string::npos ||
        lowerKey.find("maskmap") != std::string::npos;
}

bool IsLinearTextureKey(const std::string& lowerKey) {
    return lowerKey.find("normal") != std::string::npos ||
        lowerKey.find("orm") != std::string::npos ||
        lowerKey.find("mask") != std::string::npos ||
        lowerKey.find("roughness") != std::string::npos ||
        lowerKey.find("metallic") != std::string::npos;
}

bool IsTextureFile(const std::filesystem::path& path) {
    const std::string extension = ToLower(path.extension().string());
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
        extension == ".tga" || extension == ".dds" || extension == ".hdr";
}

void CollectSharedParticleTextures(ScenePreloadData& data) {
    const std::filesystem::path directory = "Resources/sprite/particle";
    std::error_code error;
    if (!std::filesystem::exists(directory, error) || error) {
        return;
    }

    for (std::filesystem::directory_iterator iterator(
             directory,
             std::filesystem::directory_options::skip_permission_denied,
             error), end;
         iterator != end;
         iterator.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        if (!iterator->is_regular_file(error) || error || !IsTextureFile(iterator->path())) {
            error.clear();
            continue;
        }

        SceneLoadManifest::TextureRequest request;
        request.path = NormalizePath(iterator->path().string());
        AddUnique(data.textures, std::move(request), [](const SceneLoadManifest::TextureRequest& item) {
            return item.path + (item.linear ? "|linear" : "|color");
        });
    }
}

void CollectSharedFadeTextures(ScenePreloadData& data) {
    const std::filesystem::path directory = "Resources/sprite/fade/crown_iris";
    std::error_code error;
    if (!std::filesystem::exists(directory, error) || error) {
        return;
    }

    for (std::filesystem::directory_iterator iterator(
             directory,
             std::filesystem::directory_options::skip_permission_denied,
             error), end;
         iterator != end;
         iterator.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        if (!iterator->is_regular_file(error) || error || !IsTextureFile(iterator->path())) {
            error.clear();
            continue;
        }

        SceneLoadManifest::TextureRequest request;
        request.path = NormalizePath(iterator->path().string());
        AddUnique(data.textures, std::move(request), [](const SceneLoadManifest::TextureRequest& item) {
            return item.path + (item.linear ? "|linear" : "|color");
        });
    }
}

void AddSharedTexture(
    ScenePreloadData& data,
    const std::string& path,
    bool linear = false) {
    SceneLoadManifest::TextureRequest request{ NormalizePath(path), linear };
    AddUnique(data.textures, std::move(request), [](const SceneLoadManifest::TextureRequest& item) {
        return item.path + (item.linear ? "|linear" : "|color");
    });
}

void CollectDependencies(
    const nlohmann::json& value,
    const std::string& propertyName,
    ScenePreloadData& data) {
    if (value.is_object()) {
        for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
            CollectDependencies(iterator.value(), iterator.key(), data);
        }
        return;
    }
    if (value.is_array()) {
        for (const auto& element : value) {
            CollectDependencies(element, propertyName, data);
        }
        return;
    }
    if (!value.is_string()) {
        return;
    }

    const std::string stringValue = value.get<std::string>();
    if (stringValue.empty()) {
        return;
    }

    const std::string lowerKey = ToLower(propertyName);
    if (lowerKey == "modelname") {
        AddUniquePath(data.modelNames, stringValue);
        return;
    }

    if (IsTextureKey(lowerKey)) {
        SceneLoadManifest::TextureRequest request;
        request.path = NormalizePath(stringValue);
        request.linear = IsLinearTextureKey(lowerKey);
        AddUnique(data.textures, std::move(request), [](const SceneLoadManifest::TextureRequest& item) {
            return item.path + (item.linear ? "|linear" : "|color");
        });
    }
}

void ExpandObjectLayoutPaths(const std::string& path, std::vector<std::string>& jsonPaths) {
    const std::filesystem::path sourcePath(NormalizePath(path));
    std::filesystem::path basePath = sourcePath;
    basePath.replace_extension();

    const std::string base = NormalizePath(basePath.string());
    const std::vector<std::string> splitPaths = {
        base + "_player.json",
        base + "_enemy.json",
        base + "_object.json",
        base + "_camera.json"
    };

    bool hasSplitFile = false;
    for (const std::string& splitPath : splitPaths) {
        if (std::filesystem::exists(splitPath)) {
            hasSplitFile = true;
            break;
        }
    }

    if (!hasSplitFile) {
        AddUniquePath(jsonPaths, path);
        return;
    }

    for (const std::string& splitPath : splitPaths) {
        if (std::filesystem::exists(splitPath)) {
            AddUniquePath(jsonPaths, splitPath);
        }
    }
}

bool LoadJsonDocument(const std::string& path, nlohmann::json& document, std::string& warning) {
    std::ifstream file(path);
    if (!file.is_open()) {
        warning = "Scene preload could not open JSON: " + path;
        return false;
    }

    try {
        file >> document;
        return true;
    }
    catch (const std::exception& exception) {
        warning = "Scene preload JSON parse failed: " + path + " (" + exception.what() + ")";
        return false;
    }
}
}

void SceneLoadManifest::AddObjectLayout(const std::string& path) {
    AddUniquePath(objectLayoutPaths, path);
}

void SceneLoadManifest::AddSpriteLayout(const std::string& path) {
    AddUniquePath(spriteLayoutPaths, path);
}

void SceneLoadManifest::AddJson(const std::string& path) {
    AddUniquePath(additionalJsonPaths, path);
}

void SceneLoadManifest::AddModel(const std::string& modelName) {
    AddUniquePath(modelNames, modelName);
}

void SceneLoadManifest::AddTexture(const std::string& path, bool linear) {
    TextureRequest request{ NormalizePath(path), linear };
    AddUnique(textures, std::move(request), [](const TextureRequest& item) {
        return item.path + (item.linear ? "|linear" : "|color");
    });
}

void ScenePreloadProgress::Begin(std::size_t totalTasks) {
    completedTasks_.store(0, std::memory_order_relaxed);
    totalTasks_.store((std::max)(totalTasks, std::size_t{ 1 }), std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(taskMutex_);
    currentTask_.clear();
}

void ScenePreloadProgress::Advance(const std::string& currentTask) {
    completedTasks_.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(taskMutex_);
    currentTask_ = currentTask;
}

void ScenePreloadProgress::AddTasks(std::size_t taskCount) {
    totalTasks_.fetch_add(taskCount, std::memory_order_relaxed);
}

void ScenePreloadProgress::Finish() {
    completedTasks_.store(totalTasks_.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

float ScenePreloadProgress::GetRatio() const {
    const std::size_t total = totalTasks_.load(std::memory_order_relaxed);
    const std::size_t completed = completedTasks_.load(std::memory_order_relaxed);
    if (total == 0) {
        return 1.0f;
    }
    return (std::min)(1.0f, static_cast<float>(completed) / static_cast<float>(total));
}

std::string ScenePreloadProgress::GetCurrentTask() const {
    std::lock_guard<std::mutex> lock(taskMutex_);
    return currentTask_;
}

bool ScenePreloadData::TakeJson(const std::string& path, nlohmann::json& destination) {
    const auto found = jsonDocuments.find(NormalizePath(path));
    if (found == jsonDocuments.end()) {
        return false;
    }
    destination = std::move(found->second);
    jsonDocuments.erase(found);
    return true;
}

std::shared_ptr<ScenePreloadData> ScenePreloader::Prepare(
    const SceneLoadManifest& manifest,
    const std::shared_ptr<ScenePreloadProgress>& progress) {
    auto result = std::make_shared<ScenePreloadData>();

    std::vector<std::string> jsonPaths;
    for (const std::string& path : manifest.objectLayoutPaths) {
        ExpandObjectLayoutPaths(path, jsonPaths);
    }
    for (const std::string& path : manifest.spriteLayoutPaths) {
        AddUniquePath(jsonPaths, path);
    }
    for (const std::string& path : manifest.additionalJsonPaths) {
        AddUniquePath(jsonPaths, path);
    }

    if (progress) {
        progress->Begin(jsonPaths.size());
    }

    for (const std::string& path : jsonPaths) {
        nlohmann::json document;
        std::string warning;
        if (LoadJsonDocument(path, document, warning)) {
            CollectDependencies(document, "", *result);
            result->jsonDocuments.emplace(NormalizePath(path), std::move(document));
        }
        else if (!warning.empty()) {
            result->warnings.push_back(std::move(warning));
        }

        if (progress) {
            progress->Advance(path);
        }
    }

    for (const std::string& modelName : manifest.modelNames) {
        AddUniquePath(result->modelNames, modelName);
    }
    for (const SceneLoadManifest::TextureRequest& texture : manifest.textures) {
        AddUnique(result->textures, texture, [](const SceneLoadManifest::TextureRequest& item) {
            return NormalizePath(item.path) + (item.linear ? "|linear" : "|color");
        });
    }
    CollectSharedParticleTextures(*result);
    CollectSharedFadeTextures(*result);
    AddSharedTexture(*result, "Resources/sprite/effect/noise0.png", true);
    AddSharedTexture(*result, "Resources/texture/lut/soft_adventure_lut.png");

    std::sort(result->modelNames.begin(), result->modelNames.end());
    std::sort(result->textures.begin(), result->textures.end(), [](const auto& left, const auto& right) {
        if (left.path == right.path) {
            return left.linear < right.linear;
        }
        return left.path < right.path;
    });

    if (progress) {
        progress->AddTasks(result->textures.size() + result->modelNames.size());
    }

    // WIC画像デコードを行うため、ワーカースレッド側でCOMを初期化します。
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (const SceneLoadManifest::TextureRequest& texture : result->textures) {
        TextureManager::GetInstance()->Prepare(
            texture.path,
            texture.linear ? TextureManager::TextureColorSpace::Linear
                           : TextureManager::TextureColorSpace::Auto);
        if (progress) {
            progress->Advance(texture.path);
        }
    }
    for (const std::string& modelName : result->modelNames) {
        ModelManager::GetInstance()->PrepareModel(modelName);
        if (progress) {
            progress->Advance(modelName);
        }
    }
    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }

    if (progress) {
        progress->Finish();
    }
    return result;
}
