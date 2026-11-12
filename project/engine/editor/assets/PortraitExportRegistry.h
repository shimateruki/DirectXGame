#pragma once
#include <string>
#include <utility>
#include <vector>

namespace engine {
/// <summary>既存Presetの撮影姿勢と、通常・被弾PNGの出力先を表す。GPUリソースは所有しない。</summary>
// 関連：PortraitExportRegistry（この型を使う機能の本体）
struct PortraitExportDefinition {
    std::string presetName;
    std::string outputDirectory;
    std::string fileStem;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float scale = 1.0f;
    float offsetY = 0.0f;
};

// 担当：Presetの撮影姿勢と画像出力先を登録し、撮影側へ渡す。
// 関連：PortraitExportDefinition（条件）、ProjectWindow（撮影・書出し）
// 主な入口：Register / Definitions
class PortraitExportRegistry {
public:
    /// <summary>定義一覧を置き換える。各Presetは一件ずつ登録し、撮影前のゲーム初期化で呼ぶ。</summary>
    static void Register(std::vector<PortraitExportDefinition> definitions) {
        Definitions() = std::move(definitions);
    }

    /// <summary>未登録なら空一覧を返す。登録の置換をまたいで参照を保持しない。</summary>
    static const std::vector<PortraitExportDefinition>& GetEntries() { return Definitions(); }

private:
    // 撮影条件の登録一覧を返す。更新は初期化側で行う。
    static std::vector<PortraitExportDefinition>& Definitions() {
        static std::vector<PortraitExportDefinition> definitions;
        return definitions;
    }
};
} // namespace engine
