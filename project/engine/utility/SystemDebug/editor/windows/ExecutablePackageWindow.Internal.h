#pragma once

#include <array>
#include <filesystem>
#include <string>

namespace ExecutablePackageInternal {
// UI で選べるビルド構成と、配布時に残すテクスチャの組み合わせ。
constexpr std::array<const char*, 3> kConfigurations = {
    "Debug",
    "Development",
    "Release"
};

/// 同名 DDS/PNG ペアを配布セットへコピーする際の優先順。
enum class TexturePackageMode {
    CompactPng = 0,
    FastDds,
    Complete
};

constexpr std::array<const char*, 3> kTextureModeLabels = {
    "コンパクト提出 (PNG優先)",
    "高速実行 (DDS優先)",
    "完全コピー (DDS + PNG)"
};

// UI とパッケージ処理で同じ検証・パス変換を使い、表示先と実際の出力先を一致させます。
/// 実際に使用される安全なパッケージ名を返します。
std::string PreviewPackageName(const std::string& input);
/// 作業ディレクトリまたは実行ファイル位置から project のルートを探します。
std::filesystem::path PreviewProjectRoot();
/// UI の UTF-8 文字列を Windows のパスへ変換します。
std::filesystem::path PreviewPathFromUtf8(const std::string& text);
/// Windows のパスを UI 表示用の UTF-8 文字列へ変換します。
std::string PreviewPathToUtf8(const std::filesystem::path& path);

} // namespace ExecutablePackageInternal
