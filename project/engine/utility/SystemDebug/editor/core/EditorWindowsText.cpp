#define NOMINMAX
#include "EditorWindowsText.h"

#include <Windows.h>

namespace EditorWindowsText {

// Win32の広文字APIへ渡すためUTF-8を変換し、無効な入力では空文字を返します。
std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    // -1指定で終端文字も変換されるため、その1文字分まで確保してから取り除きます。
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, nullptr, 0);
    if (required <= 0) {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, result.data(), required) != required) {
        return {};
    }
    result.pop_back();
    return result;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }

    // 終端文字を含めた長さで確保し、APIが書き込む範囲を越えないようにします。
    const int required = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }

    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), required, nullptr, nullptr) != required) {
        return {};
    }
    result.pop_back();
    return result;
}

} // namespace EditorWindowsText
