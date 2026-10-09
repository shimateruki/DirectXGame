#include "imgui.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
int invalidParameters = 0;
void InvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
    ++invalidParameters;
}
}

int main() {
    // 実際の攻撃状態と同じ日本語の内訳は、従来の64バイトを超えます。
    const std::string phase = "炎の扇状連射 / 実行中 / 発火 5 / 条件不成立 0 / 部品実行 0 / 部品省略 0 / 接触 0";
    const auto previous = _set_thread_local_invalid_parameter_handler(InvalidParameter);
    char oldBuffer[64]{};
    const int result = sprintf_s(oldBuffer, "%s", phase.c_str());
    const bool reproduced = phase.size() >= sizeof(oldBuffer) && result < 0 && invalidParameters == 1;
    if (!reproduced) { _set_thread_local_invalid_parameter_handler(previous); return 1; }

    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(800, 600);
    io.DeltaTime = 1.0f / 60;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();
    ImGui::Begin("Attack phase regression");
    // 修正した表示経路は固定長コピーもprintf書式解釈も必要ありません。
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(phase.c_str());
    const std::string longName = std::string(4096, 'x') + " 日本語 %s %n 100%";
    ImGui::TextUnformatted(longName.c_str());
    ImGui::PopTextWrapPos();
    ImGui::End();
    ImGui::Render();
    ImGui::DestroyContext();
    _set_thread_local_invalid_parameter_handler(previous);
    if (invalidParameters != 1) return 2;
    std::cout << "Reproduced old 64-byte CRT failure; ImGui unformatted Japanese/long/percent labels passed.\n";
    return 0;
}
