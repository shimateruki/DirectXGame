#define NOMINMAX
#include "SceneSavePreview.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
bool ReadJsonFile(const std::string& path, json& outJson) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    try {
        if (file.peek() == std::ifstream::traits_type::eof()) {
            outJson = json::object();
        }
        else {
            file >> outJson;
        }
        return true;
    }
    catch (...) {
        outJson = json::object();
        return false;
    }
}

bool HasObjectList(const json& value) {
    return value.is_object() && value.contains("objects") && value["objects"].is_array();
}

std::string JoinPath(const std::string& parent, const std::string& child) {
    if (parent.empty()) return child;
    return parent + "." + child;
}

std::string MakeArrayPath(const std::string& parent, size_t index) {
    std::ostringstream oss;
    oss << parent << "[" << index << "]";
    return oss.str();
}
}

void SceneSavePreview::Build(const std::vector<SceneSerializer::SaveTarget>& targets, const std::string& title) {
    // 保存予定の複数ファイルを先に差分化し、確認画面を開く前に件数を確定する。
    targets_ = targets;
    title_ = title;
    savedFilesLabel_.clear();
    fileDiffs_.clear();
    summary_ = {};

    for (const auto& target : targets_) {
        if (!savedFilesLabel_.empty()) {
            savedFilesLabel_ += ", ";
        }
        savedFilesLabel_ += target.label;
        fileDiffs_.push_back(BuildFileDiff(target));
    }

    UpdateSummary();
}

void SceneSavePreview::Open() {
    isOpen_ = true;
    requestOpenPopup_ = true;
}

void SceneSavePreview::Close() {
    isOpen_ = false;
    requestOpenPopup_ = false;
}

SceneSavePreview::FileDiff SceneSavePreview::BuildFileDiff(const SceneSerializer::SaveTarget& target) const {
    // 旧ファイルを読めなくても保存予定データの確認は続け、問題を差分に表示します。
    FileDiff diff;
    diff.target = target;
    diff.oldFileExists = fs::exists(target.path);
    diff.hasObjectList = HasObjectList(target.data);

    json oldJson = json::object();
    if (diff.oldFileExists) {
        diff.oldJsonValid = ReadJsonFile(target.path, oldJson);
        if (!diff.oldJsonValid) {
            diff.fileDetails.push_back("既存JSONの読み込みに失敗しました。保存すると現在の内容で上書きします。");
        }
    }
    else {
        diff.oldJsonValid = true;
        diff.fileDetails.push_back("新規ファイルとして作成されます。");
    }

    if (diff.hasObjectList) {
        BuildObjectDiff(diff, oldJson, target.data);
    }
    else {
        BuildFileLevelDiff(diff, oldJson, target.data);
    }

    return diff;
}

void SceneSavePreview::BuildObjectDiff(FileDiff& diff, const json& oldJson, const json& newJson) const {
    // Object一覧は名前をキーにして旧版と新版を突き合わせる。
    std::map<std::string, json> oldObjects;
    std::map<std::string, json> newObjects;

    if (HasObjectList(oldJson)) {
        int unnamedIndex = 0;
        for (const auto& obj : oldJson["objects"]) {
            std::string name = GetObjectName(obj, "__old_unnamed_" + std::to_string(unnamedIndex++));
            oldObjects[name] = obj;
        }
    }

    if (HasObjectList(newJson)) {
        int unnamedIndex = 0;
        for (const auto& obj : newJson["objects"]) {
            std::string name = GetObjectName(obj, "__new_unnamed_" + std::to_string(unnamedIndex++));
            newObjects[name] = obj;
        }
    }

    // 新版に存在するObjectを追加・変更・同一へ分類し、内容変更は詳細まで辿る。
    for (const auto& [name, newObj] : newObjects) {
        auto oldIt = oldObjects.find(name);
        ObjectChange change;
        change.name = name;
        change.category = GetObjectCategory(newObj);

        if (oldIt == oldObjects.end()) {
            change.kind = ChangeKind::Added;
            change.details.push_back("type: " + JsonValueToText(newObj.contains("type") ? newObj["type"] : json("")));
            change.details.push_back("modelName: " + JsonValueToText(newObj.contains("modelName") ? newObj["modelName"] : json("")));
            if (newObj.contains("position")) change.details.push_back("position: " + JsonValueToText(newObj["position"]));
            diff.addedCount++;
        }
        else if (oldIt->second != newObj) {
            change.kind = ChangeKind::Modified;
            CollectJsonDiffs(oldIt->second, newObj, "", change.details, 12);
            if (change.details.empty()) {
                change.details.push_back("JSONの内容が変更されています。");
            }
            diff.modifiedCount++;
        }
        else {
            change.kind = ChangeKind::Unchanged;
            change.details.push_back("変更なし");
            diff.unchangedCount++;
        }

        diff.objects.push_back(std::move(change));
    }

    // 旧版にしか残っていないObjectは保存後の削除として表示する。
    for (const auto& [name, oldObj] : oldObjects) {
        if (newObjects.find(name) != newObjects.end()) continue;

        ObjectChange change;
        change.kind = ChangeKind::Removed;
        change.name = name;
        change.category = GetObjectCategory(oldObj);
        change.details.push_back("保存後のJSONから削除されます。");
        if (oldObj.contains("type")) change.details.push_back("type: " + JsonValueToText(oldObj["type"]));
        diff.removedCount++;
        diff.objects.push_back(std::move(change));
    }

    std::sort(diff.objects.begin(), diff.objects.end(), [](const ObjectChange& a, const ObjectChange& b) {
        if (a.kind != b.kind) return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        return a.name < b.name;
    });
}

void SceneSavePreview::BuildFileLevelDiff(FileDiff& diff, const json& oldJson, const json& newJson) const {
    // Object一覧を持たないメタ情報などは、ファイルのJSON全体で変更を比較する。
    if (!diff.oldFileExists) {
        diff.fileDetails.push_back("メタファイルを新規作成します。");
        return;
    }

    if (!diff.oldJsonValid) {
        diff.fileDetails.push_back("読み込めなかった既存ファイルを置き換えます。");
        return;
    }

    if (oldJson == newJson) {
        diff.fileDetails.push_back("ファイル内容に変更はありません。");
        return;
    }

    CollectJsonDiffs(oldJson, newJson, "", diff.fileDetails, 16);
    if (diff.fileDetails.empty()) {
        diff.fileDetails.push_back("ファイル内容が変更されています。");
    }
}

void SceneSavePreview::UpdateSummary() {
    // ファイルとObjectの件数を別々に集計し、既存JSONの読込失敗も独立して数える。
    summary_.fileCount = static_cast<int>(fileDiffs_.size());

    for (const auto& diff : fileDiffs_) {
        summary_.addedObjects += diff.addedCount;
        summary_.removedObjects += diff.removedCount;
        summary_.modifiedObjects += diff.modifiedCount;
        summary_.unchangedObjects += diff.unchangedCount;

        if (!diff.oldFileExists) {
            summary_.addedFiles++;
        }
        else if (!diff.oldJsonValid) {
            summary_.invalidFiles++;
        }
        else if (diff.addedCount > 0 || diff.removedCount > 0 || diff.modifiedCount > 0) {
            summary_.modifiedFiles++;
        }
        else if (!diff.hasObjectList && diff.fileDetails.size() == 1 && diff.fileDetails[0] != "ファイル内容に変更はありません。") {
            summary_.modifiedFiles++;
        }
    }
}

std::string SceneSavePreview::GetObjectName(const json& obj, const std::string& fallback) {
    if (obj.is_object() && obj.contains("name") && obj["name"].is_string()) {
        return obj["name"].get<std::string>();
    }
    return fallback;
}

std::string SceneSavePreview::GetObjectCategory(const json& obj) {
    if (obj.is_object() && obj.contains("saveCategory") && obj["saveCategory"].is_string()) {
        return obj["saveCategory"].get<std::string>();
    }
    return "Object";
}

std::string SceneSavePreview::JsonValueToText(const json& value) {
    if (value.is_null()) return "null";
    if (value.is_string()) return "\"" + value.get<std::string>() + "\"";
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (value.is_number_float()) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3) << value.get<double>();
        return oss.str();
    }
    if (value.is_number_integer()) return std::to_string(value.get<long long>());
    if (value.is_number_unsigned()) return std::to_string(value.get<unsigned long long>());
    if (value.is_array()) {
        if (value.size() <= 6) {
            std::string text = "[";
            for (size_t i = 0; i < value.size(); ++i) {
                if (i > 0) text += ", ";
                text += JsonValueToText(value[i]);
            }
            text += "]";
            return text;
        }
        return "array(" + std::to_string(value.size()) + ")";
    }
    if (value.is_object()) return "object(" + std::to_string(value.size()) + ")";
    return value.dump();
}

void SceneSavePreview::CollectJsonDiffs(const json& before, const json& after, const std::string& path, std::vector<std::string>& outDetails, int maxDetails) {
    // 深い JSON でも UI が大量の行で埋まらないよう、表示件数を制限します。
    if (static_cast<int>(outDetails.size()) >= maxDetails) return;
    if (before == after) return;

    // Object同士はキーの和集合を作り、追加・削除・変更を各パスへ展開する。
    if (before.is_object() && after.is_object()) {
        std::vector<std::string> keys;
        for (auto it = before.begin(); it != before.end(); ++it) keys.push_back(it.key());
        for (auto it = after.begin(); it != after.end(); ++it) {
            if (std::find(keys.begin(), keys.end(), it.key()) == keys.end()) {
                keys.push_back(it.key());
            }
        }
        std::sort(keys.begin(), keys.end());

        for (const std::string& key : keys) {
            if (static_cast<int>(outDetails.size()) >= maxDetails) break;
            bool hasBefore = before.contains(key);
            bool hasAfter = after.contains(key);
            std::string childPath = JoinPath(path, key);

            if (!hasBefore) {
                outDetails.push_back(childPath + ": 追加 " + JsonValueToText(after[key]));
            }
            else if (!hasAfter) {
                outDetails.push_back(childPath + ": 削除 " + JsonValueToText(before[key]));
            }
            else {
                CollectJsonDiffs(before[key], after[key], childPath, outDetails, maxDetails);
            }
        }
        return;
    }

    // 小さな同長配列だけ要素ごとに追い、それ以外は値のまとまりとして示す。
    if (before.is_array() && after.is_array()) {
        if (before.size() == after.size() && before.size() <= 3) {
            for (size_t i = 0; i < before.size(); ++i) {
                if (static_cast<int>(outDetails.size()) >= maxDetails) break;
                CollectJsonDiffs(before[i], after[i], MakeArrayPath(path, i), outDetails, maxDetails);
            }
            return;
        }
    }

    std::string label = path.empty() ? "(root)" : path;
    outDetails.push_back(label + ": " + JsonValueToText(before) + " -> " + JsonValueToText(after));
}
