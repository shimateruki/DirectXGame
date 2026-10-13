#include "AssetReferenceExplorer.h"

#ifdef USE_IMGUI

#include "DebugEditor.h"
#include "Object3d.h"
#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string StripResourcesPrefix(const std::string& path) {
    const std::string normalized = AssetReferenceExplorer::Normalize(path);
    constexpr const char* prefix = "resources/";
    return normalized.rfind(prefix, 0) == 0 ? normalized.substr(std::strlen(prefix)) : normalized;
}

std::string FileStemLower(const std::string& path) {
    std::string stem = fs::path(path).stem().generic_string();
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return stem;
}

int CommonPrefixScore(const std::string& lhs, const std::string& rhs) {
    const std::size_t count = std::min(lhs.size(), rhs.size());
    int score = 0;
    for (std::size_t index = 0; index < count && lhs[index] == rhs[index]; ++index) {
        ++score;
    }
    return score;
}

} // namespace

void AssetReferenceExplorer::Initialize(DebugEditor* editor) {
    editor_ = editor;
}

void AssetReferenceExplorer::Finalize() {
    editor_ = nullptr;
    open_ = false;
    aliasToAssetIndices_.clear();
    usagesByGuid_.clear();
    missingReferences_.clear();
}

void AssetReferenceExplorer::Open(const std::string& assetPath) {
    open_ = true;
    if (!assetPath.empty()) {
        requestedAssetPath_ = assetPath;
    }
    rescanRequested_ = true;
}

std::string AssetReferenceExplorer::Normalize(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    while (value.rfind("./", 0) == 0) {
        value.erase(0, 2);
    }
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string AssetReferenceExplorer::EscapeJsonPointerToken(const std::string& token) {
    std::string result;
    result.reserve(token.size());
    for (char character : token) {
        if (character == '~') result += "~0";
        else if (character == '/') result += "~1";
        else result.push_back(character);
    }
    return result;
}

std::string AssetReferenceExplorer::GetOwnerCategory(const std::string& path) {
    const std::string lower = Normalize(path);
    if (lower.find("/3dobject/") != std::string::npos || lower.find("/scene/") != std::string::npos) return "Scene";
    if (lower.find("/prefab/") != std::string::npos || lower.find("/preset/") != std::string::npos) return "Prefab/Preset";
    if (lower.find("effect") != std::string::npos || lower.find("particle") != std::string::npos || lower.find("vfx") != std::string::npos) return "Effect";
    return "JSON";
}

bool AssetReferenceExplorer::IsReferenceLikeKey(const std::string& key) {
    const std::string lower = Normalize(key);
    static constexpr const char* hints[] = {
        "asset", "model", "texture", "normalmap", "ormmap", "shader", "effect",
        "particle", "prefab", "preset", "audio", "sound", "animator", "font", "path"
    };
    return std::any_of(std::begin(hints), std::end(hints), [&lower](const char* hint) {
        return lower.find(hint) != std::string::npos;
    });
}

bool AssetReferenceExplorer::IsDirectReferenceValue(const std::string& value) {
    const std::string lower = Normalize(value);
    return lower.rfind("guid:", 0) == 0 || lower.rfind("resources/", 0) == 0 ||
        lower.find('/') != std::string::npos || fs::path(lower).has_extension();
}

void AssetReferenceExplorer::BuildAliasIndex() {
    // 同一資産を GUID、パス、Resources 相対パスで引けるようにします。
    aliasToAssetIndices_.clear();
    const auto& assets = AssetDatabase::GetInstance()->GetAssets();
    auto addAlias = [this](const std::string& alias, std::size_t index) {
        const std::string normalized = Normalize(alias);
        if (!normalized.empty()) {
            aliasToAssetIndices_[normalized].push_back(index);
        }
    };

    for (std::size_t index = 0; index < assets.size(); ++index) {
        const EditorAssetRecord& asset = assets[index];
        const fs::path source(asset.sourcePath);
        // JSON 側では GUID、完全 Path、ファイル名、拡張子なし名が混在するため同じ Asset を複数名で引きます。
        addAlias(asset.guid, index);
        addAlias("guid:" + asset.guid, index);
        addAlias(asset.sourcePath, index);
        addAlias(StripResourcesPrefix(asset.sourcePath), index);
        addAlias(source.filename().generic_string(), index);
        addAlias(source.stem().generic_string(), index);
        fs::path sourceWithoutExtension = source;
        sourceWithoutExtension.replace_extension();
        addAlias(sourceWithoutExtension.generic_string(), index);

        const std::string lowerPath = Normalize(asset.sourcePath);
        constexpr const char* modelPrefix = "resources/3dmodel/";
        if (lowerPath.rfind(modelPrefix, 0) == 0) {
            // Model 参照にはフォルダ名だけを保存する形式もあるので、親フォルダも別名へ加えます。
            fs::path relative = fs::path(lowerPath.substr(std::strlen(modelPrefix)));
            const fs::path parent = relative.parent_path();
            if (!parent.empty()) {
                addAlias(parent.generic_string(), index);
                if (Normalize(parent.filename().generic_string()) == Normalize(source.stem().generic_string())) {
                    addAlias(parent.generic_string(), index);
                }
            }
        }
    }
}

std::vector<std::size_t> AssetReferenceExplorer::ResolveCandidates(const std::string& value, const std::string& key) const {
    (void)key;
    // Resources/ の有無だけを吸収して索引を検索します。複数一致は確定参照として扱いません。
    const std::string normalized = Normalize(value);
    auto iterator = aliasToAssetIndices_.find(normalized);
    if (iterator != aliasToAssetIndices_.end()) {
        return iterator->second;
    }
    if (normalized.rfind("resources/", 0) != 0) {
        iterator = aliasToAssetIndices_.find("resources/" + normalized);
        if (iterator != aliasToAssetIndices_.end()) {
            return iterator->second;
        }
    }
    return {};
}

void AssetReferenceExplorer::VisitJson(
    const nlohmann::json& value,
    const std::string& ownerPath,
    const std::string& pointer,
    const std::string& key,
    std::unordered_map<std::string, std::vector<Usage>>& destination,
    bool collectMissing) {
    // JSON Pointer を更新しながら葉の文字列へ降り、修復時に同じ値だけを安全に書き換えます。
    if (value.is_object()) {
        for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
            VisitJson(iterator.value(), ownerPath, pointer + "/" + EscapeJsonPointerToken(iterator.key()), iterator.key(), destination, collectMissing);
        }
        return;
    }
    if (value.is_array()) {
        for (std::size_t index = 0; index < value.size(); ++index) {
            VisitJson(value[index], ownerPath, pointer + "/" + std::to_string(index), key, destination, collectMissing);
        }
        return;
    }
    if (!value.is_string()) return;

    const std::string text = value.get<std::string>();
    if (text.empty()) return;
    const std::vector<std::size_t> candidates = ResolveCandidates(text, key);
    const auto& assets = AssetDatabase::GetInstance()->GetAssets();
    if (!candidates.empty()) {
        // Path/GUID と一意に一致した場合だけ Direct、曖昧な名称は動的参照候補に分類します。
        const bool direct = IsDirectReferenceValue(text) && candidates.size() == 1;
        for (std::size_t assetIndex : candidates) {
            if (assetIndex >= assets.size() || assets[assetIndex].sourcePath == ownerPath) continue;
            Usage usage;
            usage.ownerPath = ownerPath;
            usage.jsonPointer = pointer;
            usage.value = text;
            usage.ownerCategory = GetOwnerCategory(ownerPath);
            usage.kind = direct ? UsageKind::Direct : UsageKind::DynamicCandidate;
            destination[assets[assetIndex].guid].push_back(std::move(usage));
        }
        return;
    }

    // 一致する Asset がなく、キー名も参照らしい文字列だけ Missing 候補に残します。
    // targetScene は Asset Path ではないため除外します。
    if (collectMissing && IsReferenceLikeKey(key) &&
        (IsDirectReferenceValue(text) || text.size() >= 3) &&
        Normalize(key).find("targetscene") == std::string::npos) {
        MissingReference missing;
        missing.ownerPath = ownerPath;
        missing.jsonPointer = pointer;
        missing.key = key;
        missing.value = text;
        const std::vector<std::size_t> repairCandidates = FindRepairCandidates(missing);
        for (std::size_t candidate : repairCandidates) {
            if (candidate < assets.size()) missing.candidateGuids.push_back(assets[candidate].guid);
        }
        missingReferences_.push_back(std::move(missing));
    }
}

void AssetReferenceExplorer::ScanJsonAsset(const EditorAssetRecord& owner) {
    std::ifstream stream(owner.sourcePath);
    if (!stream.is_open()) return;
    nlohmann::json data;
    try {
        stream >> data;
    } catch (...) {
        return;
    }
    VisitJson(data, owner.sourcePath, "", "", usagesByGuid_, true);
}

std::vector<std::size_t> AssetReferenceExplorer::FindRepairCandidates(const MissingReference& missing) const {
    const auto& assets = AssetDatabase::GetInstance()->GetAssets();
    const std::string wanted = FileStemLower(missing.value);
    // 欠落参照の代替候補を並べる索引と類似度。高得点でも同一アセットと確定したわけではない。
    struct Scored { std::size_t index; int score; };
    std::vector<Scored> scored;
    scored.reserve(assets.size());
    // ファイル名の前方一致とキー名に対応する Asset 種別を加点し、上位5件だけ提示します。
    for (std::size_t index = 0; index < assets.size(); ++index) {
        const std::string stem = FileStemLower(assets[index].sourcePath);
        int score = CommonPrefixScore(wanted, stem) * 4;
        if (stem.find(wanted) != std::string::npos || wanted.find(stem) != std::string::npos) score += 20;
        if (Normalize(missing.key).find("model") != std::string::npos && assets[index].type == EditorAssetType::Model) score += 12;
        if (Normalize(missing.key).find("texture") != std::string::npos && assets[index].type == EditorAssetType::Texture) score += 12;
        if (score > 5) scored.push_back({ index, score });
    }
    std::sort(scored.begin(), scored.end(), [](const Scored& lhs, const Scored& rhs) {
        return lhs.score > rhs.score;
    });
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < std::min<std::size_t>(5, scored.size()); ++index) {
        result.push_back(scored[index].index);
    }
    return result;
}

std::string AssetReferenceExplorer::MakeReferenceValue(const EditorAssetRecord& asset, const std::string& key) const {
    const std::string lowerKey = Normalize(key);
    const std::string lowerPath = Normalize(asset.sourcePath);
    if (lowerKey.find("model") != std::string::npos && lowerPath.rfind("resources/3dmodel/", 0) == 0) {
        fs::path relative(lowerPath.substr(std::strlen("resources/3dmodel/")));
        if (!relative.parent_path().empty()) return relative.parent_path().generic_string();
    }
    return asset.sourcePath;
}

// 候補を選択した後だけ元 JSON を更新し、参照索引も作り直します。
bool AssetReferenceExplorer::RepairMissingReference(MissingReference& missing, std::string& message) {
    if (missing.candidateGuids.empty() || missing.selectedCandidate < 0 ||
        missing.selectedCandidate >= static_cast<int>(missing.candidateGuids.size())) {
        message = "修復候補が選択されていません。";
        return false;
    }
    const EditorAssetRecord* asset = AssetDatabase::GetInstance()->FindByGuid(missing.candidateGuids[missing.selectedCandidate]);
    if (!asset) {
        message = "修復候補がAsset Databaseから見つかりません。";
        return false;
    }
    std::ifstream input(missing.ownerPath);
    nlohmann::json data;
    try {
        input >> data;
        nlohmann::json::json_pointer pointer(missing.jsonPointer);
        // 再スキャン後に元 JSON が変わった場合は、古い候補で別の値を上書きしないよう中断します。
        if (!data.contains(pointer) || !data[pointer].is_string() || data[pointer].get<std::string>() != missing.value) {
            message = "参照元が再スキャン後に変更されています。再スキャンしてください。";
            return false;
        }
        data[pointer] = MakeReferenceValue(*asset, missing.key);
    } catch (const std::exception& exception) {
        message = std::string("JSONを更新できません: ") + exception.what();
        return false;
    }
    // 実際の書き込みはユーザーが候補を選んだ後だけ行い、索引再作成を要求します。
    std::ofstream output(missing.ownerPath, std::ios::trunc);
    if (!output.is_open()) {
        message = "参照元JSONを書き込めません。";
        return false;
    }
    output << data.dump(4) << '\n';
    output.close();
    AssetDatabase::GetInstance()->RequestRefresh(true);
    message = "Missing参照を修復しました: " + missing.ownerPath;
    rescanRequested_ = true;
    return true;
}

void AssetReferenceExplorer::RefreshIndex() {
    // AssetDatabase の世代が同じ間は再走査を省きます。
    AssetDatabase* database = AssetDatabase::GetInstance();
    if (!database->IsInitialized() || database->IsInitialIndexBuildInProgress()) return;
    BuildAliasIndex();
    usagesByGuid_.clear();
    missingReferences_.clear();
    for (const EditorAssetRecord& asset : database->GetAssets()) {
        if (asset.type == EditorAssetType::Json && Normalize(asset.sourcePath).find("/.trash/") == std::string::npos) {
            ScanJsonAsset(asset);
        }
    }
    indexedGeneration_ = database->GetGeneration();
    rescanRequested_ = false;
    statusMessage_ = "参照索引を更新しました。Asset " + std::to_string(database->GetAssets().size()) +
        " 件 / Missing候補 " + std::to_string(missingReferences_.size()) + " 件";
}

void AssetReferenceExplorer::SelectAssetByPath(const std::string& path) {
    const EditorAssetRecord* asset = AssetDatabase::GetInstance()->FindByPath(path);
    if (asset) selectedGuid_ = asset->guid;
}

#endif // USE_IMGUI
