#pragma once

#include "json.hpp"
#include "MaterialRenderPolicy.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

// 通常3D描画と同じ入力を使うPixel ShaderだけをJSONから追加する。
// 新しい描画資源が必要な特殊材質は従来の専用経路を使う。
class ShaderMaterialRegistry final {
public:
    // 不透明な通常3D描画へ追加する材質の保存ID、表示名、Pixel Shaderパス。
    struct Definition {
        std::string id;
        std::string displayName;
        std::string pixelShader;
    };

    static ShaderMaterialRegistry& Instance() {
        static ShaderMaterialRegistry instance;
        return instance;
    }

    // 初回にJSON一覧を読む。未登録なら nullptr、返す定義は Reload まで有効な借用参照。
    const Definition* Find(const std::string& id) {
        EnsureLoaded();
        const auto it = definitions_.find(id);
        return it == definitions_.end() ? nullptr : &it->second;
    }

    // ID順の値コピーを返す。一覧を呼び出し側で保持しても内部定義の寿命に依存しない。
    std::vector<Definition> List() {
        EnsureLoaded();
        std::vector<Definition> result;
        for (const auto& [id, definition] : definitions_)
            result.push_back(definition);
        return result;
    }

    // 直近の読込で拒否した形式・パス・重複IDの診断。Reload 後は再取得する。
    const std::vector<std::string>& Errors() {
        EnsureLoaded();
        return errors_;
    }

    // 登録済み一覧と診断を破棄してJSONを読み直す。GPUのPSO生成は呼び出し側が担当する。
    void Reload() {
        loaded_ = false;
        EnsureLoaded();
    }

    // 再読込ごとに増える世代番号。呼び出し側はキャッシュ更新の判定に使う。
    std::uint64_t Generation() {
        EnsureLoaded();
        return generation_;
    }

private:
    void EnsureLoaded() {
        // 初回または明示的な再読込でだけ走査し、毎フレームのファイル探索を避ける。
        if (loaded_)
            return;
        loaded_ = true;
        ++generation_;
        definitions_.clear();
        errors_.clear();
        namespace fs = std::filesystem;
        const fs::path root = "Resources/json/shader_materials";
        std::error_code ec;
        if (!fs::exists(root, ec))
            return;
        for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
            if (!it->is_regular_file() || it->path().extension() != ".json")
                continue;
            try {
                std::ifstream file(it->path());
                nlohmann::json data = nlohmann::json::parse(file);
                // 既存の描画入力で扱える形式と不透明パスだけを受け付ける。
                if (data.value("assetType", "") != "ShaderMaterial" || data.value("version", 0) != 1 ||
                    data.value("renderPass", "") != "opaque") {
                    errors_.push_back(it->path().generic_string() + ": 未対応の形式・描画パス");
                    continue;
                }
                Definition value{data.at("id").get<std::string>(), data.value("displayName", ""),
                                 data.at("pixelShader").get<std::string>()};
                const fs::path shader = fs::path(std::u8string(value.pixelShader.begin(), value.pixelShader.end())).lexically_normal();
                const std::string shaderName = shader.generic_string();
                std::error_code shaderError;
                // 予約IDと絶対パス・範囲外参照を拒否し、実在するシェーダーファイルだけ登録する。
                if (value.id.empty() || MaterialRenderPolicy::Find(value.id) != nullptr ||
                    shader.is_absolute() || shaderName.rfind("Resources/shader/", 0) != 0 ||
                    shaderName.find("..") != std::string::npos || shader.extension() != ".hlsl" ||
                    !fs::is_regular_file(shader, shaderError)) {
                    errors_.push_back(it->path().generic_string() + ": IDまたはPixel Shaderのパスが不正");
                    continue;
                }
                value.pixelShader = shaderName;
                if (value.displayName.empty())
                    value.displayName = value.id;
                if (!definitions_.emplace(value.id, value).second)
                    errors_.push_back(it->path().generic_string() + ": 材質IDが重複: " + value.id);
            } catch (const std::exception& error) {
                errors_.push_back(it->path().generic_string() + ": " + error.what());
            }
        }
        if (ec)
            errors_.push_back(root.generic_string() + ": 一覧の読み込みに失敗: " + ec.message());
    }

    bool loaded_ = false;
    std::uint64_t generation_ = 0;
    std::map<std::string, Definition> definitions_;
    std::vector<std::string> errors_;
};
