#pragma once

#include "json.hpp"
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace engine {

// 配布するResourcesの依存関係を収集します。元の素材と保存データは読み取りだけに使います。
// ExecutablePackageWindowは、この結果を既存のコピー処理へ渡して実行セットを作成します。
struct RuntimeResourcePlan {
    struct Input {
        std::filesystem::path path;
        std::uintmax_t bytes = 0;
        std::filesystem::file_time_type modified;
    };
    // Resourcesからの相対パス。コピー処理と同じ小文字表記で照合します。
    std::unordered_set<std::wstring> excludedPaths;
    // 含めた根拠、除外理由、解決できなかった指定と容量を記録します。
    nlohmann::json report;
    // 解析中やコピー中に編集された入力を検出し、古い依存一覧で配布を確定しないための記録。
    std::vector<Input> inputs;
};

// ゲーム用ソースと公開シーンを起点に依存をたどります。additionalKeepは改行区切りの素材・フォルダー指定です。
// 失敗時は部分的な除外結果を使わせずfalseを返し、errorへ原因を格納します。
bool BuildRuntimeResourcePlan(const std::filesystem::path& projectRoot,
                              const std::string& additionalKeep,
                              RuntimeResourcePlan& result, std::string& error);

// コピー終了後に呼びます。解析に使ったソース・JSON・モデルが変わっていれば再作成を求めます。
bool ValidateRuntimeResourcePlan(const RuntimeResourcePlan& plan, std::string& error);

} // namespace engine
