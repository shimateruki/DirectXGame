#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "RuntimeResourcePlan.h"
#include "engine/graphics/3d/model/ModelAssetPath.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <deque>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace engine {
namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;

std::string Text(const fs::path& path) {
    const auto value = path.generic_u8string();
    return {value.begin(), value.end()};
}

fs::path Path(const std::string& value) {
    return fs::path(std::u8string(value.begin(), value.end()));
}

std::string Lower(std::string value) {
    // UTF-8の日本語バイトを壊さず、ASCIIのパス区切りと英字だけを正規化します。
    std::replace(value.begin(), value.end(), '\\', '/');
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return value;
}

std::wstring CopyKey(const fs::path& path) {
    auto key = path.generic_wstring();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return std::towlower(c); });
    return key;
}

bool Under(const std::string& path, const std::string& prefix) {
    return path == prefix || path.starts_with(prefix + "/");
}

std::string Read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("読み込めません: " + Text(path));
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

bool IsModel(const fs::path& path) {
    const auto ext = Lower(Text(path.extension()));
    return ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx";
}

bool IsTexture(const fs::path& path) {
    const auto ext = Lower(Text(path.extension()));
    return ext == ".png" || ext == ".dds" || ext == ".jpg" || ext == ".jpeg" ||
           ext == ".tga" || ext == ".bmp" || ext == ".hdr";
}

std::string DevelopmentOnly(const fs::path& path) {
    const auto key = Lower(Text(path));
    // ユーザーの保存スロットと端末固有の調整値は、新しい配布先へ持ち出しません。
    for (const auto& component : path) {
        const auto name = Lower(Text(component));
        if (name == ".cache" || name == ".backup" || name == ".trash" || name == "__pycache__" || name == "tools")
            return "キャッシュ・退避・制作ツール";
    }
    if (Under(key, "generated/editor")) return "エディターの生成物";
    const auto name = Lower(Text(path.filename()));
    if (name.starts_with("savedata") || name == "imgui.ini" || name == "thumbs.db" || name == ".ds_store")
        return "個人のセーブ・端末設定";
    const auto ext = Lower(Text(path.extension()));
    if (ext == ".log" || ext == ".tmp" || ext == ".bak" || ext == ".blend" || ext == ".psd" ||
        ext == ".kra" || ext == ".py" || ext == ".ps1" || ext == ".bat" || ext == ".md")
        return "制作元データ・文書・一時ファイル";
    return {};
}

// C++のコメントや文字リテラルに書かれた例を除外し、実際の文字列リテラルだけを取り出します。
// 動的な文字列の断片も返し、フォルダー指定は呼び出し側が保守的に保持します。
std::vector<std::string> StringLiterals(const std::string& source) {
    std::vector<std::string> result;
    for (size_t i = 0; i < source.size();) {
        if (source.compare(i, 2, "//") == 0) {
            const auto end = source.find('\n', i + 2);
            i = end == std::string::npos ? source.size() : end + 1;
        } else if (source.compare(i, 2, "/*") == 0) {
            const auto end = source.find("*/", i + 2);
            i = end == std::string::npos ? source.size() : end + 2;
        } else if (source.compare(i, 2, "R\"") == 0) {
            const auto open = source.find('(', i + 2);
            if (open == std::string::npos || open - i > 18) { ++i; continue; }
            const auto end = source.find(")" + source.substr(i + 2, open - i - 2) + "\"", open + 1);
            if (end == std::string::npos) break;
            result.push_back(source.substr(open + 1, end - open - 1));
            const auto delimiterLength = open - i - 2;
            i = end + delimiterLength + 2;
        } else if (source[i] == '\'' || source[i] == '"') {
            const char quote = source[i++];
            std::string value;
            while (i < source.size() && source[i] != quote) {
                if (source[i] == '\\' && i + 1 < source.size()) {
                    ++i;
                    if (source[i] == '\\' || source[i] == '"' || source[i] == '\'') value += source[i];
                    else if (source[i] == 'n') value += '\n';
                    else if (source[i] == 't') value += '\t';
                    else value += source[i];
                    ++i;
                } else value += source[i++];
            }
            if (i < source.size()) ++i;
            if (quote == '"') result.push_back(std::move(value));
        } else ++i;
    }
    return result;
}

struct Asset {
    fs::path relative;
    uint64_t bytes = 0;
    std::string reason;
    std::string source;
    bool used = false;
};

// 一回の配布作成だけで使う索引です。共有のAssetDatabaseやGPUロード状態には触れません。
class Collector {
public:
    explicit Collector(fs::path project) : project_(std::move(project)), resources_(project_ / "Resources") {}
    RuntimeResourcePlan Build(const std::string& keep);

private:
    void Index();
    std::string ReadInput(const fs::path& path);
    void Keep(const std::string& key, const std::string& source, const std::string& reason);
    bool Folder(const std::string& key, const std::string& source, const std::string& reason);
    bool Reference(std::string value, const fs::path& relativeTo, const std::string& source, bool explicitKeep = false);
    void VisitJson(const json& value, const fs::path& relativeTo, const std::string& source);
    void Dependencies(const std::string& key);
    void Code();
    void Problem(const std::string& source, const std::string& value, const std::string& reason);

    fs::path project_, resources_;
    std::map<std::string, Asset> assets_;
    std::map<std::string, std::vector<std::string>> aliases_;
    std::map<std::string, std::vector<std::string>> guids_;
    std::set<std::string> directories_, modelFolders_;
    std::deque<std::string> pending_;
    std::set<std::string> problemKeys_;
    json problems_ = json::array();
    std::map<fs::path, RuntimeResourcePlan::Input> inputs_;
};

std::string Collector::ReadInput(const fs::path& path) {
    // 読み取り前後の更新を検査し、保存途中の内容から依存を断定しません。
    RuntimeResourcePlan::Input input{path, fs::file_size(path), fs::last_write_time(path)};
    const auto text = Read(path);
    if (fs::file_size(path) != input.bytes || fs::last_write_time(path) != input.modified)
        throw std::runtime_error("解析中に更新されました。もう一度作成してください: " + Text(path));
    if (const auto old = inputs_.find(path); old != inputs_.end() &&
        (old->second.bytes != input.bytes || old->second.modified != input.modified))
        throw std::runtime_error("解析中に更新されました。もう一度作成してください: " + Text(path));
    inputs_[path] = input;
    return text;
}

void Collector::Problem(const std::string& source, const std::string& value, const std::string& reason) {
    if (problemKeys_.insert(source + "\n" + value + "\n" + reason).second)
        problems_.push_back({{"source", source}, {"value", value}, {"reason", reason}});
}

void Collector::Index() {
    if (!fs::is_directory(resources_)) throw std::runtime_error("Resourcesが見つかりません。");
    // 名前の候補を全て集めても、依存のあるファイルだけを後で使用中にします。
    for (fs::recursive_directory_iterator it(resources_), end; it != end; ++it) {
        const auto attributes = GetFileAttributesW(it->path().c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            // 素材内部のリンク先へ入らず、コピー側と同じResources境界を守ります。
            if (it->is_directory()) it.disable_recursion_pending();
            continue;
        }
        const auto relative = it->path().lexically_relative(resources_);
        if (it->is_directory()) { directories_.insert(Lower(Text(relative))); continue; }
        if (!it->is_regular_file()) continue;
        const auto key = Lower(Text(relative));
        assets_.emplace(key, Asset{relative, it->file_size(), DevelopmentOnly(relative), {}, false});
        if (Under(key, "3dmodel") && IsModel(relative))
            modelFolders_.insert(Lower(Text(relative.parent_path())).substr(8));
        if (!assets_.at(key).reason.empty() || relative.extension() == ".meta") continue;
        auto add = [&](const std::string& name) { aliases_[Lower(name)].push_back(key); };
        add(Text(relative));
        add(Text(relative.filename()));
        add(Text(relative.stem()));
        // VFX・Recorderの移動後も、ルートからの相対名と旧来の短い名前の両方を扱います。
        auto partial = relative;
        while (partial.has_parent_path()) {
            auto part = partial.begin();
            fs::path next;
            for (++part; part != partial.end(); ++part) next /= *part;
            partial = next;
            add(Text(partial));
            auto noExtension = partial;
            noExtension.replace_extension();
            add(Text(noExtension));
        }
    }
    // GUID参照は名前を推測せず、.metaが対応する実ファイルへ戻します。
    for (const auto& [key, asset] : assets_) {
        if (asset.relative.extension() != ".meta" || !asset.reason.empty()) continue;
        try {
            const auto meta = json::parse(ReadInput(resources_ / asset.relative));
            const auto guid = meta.value("guid", "");
            if (!guid.empty()) guids_[Lower(guid)].push_back(key.substr(0, key.size() - 5));
        } catch (const json::exception&) {
            // メタ情報の破損はレポートへ残し、通常のパス指定の解析を続けます。
            Problem(key, "", "メタ情報を解析できません");
        }
    }
}

void Collector::Keep(const std::string& key, const std::string& source, const std::string& reason) {
    const auto found = assets_.find(key);
    if (found == assets_.end() || found->second.used) return;
    auto& asset = found->second;
    if (!asset.reason.empty()) {
        Problem(source, Text(asset.relative), "実行参照が制作・保存データの除外規則と競合: " + asset.reason);
        return;
    }
    asset.used = true;
    asset.source = source;
    asset.reason = reason;
    pending_.push_back(key);
    // 実行時にGUID解決する素材の.metaは本体と対で残します。
    Keep(key + ".meta", source, "使用素材のメタ情報");
    if (IsTexture(asset.relative)) {
        for (const char* extension : {".png", ".dds", ".jpg", ".jpeg", ".tga", ".hdr"}) {
            auto paired = asset.relative;
            paired.replace_extension(extension);
            Keep(Lower(Text(paired)), source, "使用画像のPNG/DDSペア");
        }
    }
}

bool Collector::Folder(const std::string& key, const std::string& source, const std::string& reason) {
    bool any = false;
    for (const auto& [candidate, asset] : assets_) {
        if (!Under(candidate, key)) continue;
        Keep(candidate, source, reason);
        any = true;
    }
    return any;
}

bool Collector::Reference(std::string value, const fs::path& relativeTo, const std::string& source, bool explicitKeep) {
    if (value.empty() || value.starts_with("data:") || value.find('\n') != std::string::npos) return false;
    if (value.size() > 1 && value.front() == '"' && value.find('"', 1) == value.size() - 1)
        value = value.substr(1, value.size() - 2);
    value = Lower(value);
    // glTFの外部URIでは空白などが%xxで保存されるため、ファイル照合前に復号します。
    for (size_t i = 0; i + 2 < value.size(); ++i) {
        if (value[i] != '%' || !std::isxdigit(static_cast<unsigned char>(value[i + 1])) ||
            !std::isxdigit(static_cast<unsigned char>(value[i + 2]))) continue;
        const auto code = static_cast<char>(std::stoi(value.substr(i + 1, 2), nullptr, 16));
        value.replace(i, 3, 1, code);
    }
    if (value.starts_with("guid:")) value.erase(0, 5);
    if (const auto guid = guids_.find(value); guid != guids_.end()) {
        // GUIDの重複がある素材も、どちらかを未使用と断定せず両方を残します。
        for (const auto& candidate : guid->second) Keep(candidate, source, "GUIDの参照先");
        if (guid->second.size() > 1) Problem(source, value, "GUIDが重複しているため全候補を保持");
        return true;
    }
    std::string key = value.starts_with("resources/") ? value.substr(10) : value;
    const auto path = Path(key).lexically_normal();
    // キャッシュの生成先や個人セーブの保存先は、コードに登場しても配布素材にはなりません。
    if (!DevelopmentOnly(path).empty()) return false;
    if (path.is_absolute() || key.find(":") != std::string::npos) return false;

    // 同名ファイルの推測より、参照元からの相対パスとResources内の完全指定を優先します。
    const std::string local = Lower(Text((relativeTo / path).lexically_normal()));
    for (const auto& candidate : {local, Lower(Text(path))}) {
        if (assets_.contains(candidate)) { Keep(candidate, source, "直接参照"); return true; }
        // PNGが存在しなくてもTextureManagerが同名DDSへ解決できる素材を保持します。
        if (IsTexture(Path(candidate))) {
            for (const auto* extension : {".dds", ".png", ".jpg", ".jpeg", ".tga", ".hdr"}) {
                auto alternative = Path(candidate);
                alternative.replace_extension(extension);
                const auto alternativeKey = Lower(Text(alternative));
                if (assets_.contains(alternativeKey)) {
                    Keep(alternativeKey, source, "画像の代替形式");
                    return true;
                }
            }
        }
        // 旧シーンには入口JSONがなく、分割ファイルだけが残っている形式もあります。
        if (Under(candidate, "json/3dobject") && path.extension() == ".json") {
            bool foundSplit = false;
            for (const char* suffix : {"_player.json", "_enemy.json", "_object.json", "_camera.json"}) {
                const auto split = Path(candidate).parent_path() / (Text(Path(candidate).stem()) + suffix);
                const auto splitKey = Lower(Text(split));
                if (assets_.contains(splitKey)) { Keep(splitKey, source, "入口のない分割シーン"); foundSplit = true; }
            }
            if (foundSplit) return true;
        }
    }

    // 実際のモデル読込と共通の規則で、フォルダー名から一つのモデルを解決します。
    std::string modelName = key.starts_with("3dmodel/") ? key.substr(8) : key;
    fs::path directory, file;
    const auto requestedModel = Path(modelName);
    const auto modelFolder = requestedModel.parent_path() / requestedModel.stem();
    const bool modelCandidate = modelFolders_.contains(Lower(Text(modelFolder))) ||
                                assets_.contains("3dmodel/" + modelName);
    // 任意の文字列ごとにディスクを探さず、収集済み索引にモデル候補がある場合だけ実ロードの探索へ進みます。
    if (modelCandidate && ResolveModelAssetPath(resources_ / "3DModel", modelName, directory, file)) {
        const auto modelKey = Lower(Text((directory / file).lexically_relative(resources_)));
        if (assets_.contains(modelKey)) { Keep(modelKey, source, "ModelManagerと同じ探索順で解決"); return true; }
    }

    // 動的に末尾を連結する固有フォルダーは、その範囲を保護します。ルート全体の列挙定数は起点にしません。
    while (!key.empty() && key.back() == '/') key.pop_back();
    const bool broadRoot = key == "" || key == "json" || key == "sprite" || key == "3dmodel" || key == "texture";
    if (!broadRoot && directories_.contains(key)) {
        return Folder(key, source, explicitKeep ? "追加保持フォルダー" : "動的なフォルダー参照を保護");
    }
    if (explicitKeep && broadRoot) return Folder(key, source, "明示的な追加保持");

    // unlock_ + 番号などの動的な名前は、同じ親フォルダー内で一致する接頭辞の全候補を残します。
    if (!path.has_extension() && !broadRoot && value.starts_with("resources/")) {
        bool matchedPrefix = false;
        for (auto it = assets_.lower_bound(key); it != assets_.end() && it->first.starts_with(key); ++it) {
            Keep(it->first, source, "動的なファイル名の接頭辞を保護");
            matchedPrefix = true;
        }
        if (matchedPrefix) return true;
    }

    // 短いプリセット名が複数フォルダーで一致する場合は全候補を保持し、除外を断定しません。
    if (const auto alias = aliases_.find(value); alias != aliases_.end()) {
        for (const auto& candidate : alias->second) Keep(candidate, source, "短い素材名の参照");
        return true;
    }
    if (value.starts_with("resources/") && !broadRoot)
        Problem(source, value, "直接参照を解決できません。追加保持欄で動的な参照元を指定できます");
    return false;
}

void Collector::VisitJson(const json& value, const fs::path& relativeTo, const std::string& source) {
    if (value.is_string()) Reference(value.get<std::string>(), relativeTo, source);
    else if (value.is_array()) for (const auto& child : value) VisitJson(child, relativeTo, source);
    else if (value.is_object()) for (const auto& [key, child] : value.items()) {
        // メモや説明文にだけ登場するパスを、実行参照へ混ぜません。
        if (!key.starts_with("_") && key != "description" && key != "comment") VisitJson(child, relativeTo, source);
        else if (key == "_sceneAsset") VisitJson(child, relativeTo, source);
    }
}

void Collector::Code() {
    for (const char* directory : {"engine", "game", "application"}) {
        const auto root = project_ / directory;
        if (!fs::is_directory(root)) continue;
        for (fs::recursive_directory_iterator it(root), end; it != end; ++it) {
            // 編集専用の例・テスト・外部ライブラリの参照を実行素材の根拠にしません。
            if (it->is_directory() && (it->path().filename() == "editor" || it->path().filename() == "tests")) {
                it.disable_recursion_pending();
                continue;
            }
            const auto ext = it->path().extension();
            if (!it->is_regular_file() || (ext != ".cpp" && ext != ".h" && ext != ".hpp" && ext != ".inl")) continue;
            const auto source = Text(it->path().lexically_relative(project_));
            for (const auto& value : StringLiterals(ReadInput(it->path()))) Reference(value, {}, source);
        }
    }
}

void Collector::Dependencies(const std::string& key) {
    const auto& asset = assets_.at(key);
    const auto path = resources_ / asset.relative;
    const auto ext = Lower(Text(path.extension()));
    if (ext == ".json" || ext == ".gltf" || ext == ".glb") {
        try {
            auto text = ReadInput(path);
            if (ext == ".glb") {
                // GLBヘッダーと先頭JSONチャンクをリトルエンディアンで検査し、埋込画像以外のURIも追います。
                auto word = [&](size_t offset) {
                    if (offset + 4 > text.size()) throw std::runtime_error("GLBヘッダーが短すぎます");
                    uint32_t result = 0;
                    for (int i = 0; i < 4; ++i) result |= static_cast<uint32_t>(static_cast<unsigned char>(text[offset + i])) << (8 * i);
                    return result;
                };
                if (word(0) != 0x46546c67 || word(4) != 2 || word(12) > text.size() - 20 || word(16) != 0x4e4f534a)
                    throw std::runtime_error("GLBのJSONチャンクを確認できません");
                text = text.substr(20, word(12));
            }
            VisitJson(json::parse(text), asset.relative.parent_path(), Text(asset.relative));
            // シーンの分割ファイルはJSONに列挙されず、LevelLoaderが同じ名前から導出します。
            if (Under(key, "json/3dobject")) {
                for (const char* suffix : {"_player.json", "_enemy.json", "_object.json", "_camera.json"}) {
                    const auto split = asset.relative.parent_path() / (Text(asset.relative.stem()) + suffix);
                    Keep(Lower(Text(split)), key, "LevelLoaderの分割シーン");
                }
            }
        } catch (const std::exception& error) {
            Problem(key, "", error.what());
            // 読めないモデルの外部依存は断定せず、同梱ファイルを残します。
            if (ext != ".json") Folder(Lower(Text(asset.relative.parent_path())), key, "解析できないモデルの関連ファイルを保護");
            else throw std::runtime_error("必要なJSONを解析できません: " + key + " / " + error.what());
        }
    } else if (ext == ".obj" || ext == ".mtl") {
        std::istringstream stream(ReadInput(path));
        for (std::string line; std::getline(stream, line);) {
            std::istringstream fields(line);
            std::string command;
            fields >> command;
            command = Lower(command);
            if ((ext == ".obj" && command != "mtllib") ||
                (ext == ".mtl" && !command.starts_with("map_") && command != "bump" && command != "norm" &&
                 command != "disp" && command != "decal" && command != "refl")) continue;
            std::string rest;
            std::getline(fields >> std::ws, rest);
            if (!rest.empty() && rest.back() == '\r') rest.pop_back();
            if (rest.size() >= 2 && rest.front() == '"' && rest.find('"', 1) == rest.size() - 1) rest = rest.substr(1, rest.size() - 2);
            // 空白を含むファイル名を先に試し、見つからなければMTLのオプションを一つずつ除きます。
            bool found = Reference(rest, asset.relative.parent_path(), key);
            if (!found && command == "mtllib") {
                // OBJが一行で複数のMTLを指定する場合も、それぞれの依存を残します。
                std::istringstream names(rest);
                for (std::string name; names >> std::quoted(name);)
                    found = Reference(name, asset.relative.parent_path(), key) || found;
            } else {
                while (!found && rest.find(' ') != std::string::npos) {
                    rest.erase(0, rest.find(' ') + 1);
                    found = Reference(rest, asset.relative.parent_path(), key);
                }
            }
            if (!found) {
                Problem(key, rest, "材質の外部依存を解決できないためモデルのフォルダーを保持");
                Folder(Lower(Text(asset.relative.parent_path())), key, "モデル外部依存の保護");
            }
        }
    } else if (IsModel(path)) {
        Folder(Lower(Text(asset.relative.parent_path())), key, "FBX等の外部依存を保護");
    }
}

RuntimeResourcePlan Collector::Build(const std::string& keep) {
    Index();
    // シェーダーのinclude、フォント、ルート直下のシステム素材は実行基盤として保持します。
    for (const auto& [key, asset] : assets_) {
        const auto ext = Lower(Text(asset.relative.extension()));
        if (ext == ".hlsl" || ext == ".hlsli" || ext == ".cso" || Under(key, "font") ||
            (asset.relative.parent_path().empty() && ext != ".meta"))
            Keep(key, "実行基盤", "シェーダー・フォント・共通素材");
        // エディターで公開したシーンを全て起点にし、新規シーン追加でも配布設定の修正を不要にします。
        if (Under(key, "json/3dobject") && ext == ".json" && asset.reason.empty()) {
            try {
                const auto data = json::parse(ReadInput(resources_ / asset.relative));
                if (data.is_object() && data.contains("_sceneAsset")) Keep(key, "Scene Asset", "公開されたシーン");
            } catch (const json::exception&) {
                Problem(key, "", "シーン候補のJSONを解析できません");
            }
        }
    }
    Code();
    // 人が指定した動的な素材を起点に加えます。存在しない指定を黙って無視すると配布を壊すため失敗にします。
    std::istringstream lines(keep);
    for (std::string line; std::getline(lines, line);) {
        const auto begin = line.find_first_not_of(" \t\r");
        if (begin == std::string::npos) continue;
        line = line.substr(begin, line.find_last_not_of(" \t\r") - begin + 1);
        if (!Reference(line, {}, "追加保持欄", true)) throw std::runtime_error("追加保持の指定が見つかりません: " + line);
    }
    while (!pending_.empty()) {
        auto key = std::move(pending_.front());
        pending_.pop_front();
        Dependencies(key);
    }

    RuntimeResourcePlan result;
    for (const auto& [path, input] : inputs_) result.inputs.push_back(input);
    result.report = {{"formatVersion", 1}, {"files", json::array()}, {"unresolvedReferences", problems_}};
    uint64_t keptBytes = 0, excludedBytes = 0, keptCount = 0;
    for (const auto& [key, asset] : assets_) {
        if (asset.used) { keptBytes += asset.bytes; ++keptCount; }
        else { excludedBytes += asset.bytes; result.excludedPaths.insert(CopyKey(asset.relative)); }
        result.report["files"].push_back({{"path", Text(asset.relative)}, {"included", asset.used},
            {"bytes", asset.bytes}, {"source", asset.source},
            {"reason", asset.reason.empty() ? "実行の起点から参照がありません" : asset.reason}});
    }
    result.report["summary"] = {{"includedFiles", keptCount}, {"excludedFiles", result.excludedPaths.size()},
                               {"includedBytes", keptBytes}, {"excludedBytes", excludedBytes}};
    return result;
}
} // namespace

bool BuildRuntimeResourcePlan(const std::filesystem::path& projectRoot, const std::string& additionalKeep,
                              RuntimeResourcePlan& result, std::string& error) {
    result = {};
    error.clear();
    try {
        result = Collector(projectRoot).Build(additionalKeep);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}
bool ValidateRuntimeResourcePlan(const RuntimeResourcePlan& plan, std::string& error) {
    for (const auto& input : plan.inputs) {
        std::error_code code;
        const auto bytes = fs::file_size(input.path, code);
        if (!code && bytes == input.bytes && fs::last_write_time(input.path, code) == input.modified && !code)
            continue;
        error = "素材またはコードが解析後に更新されました。もう一度作成してください: " + Text(input.path);
        return false;
    }
    error.clear();
    return true;
}

} // namespace engine
