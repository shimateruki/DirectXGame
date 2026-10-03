#pragma once

#include <string>

namespace EditorWindowsText {

/// <summary>Windows APIへ渡すUTF-8文字列をUTF-16へ変換します。</summary>
/// <param name="text">終端文字を含まないUTF-8文字列。</param>
/// <returns>変換後の文字列。変換できない場合は空文字列。</returns>
std::wstring Utf8ToWide(const std::string& text);

/// <summary>Windows APIから返ったUTF-16文字列をUTF-8へ変換します。</summary>
/// <param name="text">終端文字を含まないUTF-16文字列。</param>
/// <returns>変換後の文字列。変換できない場合は空文字列。</returns>
std::string WideToUtf8(const std::wstring& text);

} // namespace EditorWindowsText
