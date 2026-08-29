#include "ScenePreloader.h"

#include "ModelManager.h"
#include "AudioPlayer.h"
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
bool IsAudioFile(const std::filesystem::path& path) {
    const std::string extension = ToLower(path.extension().string());
    return extension == ".wav" || extension == ".mp3" ||
        extension == ".ogg" || extension == ".flac";
}

void RecordDependency(
    ScenePreloadData& data,
    SceneDependencyType type,
    const std::string& rawPath,
    bool validateExists = true) {
    const std::string path = NormalizePath(rawPath);
    if (path.empty()) {
        return;
    }

    const auto found = std::find_if(
        data.dependencyReport.assets.begin(),
        data.dependencyReport.assets.end(),
        [&](const SceneDependencyRecord& dependency) {
            return dependency.type == type && dependency.path == path;
        });
    if (found != data.dependencyReport.assets.end()) {
        return;
    }

    const bool exists = !validateExists || std::filesystem::exists(path);
    data.dependencyReport.assets.push_back({ type, path, exists });
    if (!exists &&
        std::find(
            data.dependencyReport.missingPaths.begin(),
            data.dependencyReport.missingPaths.end(),
            path) == data.dependencyReport.missingPaths.end()) {
        data.dependencyReport.missingPaths.push_back(path);
    }
}

void AddJsonDependency(ScenePreloadData& data, const std::string& path) {
    RecordDependency(data, SceneDependencyType::Json, path);
}

void AddAudioDependency(ScenePreloadData& data, std::string path) {
    path = NormalizePath(std::move(path));
    if (path.rfind("Resources/", 0) != 0) {
        path = NormalizePath(
            (std::filesystem::path("Resources/audio/se") / path).generic_string());
    }
    AddUniquePath(data.audioPaths, path);
    RecordDependency(data, SceneDependencyType::Audio, path);
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
        if (value.contains("type") && value["type"].is_number_integer() &&
            value.contains("presetName") && value["presetName"].is_string() &&
            value.contains("triggerTime")) {
            const int eventType = value["type"].get<int>();
            const std::string presetName =
                NormalizePath(value["presetName"].get<std::string>());
            if (!presetName.empty()) {
                if (eventType == 0) {
                    AddJsonDependency(
                        data,
                        "Resources/json/gpu_particles/" + presetName + ".json");
                }
                else if (eventType == 1) {
                    AddJsonDependency(
                        data,
                        "Resources/json/effect/" + presetName + ".json");
                }
                else if (eventType == 2) {
                    const std::filesystem::path presetPath(presetName);
                    if (ToLower(presetPath.extension().string()) == ".json") {
                        std::filesystem::path eventPath =
                            std::filesystem::path("Resources/json/audio_events") /
                            presetPath.filename();
                        AddJsonDependency(data, eventPath.generic_string());
                    }
                    else {
                        AddAudioDependency(data, presetName);
                    }
                }
            }
        }

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

    std::string stringValue = NormalizePath(value.get<std::string>());
    if (stringValue.empty()) {
        return;
    }

    const std::string lowerKey = ToLower(propertyName);
    if (lowerKey == "modelname") {
        AddUniquePath(data.modelNames, stringValue);
        RecordDependency(data, SceneDependencyType::Model, stringValue, false);
        return;
    }

    std::filesystem::path path(stringValue);
    const std::string extension = ToLower(path.extension().string());
    if (extension == ".json") {
        if ((lowerKey.find("animator") != std::string::npos ||
             lowerKey.find("controller") != std::string::npos) &&
            stringValue.rfind("Resources/", 0) != 0) {
            path = std::filesystem::path("Resources/json/animator") / path.filename();
        }
        else if (lowerKey.find("cinematic") != std::string::npos &&
            stringValue.rfind("Resources/", 0) != 0) {
            path = std::filesystem::path("Resources/json/cinematic") / path.filename();
        }
        AddJsonDependency(data, path.generic_string());
        return;
    }
    if (IsAudioFile(path)) {
        AddAudioDependency(data, stringValue);
        return;
    }
    if (IsTextureFile(path) || IsTextureKey(lowerKey)) {
        SceneLoadManifest::TextureRequest request;
        request.path = stringValue;
        request.linear = IsLinearTextureKey(lowerKey);
        AddUnique(data.textures, request, [](const SceneLoadManifest::TextureRequest& item) {
            return item.path + (item.linear ? "|linear" : "|color");
        });
        RecordDependency(data, SceneDependencyType::Texture, request.path);
        return;
    }

    if (lowerKey.find("animator") != std::string::npos ||
        lowerKey.find("controller") != std::string::npos) {
        std::filesystem::path controllerPath(stringValue);
        if (controllerPath.extension().empty()) {
            controllerPath.replace_extension(".json");
        }
        if (NormalizePath(controllerPath.generic_string()).rfind("Resources/", 0) != 0) {
            controllerPath =
                std::filesystem::path("Resources/json/animator") /
                controllerPath.filename();
        }
        AddJsonDependency(data, controllerPath.generic_string());
        return;
    }
    if (lowerKey.find("cinematic") != std::string::npos) {
        std::filesystem::path cinematicPath(stringValue);
        if (cinematicPath.extension().empty()) {
            cinematicPath.replace_extension(".json");
        }
        if (NormalizePath(cinematicPath.generic_string()).rfind("Resources/", 0) != 0) {
            cinematicPath =
                std::filesystem::path("Resources/json/cinematic") /
                cinematicPath.filename();
        }
        AddJsonDependency(data, cinematicPath.generic_string());
        return;
    }

    if (stringValue.rfind("Resources/", 0) == 0 && path.has_extension()) {
        RecordDependency(data, SceneDependencyType::Other, stringValue);
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
void SceneLoadManifest::AddAudio(const std::string& path) {
    AddUniquePath(audioPaths, path);
}

std::size_t SceneDependencyReport::Count(SceneDependencyType type) const {
    return static_cast<std::size_t>(std::count_if(
        assets.begin(),
        assets.end(),
        [type](const SceneDependencyRecord& dependency) {
            return dependency.type == type;
        }));
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

    std::size_t jsonIndex = 0;
    while (jsonIndex < jsonPaths.size()) {
        const std::string path = jsonPaths[jsonIndex++];
        RecordDependency(*result, SceneDependencyType::Json, path);

        nlohmann::json document;
        std::string warning;
        if (LoadJsonDocument(path, document, warning)) {
            CollectDependencies(document, "", *result);
            result->jsonDocuments.emplace(NormalizePath(path), std::move(document));

            const std::size_t previousCount = jsonPaths.size();
            for (const SceneDependencyRecord& dependency :
                result->dependencyReport.assets) {
                if (dependency.type == SceneDependencyType::Json) {
                    AddUniquePath(jsonPaths, dependency.path);
                }
            }
            if (progress && jsonPaths.size() > previousCount) {
                progress->AddTasks(jsonPaths.size() - previousCount);
            }
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
    for (const std::string& audioPath : manifest.audioPaths) {
        AddAudioDependency(*result, audioPath);
    }
    for (const std::string& modelName : result->modelNames) {
        RecordDependency(*result, SceneDependencyType::Model, modelName, false);
    }
    for (const SceneLoadManifest::TextureRequest& texture : result->textures) {
        RecordDependency(*result, SceneDependencyType::Texture, texture.path);
    }
    CollectSharedParticleTextures(*result);
    AddSharedTexture(*result, "Resources/sprite/effect/noise0.png", true);
    AddSharedTexture(*result, "Resources/texture/lut/soft_adventure_lut.png");

    std::sort(result->modelNames.begin(), result->modelNames.end());
    std::sort(result->textures.begin(), result->textures.end(), [](const auto& left, const auto& right) {
        if (left.path == right.path) {
            return left.linear < right.linear;
        }
        return left.path < right.path;
    });

    std::sort(result->audioPaths.begin(), result->audioPaths.end());
    std::sort(
        result->dependencyReport.missingPaths.begin(),
        result->dependencyReport.missingPaths.end());
    for (const std::string& missing : result->dependencyReport.missingPaths) {
        result->warnings.push_back("Scene dependency is missing: " + missing);
    }
    if (progress) {
        progress->AddTasks(result->textures.size() + result->modelNames.size() + result->audioPaths.size());
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
    for (const std::string& audioPath : result->audioPaths) {
        AudioPlayer::GetInstance()->LoadSoundFile(audioPath);
        if (progress) {
            progress->Advance(audioPath);
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

SceneDependencyReport ScenePreloader::Inspect(
    const SceneLoadManifest& manifest) {
    const std::shared_ptr<ScenePreloadData> data = Prepare(manifest, nullptr);
    return data ? data->dependencyReport : SceneDependencyReport{};
}
